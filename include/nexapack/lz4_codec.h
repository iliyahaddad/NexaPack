#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <vector>
#include <memory>

#include "codec_interface.h"

namespace nexapack {

// LZ4 codec: fast compression/decompression using the official LZ4 library.
// Always available when NEXAPACK_ENABLE_LZ4=ON and lz4-dev source is present.
class Lz4Codec : public ICompressionCodec {
public:
    CodecId id() const noexcept override { return CodecId::Lz4; }
    std::string_view name() const noexcept override { return "lz4"; }
    int min_level() const noexcept override { return 1; }
    int max_level() const noexcept override { return 12; }  // LZ4 HC levels 1-12
    int default_level() const noexcept override { return 1; }
    bool available() const noexcept override { return true; }

    CompressionResult compress(std::span<const uint8_t> input, int level) override;
    DecompressionResult decompress(std::span<const uint8_t> compressed,
                                   uint64_t max_output) override;

    uint64_t estimate_compress_memory(int level) const override;
    uint64_t estimate_decompress_memory(uint64_t compressed_size) const override;
};

// Register the LZ4 codec in the global registry.
void register_lz4_codec();

} // namespace nexapack
