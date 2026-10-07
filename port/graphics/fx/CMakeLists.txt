# port/graphics/fx: the portable effect front end (gpg::gal::fx) and its tools.
# See README.md. Not part of main.vcxproj: the engine build is unchanged.
#
# Host build (Windows, 32-bit like main.exe, so the D3DX dumper can link the
# DirectX SDK's x86 d3dx9.lib):
#   cmake -S port/graphics/fx -B buildstage/fx/win32 -A Win32
#   cmake --build buildstage/fx/win32 --config Release
#   ctest --test-dir buildstage/fx/win32 -C Release --output-on-failure
#
# The library itself is plain C++17 with no platform dependencies; any
# compiler builds it (the gate also compiles it for arm64 with the NDK).

cmake_minimum_required(VERSION 3.22)
project(faf_gal_fx LANGUAGES CXX)

get_filename_component(FAF_FX_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

if(PROJECT_IS_TOP_LEVEL AND NOT ANDROID)
  set(_faf_fx_host_default ON)
else()
  set(_faf_fx_host_default OFF)
endif()
option(FAF_FX_BUILD_TOOLS "Build fxmeta (and fxd3dx_dump on Win32)" ${_faf_fx_host_default})
option(FAF_FX_BUILD_TESTS "Build the CTest unit tests" ${_faf_fx_host_default})

function(faf_fx_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
    target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
  endif()
endfunction()

add_library(faf_gal_fx STATIC
  include/gpg/gal/fx/FxAst.h
  include/gpg/gal/fx/FxEffectParser.h
  include/gpg/gal/fx/FxHlslEmitter.h
  include/gpg/gal/fx/FxJson.h
  include/gpg/gal/fx/FxLexer.h
  include/gpg/gal/fx/FxMetadata.h
  include/gpg/gal/fx/FxPreprocessor.h
  include/gpg/gal/fx/FxSource.h
  include/gpg/gal/fx/FxStates.h
  src/FxEffectParser.cpp
  src/FxHlslEmitter.cpp
  src/FxJson.cpp
  src/FxLexer.cpp
  src/FxMetadata.cpp
  src/FxPreprocessor.cpp
  src/FxSource.cpp
  src/FxStates.cpp
)
add_library(faf::gal_fx ALIAS faf_gal_fx)
target_compile_features(faf_gal_fx PUBLIC cxx_std_17)
set_target_properties(faf_gal_fx PROPERTIES CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
target_include_directories(faf_gal_fx PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
faf_fx_warnings(faf_gal_fx)

if(FAF_FX_BUILD_TOOLS)
  add_executable(fxmeta tools/fxmeta.cpp)
  target_link_libraries(fxmeta PRIVATE faf_gal_fx)
  faf_fx_warnings(fxmeta)

  # SM5 HLSL for every pass stage (FxHlslEmitter), optionally compiled with FXC (Windows).
  add_executable(fxhlsl tools/fxhlsl.cpp)
  target_link_libraries(fxhlsl PRIVATE faf_gal_fx)
  faf_fx_warnings(fxhlsl)

  # The D3DX reflection dumper needs the June 2010 DirectX SDK's x86
  # d3dx9.lib, the one main.exe links (main.vcxproj AdditionalDependencies).
  set(FAF_FX_D3DX_DIR "${FAF_FX_REPO_ROOT}/dependencies/DXSDK_D3DX" CACHE PATH "DirectX SDK D3DX headers and libs")
  if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4 AND EXISTS "${FAF_FX_D3DX_DIR}/lib/x86/d3dx9.lib")
    add_executable(fxd3dx_dump tools/fxd3dx_dump.cpp tools/FakeD3D9Device.cpp tools/FakeD3D9Device.h)
    target_include_directories(fxd3dx_dump PRIVATE "${FAF_FX_D3DX_DIR}/include")
    target_link_libraries(fxd3dx_dump PRIVATE faf_gal_fx "${FAF_FX_D3DX_DIR}/lib/x86/d3dx9.lib" d3d9.lib)
    faf_fx_warnings(fxd3dx_dump)
  else()
    message(STATUS "fxd3dx_dump not built: needs Win32 (-A Win32) and ${FAF_FX_D3DX_DIR}/lib/x86/d3dx9.lib")
  endif()
endif()

if(FAF_FX_BUILD_TESTS)
  enable_testing()
  add_executable(fx_tests tests/fx_tests.cpp)
  target_link_libraries(fx_tests PRIVATE faf_gal_fx)
  faf_fx_warnings(fx_tests)
  add_test(NAME fx_tests COMMAND fx_tests)

  add_executable(fx_hlsl_tests tests/fx_hlsl_tests.cpp)
  target_link_libraries(fx_hlsl_tests PRIVATE faf_gal_fx)
  faf_fx_warnings(fx_hlsl_tests)
  add_test(NAME fx_hlsl_tests COMMAND fx_hlsl_tests)
endif()
