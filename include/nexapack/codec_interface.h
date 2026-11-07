#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <optional>
#include <span>

#include "nexapack.h"

namespace nexapack {

//---------------------------------------------------------------------------
// Codec abstraction (Milestone 5+)
//---------------------------------------------------------------------------
struct CompressionResult {
    std::vector<uint8_t> compressed;      // owned output buffer
    uint64_t uncompressed_size = 0;       // size of input
    uint64_t compressed_size = 0;         // compressed.size()
    uint32_t checksum = 0;                // checksum of uncompressed data
};

struct DecompressionResult {
    std::vector<uint8_t> uncompressed;    // owned output buffer
    uint64_t uncompressed_size = 0;
};

// A single independently-decodable chunk descriptor stored in the index.
struct ChunkDescriptor {
    uint64_t offset = 0;                  // offset of chunk data in archive
    uint64_t compressed_length = 0;       // compressed bytes on disk
    uint64_t uncompressed_length = 0;     // bytes after decode
    uint32_t checksum = 0;                // checksum of uncompressed bytes
    CodecId codec = CodecId::Store;
    uint8_t codec_level = 0;              // codec-specific level needed to decode
};

// One logical archive entry (file or directory).
struct Entry {
    std::string path;                     // UTF-8, relative, no leading slash
    bool is_directory = false;
    uint64_t uncompressed_size = 0;
    uint64_t total_compressed_size = 0;   // sum of chunk compressed_lengths
    uint32_t chunk_count = 0;
    CodecId codec = CodecId::Store;
    uint8_t codec_level = 0;
    uint32_t checksum = 0;                // checksum of full uncompressed content
    std::vector<ChunkDescriptor> chunks;
};

// Codec capability info.
struct CodecInfo {
    CodecId id;
    std::string name;
    int min_level = 0;
    int max_level = 0;
    int default_level = 0;
    bool supports_streaming = true;
    uint64_t max_window = 0;              // decoder window limit, 0 = unspecified
};

// Abstract codec interface. Concrete implementations live in src/codecs/.
class ICompressionCodec {
public:
    virtual ~ICompressionCodec() = default;

    virtual CodecId id() const noexcept = 0;
    virtual std::string_view name() const noexcept = 0;
    virtual int min_level() const noexcept = 0;
    virtual int max_level() const noexcept = 0;
    virtual int default_level() const noexcept = 0;
    virtual bool available() const noexcept = 0;

    // Compress input. level must be within [min_level, max_level].
    virtual CompressionResult compress(std::span<const uint8_t> input, int level) = 0;

    // Decompress compressed input up to max_output bytes.
    virtual DecompressionResult decompress(std::span<const uint8_t> compressed,
                                           uint64_t max_output) = 0;

    // Estimate memory required to compress/decompress one chunk (bytes).
    virtual uint64_t estimate_compress_memory(int level) const = 0;
    virtual uint64_t estimate_decompress_memory(uint64_t compressed_size) const = 0;
};

// Registry of available codecs.
class CodecRegistry {
public:
    static CodecRegistry& instance();

    void register_codec(std::unique_ptr<ICompressionCodec> codec);
    ICompressionCodec* find(CodecId id) const;
    std::vector<CodecInfo> list() const;

private:
    std::vector<std::unique_ptr<ICompressionCodec>> codecs_;
};

// Register all built-in codecs (store always; zstd when available).
// Call once at program start before using the registry.
void register_all_builtin_codecs();

} // namespace nexapack
