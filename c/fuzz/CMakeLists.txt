# libFuzzer targets (BZ2NG_BUILD_FUZZ): each drives bz2-ng and an independent
# reference with the same input and aborts on any difference.  The top-level
# CMakeLists.txt has already put the sanitisers and libFuzzer's coverage
# instrumentation on every compilation; these executables also link libFuzzer.
#
#   fuzz_decompress  bz2-ng's decoder against stock libbz2's, call for call
#   fuzz_compress    bz2-ng's encoder against stock libbz2's, call for call
#   fuzz_sort        the encoder's sort stage against an independent oracle
#   fuzz_sort_form_* the same for each of the sorter's compile-time forms
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

# The sorter's kernel forms follow the compiler and target (lib/sort.c), so
# a clang build compiles only clang's; as for the oracle tests
# (test/CMakeLists.txt), each form some build ships is also built here with
# its own lib/sort.c, which bz2_static's copy gives way to at link time.
function(bzng_fuzz_sort_form name mask_selects skip_empty_slots suffix0_apart branchless_naming)
  bzng_fuzz_target(fuzz_sort_form_${name} fuzz_sort.c ${PROJECT_SOURCE_DIR}/lib/sort.c)
  target_compile_definitions(fuzz_sort_form_${name} PRIVATE
    SORT_MASK_SELECTS=${mask_selects} SORT_SKIP_EMPTY_SLOTS=${skip_empty_slots}
    SORT_SUFFIX0_APART=${suffix0_apart} SORT_BRANCHLESS_NAMING=${branchless_naming})
endfunction()
bzng_fuzz_sort_form(apple_clang 0 0 0 1)
bzng_fuzz_sort_form(clang 0 0 1 0)
bzng_fuzz_sort_form(gcc 1 1 1 0)
bzng_fuzz_sort_form(apple_gcc 1 1 1 1)

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
