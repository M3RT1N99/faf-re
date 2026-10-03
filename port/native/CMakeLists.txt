# port/native: the portable core of the Android port (see README.md).
#
# Standalone host build (Windows, MSVC or clang):
#   cmake -S port/native -B buildstage/native-host
#   cmake --build buildstage/native-host --config Release
#   ctest --test-dir buildstage/native-host -C Release --output-on-failure
#
# From another project (port/android): add_subdirectory(<repo>/port/native <bindir>)
# and link faf::port_core. Tools and tests default to OFF there and on Android.

cmake_minimum_required(VERSION 3.22)
project(faf_port_native LANGUAGES C CXX)

get_filename_component(FAF_PORT_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

if(PROJECT_IS_TOP_LEVEL AND NOT ANDROID)
  set(_faf_port_host_default ON)
else()
  set(_faf_port_host_default OFF)
endif()
option(FAF_PORT_BUILD_TOOLS "Build the host tool faf_datacheck" ${_faf_port_host_default})
option(FAF_PORT_BUILD_TESTS "Build the CTest unit and integration tests" ${_faf_port_host_default})
option(FAF_PORT_WARNINGS_AS_ERRORS "Treat warnings in the port's own sources as errors" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(FafPortZlib)
include(FafPortLuaPlus)

faf_port_add_luaplus()

# Warnings for the port's own C++ (not for the vendored C).
function(faf_port_enable_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
    if(FAF_PORT_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
    if(FAF_PORT_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
  if(WIN32)
    target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()
endfunction()

add_library(faf_port_core STATIC
  include/faf/port/CommandLine.h
  include/faf/port/DataPath.h
  include/faf/port/FileSystem.h
  include/faf/port/Image.h
  include/faf/port/Vfs.h
  src/CommandLine.cpp
  src/DataPath.cpp
  src/FileSystem.cpp
  src/FileSystemDetail.h
  src/Image.cpp
  src/LuaCore.h
  src/NativeFile.cpp
  src/NativeFile.h
  src/Vfs.cpp
  src/ZipArchive.cpp
  src/ZipArchive.h
)
add_library(faf::port_core ALIAS faf_port_core)
target_compile_features(faf_port_core PUBLIC cxx_std_20)
set_target_properties(faf_port_core PROPERTIES
  CXX_EXTENSIONS OFF
  POSITION_INDEPENDENT_CODE ON
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
)
target_include_directories(faf_port_core
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(faf_port_core PUBLIC faf_port_lua ${FAF_PORT_ZLIB_TARGET})
if(NOT WIN32 AND NOT ANDROID)
  # 64-bit off_t for pread on 32-bit glibc hosts (NativeFile.cpp asserts it).
  target_compile_definitions(faf_port_core PRIVATE _FILE_OFFSET_BITS=64)
endif()
faf_port_enable_warnings(faf_port_core)

# Host executables get a manifest that makes the process code page UTF-8, so
# the Lua io library's fopen() takes the same UTF-8 paths the port uses
# everywhere else (Windows 10 1903+), and that opts into long paths.
set(FAF_PORT_UTF8_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/tools/faf_port_utf8.manifest")
function(faf_port_host_executable target)
  if(WIN32)
    target_sources(${target} PRIVATE "${FAF_PORT_UTF8_MANIFEST}")
  endif()
  faf_port_enable_warnings(${target})
endfunction()

if(FAF_PORT_BUILD_TOOLS OR FAF_PORT_BUILD_TESTS)
  # The check logic is shared by the tool and the integration test.
  add_library(faf_port_datacheck STATIC tools/DataCheck.cpp tools/DataCheck.h)
  target_include_directories(faf_port_datacheck PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/tools")
  target_link_libraries(faf_port_datacheck PUBLIC faf_port_core)
  faf_port_enable_warnings(faf_port_datacheck)
endif()

if(FAF_PORT_BUILD_TOOLS)
  add_executable(faf_datacheck tools/faf_datacheck.cpp)
  target_link_libraries(faf_datacheck PRIVATE faf_port_datacheck)
  faf_port_host_executable(faf_datacheck)
endif()

if(FAF_PORT_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
