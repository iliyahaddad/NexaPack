#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>
#include <span>
#include <memory>
#include <istream>
#include <ostream>

#include "nexapack.h"
#include "codec_interface.h"

namespace nexapack {

//---------------------------------------------------------------------------
// Archive writer (create)
//---------------------------------------------------------------------------
struct CreateOptions {
    std::string archive_path;
    std::vector<std::string> input_paths;   // files or directories
    bool recursive = true;
    CodecId force_codec = CodecId::Store;   // codec when codec_forced is true
    bool codec_forced = false;              // true when --codec was explicitly passed
    int force_level = -1;                   // -1 = codec default
    int threads = 1;                          // worker threads (planned)
    uint64_t chunk_size = 1024ULL * 1024ULL; // 1 MiB default chunk
    bool overwrite = false;
    bool verbose = false;
};

//---------------------------------------------------------------------------
// Archive reader (list / inspect / verify / extract)
//---------------------------------------------------------------------------
struct ExtractOptions {
    std::string archive_path;
    std::string output_dir;
    std::optional<std::string> single_file; // if set, extract only this entry
    bool overwrite = false;
    bool verbose = false;
};

struct VerifyOptions {
    std::string archive_path;
    bool verbose = false;
};

struct InspectResult {
    std::string archive_path;
    uint64_t archive_size = 0;
    uint8_t version_major = 0;
    uint8_t version_minor = 0;
    uint16_t feature_flags = 0;
    uint64_t entry_count = 0;
    uint64_t total_uncompressed = 0;
    uint64_t total_compressed = 0;
    std::vector<Entry> entries;
};

// Create an archive from input paths.
void create_archive(const CreateOptions& opts);

// List archive entries (metadata only, no data decompression).
std::vector<Entry> list_archive(std::string_view archive_path);

// Inspect an archive: read and validate header/index/footer without extracting.
InspectResult inspect_archive(std::string_view archive_path);

// Verify integrity of every chunk and metadata checksum.
void verify_archive(const VerifyOptions& opts);

// Extract the whole archive (or one file) to output_dir.
void extract_archive(const ExtractOptions& opts);

} // namespace nexapack
