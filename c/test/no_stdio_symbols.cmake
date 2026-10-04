# Requires the static library LIB (built with BZ_NO_STDIO) to reference no
# stdio function or stream.
# usage: cmake -DLIB=... -P no_stdio_symbols.cmake
execute_process(COMMAND nm -u ${LIB} OUTPUT_VARIABLE _out RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "nm failed on ${LIB}")
endif()
string(REGEX MATCHALL "_?_?(f?printf|fputs|puts|fopen|fdopen|fclose|fread|fwrite|fflush|ferror|feof|fgetc|ungetc|setmode|stdin|stdout|stderr|__stderrp|__stdoutp|__stdinp|_IO_[a-z_]+)[^A-Za-z0-9_]" _found "${_out}\n")
if(_found)
  list(REMOVE_DUPLICATES _found)
  message(FATAL_ERROR "the BZ_NO_STDIO library references stdio: ${_found}")
endif()
message(STATUS "no stdio symbols referenced")
