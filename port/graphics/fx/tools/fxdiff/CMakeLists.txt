# fxdiff: renders every pass of an effect through D3D9 (D3DX, the game's legacy compiler) and through
# the Diligent backend's effect layer (port/graphics/diligent/EffectsDiligentGpu.cpp) on D3D11, and
# runs the generated stages through Diligent's HLSL -> SPIR-V path. See fxdiff.cpp and
# scripts/port/fxdiff.py, which configures, builds and runs it.
#
# Win32 Debug only: it links the Diligent Debug libraries that scripts/port/build_diligent_win32.ps1
# installs into buildstage/diligent/install/Win32 (/MDd, _ITERATOR_DEBUG_LEVEL=0, the settings
# main.vcxproj's Debug|Win32 uses), and the DirectX SDK's x86 d3dx9.lib.
#
#   cmake -S port/graphics/fx/tools/fxdiff -B buildstage/fxdiff -A Win32
#   cmake --build buildstage/fxdiff --config Debug

cmake_minimum_required(VERSION 3.22)
project(fxdiff LANGUAGES CXX)

get_filename_component(FXDIFF_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
set(FXDIFF_DILIGENT_INSTALL "${FXDIFF_REPO_ROOT}/buildstage/diligent/install/Win32" CACHE PATH "Diligent Win32 install")
set(FXDIFF_DILIGENT_SOURCE "${FXDIFF_REPO_ROOT}/dependencies/DiligentCore" CACHE PATH "DiligentCore source (GLSLangUtils.hpp)")
set(FXDIFF_D3DX "${FXDIFF_REPO_ROOT}/dependencies/DXSDK_D3DX" CACHE PATH "DirectX SDK D3DX")

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "fxdiff is Win32 only (configure with -A Win32): the Diligent install and d3dx9.lib are x86")
endif()

set(FX_DIR "${FXDIFF_REPO_ROOT}/port/graphics/fx")
add_executable(fxdiff
  fxdiff.cpp
  ${FX_DIR}/src/FxEffectParser.cpp
  ${FX_DIR}/src/FxHlslEmitter.cpp
  ${FX_DIR}/src/FxJson.cpp
  ${FX_DIR}/src/FxLexer.cpp
  ${FX_DIR}/src/FxMetadata.cpp
  ${FX_DIR}/src/FxPreprocessor.cpp
  ${FX_DIR}/src/FxSource.cpp
  ${FX_DIR}/src/FxStates.cpp
  ${FXDIFF_REPO_ROOT}/port/graphics/diligent/EffectsDiligentGpu.cpp
)
target_compile_features(fxdiff PRIVATE cxx_std_17)
set_property(TARGET fxdiff PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
target_include_directories(fxdiff PRIVATE
  "${FX_DIR}/include"
  "${FXDIFF_REPO_ROOT}"
  "${FXDIFF_DILIGENT_INSTALL}/include"
  "${FXDIFF_DILIGENT_INSTALL}/include/Graphics/GraphicsEngine/interface"
  "${FXDIFF_DILIGENT_INSTALL}/include/Primitives/interface"
  "${FXDIFF_DILIGENT_SOURCE}/Graphics/ShaderTools/include"
  "${FXDIFF_D3DX}/include"
)
target_compile_definitions(fxdiff PRIVATE
  PLATFORM_WIN32=1 D3D11_SUPPORTED=1 GLSLANG_SUPPORTED=1
  $<$<CONFIG:Debug>:DILIGENT_DEVELOPMENT DILIGENT_DEBUG _ITERATOR_DEBUG_LEVEL=0>
  NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_compile_options(fxdiff PRIVATE /W3 /permissive- /utf-8 /Zc:__cplusplus)

set(_dl "${FXDIFF_DILIGENT_INSTALL}/lib/$<IF:$<CONFIG:Debug>,Debug,Release>")
set(_d "$<$<CONFIG:Debug>:d>")
target_link_libraries(fxdiff PRIVATE
  "${_dl}/DiligentCore.lib"
  "${_dl}/glslang${_d}.lib" "${_dl}/SPIRV${_d}.lib" "${_dl}/MachineIndependent${_d}.lib" "${_dl}/OSDependent${_d}.lib"
  "${_dl}/GenericCodeGen${_d}.lib" "${_dl}/SPIRV-Tools-opt.lib" "${_dl}/SPIRV-Tools.lib" "${_dl}/xxhash.lib"
  "${FXDIFF_D3DX}/lib/x86/d3dx9.lib" d3d9.lib d3d11.lib dxgi.lib d3dcompiler.lib dxguid.lib)
