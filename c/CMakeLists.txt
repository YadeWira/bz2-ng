# bz2-ng: a drop-in replacement for libbz2 1.0.8.
#
#   cmake -S c -B c/build -DCMAKE_BUILD_TYPE=Release && cmake --build c/build && ctest --test-dir c/build
#
# The library is a flat list of C11 sources (BZ2NG_SOURCES). Every source is
# compiled on every architecture; the ISA-specific ones compile to nothing
# where they do not apply. Only the five ISA translation units get
# instruction-set flags, and only on their own architecture (bzng_isa_flags
# below); the run-time dispatch (lib/kern.h, lib/crc32.c) calls them only on
# CPUs that have the instructions. Nothing is generated at configure time
# except bzip2.pc and the shared library's list of exported symbols (and, for
# the tests, the randomising compressor of test/CMakeLists.txt).
cmake_minimum_required(VERSION 3.16)
project(bz2ng VERSION 0.1.0 LANGUAGES C)

option(BZ2NG_BUILD_SHARED "Build the shared library" ON)
set(BZ2NG_SONAME "1.0" CACHE STRING "Shared-library SONAME version: 1.0 (upstream, Debian) or 1 (Fedora, openSUSE)")
set_property(CACHE BZ2NG_SONAME PROPERTY STRINGS "1.0" "1")
option(BZ2NG_NO_STDIO "Build without the FILE* layer (BZ_NO_STDIO); static library only" OFF)
option(BZ2NG_SANITIZE "Build everything with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(BZ2NG_BUILD_PROGRAMS "Build the bzip2 and bzip2recover programs" ON)
option(BZ2NG_BUILD_TESTS "Build the tests (CTest)" ON)
option(BZ2NG_BUILD_BENCH "Build the benchmark tool" ON)
option(BZ2NG_BUILD_FUZZ
       "Build the libFuzzer targets in fuzz/ (clang with libFuzzer; everything built with ASan and UBSan; needs BZ2NG_BUILD_TESTS)"
       OFF)
option(BZ2NG_DECODER_IN_FORMULA
       "Decoder memory inside libbz2's documented 100 kB + 4 x block size (default: 105 kB + 5 x block size, faster on full-size blocks)"
       OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  set(BZ2NG_WARNINGS -Wall -Wextra)
endif()
if(BZ2NG_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
  add_link_options(-fsanitize=address,undefined)
endif()
# The fuzz build instruments everything, the reference libbz2 included, with
# the sanitisers and libFuzzer's coverage; only the targets in fuzz/ link
# libFuzzer itself.  Apple's clang has no libFuzzer, hence the check.
if(BZ2NG_BUILD_FUZZ)
  include(CheckCSourceCompiles)
  set(CMAKE_REQUIRED_FLAGS -fsanitize=fuzzer)
  check_c_source_compiles("
    #include <stddef.h>
    #include <stdint.h>
    int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) { return 0; }"
    BZ2NG_HAVE_LIBFUZZER)
  unset(CMAKE_REQUIRED_FLAGS)
  if(NOT BZ2NG_HAVE_LIBFUZZER)
    message(FATAL_ERROR "BZ2NG_BUILD_FUZZ needs a clang with libFuzzer (-fsanitize=fuzzer); Apple's clang has none")
  endif()
  add_compile_options(-fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
  add_link_options(-fsanitize=address,undefined)
endif()

# The decoder's per-stream memory layout used by BZ2_bzDecompressInit
# (lib/dec.h).
if(BZ2NG_DECODER_IN_FORMULA)
  set(BZ2NG_DEFAULT_LAYOUT BZNG_LAYOUT_FIT)
else()
  set(BZ2NG_DEFAULT_LAYOUT BZNG_LAYOUT_WIDE)
endif()

# Architecture of the target, for the ISA-specific flags.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(BZ2NG_ARCH x86_64)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
  set(BZ2NG_ARCH aarch64)
else()
  set(BZ2NG_ARCH other)
endif()
if(APPLE AND CMAKE_OSX_ARCHITECTURES)
  list(LENGTH CMAKE_OSX_ARCHITECTURES _n_arch)
  if(_n_arch EQUAL 1)
    if(CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")
      set(BZ2NG_ARCH x86_64)
    elseif(CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
      set(BZ2NG_ARCH aarch64)
    endif()
  endif()
endif()

# ---- library sources --------------------------------------------------------------
set(BZ2NG_SOURCES
  lib/bzlib.c            # libbz2's stdio, buffer-to-buffer and zlib-style layer, altered
  lib/compress.c         # legacy compression driver (derived from libbz2)
  lib/enc_block.c        # block core
  lib/enc_huff.c         # Huffman stage (contains libbz2 code)
  lib/enc_sort.c         # least rotation into the sorter
  lib/sort_libsais.c     # block sorter: compiles third_party/libsais
  lib/decompress.c       # legacy decompression driver and header parser (derived from libbz2)
  lib/dec_block.c        # Huffman tables, inverse BWT, text source
  lib/randtable.c        # randomisation table (libbz2)
  lib/crc32.c            # CRC table kernel and dispatch
  lib/crc32_table.c
  lib/utils.c
  lib/kern_scalar.c      # portable kernel tier
  lib/arm/cpu_features.c
  lib/arm/crc32_armv8.c  # ISA: AArch64 CRC32
  lib/arm/kern_neon.c    # AArch64 NEON tier (baseline, no flags)
  lib/x86/cpu_features.c
  lib/x86/crc32_pclmul.c # ISA: x86-64 PCLMULQDQ
  lib/x86/crc32_vpclmul.c # ISA: x86-64 VPCLMULQDQ
  lib/x86/kern_sse4.c    # ISA: x86-64-v2 tier
  lib/x86/kern_avx2.c    # ISA: x86-64-v3 tier
)

# Per-file ISA flags (GCC/Clang spelling).
function(bzng_isa_flags)
  if(BZ2NG_ARCH STREQUAL "x86_64")
    set_source_files_properties(lib/x86/kern_sse4.c PROPERTIES COMPILE_OPTIONS "-march=x86-64-v2")
    set_source_files_properties(lib/x86/kern_avx2.c PROPERTIES COMPILE_OPTIONS "-march=x86-64-v3")
    set_source_files_properties(lib/x86/crc32_pclmul.c PROPERTIES COMPILE_OPTIONS "-msse4.1;-mpclmul")
    set_source_files_properties(lib/x86/crc32_vpclmul.c PROPERTIES COMPILE_OPTIONS "-mavx2;-mpclmul;-mvpclmulqdq")
  elseif(BZ2NG_ARCH STREQUAL "aarch64")
    set_source_files_properties(lib/arm/crc32_armv8.c PROPERTIES COMPILE_OPTIONS "-march=armv8-a+crc")
  endif()
endfunction()
bzng_isa_flags()

# Optimisation: the encoder and sorter at -O3, everything else at -O2
# (measured: the encoder is faster at -O3, the decoder and the kernel tiers
# gain nothing from it; libsais is built as it was timed, and at -O2 it was
# no different where that was tried, research/L3-libsais-gcc-aarch64.md).
# Appended after the build type's flags, so they win.
set(BZ2NG_O3_SOURCES lib/compress.c lib/enc_block.c lib/enc_huff.c lib/enc_sort.c lib/sort_libsais.c)

set(BZ2NG_EXPORTED
  BZ2_bzCompressInit BZ2_bzCompress BZ2_bzCompressEnd
  BZ2_bzDecompressInit BZ2_bzDecompress BZ2_bzDecompressEnd
  BZ2_bzBuffToBuffCompress BZ2_bzBuffToBuffDecompress BZ2_bzlibVersion)
if(NOT BZ2NG_NO_STDIO)
  list(APPEND BZ2NG_EXPORTED
    BZ2_bzReadOpen BZ2_bzReadClose BZ2_bzReadGetUnused BZ2_bzRead
    BZ2_bzWriteOpen BZ2_bzWrite BZ2_bzWriteClose BZ2_bzWriteClose64
    BZ2_bzopen BZ2_bzdopen BZ2_bzread BZ2_bzwrite BZ2_bzflush BZ2_bzclose BZ2_bzerror)
endif()

# Common settings of every compilation of the library sources.
function(bzng_library_settings target)
  # PROJECT_SOURCE_DIR, not the current directory: test/ calls this too, and
  # a wrong path goes unnoticed wherever the system has a bzlib.h of its own.
  target_include_directories(${target} PUBLIC ${PROJECT_SOURCE_DIR} PRIVATE ${PROJECT_SOURCE_DIR}/lib)
  # _DEFAULT_SOURCE: glibc hides POSIX functions (fdopen, used by the stdio
  # layer) under a strict -std=c11.
  target_compile_definitions(${target} PRIVATE BZNG_VERSION="${PROJECT_VERSION}" _FILE_OFFSET_BITS=64 _DEFAULT_SOURCE)
  if(BZ2NG_NO_STDIO)
    target_compile_definitions(${target} PUBLIC BZ_NO_STDIO)
  endif()
  # ASSERT (lib/lib_common.h) is compiled in for debug and sanitiser builds.
  if(BZ2NG_SANITIZE OR BZ2NG_BUILD_FUZZ)
    target_compile_definitions(${target} PRIVATE BZNG_ENABLE_ASSERTIONS)
  else()
    target_compile_definitions(${target} PRIVATE $<$<CONFIG:Debug>:BZNG_ENABLE_ASSERTIONS>)
  endif()
  target_compile_options(${target} PRIVATE ${BZ2NG_WARNINGS})
  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${target} PRIVATE -fvisibility=hidden $<$<CONFIG:Release,RelWithDebInfo>:-O2>)
  endif()
endfunction()
foreach(_src IN LISTS BZ2NG_O3_SOURCES)
  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    get_source_file_property(_opts ${_src} COMPILE_OPTIONS)
    if(NOT _opts)
      set(_opts "")
    endif()
    list(APPEND _opts "$<$<CONFIG:Release,RelWithDebInfo>:-O3>")
    set_source_files_properties(${_src} PROPERTIES COMPILE_OPTIONS "${_opts}")
  endif()
endforeach()
# gcc's first scheduling pass on AArch64 moves a prefetch in libsais's final
# induction scans above the test that guards it, since a prefetch cannot trap.
# It then also runs for marked entries, at addresses gigabytes outside the
# arrays, and Arm cores stall on each of those for tens of nanoseconds
# (research/L3-libsais-gcc-aarch64.md).
if(CMAKE_C_COMPILER_ID STREQUAL "GNU" AND BZ2NG_ARCH STREQUAL "aarch64")
  set_property(SOURCE lib/sort_libsais.c APPEND PROPERTY COMPILE_OPTIONS -fno-sched-spec)
endif()
# libbz2's own code in bzlib.c has an unused parameter (BZ2_bzflush).
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  set_source_files_properties(lib/bzlib.c PROPERTIES COMPILE_OPTIONS "-Wno-unused-parameter")
endif()

# decompress.c is compiled on its own so the tests can build libraries with
# each decoder memory layout as the default.
set(BZ2NG_CORE_SOURCES ${BZ2NG_SOURCES})
list(REMOVE_ITEM BZ2NG_CORE_SOURCES lib/decompress.c)
add_library(bz2_core_objects OBJECT ${BZ2NG_CORE_SOURCES})
bzng_library_settings(bz2_core_objects)
add_library(bz2_dec_objects OBJECT lib/decompress.c)
bzng_library_settings(bz2_dec_objects)
target_compile_definitions(bz2_dec_objects PRIVATE BZNG_DEFAULT_LAYOUT=${BZ2NG_DEFAULT_LAYOUT})

add_library(bz2_static STATIC $<TARGET_OBJECTS:bz2_core_objects> $<TARGET_OBJECTS:bz2_dec_objects>)
set_target_properties(bz2_static PROPERTIES OUTPUT_NAME bz2)
target_include_directories(bz2_static PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(BZ2NG_NO_STDIO)
  target_compile_definitions(bz2_static PUBLIC BZ_NO_STDIO)
endif()

if(BZ2NG_BUILD_SHARED AND NOT BZ2NG_NO_STDIO)
  add_library(bz2_shared SHARED $<TARGET_OBJECTS:bz2_core_objects> $<TARGET_OBJECTS:bz2_dec_objects>)
  set_target_properties(bz2_shared PROPERTIES OUTPUT_NAME bz2 VERSION 1.0.8 SOVERSION ${BZ2NG_SONAME})
  target_include_directories(bz2_shared PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  # Export exactly the public functions.
  if(APPLE)
    set(_list "")
    foreach(_sym IN LISTS BZ2NG_EXPORTED)
      string(APPEND _list "_${_sym}\n")
    endforeach()
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/bz2.exports "${_list}")
    target_link_options(bz2_shared PRIVATE "LINKER:-exported_symbols_list,${CMAKE_CURRENT_BINARY_DIR}/bz2.exports")
  elseif(WIN32)
    # A module-definition file, as libbz2's own libbz2.def: it also gives the
    # names undecorated where WINAPI decorates them (32-bit x86).
    set(_list "EXPORTS\n")
    foreach(_sym IN LISTS BZ2NG_EXPORTED)
      string(APPEND _list "  ${_sym}\n")
    endforeach()
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/bz2.def "${_list}")
    target_sources(bz2_shared PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/bz2.def)
  else()
    set(_list "")
    foreach(_sym IN LISTS BZ2NG_EXPORTED)
      string(APPEND _list "    ${_sym};\n")
    endforeach()
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/bz2.map "{\n  global:\n${_list}  local:\n    *;\n};\n")
    target_link_options(bz2_shared PRIVATE "LINKER:--version-script=${CMAKE_CURRENT_BINARY_DIR}/bz2.map")
  endif()
  # A drop-in DLL needs no GCC runtime DLL beside it (the portable tier calls
  # libgcc's popcount).  On 32-bit x86 the .def file's undecorated names are
  # matched to the stdcall-decorated functions, which is intended, so ld is
  # told not to warn about it.
  if(MINGW)
    target_link_options(bz2_shared PRIVATE -static-libgcc "LINKER:--enable-stdcall-fixup")
  endif()
endif()

# ---- pkg-config and install ---------------------------------------------------------
include(GNUInstallDirs)
if(BZ2NG_NO_STDIO)
  set(BZ2NG_PC_CFLAGS " -DBZ_NO_STDIO")
else()
  set(BZ2NG_PC_CFLAGS "")
endif()
configure_file(bzip2.pc.in ${CMAKE_CURRENT_BINARY_DIR}/bzip2.pc @ONLY)
install(FILES bzlib.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/bzip2.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
install(TARGETS bz2_static ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
if(TARGET bz2_shared)
  install(TARGETS bz2_shared LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()

# ---- programs (libbz2 1.0.8's, unmodified) -------------------------------------------
if(BZ2NG_BUILD_PROGRAMS AND NOT BZ2NG_NO_STDIO)
  foreach(_prog bzip2 bzip2recover)
    add_executable(${_prog} programs/${_prog}.c)
    target_compile_definitions(${_prog} PRIVATE _FILE_OFFSET_BITS=64 _DEFAULT_SOURCE)
    target_link_libraries(${_prog} PRIVATE bz2_static)
    # libbz2's unmodified programs: quiet the warnings modern compilers give
    # for their code (an ignored fchown result, "%Lu" for a 64-bit value,
    # sprintf, which macOS's SDK marks deprecated).
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
      target_compile_options(${_prog} PRIVATE -Wno-unused-result -Wno-format -Wno-deprecated-declarations)
    endif()
    install(TARGETS ${_prog} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  endforeach()
endif()

# ---- stock libbz2 1.0.8 for the tests and the benchmark ---------------------------
# test/reference holds its unmodified sources; built with every global symbol
# renamed ref_* (test/reference_rename.h) so that it links next to bz2-ng, at
# libbz2's own -O2.
if((BZ2NG_BUILD_TESTS OR BZ2NG_BUILD_BENCH) AND NOT BZ2NG_NO_STDIO)
  set(BZ2NG_REFERENCE_SOURCES blocksort bzlib compress crctable decompress huffman randtable)
  list(TRANSFORM BZ2NG_REFERENCE_SOURCES PREPEND test/reference/)
  list(TRANSFORM BZ2NG_REFERENCE_SOURCES APPEND .c)
  add_library(bz2_reference STATIC ${BZ2NG_REFERENCE_SOURCES})
  target_include_directories(bz2_reference BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/test/reference
                             INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/test)
  target_compile_definitions(bz2_reference PRIVATE BZPFX=ref_ _FILE_OFFSET_BITS=64 _DEFAULT_SOURCE)
  target_compile_options(bz2_reference PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/test/reference_rename.h -w
                         $<$<CONFIG:Release,RelWithDebInfo>:-O2>)
endif()

if(BZ2NG_BUILD_TESTS AND NOT BZ2NG_NO_STDIO)
  enable_testing()
  add_subdirectory(test)
elseif(BZ2NG_BUILD_TESTS)
  # Without stdio: the buffer-to-buffer functions with the integrator's
  # bz_internal_error, and a check that the library references no stdio.
  enable_testing()
  add_executable(no_stdio_test test/no_stdio_test.c)
  target_link_libraries(no_stdio_test PRIVATE bz2_static)
  add_test(NAME no_stdio_round_trip COMMAND no_stdio_test)
  add_test(NAME no_stdio_symbols
           COMMAND ${CMAKE_COMMAND} -DLIB=$<TARGET_FILE:bz2_static> -P ${CMAKE_CURRENT_SOURCE_DIR}/test/no_stdio_symbols.cmake)
endif()
if(BZ2NG_BUILD_FUZZ)
  # The seed generator reuses the tests' reference and randomising builds.
  if(NOT BZ2NG_BUILD_TESTS OR BZ2NG_NO_STDIO)
    message(FATAL_ERROR "BZ2NG_BUILD_FUZZ needs BZ2NG_BUILD_TESTS and the stdio layer")
  endif()
  add_subdirectory(fuzz)
endif()
if(BZ2NG_BUILD_BENCH AND BZ2NG_NO_STDIO)
  message(STATUS "bench: not built (it uses the stdio layer; BZ2NG_NO_STDIO is on)")
elseif(BZ2NG_BUILD_BENCH)
  add_subdirectory(bench)
endif()
