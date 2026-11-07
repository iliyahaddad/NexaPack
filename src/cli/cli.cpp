#include "nexapack/archive.h"
#include "nexapack/codec_interface.h"
#include "nexapack/cli_exit.h"
#include "nexapack/nexapack.h"

#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <cstdio>
#include <cstdint>

namespace nexapack {

namespace {

void print_help() {
    std::printf(
        "NexaPack v%s - adaptive archive format and compression engine\n"
        "\n"
        "Usage: npx <command> [options]\n"
        "\n"
        "Commands:\n"
        "  create <archive.npx> <input paths...>   Create an archive\n"
        "  list <archive.npx>                      List archive entries\n"
        "  inspect <archive.npx>                   Inspect and validate structure\n"
        "  verify <archive.npx>                    Verify integrity of all chunks\n"
        "  extract <archive.npx> --output <dir>    Extract all entries\n"
        "  extract <archive.npx> --file <path> --output <dir>  Extract one entry\n"
        "  benchmark <dataset directory>           Run benchmarks\n"
        "  version                                 Show version\n"
        "\n"
        "Options:\n"
        "  --mode fast|balanced|dense|store        Compression planner policy (default: balanced)\n"
        "  --codec store|zstd|lz4                  Force a codec (default: store/planner)\n"
        "  --level <int>                           Compression level\n"
        "  --chunk-size <bytes>                    Chunk size (default: 1048576)\n"
        "  --threads <count>                       Worker threads (default: 1)\n"
        "  --overwrite                             Overwrite existing outputs\n"
        "  --no-recursive                          Do not recurse into directories\n"
        "  --verbose                               Verbose output\n"
        "  --json                                  Machine-readable JSON output\n"
        "  --help                                  Show this help\n"
        "\n"
        "Exit codes: 0 ok, 1 invalid args, 2 unsupported format, 3 corrupt archive,\n"
        "            4 I/O failure, 5 verification failure, 6 internal error.\n"
        "\n"
        "Status: experimental research release. Store + Zstd codecs.\n",
        NEXAPACK_VERSION_STRING ? NEXAPACK_VERSION_STRING : "0.1.0");
}

std::string require_value(int argc, char** argv, int& i, std::string_view flag) {
    if (i + 1 >= argc) {
        std::fprintf(stderr, "error: %s requires a value\n", std::string(flag).c_str());
        std::exit(static_cast<int>(ExitCode::ExitInvalidArguments));
    }
    return argv[++i];
}

} // namespace

int run_cli(int argc, char** argv) {
    if (argc < 2) {
        print_help();
        return static_cast<int>(ExitCode::ExitInvalidArguments);
    }

    std::string cmd = argv[1];

    if (cmd == "--help" || cmd == "-h" || cmd == "help") {
        print_help();
        return static_cast<int>(ExitCode::ExitOk);
    }
    if (cmd == "version" || cmd == "--version") {
        std::printf("NexaPack v%s (experimental)\n",
                    NEXAPACK_VERSION_STRING ? NEXAPACK_VERSION_STRING : "0.1.0");
        return static_cast<int>(ExitCode::ExitOk);
    }

    auto fail = [](int code, std::string_view msg) {
        std::fprintf(stderr, "error: %s\n", std::string(msg).c_str());
        return code;
    };

    try {
        if (cmd == "create") {
            CreateOptions opts;
            bool verbose = false, json = false;
            std::string mode = "balanced";
            std::string force_codec;
            int level = -1;
            uint64_t chunk_size = 1024ULL * 1024ULL;
            int threads = 1;
            bool overwrite = false;
            bool recursive = true;
            int i = 2;
            std::string archive_path;
            while (i < argc) {
                std::string_view a = argv[i];
                if (a == "--mode") mode = require_value(argc, argv, i, "--mode");
                else if (a == "--codec") force_codec = require_value(argc, argv, i, "--codec");
                else if (a == "--level") level = std::stoi(require_value(argc, argv, i, "--level"));
                else if (a == "--chunk-size") chunk_size = std::stoull(require_value(argc, argv, i, "--chunk-size"));
                else if (a == "--threads") threads = std::stoi(require_value(argc, argv, i, "--threads"));
                else if (a == "--overwrite") overwrite = true;
                else if (a == "--no-recursive") recursive = false;
                else if (a == "--verbose") verbose = true;
                else if (a == "--json") json = true;
                else if (a == "--help") { print_help(); return static_cast<int>(ExitCode::ExitOk); }
                else if (archive_path.empty()) archive_path = std::string(a);
                else opts.input_paths.push_back(std::string(a));
                ++i;
            }
            if (archive_path.empty()) return fail(ExitCode::ExitInvalidArguments, "create requires <archive.npx>");
            if (opts.input_paths.empty()) return fail(ExitCode::ExitInvalidArguments, "create requires at least one input path");
            if (mode == "fast") { /* planner policy */ }
            else if (mode == "balanced") { }
            else if (mode == "dense") { }
            else if (mode == "store") { }
            else return fail(ExitCode::ExitInvalidArguments, "invalid --mode");
            if (force_codec == "store") { opts.force_codec = CodecId::Store; opts.codec_forced = true; }
            else if (force_codec == "zstd") { opts.force_codec = CodecId::Zstd; opts.codec_forced = true; }
            else if (force_codec == "lz4") { opts.force_codec = CodecId::Lz4; opts.codec_forced = true; }
            else if (!force_codec.empty()) return fail(ExitCode::ExitInvalidArguments, "invalid --codec");
            opts.force_level = level;
            opts.chunk_size = chunk_size;
            opts.threads = threads;
            opts.overwrite = overwrite;
            opts.recursive = recursive;
            opts.verbose = verbose;
            opts.archive_path = archive_path;

            create_archive(opts);
            if (json) {
                std::printf("{\"command\":\"create\",\"archive\":\"%s\",\"status\":\"ok\"}\n", archive_path.c_str());
            } else if (verbose) {
                std::printf("created %s\n", archive_path.c_str());
            }
            return static_cast<int>(ExitCode::ExitOk);
        }
        else if (cmd == "list") {
            std::string archive_path;
            bool json = false, verbose = false;
            int i = 2;
            while (i < argc) {
                std::string_view a = argv[i];
                if (a == "--json") json = true;
                else if (a == "--verbose") verbose = true;
                else if (a == "--help") { print_help(); return static_cast<int>(ExitCode::ExitOk); }
                else archive_path = std::string(a);
                ++i;
            }
            if (archive_path.empty()) return fail(ExitCode::ExitInvalidArguments, "list requires <archive.npx>");
            auto entries = list_archive(archive_path);
            (void)verbose;
            if (json) {
                std::printf("{\"command\":\"list\",\"archive\":\"%s\",\"entries\":[%s]}\n",
                    archive_path.c_str(), "");
                // Simple JSON array
                std::printf("{\"command\":\"list\",\"archive\":\"%s\",\"count\":%zu,\"entries\":[",
                    archive_path.c_str(), entries.size());
                for (size_t k = 0; k < entries.size(); ++k) {
                    if (k) std::printf(",");
                    std::printf("{\"path\":\"%s\",\"directory\":%s,\"size\":%llu,\"codec\":%d}",
                        entries[k].path.c_str(),
                        entries[k].is_directory ? "true" : "false",
                        (unsigned long long)entries[k].uncompressed_size,
                        static_cast<int>(entries[k].codec));
                }
                std::printf("]}\n");
            } else {
                std::printf("%-6s %-12s %-12s %s\n", "SIZE", "CODEC", "TYPE", "PATH");
                for (auto& e : entries) {
                    std::printf("%-12llu %-12s %-12s %s\n",
                        (unsigned long long)e.uncompressed_size,
                        e.codec == CodecId::Store ? "store" :
                        e.codec == CodecId::Zstd ? "zstd" :
                        e.codec == CodecId::Lz4 ? "lz4" : "?",
                        e.is_directory ? "dir" : "file",
                        e.path.c_str());
                }
            }
            return static_cast<int>(ExitCode::ExitOk);
        }
        else if (cmd == "inspect") {
            std::string archive_path;
            bool json = false, verbose = false;
            int i = 2;
            while (i < argc) {
                std::string_view a = argv[i];
                if (a == "--json") json = true;
                else if (a == "--verbose") verbose = true;
                else if (a == "--help") { print_help(); return static_cast<int>(ExitCode::ExitOk); }
                else archive_path = std::string(a);
                ++i;
            }
            if (archive_path.empty()) return fail(ExitCode::ExitInvalidArguments, "inspect requires <archive.npx>");
            auto r = inspect_archive(archive_path);
            (void)verbose;
            if (json) {
                std::printf("{\"command\":\"inspect\",\"archive\":\"%s\",\"version\":%u.%u,\"entries\":%llu,\"total_uncompressed\":%llu,\"total_compressed\":%llu,\"status\":\"valid\"}\n",
                    archive_path.c_str(), r.version_major, r.version_minor,
                    (unsigned long long)r.entry_count,
                    (unsigned long long)r.total_uncompressed,
                    (unsigned long long)r.total_compressed);
            } else {
                std::printf("Archive: %s\n", archive_path.c_str());
                std::printf("Format: NexaPack v%u.%u (experimental)\n", r.version_major, r.version_minor);
                std::printf("Entries: %llu\n", (unsigned long long)r.entry_count);
                std::printf("Total uncompressed: %llu bytes\n", (unsigned long long)r.total_uncompressed);
                std::printf("Total compressed: %llu bytes\n", (unsigned long long)r.total_compressed);
                std::printf("Structure: valid\n");
            }
            return static_cast<int>(ExitCode::ExitOk);
        }
        else if (cmd == "verify") {
            std::string archive_path;
            bool verbose = false;
            int i = 2;
            while (i < argc) {
                std::string_view a = argv[i];
                if (a == "--verbose") verbose = true;
                else if (a == "--help") { print_help(); return static_cast<int>(ExitCode::ExitOk); }
                else archive_path = std::string(a);
                ++i;
            }
            if (archive_path.empty()) return fail(ExitCode::ExitInvalidArguments, "verify requires <archive.npx>");
            VerifyOptions vopts;
            vopts.archive_path = archive_path;
            vopts.verbose = verbose;
            try {
                verify_archive(vopts);
                return static_cast<int>(ExitCode::ExitOk);
            } catch (const Exception& ex) {
                return fail(ExitCode::ExitVerificationFail, ex.what());
            }
        }
        else if (cmd == "extract") {
            ExtractOptions opts;
            bool verbose = false, json = false;
            int i = 2;
            while (i < argc) {
                std::string_view a = argv[i];
                if (a == "--output") opts.output_dir = require_value(argc, argv, i, "--output");
                else if (a == "--file") opts.single_file = require_value(argc, argv, i, "--file");
                else if (a == "--overwrite") opts.overwrite = true;
                else if (a == "--verbose") verbose = true;
                else if (a == "--json") json = true;
                else if (a == "--help") { print_help(); return static_cast<int>(ExitCode::ExitOk); }
                else if (opts.archive_path.empty()) opts.archive_path = std::string(a);
                ++i;
            }
            if (opts.archive_path.empty()) return fail(ExitCode::ExitInvalidArguments, "extract requires <archive.npx>");
            if (opts.output_dir.empty()) return fail(ExitCode::ExitInvalidArguments, "extract requires --output <dir>");
            opts.verbose = verbose;
            try {
                extract_archive(opts);
                if (json) {
                    std::printf("{\"command\":\"extract\",\"archive\":\"%s\",\"output\":\"%s\",\"status\":\"ok\"}\n",
                        opts.archive_path.c_str(), opts.output_dir.c_str());
                } else if (verbose) {
                    std::printf("extracted %s -> %s\n", opts.archive_path.c_str(), opts.output_dir.c_str());
                }
                return static_cast<int>(ExitCode::ExitOk);
            } catch (const Exception& ex) {
                switch (ex.code()) {
                    case ErrorCode::CorruptArchive:
                    case ErrorCode::ChecksumMismatch:
                    case ErrorCode::TruncatedArchive:
                    case ErrorCode::InvalidOffset:
                        return fail(ExitCode::ExitCorruptArchive, ex.what());
                    case ErrorCode::UnknownCodec:
                    case ErrorCode::UnsupportedFormat:
                    case ErrorCode::NotImplemented:
                        return fail(ExitCode::ExitUnsupportedFormat, ex.what());
                    case ErrorCode::IoError:
                        return fail(ExitCode::ExitIoFailure, ex.what());
                    case ErrorCode::LimitExceeded:
                    case ErrorCode::DecompressionBomb:
                    case ErrorCode::PathUnsafe:
                        return fail(ExitCode::ExitVerificationFail, ex.what());
                    case ErrorCode::InvalidArgument:
                        return fail(ExitCode::ExitInvalidArguments, ex.what());
                    default:
                        return fail(ExitCode::ExitInternalError, ex.what());
                }
            } catch (const std::exception& ex) {
                return fail(ExitCode::ExitInternalError, ex.what());
            }
        }
        else if (cmd == "benchmark") {
            std::string dataset;
            int i = 2;
            while (i < argc) {
                std::string_view a = argv[i];
                if (a == "--help") { print_help(); return static_cast<int>(ExitCode::ExitOk); }
                else dataset = std::string(a);
                ++i;
            }
            if (dataset.empty()) return fail(ExitCode::ExitInvalidArguments, "benchmark requires <dataset directory>");
            // Delegate to Python benchmark harness if present.
            std::fprintf(stderr, "benchmark: use benchmarks/run_benchmarks.py directly\n");
            return fail(ExitCode::ExitInvalidArguments, "benchmark not implemented in C++ CLI");
        }
        else {
            return fail(ExitCode::ExitInvalidArguments, "unknown command: " + cmd);
        }
    } catch (const Exception& ex) {
        switch (ex.code()) {
            case ErrorCode::CorruptArchive:
            case ErrorCode::ChecksumMismatch:
            case ErrorCode::TruncatedArchive:
            case ErrorCode::InvalidOffset:
                return fail(ExitCode::ExitCorruptArchive, ex.what());
            case ErrorCode::UnknownCodec:
            case ErrorCode::UnsupportedFormat:
            case ErrorCode::NotImplemented:
                return fail(ExitCode::ExitUnsupportedFormat, ex.what());
            case ErrorCode::IoError:
                return fail(ExitCode::ExitIoFailure, ex.what());
            case ErrorCode::LimitExceeded:
            case ErrorCode::DecompressionBomb:
            case ErrorCode::PathUnsafe:
                return fail(ExitCode::ExitVerificationFail, ex.what());
            case ErrorCode::InvalidArgument:
                return fail(ExitCode::ExitInvalidArguments, ex.what());
            default:
                return fail(ExitCode::ExitInternalError, ex.what());
        }
    } catch (const std::exception& ex) {
        return fail(ExitCode::ExitInternalError, ex.what());
    }
    return static_cast<int>(ExitCode::ExitInternalError);
}

} // namespace nexapack


