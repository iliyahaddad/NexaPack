#include "nexapack/format.h"

// Explicit instantiations for the widths we use.
namespace nexapack {
template uint64_t read_le<1>(std::span<const uint8_t>);
template uint64_t read_le<2>(std::span<const uint8_t>);
template uint64_t read_le<4>(std::span<const uint8_t>);
template uint64_t read_le<8>(std::span<const uint8_t>);

template void write_le<1>(std::span<uint8_t>, uint64_t);
template void write_le<2>(std::span<uint8_t>, uint64_t);
template void write_le<4>(std::span<uint8_t>, uint64_t);
template void write_le<8>(std::span<uint8_t>, uint64_t);
}


