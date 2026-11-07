#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <system_error>
#include <stdexcept>
#include <vector>

namespace nexapack {

// Project version string (filled in by CMake at configure time).
extern const char* NEXAPACK_VERSION_STRING;

// Archive magic signature: "NPX1" (provisional - subject to collision check).
inline constexpr char NEXAPACK_MAGIC[4] = {'N', 'P', 'X', '1'};
// Footer magic signature.
inline constexpr char NEXAPACK_FOOTER_MAGIC[4] = {'N', 'P', 'X', 'F'};

// Format version: v0 is explicitly experimental.
inline constexpr uint8_t NEXAPACK_FORMAT_MAJOR = 0;
inline constexpr uint8_t NEXAPACK_FORMAT_MINOR = 0;

// Fixed-size header layout (all multi-byte integers little-endian).
//   offset 0  : magic[4]            = "NPX1"
//   offset 4  : version_major  u8
//   offset 5  : version_minor  u8
//   offset 6  : feature_flags  u16
//   offset 8  : header_size    u32  (total bytes of header section)
//   offset 12 : entry_count    u64
//   offset 20 : metadata_size  u64  (bytes of metadata section following header)
//   offset 28 : data_offset    u64  (offset to data-chunks section)
//   offset 36 : index_offset   u64
//   offset 44 : index_size     u64
//   offset 52 : footer_offset  u64
//   offset 60 : footer_size    u64
//   offset 68 : header_crc     u32  (covers bytes [0, 68))
inline constexpr std::size_t NEXAPACK_HEADER_FIXED_SIZE = 72;
inline constexpr std::size_t NEXAPACK_HEADER_CRC_OFFSET = 68;

// Codec identifiers (stored per entry / per chunk).
// Brotli and Lzma2 are optional and not yet implemented; their IDs are
// reserved for future integration.
enum class CodecId : uint8_t {
    Store  = 0,
    Zstd   = 1,
    Lz4    = 2,
    Brotli = 3,
    Lzma2  = 4,
};

// Entry flags.
enum class EntryFlags : uint16_t {
    None        = 0x0000,
    IsDirectory = 0x0001,
    Symlink     = 0x0002,  // represented but not followed by default
};

// Limits (hard safety bounds).
inline constexpr uint64_t NEXAPACK_MAX_PATH_LENGTH      = 4096;
inline constexpr uint64_t NEXAPACK_MAX_ENTRY_COUNT      = 1'000'000;
inline constexpr uint64_t NEXAPACK_MAX_METADATA_SIZE    = 256ULL * 1024ULL * 1024ULL; // 256 MiB
inline constexpr uint64_t NEXAPACK_MAX_CHUNK_SIZE       = 256ULL * 1024ULL * 1024ULL; // 256 MiB
inline constexpr uint64_t NEXAPACK_MAX_DECOMPRESSED_PER_ENTRY = 4ULL * 1024ULL * 1024ULL * 1024ULL; // 4 GiB
inline constexpr uint64_t NEXAPACK_MAX_TOTAL_DECOMPRESSED = 16ULL * 1024ULL * 1024ULL * 1024ULL; // 16 GiB
inline constexpr uint64_t NEXAPACK_MAX_ARCHIVE_SIZE     = 1ULL << 32; // 4 GiB

//---------------------------------------------------------------------------
// Error reporting
//---------------------------------------------------------------------------
enum class ErrorCode : int {
    Ok                  = 0,
    InvalidArgument     = 1,
    IoError             = 2,
    UnsupportedFormat   = 3,
    UnknownCodec        = 4,
    CorruptArchive      = 5,
    TruncatedArchive    = 6,
    InvalidOffset       = 7,
    Overflow            = 8,
    LimitExceeded       = 9,
    PathUnsafe          = 10,
    DecompressionBomb   = 11,
    ChecksumMismatch    = 12,
    NotImplemented      = 13,
    InternalError       = 14,
};

class Exception : public std::runtime_error {
public:
    explicit Exception(ErrorCode code, std::string message)
        : std::runtime_error(std::move(message)), code_(code) {}
    ErrorCode code() const noexcept { return code_; }
private:
    ErrorCode code_;
};

inline void Throw(ErrorCode code, std::string message) {
    throw Exception(code, std::move(message));
}

} // namespace nexapack
