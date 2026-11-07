#include "nexapack/archive.h"
#include "nexapack/format.h"
#include "nexapack/integrity.h"
#include "nexapack/safe_path.h"
#include "nexapack/codec_interface.h"
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
#include <string_view>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <cstdio>
#include <set>

namespace fs = std::filesystem;

namespace nexapack {

namespace {

struct LoadedArchive {
    std::vector<uint8_t> file;
    uint64_t file_size = 0;
    uint64_t header_size = 0;
    uint64_t entry_count = 0;
    uint64_t metadata_size = 0;
    uint64_t data_offset = 0;
    uint64_t index_offset = 0;
    uint64_t index_size = 0;
    uint64_t footer_offset = 0;
    uint64_t footer_size = 0;
    std::vector<Entry> entries;
    bool valid = false;
};

std::vector<uint8_t> read_whole_file(std::string_view path) {
    std::ifstream in(std::string(path), std::ios::binary);
    if (!in) Throw(ErrorCode::IoError, "cannot open: " + std::string(path));
    in.seekg(0, std::ios::end);
    auto sz = in.tellg();
    if (sz < 0) Throw(ErrorCode::IoError, "cannot determine size: " + std::string(path));
    if (static_cast<uint64_t>(sz) > NEXAPACK_MAX_ARCHIVE_SIZE) {
        Throw(ErrorCode::LimitExceeded, "archive exceeds maximum size");
    }
    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> buf(static_cast<std::size_t>(sz));
    in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
    if (!in) Throw(ErrorCode::IoError, "short read: " + std::string(path));
    return buf;
}

// Bounds-checked read of n bytes at pos.
std::span<const uint8_t> read_bytes(const std::vector<uint8_t>& file,
                                    uint64_t pos, uint64_t n,
                                    std::string_view what) {
    check_range(pos, n, static_cast<uint64_t>(file.size()), std::string(what));
    return {file.data() + pos, static_cast<std::size_t>(n)};
}

std::string read_lp_string(const std::vector<uint8_t>& file, uint64_t& pos,
                           std::string_view what) {
    auto sb = read_bytes(file, pos, 4, what);
    uint32_t len = read_u32(sb);
    pos += 4;
    if (len > NEXAPACK_MAX_PATH_LENGTH) {
        Throw(ErrorCode::LimitExceeded, "path length exceeds maximum: " + std::string(what));
    }
    auto sp = read_bytes(file, pos, len, what);
    std::string s(reinterpret_cast<const char*>(sp.data()), len);
    pos += len;
    return s;
}

void parse_header(LoadedArchive& la) {
    if (la.file.size() < NEXAPACK_HEADER_FIXED_SIZE) {
        Throw(ErrorCode::TruncatedArchive, "archive smaller than fixed header");
    }
    if (std::memcmp(la.file.data(), NEXAPACK_MAGIC, 4) != 0) {
        Throw(ErrorCode::UnsupportedFormat, "bad magic signature");
    }
    la.header_size = read_u32({la.file.data() + 8, 4});
    if (la.header_size < NEXAPACK_HEADER_FIXED_SIZE) {
        Throw(ErrorCode::CorruptArchive, "header_size smaller than fixed header");
    }
    if (la.header_size > la.file.size()) {
        Throw(ErrorCode::InvalidOffset, "header_size beyond archive size");
    }
    uint32_t stored_crc = read_u32({la.file.data() + NEXAPACK_HEADER_CRC_OFFSET, 4});
    uint32_t computed = crc32c({la.file.data(), NEXAPACK_HEADER_CRC_OFFSET});
    if (stored_crc != computed) {
        Throw(ErrorCode::ChecksumMismatch, "header checksum mismatch");
    }
    la.entry_count = read_u64({la.file.data() + 12, 8});
    la.metadata_size = read_u64({la.file.data() + 20, 8});
    la.data_offset = read_u64({la.file.data() + 28, 8});
    la.index_offset = read_u64({la.file.data() + 36, 8});
    la.index_size = read_u64({la.file.data() + 44, 8});
    la.footer_offset = read_u64({la.file.data() + 52, 8});
    la.footer_size = read_u64({la.file.data() + 60, 8});

    if (la.entry_count > NEXAPACK_MAX_ENTRY_COUNT) {
        Throw(ErrorCode::LimitExceeded, "entry count exceeds maximum");
    }
    if (la.metadata_size > NEXAPACK_MAX_METADATA_SIZE) {
        Throw(ErrorCode::LimitExceeded, "metadata size exceeds maximum");
    }
    check_range(la.header_size, la.metadata_size, la.file.size(), "metadata section");
    check_range(la.data_offset, 0, la.file.size(), "data offset");
    check_range(la.index_offset, la.index_size, la.file.size(), "index section");
    check_range(la.footer_offset, la.footer_size, la.file.size(), "footer section");
    if (la.data_offset < la.header_size + la.metadata_size) {
        Throw(ErrorCode::CorruptArchive, "data_offset overlaps metadata");
    }
    if (la.index_offset < la.data_offset) {
        Throw(ErrorCode::CorruptArchive, "index_offset before data_offset");
    }
    if (la.footer_offset < la.index_offset + la.index_size) {
        Throw(ErrorCode::CorruptArchive, "footer_offset before end of index");
    }
}

void parse_metadata(LoadedArchive& la) {
    uint64_t pos = la.header_size;
    uint64_t end = la.header_size + la.metadata_size;

    if (end - pos < 8) Throw(ErrorCode::TruncatedArchive, "truncated entry count");
    uint64_t count = read_u64({la.file.data() + pos, 8});
    pos += 8;
    if (count != la.entry_count) {
        Throw(ErrorCode::CorruptArchive, "metadata entry count mismatch");
    }

    for (uint64_t i = 0; i < count; ++i) {
        Entry e;
        e.path = read_lp_string(la.file, pos, "entry path");

        auto b = read_bytes(la.file, pos, 3, "entry header");
        uint8_t flags = b[0];
        e.is_directory = (flags & static_cast<uint8_t>(EntryFlags::IsDirectory)) != 0;
        e.codec = static_cast<CodecId>(b[1]);
        e.codec_level = b[2];
        pos += 3;

        {
            auto bb = read_bytes(la.file, pos, 8, "uncompressed_size");
            e.uncompressed_size = read_u64(bb);
            pos += 8;
        }
        {
            auto bb = read_bytes(la.file, pos, 8, "total_compressed_size");
            e.total_compressed_size = read_u64(bb);
            pos += 8;
        }
        {
            auto bb = read_bytes(la.file, pos, 4, "chunk_count");
            e.chunk_count = read_u32(bb);
            pos += 4;
            {
            }
        }
        if (e.chunk_count > 1000000) {
            Throw(ErrorCode::LimitExceeded, "chunk count exceeds maximum");
        }
        {
            auto bb = read_bytes(la.file, pos, 4, "entry_checksum");
            e.checksum = read_u32(bb);
            pos += 4;
        }
        for (uint32_t c = 0; c < e.chunk_count; ++c) {
            ChunkDescriptor cd;
            auto bb = read_bytes(la.file, pos, 32, "chunk descriptor");
            cd.offset = read_u64({bb.data(), 8});
            cd.compressed_length = read_u64({bb.data() + 8, 8});
            cd.uncompressed_length = read_u64({bb.data() + 16, 8});
            cd.checksum = read_u32({bb.data() + 24, 4});
            cd.codec = static_cast<CodecId>(bb[28]);
            cd.codec_level = bb[29];
            pos += 32;

            check_range(cd.offset, cd.compressed_length, la.file.size(), "chunk data");
            {
            }
            if (cd.offset < la.data_offset) {
                Throw(ErrorCode::InvalidOffset, "chunk offset before data section");
            }
            // Chunk data must not extend into the index or footer section.
            if (cd.offset + cd.compressed_length > la.index_offset) {
                Throw(ErrorCode::InvalidOffset,
                      "chunk data overlaps index or footer section");
            }
            if (cd.uncompressed_length > NEXAPACK_MAX_CHUNK_SIZE) {
                Throw(ErrorCode::LimitExceeded, "chunk uncompressed length exceeds maximum");
            }
            e.chunks.push_back(cd);
        }
        la.entries.push_back(std::move(e));
    }
    if (pos != end) {
        Throw(ErrorCode::CorruptArchive, "metadata size mismatch");
    }
}

void parse_index(LoadedArchive& la) {
    uint64_t pos = la.index_offset;
    uint64_t end = la.index_offset + la.index_size;
    if (end - pos < 8) Throw(ErrorCode::TruncatedArchive, "truncated index count");
    uint64_t count = read_u64({la.file.data() + pos, 8});
    pos += 8;
    if (count != la.entry_count) {
        Throw(ErrorCode::CorruptArchive, "index entry count mismatch");
    }
    for (uint64_t i = 0; i < count; ++i) {
        read_lp_string(la.file, pos, "index path");
        if (end - pos < 32) Throw(ErrorCode::TruncatedArchive, "truncated index record");
        pos += 32; // first_offset(8) + total_comp(8) + usize(8) + checksum(4) + chunk_count(4)
    }
    if (pos != end) Throw(ErrorCode::CorruptArchive, "index size mismatch");
}

void parse_footer(LoadedArchive& la) {
    auto fbytes = read_bytes(la.file, la.footer_offset, la.footer_size, "footer");
    if (fbytes.size() < 16) Throw(ErrorCode::TruncatedArchive, "truncated footer");
    if (std::memcmp(fbytes.data(), NEXAPACK_FOOTER_MAGIC, 4) != 0) {
        Throw(ErrorCode::UnsupportedFormat, "bad footer magic");
    }
    uint64_t archive_size = read_u64({fbytes.data() + 4, 8});
    if (archive_size != static_cast<uint64_t>(la.file.size())) {
        Throw(ErrorCode::CorruptArchive, "footer archive size mismatch");
    }
    uint32_t stored = read_u32({fbytes.data() + 12, 4});
    uint32_t computed = crc32c({fbytes.data(), 12});
    if (stored != computed) {
        Throw(ErrorCode::ChecksumMismatch, "footer checksum mismatch");
    }
}

LoadedArchive load_and_validate(std::string_view archive_path) {
    LoadedArchive la;
    la.file = read_whole_file(archive_path);
    la.file_size = static_cast<uint64_t>(la.file.size());
    parse_header(la);
    parse_metadata(la);
    parse_index(la);
    parse_footer(la);
    la.valid = true;
    return la;
}

} // namespace

std::vector<Entry> list_archive(std::string_view archive_path) {
    register_all_builtin_codecs();
    auto la = load_and_validate(archive_path);
    return la.entries;
}

InspectResult inspect_archive(std::string_view archive_path) {
    register_all_builtin_codecs();
    auto la = load_and_validate(archive_path);
    InspectResult r;
    r.archive_path = std::string(archive_path);
    r.archive_size = la.file_size;
    r.version_major = la.file[4];
    r.version_minor = la.file[5];
    r.feature_flags = read_u16({la.file.data() + 6, 2});
    r.entry_count = la.entry_count;
    uint64_t tu = 0, tc = 0;
    for (auto& e : la.entries) {
        tu += e.uncompressed_size;
        tc += e.total_compressed_size;
    }
    r.total_uncompressed = tu;
    r.total_compressed = tc;
    r.entries = std::move(la.entries);
    return r;
}

void verify_archive(const VerifyOptions& opts) {
    register_all_builtin_codecs();
    auto la = load_and_validate(opts.archive_path);
    uint64_t total_out = 0;
    for (auto& e : la.entries) {
        if (e.is_directory) continue;
        uint32_t running = 0;
        uint64_t out_total = 0;
        for (auto& cd : e.chunks) {
            auto comp = read_bytes(la.file, cd.offset, cd.compressed_length, "chunk");
            auto* codec = CodecRegistry::instance().find(cd.codec);
            if (!codec || !codec->available()) {
                Throw(ErrorCode::UnknownCodec, "chunk uses unavailable codec");
            }
            auto dec = codec->decompress(comp, e.uncompressed_size - out_total);
            if (dec.uncompressed_size != cd.uncompressed_length) {
                Throw(ErrorCode::CorruptArchive, "chunk decompressed length mismatch");
            }
            if (crc32c({dec.uncompressed.data(), dec.uncompressed.size()}) != cd.checksum) {
                Throw(ErrorCode::ChecksumMismatch, "chunk checksum mismatch");
            }
            running = crc32c_update(running, {dec.uncompressed.data(), dec.uncompressed.size()});
            out_total += cd.uncompressed_length;
            total_out += cd.uncompressed_length;
            if (total_out > NEXAPACK_MAX_TOTAL_DECOMPRESSED) {
                Throw(ErrorCode::DecompressionBomb, "total decompressed output exceeds limit");
            }
        }
        if (out_total != e.uncompressed_size) {
            Throw(ErrorCode::CorruptArchive, "entry decompressed size mismatch");
        }
        if (running != e.checksum) {
            Throw(ErrorCode::ChecksumMismatch, "entry checksum mismatch");
        }
    }
    if (opts.verbose) {
        std::printf("verify OK: %s (%llu entries, %llu bytes verified)\n",
                    opts.archive_path.c_str(),
                    (unsigned long long)la.entries.size(),
                    (unsigned long long)total_out);
    }
}

void extract_archive(const ExtractOptions& opts) {
    register_all_builtin_codecs();
    auto la = load_and_validate(opts.archive_path);
    std::error_code ec;
    fs::create_directories(opts.output_dir, ec);
    if (ec) Throw(ErrorCode::IoError, "cannot create output directory: " + ec.message());

    std::set<std::string> written;

    for (auto& e : la.entries) {
        if (opts.single_file && *opts.single_file != e.path) continue;
        if (e.is_directory) {
            auto dest = safe_output_path(opts.output_dir, e.path);
            if (!dest) Throw(ErrorCode::PathUnsafe, "unsafe entry path: " + e.path);
            fs::create_directories(*dest, ec);
            continue;
        }
        auto dest = safe_output_path(opts.output_dir, e.path);
        if (!dest) Throw(ErrorCode::PathUnsafe, "unsafe entry path: " + e.path);
        if (!written.insert(e.path).second) {
            Throw(ErrorCode::InvalidArgument, "duplicate entry path: " + e.path);
        }

        std::vector<uint8_t> out;
        if (e.uncompressed_size > 0) {
            out.reserve(static_cast<std::size_t>(e.uncompressed_size));
        }
        uint64_t out_total = 0;
        for (auto& cd : e.chunks) {
            auto comp = read_bytes(la.file, cd.offset, cd.compressed_length, "chunk");
            auto* codec = CodecRegistry::instance().find(cd.codec);
            if (!codec || !codec->available()) {
                Throw(ErrorCode::UnknownCodec, "chunk uses unavailable codec");
            }
            auto dec = codec->decompress(comp, e.uncompressed_size - out_total);
            out.insert(out.end(), dec.uncompressed.begin(), dec.uncompressed.end());
            out_total += cd.uncompressed_length;
        }
        if (out_total != e.uncompressed_size) {
            Throw(ErrorCode::CorruptArchive, "entry decompressed size mismatch");
        }
        if (crc32c({out.data(), out.size()}) != e.checksum) {
            Throw(ErrorCode::ChecksumMismatch, "entry checksum mismatch after extraction");
        }

        std::error_code wec = atomic_write_file(*dest, {out.data(), out.size()}, opts.overwrite);
        if (wec) Throw(ErrorCode::IoError, "cannot write " + e.path + ": " + wec.message());
    }

    if (opts.single_file && written.empty()) {
        Throw(ErrorCode::InvalidArgument, "file not found in archive: " + *opts.single_file);
    }
}

} // namespace nexapack


