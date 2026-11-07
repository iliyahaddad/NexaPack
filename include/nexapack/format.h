#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <string_view>

#include "nexapack.h"

namespace nexapack {

//---------------------------------------------------------------------------
// Little-endian integer encoding / decoding with bounds checking.
// All multi-byte integers in the NexaPack format are little-endian.
//---------------------------------------------------------------------------

// Read a little-endian unsigned integer of size N from src.
// Throws ErrorCode::TruncatedArchive if src has fewer than N bytes.
// Throws ErrorCode::Overflow if the value cannot be represented (N <= 8 only).
template <std::size_t N>
uint64_t read_le(std::span<const uint8_t> src) {
    static_assert(N >= 1 && N <= 8, "read_le supports 1..8 byte integers");
    if (src.size() < N) {
        Throw(ErrorCode::TruncatedArchive,
              "insufficient bytes to read little-endian integer");
    }
    uint64_t value = 0;
    for (std::size_t i = 0; i < N; ++i) {
        value |= static_cast<uint64_t>(src[i]) << (8 * i);
    }
    return value;
}

// Write a little-endian unsigned integer of size N to dst.
// dst must have at least N bytes.
template <std::size_t N>
void write_le(std::span<uint8_t> dst, uint64_t value) {
    static_assert(N >= 1 && N <= 8, "write_le supports 1..8 byte integers");
    if (dst.size() < N) {
        Throw(ErrorCode::InvalidArgument,
              "destination buffer too small for little-endian integer");
    }
    // Check that value fits in N bytes.
    uint64_t mask;
    if constexpr (N == 8) {
        mask = ~0ULL;
    } else {
        mask = (1ULL << (8 * N)) - 1;
    }
    if ((value & ~mask) != 0) {
        Throw(ErrorCode::Overflow,
              "integer value does not fit in requested byte width");
    }
    for (std::size_t i = 0; i < N; ++i) {
        dst[i] = static_cast<uint8_t>((value >> (8 * i)) & 0xFF);
    }
}

// Convenience aliases.
inline uint16_t read_u16(std::span<const uint8_t> src) { return static_cast<uint16_t>(read_le<2>(src)); }
inline uint32_t read_u32(std::span<const uint8_t> src) { return static_cast<uint32_t>(read_le<4>(src)); }
inline uint64_t read_u64(std::span<const uint8_t> src) { return read_le<8>(src); }
inline uint8_t  read_u8 (std::span<const uint8_t> src) { return static_cast<uint8_t>(read_le<1>(src)); }

inline void write_u16(std::span<uint8_t> dst, uint16_t v) { write_le<2>(dst, v); }
inline void write_u32(std::span<uint8_t> dst, uint32_t v) { write_le<4>(dst, v); }
inline void write_u64(std::span<uint8_t> dst, uint64_t v) { write_le<8>(dst, v); }
inline void write_u8 (std::span<uint8_t> dst, uint8_t  v) { write_le<1>(dst, v); }

// Checked addition that throws ErrorCode::Overflow on overflow.
inline uint64_t checked_add(uint64_t a, uint64_t b, std::string_view what) {
    if (b > ~0ULL - a) {
        Throw(ErrorCode::Overflow, std::string("overflow in checked_add: ") +
                                   std::string(what));
    }
    return a + b;
}

// Checked multiplication that throws ErrorCode::Overflow on overflow.
inline uint64_t checked_mul(uint64_t a, uint64_t b, std::string_view what) {
    if (a != 0 && b > ~0ULL / a) {
        Throw(ErrorCode::Overflow, std::string("overflow in checked_mul: ") +
                                   std::string(what));
    }
    return a * b;
}

// Verify that [offset, offset+length) lies within [0, size) with no overflow.
inline void check_range(uint64_t offset, uint64_t length, uint64_t size,
                        std::string_view what) {
    if (offset > size) {
        Throw(ErrorCode::InvalidOffset,
              std::string("offset ") + std::to_string(offset) +
              " beyond archive size " + std::to_string(size) + ": " +
              std::string(what));
    }
    if (length > size - offset) {
        Throw(ErrorCode::InvalidOffset,
              std::string("range [") + std::to_string(offset) + ", " +
              std::to_string(offset + length) + ") exceeds archive size " +
              std::to_string(size) + ": " + std::string(what));
    }
}

} // namespace nexapack
