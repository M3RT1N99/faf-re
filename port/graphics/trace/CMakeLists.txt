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

add_library(galtrace_format STATIC format/GalTraceFormat.cpp format/GalTraceIO.cpp format/GalTraceResolver.cpp)
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

# galtrace-refs (M7a1): recording -> version 2 trace without game data, and its checks. It mounts the game
# data as the device does, through port/native (faf::port_core: the VFS and the data-path script; zlib
# and LuaPlus are built from dependencies/, nothing is downloaded).
option(GALTRACE_WITH_PORT_NATIVE "Build galtrace-refs against port/native's VFS" ON)
if(GALTRACE_WITH_PORT_NATIVE AND NOT ANDROID AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../native/CMakeLists.txt")
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../native" "${CMAKE_CURRENT_BINARY_DIR}/port_native")
  add_executable(galtrace-refs tools/galtrace_refs.cpp tools/GalTraceRefs.cpp tools/GalTraceRefs.h)
  target_link_libraries(galtrace-refs PRIVATE galtrace_format faf::port_core)
  target_compile_features(galtrace-refs PRIVATE cxx_std_20)
  if(MSVC)
    target_compile_options(galtrace-refs PRIVATE /W4 /WX /utf-8)
    target_compile_definitions(galtrace-refs PRIVATE _CRT_SECURE_NO_WARNINGS)
  else()
    target_compile_options(galtrace-refs PRIVATE -Wall -Wextra -Wshadow -Werror)
  endif()
endif()
