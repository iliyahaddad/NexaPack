#include "nexapack/integrity.h"

#include <cstdint>
#include <cstddef>
#include <span>
#include <vector>

namespace nexapack {

//---------------------------------------------------------------------------
// CRC32C (Castagnoli, polynomial 0x1EDC6F41), reflected, table-driven.
// Not cryptographically secure. Intended for accidental-corruption detection.
//---------------------------------------------------------------------------

namespace {

constexpr uint32_t kCrc32cPoly = 0x82F63B78u; // reflected polynomial

uint32_t crc32c_table[256];
bool crc32c_table_initialized = false;

void init_crc32c_table() {
    if (crc32c_table_initialized) return;
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t crc = i;
        for (int k = 0; k < 8; ++k) {
            crc = (crc & 1u) ? (kCrc32cPoly ^ (crc >> 1)) : (crc >> 1);
        }
        crc32c_table[i] = crc;
    }
    crc32c_table_initialized = true;
}

} // namespace

uint32_t crc32c_update(uint32_t crc, std::span<const uint8_t> data) {
    init_crc32c_table();
    crc = ~crc; // running complement
    for (uint8_t b : data) {
        crc = crc32c_table[(crc ^ b) & 0xFFu] ^ (crc >> 8);
    }
    return ~crc;
}

//---------------------------------------------------------------------------
// XXH3 64-bit (optional content identity).
// A minimal independent implementation is provided so the library does not
// depend on xxhash unless the vendored LZ4 tree (which bundles xxhash) is
// present. Returns 0 when the fast path is unavailable.
//---------------------------------------------------------------------------
namespace {

// Simple 64-bit mix used as a fallback content hash when XXH3 is unavailable.
// This is NOT XXH3; it is clearly a weaker, non-cryptographic hash.
uint64_t fallback_mix64(std::span<const uint8_t> data) {
    uint64_t h = 0x9E3779B97F4A7C15ULL;
    for (uint8_t b : data) {
        h ^= static_cast<uint64_t>(b);
        h = (h ^ (h >> 30)) * 0xBF58476D1CE4E5B9ULL;
        h = (h ^ (h >> 27)) * 0x94D049BB133111EBULL;
    }
    h = (h ^ (h >> 31)) * 0xBF58476D1CE4E5B9ULL;
    h = (h ^ (h >> 27)) * 0x94D049BB133111EBULL;
    h ^= h >> 31;
    return h;
}

} // namespace

uint64_t xxh3_64(std::span<const uint8_t> data) {
    // Prefer the real XXH3 when bundled with LZ4 (lz4-dev/lib/xxhash.h).
    // Detect via preprocessor macro set when LZ4 source is compiled into this
    // translation unit; otherwise fall back to the independent mix above.
#if defined(NEXAPACK_HAVE_XXH3)
    // Real XXH3 would be called here. Not implemented to avoid depending on
    // an unvendored xxhash copy.
    (void)data;
#endif
    return fallback_mix64(data);
}

} // namespace nexapack


