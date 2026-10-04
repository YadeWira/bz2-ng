# Benchmark: bz2-ng against stock libbz2 (the test reference, ref_ prefix) in
# one binary. Needs the tests' reference library.
if(TARGET bz2_reference)
  add_executable(bench bench.c)
  target_include_directories(bench PRIVATE ${PROJECT_SOURCE_DIR}/lib ${PROJECT_SOURCE_DIR}/test)
  target_compile_options(bench PRIVATE ${BZ2NG_WARNINGS})
  target_link_libraries(bench PRIVATE bz2_static bz2_reference)
endif()
