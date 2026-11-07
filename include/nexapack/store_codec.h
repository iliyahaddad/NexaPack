#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <vector>
#include <memory>

#include "codec_interface.h"

namespace nexapack {

// Store codec: passthrough, no compression. Always available.
class StoreCodec : public ICompressionCodec {
public:
    CodecId id() const noexcept override { return CodecId::Store; }
    std::string_view name() const noexcept override { return "store"; }
    int min_level() const noexcept override { return 0; }
    int max_level() const noexcept override { return 0; }
    int default_level() const noexcept override { return 0; }
    bool available() const noexcept override { return true; }

    CompressionResult compress(std::span<const uint8_t> input, int level) override;
    DecompressionResult decompress(std::span<const uint8_t> compressed,
                                   uint64_t max_output) override;

    uint64_t estimate_compress_memory(int level) const override;
    uint64_t estimate_decompress_memory(uint64_t compressed_size) const override;
};

// Register the store codec in the global registry.
void register_store_codec();

} // namespace nexapack
