# NexaPack

NexaPack is a C++20 archive and compression engine designed around a modular,
codec-independent archive format. It provides a command-line interface and a
reusable C++ library.

## Overview

NexaPack separates archive handling from compression codecs. This allows the
archive layer to work independently of the compression implementation and
supports adding codecs without changing the container format.

## Main capabilities

- Create and extract `.npx` archives
- List and inspect archive contents
- Verify archive and chunk integrity
- Selective file extraction
- Store mode for uncompressed data
- Optional Zstandard and LZ4 codec integrations
- Adaptive compression planning
- Chunk-based archive organization
- CRC32C integrity checks
- Safe extraction with path-traversal protection

## Components

- `include/nexapack/` — public C++ headers
- `src/archive/` — archive reading and writing
- `src/codecs/` — compression codec implementations
- `src/format/` — archive format handling
- `src/integrity/` — integrity verification
- `src/analyzer/` — content analysis
- `src/planner/` — codec selection and compression planning
- `src/filesystem/` — safe filesystem operations
- `src/cli/` — command-line interface

## Command-line interface

The command-line executable is named `npx`.

```text
npx create <archive.npx> <files...>
npx list <archive.npx>
npx inspect <archive.npx>
npx verify <archive.npx>
npx extract <archive.npx> --output <directory>
```

## Project status

NexaPack is experimental. Its archive format and public interfaces may change.
It is not an encrypted archive format and should not be used to protect
confidential data.

## Platform and technology

- Language: C++20
- Build configuration: CMake
- Intended platforms: Windows and Linux
- Archive extension: `.npx`
