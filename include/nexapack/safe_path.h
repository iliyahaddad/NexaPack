#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <system_error>
#include <span>

#include "nexapack.h"

namespace nexapack {

//---------------------------------------------------------------------------
// Safe filesystem path handling for extraction.
// All paths are treated as untrusted. The destination directory is the trust
// anchor; no resolved output path may escape it.
//---------------------------------------------------------------------------

// Resolve a candidate output path relative to destination, returning the
// canonical absolute path if it lies within destination, else std::nullopt.
// Symlinks inside destination are resolved and re-checked; hard links and
// junction points are rejected if they would escape.
// On Windows, absolute paths, drive-letter paths, and UNC paths are rejected.
// On all platforms, ".." components and trailing separators are normalized.
std::optional<std::string> safe_output_path(std::string_view destination,
                                            std::string_view entry_path);

// Validate an entry path for storage in an archive. Returns nullopt if the
// path must be rejected (absolute, traversal, too long, NUL, etc.).
std::optional<std::string> validate_entry_path(std::string_view path);

// Create parent directories for a file path within destination. Returns the
// created directory path, or error_code on failure.
std::error_code ensure_parent_dir(const std::string& file_path);

// Atomic write: write content to a temp file in the same directory, then
// rename over the target. Never clobbers an existing file unless overwrite
// is true. Returns error_code on failure.
std::error_code atomic_write_file(std::string_view target_path,
                                  std::span<const uint8_t> content,
                                  bool overwrite);

} // namespace nexapack
