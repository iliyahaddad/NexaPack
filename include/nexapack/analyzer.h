#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <span>
#include <vector>

namespace nexapack {

struct InputFeatures {
    uint64_t size = 0;
    double entropy_bits_per_byte = 0.0;
    double alphabet_ratio = 0.0;
    bool looks_compressed = false;
};

InputFeatures analyze_input(std::string_view file_path, uint64_t file_size,
                            std::size_t max_sample = 64 * 1024);

InputFeatures analyze_buffer(std::span<const uint8_t> data);

} // namespace nexapack
