cmake_minimum_required(VERSION 3.21 FATAL_ERROR)

project(NexaPack
  VERSION 0.1.0
  DESCRIPTION "NexaPack - adaptive archive format and compression engine"
  LANGUAGES C CXX
)

#---------------------------------------------------------------------------
# C++ standard
#---------------------------------------------------------------------------
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  # MSVC: /std:c++20 is set via presets; enforce here as a fallback.
set(CMAKE_CXX_EXTENSIONS OFF)
else()
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_C_EXTENSIONS OFF)
  set(CMAKE_CXX_EXTENSIONS OFF)
endif()

#---------------------------------------------------------------------------
# Options
#---------------------------------------------------------------------------
option(NEXAPACK_ENABLE_ZSTD "Enable the Zstandard codec (requires zstd source)" ON)
option(NEXAPACK_ENABLE_LZ4 "Enable the LZ4 codec (requires lz4 source)" OFF)
option(NEXAPACK_ENABLE_BROTLI "Enable the Brotli codec (requires brotli source)" OFF)
option(NEXAPACK_ENABLE_LZMA2 "Enable the LZMA2 codec (requires lzma2 source)" OFF)

#---------------------------------------------------------------------------
# Optional third-party codec source locations
#---------------------------------------------------------------------------
set(NEXAPACK_VENDOR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party" CACHE PATH "Directory containing optional codec source trees")
set(NEXAPACK_ZSTD_SOURCE_DIR "${NEXAPACK_VENDOR_ROOT}/zstd" CACHE PATH "Path to zstd source tree")
set(NEXAPACK_LZ4_SOURCE_DIR "${NEXAPACK_VENDOR_ROOT}/lz4" CACHE PATH "Path to lz4 source tree")
set(NEXAPACK_BROTLI_SOURCE_DIR "${NEXAPACK_VENDOR_ROOT}/brotli" CACHE PATH "Path to brotli source tree")
set(NEXAPACK_LZMA2_SOURCE_DIR "${NEXAPACK_VENDOR_ROOT}/lzma2" CACHE PATH "Path to lzma2 source tree")

#---------------------------------------------------------------------------
# Zstandard
#---------------------------------------------------------------------------
set(NEXAPACK_HAVE_ZSTD OFF)
if(NEXAPACK_ENABLE_ZSTD)
  if(EXISTS "${NEXAPACK_ZSTD_SOURCE_DIR}/lib/zstd.h")
    set(NEXAPACK_HAVE_ZSTD ON)
    # Build zstd from its own sources directly (the vendored zstd CMakeLists
    # uses the removed JoinPaths module and is incompatible with CMake 4.x;
    # compiling the sources into a static library avoids that entirely).
    set(ZSTD_COMMON_SRCS
      "${NEXAPACK_ZSTD_SOURCE_DIR}/lib/common/debug.c"
      "${NEXAPACK_ZSTD_SOURCE_DIR}/lib/common/entropy_common.c"
      "${NEXAPACK_ZSTD_SOURCE_DIR}/lib/common/error_private.c"
      "${NEXAPACK_ZSTD_SOURCE_DIR}/lib/common/fse_decompress.c"
      "${NEXAPACK_ZSTD_SOURCE_DIR}/lib/common/pool.c"
      "${NEXAPACK_ZSTD_SOURCE_DIR}/lib/common/threading.c"
      "${NEXAPACK_ZSTD_SOURCE_DIR}/lib/common/xxhash.c"
      "${NEXAPACK_ZSTD_SOURCE_DIR}/lib/common/zstd_common.c"
    )
    set(ZSTD_COMPRESS_SRCS
      "${NEXAPACK_ZSTD_SOURCE_DIR}/lib/compress/fse_compress.c"
      "${NEXAPACK_ZSTD_SOURCE_DIR}/lib/compress/hist.c"
      "${NEXAPACK_ZSTD_SOURCE_DIR}/lib/compress/huf_compress.c"
      "${NEXAPACK_ZSTD_SOURCE_DIR}/lib/compress/zstd_compress.c"
      "${NEXAPACK_ZSTD_SOURCE_DIR}/lib/compress/zstd_compress_literals.c"
      "${NEXAPACK_ZSTD_SOURCE_DIR}/lib/compress/zstd_compress_sequences.c"
      "${NEXAPACK_ZSTD_SOURCE_DIR}/lib/compress/zstd_compress_superblock.c"
      "${NEXAPACK_ZSTD_SOURCE_DIR}/lib/compress/zstd_double_fast.c"
      "${NEXAPACK_ZSTD_SOURCE_DIR}/lib/compress/zstd_fast.c"
      "${NEXAPACK_ZSTD_SOURCE_DIR}/lib/compress/zstd_lazy.c"
      "${NEXAPACK_ZSTD_SOURCE_DIR}/lib/compress/zstd_ldm.c"
      "${NEXAPACK_ZSTD_SOURCE_DIR}/lib/compress/zstd_opt.c"
      "${NEXAPACK_ZSTD_SOURCE_DIR}/lib/compress/zstd_preSplit.c"
    )
    set(ZSTD_DECOMPRESS_SRCS
      "${NEXAPACK_ZSTD_SOURCE_DIR}/lib/decompress/huf_decompress.c"
      "${NEXAPACK_ZSTD_SOURCE_DIR}/lib/decompress/zstd_ddict.c"
      "${NEXAPACK_ZSTD_SOURCE_DIR}/lib/decompress/zstd_decompress.c"
      "${NEXAPACK_ZSTD_SOURCE_DIR}/lib/decompress/zstd_decompress_block.c"
    )
    set(ZSTD_DICT_SRCS
      "${NEXAPACK_ZSTD_SOURCE_DIR}/lib/dictBuilder/cover.c"
      "${NEXAPACK_ZSTD_SOURCE_DIR}/lib/dictBuilder/divsufsort.c"
      "${NEXAPACK_ZSTD_SOURCE_DIR}/lib/dictBuilder/fastcover.c"
      "${NEXAPACK_ZSTD_SOURCE_DIR}/lib/dictBuilder/zdict.c"
    )
    set(ZSTD_LEGACY_SRCS
      "${NEXAPACK_ZSTD_SOURCE_DIR}/lib/legacy/zstd_v01.c"
      "${NEXAPACK_ZSTD_SOURCE_DIR}/lib/legacy/zstd_v02.c"
      "${NEXAPACK_ZSTD_SOURCE_DIR}/lib/legacy/zstd_v03.c"
      "${NEXAPACK_ZSTD_SOURCE_DIR}/lib/legacy/zstd_v04.c"
      "${NEXAPACK_ZSTD_SOURCE_DIR}/lib/legacy/zstd_v05.c"
      "${NEXAPACK_ZSTD_SOURCE_DIR}/lib/legacy/zstd_v06.c"
      "${NEXAPACK_ZSTD_SOURCE_DIR}/lib/legacy/zstd_v07.c"
    )
    add_library(zstd_static STATIC
      ${ZSTD_COMMON_SRCS}
      ${ZSTD_COMPRESS_SRCS}
      ${ZSTD_DECOMPRESS_SRCS}
      ${ZSTD_DICT_SRCS}
      ${ZSTD_LEGACY_SRCS}
    )
    target_include_directories(zstd_static PUBLIC
      "${NEXAPACK_ZSTD_SOURCE_DIR}/lib"
      "${NEXAPACK_ZSTD_SOURCE_DIR}/lib/common"
    )
    set_target_properties(zstd_static PROPERTIES
      C_STANDARD 11
      C_STANDARD_REQUIRED ON
      C_EXTENSIONS OFF
    )
    # Vendored zstd sources trigger -Wunused-function under some compilers;
    # downgrade to a warning (do not fail the build on vendored code).
    if(MSVC)
      target_compile_options(zstd_static PRIVATE /wd4100 /wd4101 /wd4133 /wd4244 /wd4267 /wd4706 -Wno-documentation-unknown-command -Wno-unsafe-buffer-usage -Wno-reserved-identifier -Wno-reserved-macro-identifier -Wno-covered-switch-default -Wno-padded -Wno-unused-macros -Wno-sign-conversion -Wno-missing-variable-declarations -Wno-unused-function)
    else()
      target_compile_options(zstd_static PRIVATE -Wno-unused-function -Wno-error=unused-function -Wno-sign-compare -Wno-sign-conversion -Wno-documentation-unknown-command -Wno-unsafe-buffer-usage -Wno-reserved-identifier -Wno-reserved-macro-identifier -Wno-covered-switch-default -Wno-padded -Wno-unused-macros -Wno-sign-conversion -Wno-missing-variable-declarations)
    endif()
    set(ZSTD_LIBRARY_TARGET zstd_static)
  else()
    if(NEXAPACK_ENABLE_ZSTD STREQUAL "ON")
      message(FATAL_ERROR "Zstandard source tree not found at ${NEXAPACK_ZSTD_SOURCE_DIR}")
    endif()
    message(STATUS "Zstandard source not found; Zstd codec disabled.")
  endif()
endif()

#---------------------------------------------------------------------------
# LZ4
#---------------------------------------------------------------------------
set(NEXAPACK_HAVE_LZ4 OFF)
if(NEXAPACK_ENABLE_LZ4)
  if(EXISTS "${NEXAPACK_LZ4_SOURCE_DIR}/lib/lz4.c")
    set(NEXAPACK_HAVE_LZ4 ON)
    # Compile LZ4 source files directly (like zstd, bypassing any vendored CMake
    # that may use removed modules or incompatible settings).
    set(LZ4_SOURCE_FILES
      "${NEXAPACK_LZ4_SOURCE_DIR}/lib/lz4.c"
      "${NEXAPACK_LZ4_SOURCE_DIR}/lib/lz4hc.c"
      "${NEXAPACK_LZ4_SOURCE_DIR}/lib/xxhash.c"
    )
    add_library(lz4_static STATIC ${LZ4_SOURCE_FILES})
    target_include_directories(lz4_static PUBLIC
      "${NEXAPACK_LZ4_SOURCE_DIR}/lib"
    )
    set_target_properties(lz4_static PROPERTIES
      C_STANDARD 11
      C_STANDARD_REQUIRED ON
      C_EXTENSIONS OFF
    )
    # Vendored lz4 sources may trigger warnings under strict flags; downgrade.
    if(MSVC)
      target_compile_options(lz4_static PRIVATE /wd4100 /wd4101 /wd4133 /wd4244 /wd4267 /wd4706 -Wno-unused-function)
    else()
      target_compile_options(lz4_static PRIVATE -Wno-unused-function -Wno-error=unused-function -Wno-sign-compare -Wno-sign-conversion)
    endif()
    set(LZ4_LIBRARY_TARGET lz4_static)
  else()
    if(NEXAPACK_ENABLE_LZ4 STREQUAL "ON")
      message(FATAL_ERROR "LZ4 source tree not found at ${NEXAPACK_LZ4_SOURCE_DIR}")
    endif()
    message(STATUS "LZ4 source not found; LZ4 codec disabled.")
  endif()
endif()

#---------------------------------------------------------------------------
# Brotli (optional, only when source tree is present)
#---------------------------------------------------------------------------
set(NEXAPACK_HAVE_BROTLI OFF)
if(NEXAPACK_ENABLE_BROTLI)
  if(EXISTS "${NEXAPACK_BROTLI_SOURCE_DIR}/enc/encode.cc")
    set(NEXAPACK_HAVE_BROTLI ON)
    set(BROTLI_SOURCE_FILES
      "${NEXAPACK_BROTLI_SOURCE_DIR}/enc/encode.cc"
      "${NEXAPACK_BROTLI_SOURCE_DIR}/enc/bit_writer.cc"
      "${NEXAPACK_BROTLI_SOURCE_DIR}/enc/backward_references.cc"
      "${NEXAPACK_BROTLI_SOURCE_DIR}/enc/block_splitter.cc"
      "${NEXAPACK_BROTLI_SOURCE_DIR}/enc/brotli_bit_stream.cc"
      "${NEXAPACK_BROTLI_SOURCE_DIR}/enc/hash.cc"
      "${NEXAPACK_BROTLI_SOURCE_DIR}/enc/literal_cost.cc"
      "${NEXAPACK_BROTLI_SOURCE_DIR}/enc/memory.cc"
      "${NEXAPACK_BROTLI_SOURCE_DIR}/enc/metablock.cc"
      "${NEXAPACK_BROTLI_SOURCE_DIR}/enc/static_dict.cc"
      "${NEXAPACK_BROTLI_SOURCE_DIR}/enc/utf8_util.cc"
      "${NEXAPACK_BROTLI_SOURCE_DIR}/dec/decode.cc"
      "${NEXAPACK_BROTLI_SOURCE_DIR}/dec/bit_reader.cc"
      "${NEXAPACK_BROTLI_SOURCE_DIR}/dec/huffman.cc"
      "${NEXAPACK_BROTLI_SOURCE_DIR}/dec/state.cc"
      "${NEXAPACK_BROTLI_SOURCE_DIR}/common/context.cc"
      "${NEXAPACK_BROTLI_SOURCE_DIR}/common/dictionary.cc"
      "${NEXAPACK_BROTLI_SOURCE_DIR}/common/platform.cc"
      "${NEXAPACK_BROTLI_SOURCE_DIR}/common/qplib.cc"
    )
    add_library(brotli_static STATIC ${BROTLI_SOURCE_FILES})
    target_include_directories(brotli_static PUBLIC
      "${NEXAPACK_BROTLI_SOURCE_DIR}/enc"
      "${NEXAPACK_BROTLI_SOURCE_DIR}/dec"
      "${NEXAPACK_BROTLI_SOURCE_DIR}/common"
    )
    set_target_properties(brotli_static PROPERTIES
      CXX_STANDARD 11
      CXX_STANDARD_REQUIRED ON
      CXX_EXTENSIONS OFF
    )
    set(BROTLI_LIBRARY_TARGET brotli_static)
  else()
    if(NEXAPACK_ENABLE_BROTLI STREQUAL "ON")
      message(FATAL_ERROR "Brotli source tree not found at ${NEXAPACK_BROTLI_SOURCE_DIR}")
    endif()
    message(STATUS "Brotli source not found; Brotli codec disabled.")
  endif()
endif()

#---------------------------------------------------------------------------
# LZMA2 (optional, only when source tree is present)
#---------------------------------------------------------------------------
set(NEXAPACK_HAVE_LZMA2 OFF)
if(NEXAPACK_ENABLE_LZMA2)
  if(EXISTS "${NEXAPACK_LZMA2_SOURCE_DIR}/CPP/7zip/IStream.h")
    set(NEXAPACK_HAVE_LZMA2 ON)
    message(STATUS "LZMA2 source tree found at ${NEXAPACK_LZMA2_SOURCE_DIR}")
    message(STATUS "LZMA2 codec requires manual integration (complex 7-Zip sources).")
  else()
    if(NEXAPACK_ENABLE_LZMA2 STREQUAL "ON")
      message(FATAL_ERROR "LZMA2 source tree not found at ${NEXAPACK_LZMA2_SOURCE_DIR}")
    endif()
    message(STATUS "LZMA2 source not found; LZMA2 codec disabled.")
  endif()
endif()

#---------------------------------------------------------------------------
# Compiler warnings
#---------------------------------------------------------------------------
if(MSVC)
  set(NEXAPACK_WARNING_FLAGS "/W4" "/WX" "/EHsc")
else()
  set(NEXAPACK_WARNING_FLAGS "-Wall" "-Wextra" "-Werror")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Release>:Release>")

#---------------------------------------------------------------------------
# NexaPack library
#---------------------------------------------------------------------------
add_library(nexapack STATIC
  src/archive/archive_reader.cpp
  src/archive/archive_writer.cpp
  src/format/format.cpp
  src/codecs/codec_interface.cpp
  src/codecs/store_codec.cpp
  src/integrity/integrity.cpp
  src/analyzer/analyzer.cpp
  src/planner/planner.cpp
  src/filesystem/safe_path.cpp
  src/cli/cli.cpp
  src/version.cpp
)

target_include_directories(nexapack PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(nexapack PUBLIC
  NEXAPACK_VERSION=\"${PROJECT_VERSION}\"
)

if(NEXAPACK_HAVE_ZSTD)
  target_sources(nexapack PRIVATE src/codecs/zstd_codec.cpp)
  target_include_directories(nexapack PRIVATE
    "${NEXAPACK_ZSTD_SOURCE_DIR}/lib"
    "${NEXAPACK_ZSTD_SOURCE_DIR}/lib/compress"
    "${NEXAPACK_ZSTD_SOURCE_DIR}/lib/decompress"
    "${NEXAPACK_ZSTD_SOURCE_DIR}/lib/dictBuilder"
    "${NEXAPACK_ZSTD_SOURCE_DIR}/lib/common"
  )
  target_link_libraries(nexapack PUBLIC zstd_static)
  target_compile_definitions(nexapack PUBLIC NEXAPACK_HAVE_ZSTD=1)
endif()

if(NEXAPACK_HAVE_LZ4)
  target_sources(nexapack PRIVATE src/codecs/lz4_codec.cpp)
  target_include_directories(nexapack PRIVATE
    "${NEXAPACK_LZ4_SOURCE_DIR}/lib"
  )
  target_link_libraries(nexapack PUBLIC ${LZ4_LIBRARY_TARGET})
  target_compile_definitions(nexapack PUBLIC NEXAPACK_HAVE_LZ4=1)
endif()

if(NEXAPACK_HAVE_BROTLI)
  message(STATUS "Brotli source found but codec wrapper not yet implemented.")
  message(STATUS "Add src/codecs/brotli_codec.cpp and register via register_brotli_codec().")
endif()

if(NEXAPACK_HAVE_LZMA2)
  message(STATUS "LZMA2 source found but codec wrapper not yet implemented.")
  message(STATUS "Add src/codecs/lzma2_codec.cpp and register via register_lzma2_codec().")
endif()

target_compile_options(nexapack PRIVATE ${NEXAPACK_WARNING_FLAGS})


#---------------------------------------------------------------------------
# CLI executable
#---------------------------------------------------------------------------
add_executable(npx
  src/cli/main.cpp
)

target_link_libraries(npx PRIVATE nexapack)
target_compile_options(npx PRIVATE ${NEXAPACK_WARNING_FLAGS})


#---------------------------------------------------------------------------
# Install
#---------------------------------------------------------------------------
install(TARGETS npx nexapack
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
)
install(DIRECTORY include/ DESTINATION include/nexapack)
