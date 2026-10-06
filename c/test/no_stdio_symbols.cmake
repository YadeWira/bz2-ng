# Requires the static library LIB (built with BZ_NO_STDIO) to reference no
# stdio function or stream.  The library's undefined symbols come from nm, or
# with MSVC's toolchain from DUMPBIN (its dumpbin); they must include malloc,
# which the default allocator calls, so that a listing read wrongly, and so
# empty, fails instead of passing.  Microsoft's C runtime reaches stdio
# through __acrt_iob_func (stdin, stdout, stderr) and __stdio_common_*
# (the printf family).
# usage: cmake -DLIB=... [-DDUMPBIN=...] -P no_stdio_symbols.cmake
if(DUMPBIN)
  execute_process(COMMAND ${DUMPBIN} /nologo /symbols ${LIB} OUTPUT_VARIABLE _syms RESULT_VARIABLE _rc)
  string(REPLACE "\r" "" _syms "${_syms}")
  string(REPLACE "\n" ";" _lines "${_syms}")
  set(_out "")
  foreach(_line IN LISTS _lines)
    if(_line MATCHES " UNDEF .* External +\\| ([^ ]+)")
      string(APPEND _out "${CMAKE_MATCH_1}\n")
    endif()
  endforeach()
else()
  execute_process(COMMAND nm -u ${LIB} OUTPUT_VARIABLE _out RESULT_VARIABLE _rc)
endif()
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "listing the symbols of ${LIB} failed")
endif()
if(NOT "${_out}\n" MATCHES "(^|[ \t\n_])malloc[@\n]")
  message(FATAL_ERROR "malloc is not among the undefined symbols of ${LIB}: the symbol listing was not read")
endif()
string(REGEX MATCHALL "_?_?(f?printf|fputs|puts|fopen|fdopen|fclose|fread|fwrite|fflush|ferror|feof|fgetc|ungetc|setmode|stdin|stdout|stderr|__stderrp|__stdoutp|__stdinp|_IO_[a-z_]+|acrt_iob_func|stdio_common_[a-z_]+)[^A-Za-z0-9_]" _found "${_out}\n")
if(_found)
  list(REMOVE_DUPLICATES _found)
  message(FATAL_ERROR "the BZ_NO_STDIO library references stdio: ${_found}")
endif()
message(STATUS "no stdio symbols referenced")
