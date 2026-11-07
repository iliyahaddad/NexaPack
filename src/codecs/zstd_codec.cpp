#include "nexapack/codec_interface.h"
#include "nexapack/integrity.h"
#include "nexapack/nexapack.h"

#include <zstd.h>

#include <cstring>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>
#include <span>

namespace nexapack {

class ZstdCodec : public ICompressionCodec {
public:
    CodecId id() const noexcept override { return CodecId::Zstd; }
    std::string_view name() const noexcept override { return "zstd"; }
    int min_level() const noexcept override { return 1; }
    int max_level() const noexcept override { return 22; } // ZSTD_maxCLevel()
    int default_level() const noexcept override { return 3; }
    bool available() const noexcept override { return true; }

    CompressionResult compress(std::span<const uint8_t> input, int level) override {
        if (level < min_level() || level > max_level()) {
            Throw(ErrorCode::InvalidArgument,
                  "zstd level must be in [1, 22]");
        }
        CompressionResult r;
        r.uncompressed_size = static_cast<uint64_t>(input.size());
        // Bound the output buffer so allocation is deterministic and bounded.
        const size_t bound = ZSTD_compressBound(r.uncompressed_size);
        r.compressed.resize(bound);
        const size_t ret = ZSTD_compress(r.compressed.data(), bound,
                                         input.data(), input.size(), level);
        if (ZSTD_isError(ret)) {
            Throw(ErrorCode::InternalError,
                  "zstd compression failed: " +
                  std::string(ZSTD_getErrorName(ret)));
        }
        r.compressed.resize(ret);
        r.compressed_size = static_cast<uint64_t>(ret);
        r.checksum = crc32c(input); // checksum of the uncompressed bytes
        return r;
    }

    DecompressionResult decompress(std::span<const uint8_t> compressed,
                                   uint64_t max_output) override {
        // Determine the frame's declared content size before allocating, so a
        // malicious frame cannot force an oversized allocation.
        const long long declared = ZSTD_getFrameContentSize(
            compressed.data(), compressed.size());
        if (declared < 0) {
            // ZSTD_CONTENTSIZE_ERROR or UNKNOWN: cannot determine up front.
            // Fall back to a bounded single-pass decompress with a 64 KiB
            // initial allocation; ZSTD_decompress will reject invalid frames.
            constexpr uint64_t kInitialBound = 64ULL * 1024ULL;
            uint64_t bound = std::min<uint64_t>(kInitialBound, max_output);
            std::vector<uint8_t> dst(static_cast<size_t>(bound));
            const size_t ret = ZSTD_decompress(dst.data(), dst.size(),
                                               compressed.data(), compressed.size());
            if (ZSTD_isError(ret)) {
                Throw(ErrorCode::ChecksumMismatch,
                      "zstd decompression failed: " +
                      std::string(ZSTD_getErrorName(ret)));
            }
            dst.resize(ret);
            DecompressionResult r;
            r.uncompressed = std::move(dst);
            r.uncompressed_size = static_cast<uint64_t>(ret);
            return r;
        }
        if (declared == 0) {
            DecompressionResult r;
            r.uncompressed_size = 0;
            return r;
        }
        if (static_cast<uint64_t>(declared) > max_output) {
            Throw(ErrorCode::DecompressionBomb,
                  "declared zstd content size exceeds output limit");
        }
        std::vector<uint8_t> dst(static_cast<size_t>(declared));
        const size_t ret = ZSTD_decompress(dst.data(), dst.size(),
                                           compressed.data(), compressed.size());
        if (ZSTD_isError(ret)) {
            Throw(ErrorCode::ChecksumMismatch,
                  "zstd decompression failed (corrupt or unsupported frame): " +
                  std::string(ZSTD_getErrorName(ret)));
        }
        dst.resize(ret);
        DecompressionResult r;
        r.uncompressed = std::move(dst);
        r.uncompressed_size = static_cast<uint64_t>(ret);
        return r;
    }

    uint64_t estimate_compress_memory(int /*level*/) const override {
        // One-shot compression needs a context plus the output bound.
        return ZSTD_compressBound(1024ULL * 1024ULL) + (128ULL * 1024ULL);
    }
    uint64_t estimate_decompress_memory(uint64_t /*compressed_size*/) const override {
        return 128ULL * 1024ULL;
    }
};

void register_zstd_codec() {
    CodecRegistry::instance().register_codec(
        std::make_unique<ZstdCodec>());
}

} // namespace nexapack
