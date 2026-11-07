#include "nexapack/safe_path.h"

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <system_error>
#include <cstring>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

namespace nexapack {

namespace {

bool has_path_traversal(std::string_view p) {
    // Reject ".." as any path component.
    std::size_t start = 0;
    while (start <= p.size()) {
        auto sep = p.find('/', start);
        std::string_view comp;
        if (sep == std::string_view::npos) {
            comp = p.substr(start);
            start = p.size() + 1;
        } else {
            comp = p.substr(start, sep - start);
            start = sep + 1;
        }
        if (comp == ".." || comp.empty()) return true;
        if (sep == std::string_view::npos) break;
    }
    return false;
}

bool is_windows_absolute_or_drive(std::string_view p) {
    if (p.empty()) return false;
    // Starts with '/' or '\\' -> absolute/UNC
    if (p[0] == '/' || p[0] == '\\') return true;
    // Drive letter: X: or X:/ or X:
    if (p.size() >= 2 && p[1] == ':' &&
        ((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z'))) {
        return true;
    }
    return false;
}

bool contains_nul(std::string_view p) {
    return p.find('\0') != std::string_view::npos;
}

std::string to_native_separators(std::string_view p) {
    std::string out;
    out.reserve(p.size());
    for (char c : p) out.push_back(c == '/' ? '\\' : c);
    return out;
}

} // namespace

std::optional<std::string> validate_entry_path(std::string_view path) {
    if (path.empty()) return std::nullopt;
    if (path.size() > NEXAPACK_MAX_PATH_LENGTH) return std::nullopt;
    if (contains_nul(path)) return std::nullopt;
    if (is_windows_absolute_or_drive(path)) return std::nullopt;
    if (has_path_traversal(path)) return std::nullopt;
    // Reject control characters and reserved names.
    for (char c : path) {
        if (static_cast<unsigned char>(c) < 0x20) return std::nullopt;
    }
    // Reject Windows reserved base names (NUL, CON, PRN, etc.) as full
    // path components to avoid filesystem weirdness.
    std::size_t start = 0;
    while (start <= path.size()) {
        auto sep = path.find('/', start);
        std::string_view comp;
        if (sep == std::string_view::npos) { comp = path.substr(start); start = path.size()+1; }
        else { comp = path.substr(start, sep-start); start = sep+1; }
        if (!comp.empty()) {
            std::string base;
            base.reserve(comp.size());
            for (char c : comp) {
                if (c == '.') break;
                base.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
            }
            static const char* reserved[] = {
                "CON","PRN","AUX","NUL","COM1","COM2","COM3","COM4","COM5",
                "COM6","COM7","COM8","COM9","LPT1","LPT2","LPT3","LPT4",
                "LPT5","LPT6","LPT7","LPT8","LPT9"
            };
            for (const char* r : reserved) {
                if (base == r) return std::nullopt;
            }
        }
        if (sep == std::string_view::npos) break;
    }
    return std::string(path);
}

std::optional<std::string> safe_output_path(std::string_view destination,
                                            std::string_view entry_path) {
    auto validated = validate_entry_path(entry_path);
    if (!validated) return std::nullopt;

    fs::path dest(destination);
    std::error_code ec;
    fs::create_directories(dest, ec);
    if (ec) return std::nullopt;
    auto dest_canon = fs::weakly_canonical(dest, ec);
    if (ec) return std::nullopt;

    // Build candidate path under destination.
    fs::path cand = dest / to_native_separators(*validated);
    auto cand_canon = fs::weakly_canonical(cand, ec);
    if (ec) return std::nullopt;

    // Check candidate is within destination.
    auto dest_str = dest_canon.generic_string();
    auto cand_str = cand_canon.generic_string();
    // Normalize trailing separator.
    if (!dest_str.empty() && dest_str.back() != '/' && dest_str.back() != '\\') {
        dest_str += '/';
    }
    if (cand_str.compare(0, dest_str.size(), dest_str) != 0) {
        return std::nullopt;
    }
    return cand_canon.generic_string();
}

std::error_code ensure_parent_dir(const std::string& file_path) {
    fs::path p(file_path);
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    return ec;
}

std::error_code atomic_write_file(std::string_view target_path,
                                  std::span<const uint8_t> content,
                                  bool overwrite) {
    fs::path target(target_path);
    std::error_code ec;
    if (fs::exists(target, ec) && !overwrite) {
        return std::make_error_code(std::errc::file_exists);
    }
    fs::path parent = target.parent_path();
    if (parent.empty()) parent = ".";
    fs::create_directories(parent, ec);
    if (ec) return ec;

    // Temp file in same directory (same volume for atomic rename).
    // Pick a non-existent temp name.
    fs::path temp;
    {
        std::string base = target.filename().string() + ".npx_tmp";
        for (int attempt = 0; attempt < 1000; ++attempt) {
            fs::path cand = parent / (attempt == 0 ? base : (base + "." + std::to_string(attempt)));
            std::error_code exists_ec;
            bool exists = fs::exists(cand, exists_ec);
            if (!exists || exists_ec) { temp = cand; break; }
        }
    }
    if (temp.empty()) return std::make_error_code(std::errc::io_error);

    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) { fs::remove(temp, ec); return std::make_error_code(std::errc::io_error); }
        out.write(reinterpret_cast<const char*>(content.data()),
                  static_cast<std::streamsize>(content.size()));
        if (!out) { fs::remove(temp, ec); return std::make_error_code(std::errc::io_error); }
        out.flush();
    }
    std::error_code rec_ec;
    bool renamed = false;
    if (overwrite) {
        // On Windows rename cannot replace an existing file; remove first.
        // Retry in case of transient sharing violations.
        for (int attempt = 0; attempt < 5 && !renamed; ++attempt) {
            fs::remove(target, ec);
            std::error_code r;
            fs::rename(temp, target, r);
            renamed = !r;
        }
        if (!renamed) {
            // Fall back to a direct truncating write (overwrite was requested).
            std::ofstream out(target, std::ios::binary | std::ios::trunc);
            if (!out) rec_ec = std::make_error_code(std::errc::io_error);
            else {
                out.write(reinterpret_cast<const char*>(content.data()),
                          static_cast<std::streamsize>(content.size()));
                if (!out) rec_ec = std::make_error_code(std::errc::io_error);
                else out.flush();
            }
            fs::remove(temp, ec);
            return rec_ec;
        }
    } else {
        fs::rename(temp, target, rec_ec);
    }
    if (rec_ec) {
        fs::remove(temp, ec);
        return rec_ec;
    }
    return {};
}

} // namespace nexapack


