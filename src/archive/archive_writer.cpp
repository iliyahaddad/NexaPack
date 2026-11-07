#include "nexapack/archive.h"
#include "nexapack/format.h"
#include "nexapack/integrity.h"
#include "nexapack/safe_path.h"
#include "nexapack/codec_interface.h"
#include "nexapack/planner.h"
#include "nexapack/analyzer.h"
#include "nexapack/store_codec.h"

#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
#include <string_view>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <set>
#include <span>
#include <cstdio>

namespace fs = std::filesystem;

namespace nexapack {

namespace {

// One chunk awaiting write, with its computed descriptor.
struct PendingChunk {
    std::vector<uint8_t> data;          // compressed bytes
    uint64_t offset = 0;                // offset within data section
    uint32_t checksum = 0;
};

void append_lp_bytes(std::vector<uint8_t>& out, std::string_view s) {
    if (s.size() > NEXAPACK_MAX_PATH_LENGTH) {
        Throw(ErrorCode::LimitExceeded, "path length exceeds maximum");
    }
    auto before = out.size();
    out.resize(before + 4 + s.size());
    write_u32({out.data() + before, 4}, static_cast<uint32_t>(s.size()));
    std::memcpy(out.data() + before + 4, s.data(), s.size());
}

// Collect input files recursively. Returns (absolute_path, entry_path) pairs.
// entry_path is UTF-8 relative, directory-separated by '/', no leading slash.
std::vector<std::pair<fs::path, std::string>> collect_input_files(
    const std::vector<std::string>& inputs, bool recursive) {

    std::vector<std::pair<fs::path, std::string>> result;
    std::set<std::string> seen;

    for (const auto& input : inputs) {
        fs::path p(input);
        std::error_code ec;
        auto status = fs::symlink_status(p, ec);
        if (ec || status.type() == fs::file_type::not_found) {
            Throw(ErrorCode::IoError, "input does not exist: " + input);
        }
        if (status.type() == fs::file_type::symlink) {
            Throw(ErrorCode::UnsupportedFormat, "symlink inputs are not supported: " + input);
        }

        if (fs::is_directory(status)) {
            if (!recursive) {
                auto entry = validate_entry_path(p.filename().string());
                if (!entry) Throw(ErrorCode::PathUnsafe, "invalid path: " + input);
                if (!seen.insert(*entry).second) {
                    Throw(ErrorCode::InvalidArgument, "duplicate path: " + *entry);
                }
                result.emplace_back(p, *entry);
                continue;
            }
            for (auto& entry : fs::recursive_directory_iterator(p, fs::directory_options::skip_permission_denied, ec)) {
                if (ec) continue;
                auto st = entry.status();
                if (st.type() == fs::file_type::symlink) continue;
                if (st.type() == fs::file_type::directory) {
                    auto rel = entry.path().generic_string();
                    std::string epath;
                    epath.reserve(rel.size());
                    for (char c : rel) epath.push_back(c == '\\' ? '/' : c);
                    if (!epath.empty() && epath.back() == '/') epath.pop_back();
                    if (epath.empty()) continue;
                    auto validated = validate_entry_path(epath);
                    if (!validated) continue;
                    if (!seen.insert(*validated).second) {
                        Throw(ErrorCode::InvalidArgument, "duplicate path: " + *validated);
                    }
                    result.emplace_back(entry.path(), *validated);
                } else if (st.type() == fs::file_type::regular) {
                    auto rel = fs::relative(entry.path(), p).generic_string();
                    std::string epath;
                    epath.reserve(rel.size());
                    for (char c : rel) epath.push_back(c == '\\' ? '/' : c);
                    auto validated = validate_entry_path(epath);
                    if (!validated) Throw(ErrorCode::PathUnsafe, "invalid path: " + entry.path().string());
                    if (!seen.insert(*validated).second) {
                        Throw(ErrorCode::InvalidArgument, "duplicate path: " + *validated);
                    }
                    result.emplace_back(entry.path(), *validated);
                }
            }
        } else if (fs::is_regular_file(status)) {
            auto entry = validate_entry_path(p.filename().string());
            if (!entry) Throw(ErrorCode::PathUnsafe, "invalid path: " + input);
            if (!seen.insert(*entry).second) {
                Throw(ErrorCode::InvalidArgument, "duplicate path: " + *entry);
            }
            result.emplace_back(p, *entry);
        } else {
            Throw(ErrorCode::UnsupportedFormat,
                  "input is not a regular file or directory: " + input);
        }
    }
    return result;
}

} // namespace

void create_archive(const CreateOptions& opts) {
    register_all_builtin_codecs();
    std::error_code ec;
    if (fs::exists(opts.archive_path, ec) && !opts.overwrite) {
        Throw(ErrorCode::InvalidArgument,
              "archive already exists (use --overwrite): " + opts.archive_path);
    }

    auto inputs = collect_input_files(opts.input_paths, opts.recursive);

    PlannerConfig pcfg;
    pcfg.policy = PlannerPolicy::Balanced;
    pcfg.threads = 1;

    struct FileData {
        Entry entry;
        std::vector<PendingChunk> chunks;
    };
    std::vector<FileData> files;
    files.reserve(inputs.size());

    for (auto& [abs_path, rel_path] : inputs) {
        std::ifstream in(abs_path, std::ios::binary);
        if (!in) Throw(ErrorCode::IoError, "cannot open input: " + abs_path.string());

        in.seekg(0, std::ios::end);
        auto file_size = in.tellg();
        if (file_size < 0) Throw(ErrorCode::IoError, "cannot determine size: " + abs_path.string());
        in.seekg(0, std::ios::beg);

        uint64_t usize = static_cast<uint64_t>(file_size);
        if (usize > NEXAPACK_MAX_DECOMPRESSED_PER_ENTRY) {
            Throw(ErrorCode::LimitExceeded, "file too large: " + abs_path.string());
        }

        Entry entry;
        entry.path = rel_path;
        entry.is_directory = false;
        entry.uncompressed_size = usize;
        entry.codec = CodecId::Store;
        entry.codec_level = 0;

        FileData fd;
        // fd.entry is populated fully after the chunk loop below.

        if (usize == 0) {
            entry.chunk_count = 0;
            entry.total_compressed_size = 0;
            entry.checksum = crc32c({});
            fd.entry = entry;
            files.push_back(std::move(fd));
            continue;
        }

        std::vector<uint8_t> buffer(usize);
        in.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(usize));
        if (!in || in.gcount() != static_cast<std::streamsize>(usize)) {
            Throw(ErrorCode::IoError, "short read: " + abs_path.string());
        }

        auto features = analyze_buffer(buffer);
        auto decision = plan(features, pcfg);
        // Respect an explicitly forced codec; otherwise follow the planner.
        // If the chosen codec is unavailable, fall back to Store.
        CodecId chosen_codec = decision.codec;
        int chosen_level = decision.level;
        if (opts.codec_forced) {
            chosen_codec = opts.force_codec;
            if (opts.force_level >= 0) {
                chosen_level = opts.force_level;
            } else if (chosen_codec != CodecId::Store && decision.level == 0) {
                // Planner picked Store but a real codec was forced: use its
                // default level so the forced codec is actually applied.
                auto* cc = CodecRegistry::instance().find(chosen_codec);
                chosen_level = (cc && cc->available()) ? cc->default_level() : 0;
            } else {
                chosen_level = decision.level;
            }
        }
        if (chosen_codec != CodecId::Store) {
            auto* cc = CodecRegistry::instance().find(chosen_codec);
            if (!cc || !cc->available()) {
                chosen_codec = CodecId::Store;
                chosen_level = 0;
            }
        }

        entry.codec = chosen_codec;
        entry.codec_level = static_cast<uint8_t>(chosen_level);

        uint64_t remaining = usize;
        std::size_t pos = 0;
        uint64_t total_compressed = 0;
        uint32_t running_crc = 0;

        while (remaining > 0) {
            uint64_t chunk_bytes = std::min<uint64_t>(opts.chunk_size, remaining);
            auto chunk_src = std::span<const uint8_t>(buffer.data() + pos, static_cast<std::size_t>(chunk_bytes));

            CompressionResult cr;
            if (chosen_codec == CodecId::Store) {
                cr = StoreCodec().compress(chunk_src, 0);
            } else {
                auto* codec = CodecRegistry::instance().find(chosen_codec);
                if (!codec || !codec->available()) {
                    Throw(ErrorCode::UnknownCodec, "selected codec unavailable");
                }
                cr = codec->compress(chunk_src, chosen_level);
            }

            PendingChunk pc;
            pc.data = std::move(cr.compressed);
            pc.checksum = cr.checksum;

            ChunkDescriptor cd;
            cd.compressed_length = pc.data.size();
            cd.uncompressed_length = static_cast<uint64_t>(chunk_bytes);
            cd.checksum = pc.checksum;
            cd.codec = chosen_codec;
            cd.codec_level = static_cast<uint8_t>(chosen_level);
            entry.chunks.push_back(cd);

            running_crc = crc32c_update(running_crc, chunk_src);
            total_compressed += pc.data.size();
            fd.chunks.push_back(std::move(pc));

            remaining -= chunk_bytes;
            pos += static_cast<std::size_t>(chunk_bytes);
        }

        entry.chunk_count = static_cast<uint32_t>(entry.chunks.size());
        entry.total_compressed_size = total_compressed;
        entry.checksum = running_crc;

        fd.entry = entry;
        files.push_back(std::move(fd));
    }

    if (files.size() > NEXAPACK_MAX_ENTRY_COUNT) {
        Throw(ErrorCode::LimitExceeded, "too many entries");
    }

    //--------------------------------------------------------------------------
    // Serialize metadata section.
    //--------------------------------------------------------------------------
    std::vector<uint8_t> metadata;
    auto grow = [&](std::vector<uint8_t>& buf, std::size_t n) -> std::size_t {
        auto b = buf.size();
        buf.resize(b + n);
        return b;
    };
    grow(metadata, 8);
    write_u64({metadata.data(), 8}, static_cast<uint64_t>(files.size()));
    for (auto& fd : files) {
        auto& e = fd.entry;
        append_lp_bytes(metadata, e.path);
        {
            auto b = grow(metadata, 3);
            metadata[b + 0] = static_cast<uint8_t>(e.is_directory ? EntryFlags::IsDirectory : EntryFlags::None);
            metadata[b + 1] = static_cast<uint8_t>(e.codec);
            metadata[b + 2] = e.codec_level;
        }
        {
            auto b = grow(metadata, 8);
            write_u64({metadata.data() + b, 8}, e.uncompressed_size);
        }
        {
            auto b = grow(metadata, 8);
            write_u64({metadata.data() + b, 8}, e.total_compressed_size);
        }
        {
            auto b = grow(metadata, 4);
            write_u32({metadata.data() + b, 4}, e.chunk_count);
        }
        {
            auto b = grow(metadata, 4);
            write_u32({metadata.data() + b, 4}, e.checksum);
        }
        for (auto& cd : e.chunks) {
            auto b = grow(metadata, 32);
            write_u64({metadata.data() + b, 8}, 0);   // offset filled after data layout
            write_u64({metadata.data() + b + 8, 8}, cd.compressed_length);
            write_u64({metadata.data() + b + 16, 8}, cd.uncompressed_length);
            write_u32({metadata.data() + b + 24, 4}, cd.checksum);
            metadata[b + 28] = static_cast<uint8_t>(cd.codec);
            metadata[b + 29] = cd.codec_level;
            metadata[b + 30] = 0;
            metadata[b + 31] = 0;
        }
    }
    uint64_t metadata_size = static_cast<uint64_t>(metadata.size());

    //--------------------------------------------------------------------------
    // Serialize data section contiguously and backfill chunk offsets.
    //--------------------------------------------------------------------------
    uint64_t data_offset = 72 + metadata_size;
    std::vector<uint8_t> data;
    for (auto& fd : files) {
        for (auto& pc : fd.chunks) {
            pc.offset = data_offset + static_cast<uint64_t>(data.size());
            data.insert(data.end(), pc.data.begin(), pc.data.end());
        }
    }
    {
        std::size_t mpos = 8; // skip entry_count
        for (auto& fd : files) {
            uint32_t pl = read_u32({metadata.data() + mpos, 4});
            mpos += 4 + pl;
            mpos += 3; // flags/codec/level
            mpos += 8; // uncompressed_size
            mpos += 8; // total_compressed_size
            uint32_t cc = read_u32({metadata.data() + mpos, 4}); // chunk_count
            mpos += 4; // chunk_count
            mpos += 4; // entry_checksum
            for (uint32_t i = 0; i < cc; ++i) {
                write_u64({metadata.data() + mpos, 8}, fd.chunks[i].offset);
                mpos += 32;
            }
        }
    }
    uint64_t data_size = static_cast<uint64_t>(data.size());

    //--------------------------------------------------------------------------
    // Serialize index section.
    //--------------------------------------------------------------------------
    std::vector<uint8_t> index;
    grow(index, 8);
    write_u64({index.data(), 8}, static_cast<uint64_t>(files.size()));
    for (auto& fd : files) {
        auto& e = fd.entry;
        append_lp_bytes(index, e.path);
        auto b = grow(index, 32);
        write_u64({index.data() + b, 8}, e.chunks.empty() ? 0 : e.chunks[0].offset);
        write_u64({index.data() + b + 8, 8}, e.total_compressed_size);
        write_u64({index.data() + b + 16, 8}, e.uncompressed_size);
        write_u32({index.data() + b + 24, 4}, e.checksum);
        write_u32({index.data() + b + 28, 4}, e.chunk_count);
    }
    uint64_t index_size = static_cast<uint64_t>(index.size());

    //--------------------------------------------------------------------------
    // Footer.
    //--------------------------------------------------------------------------
    std::vector<uint8_t> footer(4 + 8 + 4);
    std::memcpy(footer.data(), NEXAPACK_FOOTER_MAGIC, 4);
    uint64_t archive_size = 72 + metadata_size + data_size + index_size + static_cast<uint64_t>(footer.size());
    write_u64({footer.data() + 4, 8}, archive_size);
    uint32_t fcrc = crc32c({footer.data(), 12});
    write_u32({footer.data() + 12, 4}, fcrc);
    uint64_t footer_size = static_cast<uint64_t>(footer.size());

    //--------------------------------------------------------------------------
    // Header.
    //--------------------------------------------------------------------------
    uint64_t header_size = NEXAPACK_HEADER_FIXED_SIZE;
    uint64_t index_offset = data_offset + data_size;
    uint64_t footer_offset = index_offset + index_size;
    uint64_t archive_total = footer_offset + footer_size;

    if (archive_total > NEXAPACK_MAX_ARCHIVE_SIZE) {
        Throw(ErrorCode::LimitExceeded, "archive exceeds maximum size");
    }

    std::vector<uint8_t> header(NEXAPACK_HEADER_FIXED_SIZE);
    std::memcpy(header.data(), NEXAPACK_MAGIC, 4);
    header[4] = NEXAPACK_FORMAT_MAJOR;
    header[5] = NEXAPACK_FORMAT_MINOR;
    write_u16({header.data() + 6, 2}, 0);
    write_u32({header.data() + 8, 4}, static_cast<uint32_t>(header_size));
    write_u64({header.data() + 12, 8}, static_cast<uint64_t>(files.size()));
    write_u64({header.data() + 20, 8}, metadata_size);
    write_u64({header.data() + 28, 8}, data_offset);
    write_u64({header.data() + 36, 8}, index_offset);
    write_u64({header.data() + 44, 8}, index_size);
    write_u64({header.data() + 52, 8}, footer_offset);
    write_u64({header.data() + 60, 8}, footer_size);
    uint32_t hcrc = crc32c({header.data(), NEXAPACK_HEADER_CRC_OFFSET});
    write_u32({header.data() + NEXAPACK_HEADER_CRC_OFFSET, 4}, hcrc);

    //--------------------------------------------------------------------------
    // Write file.
    //--------------------------------------------------------------------------
    std::ofstream out(opts.archive_path, std::ios::binary | std::ios::trunc);
    if (!out) Throw(ErrorCode::IoError, "cannot open archive for writing: " + opts.archive_path);
    out.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
    out.write(reinterpret_cast<const char*>(metadata.data()), static_cast<std::streamsize>(metadata.size()));
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    out.write(reinterpret_cast<const char*>(index.data()), static_cast<std::streamsize>(index.size()));
    out.write(reinterpret_cast<const char*>(footer.data()), static_cast<std::streamsize>(footer.size()));
    if (!out) Throw(ErrorCode::IoError, "failed writing archive: " + opts.archive_path);
    out.flush();

    if (opts.verbose) {
        std::printf("created %s: entries=%zu data=%llu metadata=%llu index=%llu footer=%llu total=%llu\n",
                    opts.archive_path.c_str(), files.size(),
                    (unsigned long long)data_size, (unsigned long long)metadata_size,
                    (unsigned long long)index_size, (unsigned long long)footer_size,
                    (unsigned long long)archive_total);
    }
}

} // namespace nexapack


