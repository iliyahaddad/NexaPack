#include "nexapack/lz4_codec.h"
#include "nexapack/integrity.h"

#include <lz4.h>
#include <lz4hc.h>

#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <span>

namespace nexapack {

CompressionResult Lz4Codec::compress(std::span<const uint8_t> input, int level) {
    if (level < min_level() || level > max_level()) {
        Throw(ErrorCode::InvalidArgument,
              "lz4 level must be in [1, 12]");
    }

    CompressionResult r;
    r.uncompressed_size = static_cast<uint64_t>(input.size());

    if (r.uncompressed_size == 0) {
        r.compressed.clear();
        r.compressed_size = 0;
        r.checksum = crc32c({});
        return r;
    }

    // Bound the output buffer.
    const int bound = LZ4_compressBound(static_cast<int>(r.uncompressed_size));
    if (bound <= 0) {
        Throw(ErrorCode::InternalError, "LZ4 compressBound failed");
    }

    r.compressed.resize(static_cast<std::size_t>(bound));

    int ret;
    if (level >= 1 && level <= 3) {
        // Fast compression (levels 1-3 map to acceleration factor)
        ret = LZ4_compress_fast(reinterpret_cast<const char*>(input.data()),
                                reinterpret_cast<char*>(r.compressed.data()),
                                static_cast<int>(r.uncompressed_size),
                                static_cast<int>(bound),
                                level);
    } else {
        // High compression (levels 4-12 map to LZ4_HC levels 1-9)
        int hc_level = level - 3;
        ret = LZ4_compress_HC(reinterpret_cast<const char*>(input.data()),
                              reinterpret_cast<char*>(r.compressed.data()),
                              static_cast<int>(r.uncompressed_size),
                              static_cast<int>(bound),
                              hc_level);
    }

    if (ret <= 0) {
        Throw(ErrorCode::InternalError,
              "LZ4 compression failed with code " + std::to_string(ret));
    }

    r.compressed.resize(static_cast<std::size_t>(ret));
    r.compressed_size = static_cast<uint64_t>(ret);
    r.checksum = crc32c(input);  // checksum of uncompressed bytes

    return r;
}

DecompressionResult Lz4Codec::decompress(std::span<const uint8_t> compressed,
                                         uint64_t max_output) {
    // LZ4 decompress_safe needs the original uncompressed size as dstCapacity.
    // We use max_output as the bound; the actual decompressed size is returned.
    if (compressed.size() == 0) {
        DecompressionResult r;
        r.uncompressed.clear();
        r.uncompressed_size = 0;
        return r;
    }

    // Allocate up to max_output (bounded by entry size limits from archive layer).
    std::vector<uint8_t> dst(static_cast<std::size_t>(max_output));
    if (dst.empty() && max_output > 0) {
        Throw(ErrorCode::DecompressionBomb,
              "declared output exceeds available memory");
    }

    const int ret = LZ4_decompress_safe(reinterpret_cast<const char*>(compressed.data()),
                                        reinterpret_cast<char*>(dst.data()),
                                        static_cast<int>(compressed.size()),
                                        static_cast<int>(max_output));

    if (ret < 0) {
        Throw(ErrorCode::ChecksumMismatch,
              "LZ4 decompression failed (corrupt or unsupported block): " +
              std::to_string(ret));
    }

    dst.resize(static_cast<std::size_t>(ret));
    DecompressionResult r;
    r.uncompressed = std::move(dst);
    r.uncompressed_size = static_cast<uint64_t>(ret);
    return r;
}

uint64_t Lz4Codec::estimate_compress_memory(int /*level*/) const {
    // LZ4 compression state is small; context + output buffer.
    return LZ4_compressBound(1024ULL * 1024ULL) + (64ULL * 1024ULL);
}

uint64_t Lz4Codec::estimate_decompress_memory(uint64_t /*compressed_size*/) const {
    return 64ULL * 1024ULL;
}

void register_lz4_codec() {
    CodecRegistry::instance().register_codec(
        std::make_unique<Lz4Codec>());
}

} // namespace nexapack
