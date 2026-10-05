# libFuzzer targets (BZ2NG_BUILD_FUZZ): each drives bz2-ng and an independent
# reference with the same input and aborts on any difference.  The top-level
# CMakeLists.txt has already put the sanitisers and libFuzzer's coverage
# instrumentation on every compilation; these executables also link libFuzzer.
#
#   fuzz_decompress  bz2-ng's decoder against stock libbz2's, call for call
#   fuzz_compress    bz2-ng's encoder against stock libbz2's, call for call
#   fuzz_sort        the encoder's sort stage against an independent oracle
#   fuzz_seeds DIR   writes a seed corpus for each target under DIR
#
# Each target runs on the kernel tier the library selects; BZ2NG_TIER forces
# one (fuzz/campaign.sh runs every tier the machine has).

# A fuzz target linked against bz2-ng (static, so internal functions such as
# bzng_decompress_init are reachable) and the reference.
function(bzng_fuzz_target name)
  add_executable(${name} ${ARGN})
  target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/lib ${PROJECT_SOURCE_DIR}/test)
  target_compile_options(${name} PRIVATE ${BZ2NG_WARNINGS})
  target_compile_definitions(${name} PRIVATE _DEFAULT_SOURCE _DARWIN_C_SOURCE _FILE_OFFSET_BITS=64)
  target_link_libraries(${name} PRIVATE bz2_static bz2_reference)
  target_link_options(${name} PRIVATE -fsanitize=fuzzer)
endfunction()

bzng_fuzz_target(fuzz_decompress fuzz_decompress.c ref_state.c)
bzng_fuzz_target(fuzz_compress fuzz_compress.c)
bzng_fuzz_target(fuzz_sort fuzz_sort.c)

# The seed generator is an ordinary program; it reuses the decoder tester's
# crafted streams and the randomising compressor of test/CMakeLists.txt.
add_executable(fuzz_seeds gen_seeds.c)
target_include_directories(fuzz_seeds PRIVATE ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/lib ${PROJECT_SOURCE_DIR}/test)
target_compile_options(fuzz_seeds PRIVATE ${BZ2NG_WARNINGS})
target_compile_definitions(fuzz_seeds PRIVATE _DEFAULT_SOURCE _DARWIN_C_SOURCE _FILE_OFFSET_BITS=64)
target_link_libraries(fuzz_seeds PRIVATE bz2_static bz2_reference bz2_rand)
add_custom_target(fuzz_seed_corpus
                  COMMAND fuzz_seeds ${CMAKE_CURRENT_BINARY_DIR}/seeds
                  COMMENT "Writing the fuzz seed corpus to ${CMAKE_CURRENT_BINARY_DIR}/seeds")
