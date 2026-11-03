# fxc_compare (FxcCompare.cpp): the Android build's effect preprocessor against d3dcompiler_47, on
# Windows, where d3dcompiler_47 is part of the system. A project of its own, for the build host:
#
#   cmake -S android/native/tests/fxc -B build/fxc-compare && cmake --build build/fxc-compare --config Release

cmake_minimum_required(VERSION 3.25)
project(fxc_compare LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "fxc_compare runs d3dcompiler_47 and is built on Windows.")
endif()

add_executable(fxc_compare FxcCompare.cpp ../../src/gal/FxcPreprocessor.cpp)
target_compile_features(fxc_compare PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(fxc_compare PRIVATE /W4 /utf-8)
endif()
