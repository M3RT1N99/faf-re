# gdifont_compare (GdiFontCompare.cpp): the Android build's GDI fonts on FreeType
# (src/win32/GdiFonts.cpp) against GDI, on Windows. A project of its own, for the build host, with
# the FreeType the Android build pins (cmake/FafAndroidFreetype.cmake):
#
#   cmake -S android/native/tests/gdifont -B build/gdifont-compare && cmake --build build/gdifont-compare --config Release

cmake_minimum_required(VERSION 3.25)
project(gdifont_compare LANGUAGES C CXX)

if(NOT WIN32)
  message(FATAL_ERROR "gdifont_compare compares with GDI and is built on Windows.")
endif()

get_filename_component(FAF_REPO "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)
include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/FafAndroidFreetype.cmake")

add_executable(gdifont_compare GdiFontCompare.cpp ../../src/win32/GdiFonts.cpp)
target_compile_features(gdifont_compare PRIVATE cxx_std_20)
target_link_libraries(gdifont_compare PRIVATE freetype gdi32)
if(MSVC)
  target_compile_options(gdifont_compare PRIVATE /W4 /utf-8)
endif()
