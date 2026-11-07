#include "nexapack/store_codec.h"
#include "nexapack/integrity.h"

#include <cstring>
#include <vector>
#include <span>
#include <string>

namespace nexapack {

CompressionResult StoreCodec::compress(std::span<const uint8_t> input, int level) {
    if (level < min_level() || level > max_level()) {
        Throw(ErrorCode::InvalidArgument, "store codec level must be 0");
    }
    CompressionResult r;
    r.uncompressed_size = static_cast<uint64_t>(input.size());
    r.compressed.assign(input.begin(), input.end());
    r.compressed_size = static_cast<uint64_t>(r.compressed.size());
    r.checksum = crc32c(r.compressed);
    return r;
}

DecompressionResult StoreCodec::decompress(std::span<const uint8_t> compressed,
                                           uint64_t max_output) {
    if (compressed.size() > max_output) {
        Throw(ErrorCode::DecompressionBomb,
              "declared store output exceeds max_output limit");
    }
    DecompressionResult r;
    r.uncompressed.assign(compressed.begin(), compressed.end());
    r.uncompressed_size = static_cast<uint64_t>(r.uncompressed.size());
    return r;
}

uint64_t StoreCodec::estimate_compress_memory(int /*level*/) const { return 0; }
uint64_t StoreCodec::estimate_decompress_memory(uint64_t /*compressed_size*/) const { return 0; }

void register_store_codec() {
    CodecRegistry::instance().register_codec(std::make_unique<StoreCodec>());
}

} // namespace nexapack
