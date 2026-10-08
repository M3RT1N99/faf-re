# galtrace host tools: the portable reader and decoder (format/), galtrace-dump and the format unit
# test. Not part of main.exe: the graphics build compiles format/, record/ and play/ through
# port/graphics/port_graphics.props. Android x86_64/arm64 builds of the same sources:
# android/build_reader.py.
#
#   cmake -S port/graphics/trace -B <build> -A Win32   (or -A x64)
#   cmake --build <build> --config Release
#   ctest --test-dir <build> -C Release
cmake_minimum_required(VERSION 3.16)
project(galtrace CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(galtrace_format STATIC format/GalTraceFormat.cpp format/GalTraceIO.cpp)
target_include_directories(galtrace_format PUBLIC format)

if(MSVC)
  target_compile_options(galtrace_format PRIVATE /W4 /WX /utf-8)
  target_compile_definitions(galtrace_format PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(galtrace_format PRIVATE -Wall -Wextra -Wshadow -Werror)
endif()

add_executable(galtrace-dump tools/galtrace_dump.cpp)
target_link_libraries(galtrace-dump PRIVATE galtrace_format)

add_executable(galtrace_format_test tests/galtrace_format_test.cpp)
target_link_libraries(galtrace_format_test PRIVATE galtrace_format)

if(MSVC)
  foreach(tool galtrace-dump galtrace_format_test)
    target_compile_options(${tool} PRIVATE /W4 /WX /utf-8)
    target_compile_definitions(${tool} PRIVATE _CRT_SECURE_NO_WARNINGS)
  endforeach()
endif()

enable_testing()
add_test(NAME galtrace_format_test COMMAND galtrace_format_test ${CMAKE_CURRENT_BINARY_DIR})
