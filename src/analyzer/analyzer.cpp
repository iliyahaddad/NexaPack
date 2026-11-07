#include "nexapack/analyzer.h"
#include <cmath>
#include <array>
#include <algorithm>
#include <fstream>

namespace nexapack {

namespace {

constexpr std::size_t kEntropySample = 64 * 1024;

double shannon_entropy(std::span<const uint8_t> data) {
    if (data.empty()) return 0.0;
    std::array<uint64_t, 256> counts{};
    for (uint8_t b : data) ++counts[b];
    double n = static_cast<double>(data.size());
    double entropy = 0.0;
    for (uint64_t c : counts) {
        if (c == 0) continue;
        double p = static_cast<double>(c) / n;
        entropy -= p * std::log2(p);
    }
    return entropy; // 0..8 bits per byte
}

bool looks_like_compressed(std::span<const uint8_t> data) {
    if (data.size() < 4) return false;
    // Common magic bytes for already-compressed formats.
    const uint8_t* d = data.data();
    // Zstandard frame magic: 28 B5 2F FD
    if (d[0] == 0x28 && d[1] == 0xB5 && d[2] == 0x2F && d[3] == 0xFD) return true;
    // LZ4 frame magic: 04 22 4D 18
    if (d[0] == 0x04 && d[1] == 0x22 && d[2] == 0x4D && d[3] == 0x18) return true;
    // gzip: 1F 8B
    if (d[0] == 0x1F && d[1] == 0x8B) return true;
    // zip: PK
    if (d[0] == 'P' && d[1] == 'K') return true;
    // Brotli: no fixed magic; skip.
    return false;
}

} // namespace

InputFeatures analyze_input(std::string_view file_path, uint64_t file_size,
                            std::size_t max_sample) {
    InputFeatures f;
    f.size = file_size;
    // Read a bounded sample.
    std::size_t sample_bytes = static_cast<std::size_t>(
        std::min<uint64_t>(file_size, static_cast<uint64_t>(max_sample)));
    std::vector<uint8_t> sample(sample_bytes);
    if (sample_bytes > 0) {
        std::ifstream in(std::string(file_path), std::ios::binary);
        if (!in) return f; // can't read; return zeroed features
        in.read(reinterpret_cast<char*>(sample.data()),
                static_cast<std::streamsize>(sample_bytes));
        sample_bytes = static_cast<std::size_t>(in.gcount());
        sample.resize(sample_bytes);
    }
    return analyze_buffer(sample);
}

InputFeatures analyze_buffer(std::span<const uint8_t> data) {
    InputFeatures f;
    f.size = static_cast<uint64_t>(data.size());
    if (data.empty()) {
        f.entropy_bits_per_byte = 0.0;
        f.alphabet_ratio = 0.0;
        f.looks_compressed = false;
        return f;
    }
    std::span<const uint8_t> sample = data;
    if (data.size() > kEntropySample) {
        sample = {data.data(), kEntropySample};
    }
    f.entropy_bits_per_byte = shannon_entropy(sample);
    {
        std::array<bool, 256> seen{};
        uint64_t distinct = 0;
        for (uint8_t b : sample) {
            if (!seen[b]) { seen[b] = true; ++distinct; }
        }
        f.alphabet_ratio = static_cast<double>(distinct) / 256.0;
    }
    f.looks_compressed = looks_like_compressed(sample);
    return f;
}

} // namespace nexapack


