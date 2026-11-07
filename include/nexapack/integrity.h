#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <span>

#include "nexapack.h"

namespace nexapack {

//---------------------------------------------------------------------------
// Integrity checksums.
// CRC32C (Castagnoli) for fast accidental-corruption detection.
// Not cryptographically secure; never used as a substitute for bounds checks.
//---------------------------------------------------------------------------

// Compute CRC32C over data using a software table-driven implementation.
// crc is the running CRC (0 to start). Returns updated CRC.
uint32_t crc32c_update(uint32_t crc, std::span<const uint8_t> data);

// Convenience: CRC32C of a buffer, starting from 0.
inline uint32_t crc32c(std::span<const uint8_t> data) {
    return crc32c_update(0u, data);
}

// XXH3 64-bit hash (optional, for content identity when available).
// Returns 0 if the implementation is unavailable.
uint64_t xxh3_64(std::span<const uint8_t> data);

// Coverage rules:
//   * chunk_checksum   : covers the uncompressed bytes of a single chunk.
//   * entry_checksum   : covers the concatenation of all uncompressed chunks
//                        for an entry (i.e. the full file content).
//   * metadata_crc     : covers the serialized metadata/entries section.
//   * header_crc       : covers header bytes [0, NEXAPACK_HEADER_CRC_OFFSET).
//   * archive_footer_crc: covers the footer section.

} // namespace nexapack
