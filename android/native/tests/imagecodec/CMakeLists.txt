# imagecodec_compare (ImageCodecCompare.cpp): the Android build's texture codec against D3DX11 on a
# WARP device, and its BC decode against WARP's, on Windows, where d3dx11_43.dll (the DirectX
# End-User Runtime) and WARP are part of the system. A project of its own, for the build host:
#
#   cmake -S android/native/tests/imagecodec -B build/imagecodec-compare && cmake --build build/imagecodec-compare --config Release
#
# The D3DX11 header and zlib come from the engine's dependencies (cmake/FafDependencies.cmake).

cmake_minimum_required(VERSION 3.25)
project(imagecodec_compare LANGUAGES C CXX)

if(NOT WIN32)
  message(FATAL_ERROR "imagecodec_compare runs D3DX11 and WARP and is built on Windows.")
endif()

get_filename_component(FAF_REPO "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)
set(FAF_DEPENDENCIES_DIR "${FAF_REPO}/dependencies" CACHE PATH "The engine's dependencies folder")
set(_zlib "${FAF_DEPENDENCIES_DIR}/zlib-1.2.3")

add_library(imagecodec_zlib STATIC
  ${_zlib}/adler32.c ${_zlib}/compress.c ${_zlib}/crc32.c ${_zlib}/deflate.c ${_zlib}/inffast.c
  ${_zlib}/inflate.c ${_zlib}/inftrees.c ${_zlib}/trees.c ${_zlib}/uncompr.c ${_zlib}/zutil.c)
target_include_directories(imagecodec_zlib PUBLIC "${_zlib}")
target_compile_definitions(imagecodec_zlib PRIVATE _CRT_SECURE_NO_WARNINGS)

add_executable(imagecodec_compare
  ImageCodecCompare.cpp
  ../../src/gal/BlockCompressionAndroid.cpp
  ../../src/gal/ImageCodecAndroid.cpp
  ../../src/gal/ImageFilesAndroid.cpp
  "${FAF_REPO}/src/gpg/gal/backends/dxgi/DxgiFormats.cpp")
target_include_directories(imagecodec_compare PRIVATE "${FAF_REPO}/src" "${FAF_DEPENDENCIES_DIR}/DXSDK_D3DX/include")
target_compile_definitions(imagecodec_compare PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_features(imagecodec_compare PRIVATE cxx_std_20)
target_link_libraries(imagecodec_compare PRIVATE imagecodec_zlib d3d11)
if(MSVC)
  target_compile_options(imagecodec_compare PRIVATE /W4 /utf-8 /fp:precise)
endif()
