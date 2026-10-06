# Requires the build to leave no libsais symbol where an application could see
# it, and the sorter's object to call no allocator: the static library STATIC
# defines and references no global symbol containing "libsais", the shared
# library SHARED (optional) exports none, and the object OBJ references no
# malloc, calloc, realloc or free. Local symbols (libsais's functions, given
# internal linkage) are allowed. NM is the binutils-style nm for the target;
# with MSVC's toolchain, DUMPBIN is its dumpbin. The sorter's entry point,
# bzng_bwt, must be among the library's symbols and the object's definitions,
# so that a listing read wrongly, and so empty, fails instead of passing.
# usage: cmake {-DNM=... | -DDUMPBIN=...} -DSTATIC=... -DOBJ=... [-DSHARED=...] -P sais_symbols.cmake

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

# Fails unless the list NAMES, from WHAT, holds bzng_bwt (with the leading
# underscore of Mach-O and 32-bit Windows names, or without).
function(require_bwt names what)
  list(FILTER names INCLUDE REGEX "^_?bzng_bwt$")
  if(NOT names)
    message(FATAL_ERROR "bzng_bwt is not among ${what}: the symbol listing was not read")
  endif()
endfunction()

# The external symbols of the COFF objects in FILE (an object or a library),
# from dumpbin's symbol tables, as nm-style lines " U name" (undefined) or
# " T name" (defined), in OUT.
function(coff_symbols file out)
  execute_process(COMMAND ${DUMPBIN} /nologo /symbols ${file} OUTPUT_VARIABLE _out RESULT_VARIABLE _rc)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "${DUMPBIN} failed on ${file}")
  endif()
  string(REPLACE "\r" "" _out "${_out}")
  string(REPLACE "\n" ";" _lines "${_out}")
  set(_text "")
  foreach(_line IN LISTS _lines)
    if(_line MATCHES " UNDEF .* External +\\| ([^ ]+)")
      string(APPEND _text " U ${CMAKE_MATCH_1}\n")
    elseif(_line MATCHES " External +\\| ([^ ]+)")
      string(APPEND _text " T ${CMAKE_MATCH_1}\n")
    endif()
  endforeach()
  set(${out} "${_text}" PARENT_SCOPE)
endfunction()

if(DUMPBIN)
  coff_symbols(${STATIC} _static)
else()
  global_symbols(${STATIC} FALSE _static)
endif()
symbol_names("${_static}" _names)
require_bwt("${_names}" "the static library's global symbols")
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

if(DUMPBIN)
  coff_symbols(${OBJ} _all)
  string(REGEX MATCHALL " T [^\n]*\n" _defined "${_all}")
  string(REPLACE ";" "" _defined "${_defined}")
  string(REGEX MATCHALL "U [^\n]*\n" _undef "${_all}")
  string(REPLACE ";" "" _undef "${_undef}")
else()
  global_symbols(${OBJ} TRUE _defined)
  execute_process(COMMAND ${NM} -u ${OBJ} OUTPUT_VARIABLE _undef RESULT_VARIABLE _rc ERROR_QUIET)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "${NM} failed on ${OBJ}")
  endif()
endif()
symbol_names("${_defined}" _names)
require_bwt("${_names}" "the sorter object's definitions")
string(REGEX MATCHALL "(^|[ \t\n_])(malloc|calloc|realloc|free|aligned_alloc|posix_memalign)([@\n]|$)" _alloc "${_undef}")
if(_alloc)
  message(FATAL_ERROR "the sorter's object references an allocator: ${_alloc}")
endif()
message(STATUS "no libsais symbol visible; the sorter calls no allocator")
