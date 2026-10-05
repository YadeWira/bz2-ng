# Requires the build to leave no libsais symbol where an application could see
# it, and the sorter's object to call no allocator: the static library STATIC
# defines and references no global symbol containing "libsais", the shared
# library SHARED (optional) exports none, and the object OBJ references no
# malloc, calloc, realloc or free. Local symbols (libsais's functions, given
# internal linkage) are allowed. NM is the binutils-style nm for the target.
# usage: cmake -DNM=... -DSTATIC=... -DOBJ=... [-DSHARED=...] -P sais_symbols.cmake

# The global symbols nm lists for FILE (defined, and undefined unless
# DEFINED_ONLY), in OUT.
function(global_symbols file defined_only out)
  set(_args -g)
  if(defined_only)
    list(APPEND _args --defined-only)
  endif()
  execute_process(COMMAND ${NM} ${_args} ${file} OUTPUT_VARIABLE _out RESULT_VARIABLE _rc ERROR_QUIET)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "${NM} failed on ${file}")
  endif()
  set(${out} "${_out}" PARENT_SCOPE)
endfunction()

# Symbol names only: nm also prints member and file names, which contain
# "sort_libsais".
function(symbol_names text out)
  string(REPLACE "\n" ";" _lines "${text}")
  set(_names "")
  foreach(_line IN LISTS _lines)
    if(_line MATCHES "^[0-9a-fA-F ]* [A-Za-z] ([^ ]+)$")
      list(APPEND _names ${CMAKE_MATCH_1})
    endif()
  endforeach()
  set(${out} "${_names}" PARENT_SCOPE)
endfunction()

global_symbols(${STATIC} FALSE _static)
symbol_names("${_static}" _names)
list(FILTER _names INCLUDE REGEX "libsais")
if(_names)
  message(FATAL_ERROR "the static library has global libsais symbols: ${_names}")
endif()

if(SHARED AND NOT SHARED MATCHES "[.]dll$")
  global_symbols(${SHARED} TRUE _shared)
  symbol_names("${_shared}" _names)
  list(FILTER _names INCLUDE REGEX "libsais")
  if(_names)
    message(FATAL_ERROR "the shared library exports libsais symbols: ${_names}")
  endif()
endif()

execute_process(COMMAND ${NM} -u ${OBJ} OUTPUT_VARIABLE _undef RESULT_VARIABLE _rc ERROR_QUIET)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "${NM} failed on ${OBJ}")
endif()
string(REGEX MATCHALL "(^|[ \t\n_])(malloc|calloc|realloc|free|aligned_alloc|posix_memalign)([@\n]|$)" _alloc "${_undef}")
if(_alloc)
  message(FATAL_ERROR "the sorter's object references an allocator: ${_alloc}")
endif()
message(STATUS "no libsais symbol visible; the sorter calls no allocator")
