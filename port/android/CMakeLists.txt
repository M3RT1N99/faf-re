# port/android: the native game runtime, libfaf_android.so, which GameActivity
# (an android.app.NativeActivity) loads. One shared library that carries
# everything it needs: the portable core (port/native), DiligentCore's Vulkan
# and OpenGL ES backends and the C++ runtime, all linked statically. See
# docs/port/android.md for the build and the launch contract.
#
# scripts/port/build_android.ps1 configures and builds this project in
# buildstage/android-native and packages the result. By hand:
#
#   cmake -S port/android -B buildstage/android-native -G Ninja `
#     -DCMAKE_TOOLCHAIN_FILE=<ndk>/build/cmake/android.toolchain.cmake `
#     -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 -DANDROID_STL=c++_static `
#     -DCMAKE_BUILD_TYPE=Release
#   cmake --build buildstage/android-native --target faf_android
#
# The library in the build directory keeps its symbols and debug info (for
# ndk-stack / llvm-symbolizer); the build script strips the copy that goes into
# the APK.

cmake_minimum_required(VERSION 3.22...4.4)

# The APK ships exactly one library and no libc++_shared.so, so the C++ runtime
# must be static. These defaults are read by the NDK toolchain file during
# project(); values passed on the command line win.
if(NOT DEFINED ANDROID_STL)
  set(ANDROID_STL c++_static)
endif()
if(NOT DEFINED ANDROID_ABI)
  set(ANDROID_ABI arm64-v8a)
endif()
if(NOT DEFINED ANDROID_PLATFORM)
  set(ANDROID_PLATFORM android-26)
endif()

# C++20 sources and Ninja would otherwise make CMake scan every file for module
# dependencies with clang-scan-deps; nothing here uses modules.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

project(faf_android LANGUAGES C CXX)

if(NOT ANDROID)
  message(FATAL_ERROR "Configure port/android with the Android NDK toolchain "
    "(-DCMAKE_TOOLCHAIN_FILE=<ndk>/build/cmake/android.toolchain.cmake).")
endif()
if(NOT ANDROID_STL STREQUAL "c++_static")
  message(FATAL_ERROR "ANDROID_STL is '${ANDROID_STL}'; libfaf_android.so must link the C++ runtime "
    "statically (c++_static) because the APK does not ship libc++_shared.so.")
endif()
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

get_filename_component(FAF_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

# versionName goes into status.json so the launcher can tell which runtime
# wrote it. build_android.ps1 passes it; a plain configure reads the same file.
set(FAF_ANDROID_VERSION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/version.properties")
if(NOT FAF_ANDROID_VERSION_NAME)
  file(STRINGS "${FAF_ANDROID_VERSION_FILE}" _faf_version_line REGEX "^[ \t]*versionName[ \t]*=")
  string(REGEX REPLACE "^[ \t]*versionName[ \t]*=[ \t]*" "" _faf_version_name "${_faf_version_line}")
  string(STRIP "${_faf_version_name}" _faf_version_name)
  if(NOT _faf_version_name)
    message(FATAL_ERROR "${FAF_ANDROID_VERSION_FILE} does not define versionName.")
  endif()
  set(FAF_ANDROID_VERSION_NAME "${_faf_version_name}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${FAF_ANDROID_VERSION_FILE}")
endif()
if(NOT FAF_ANDROID_VERSION_NAME MATCHES "^[0-9A-Za-z.+-]+$")
  message(FATAL_ERROR "Invalid FAF_ANDROID_VERSION_NAME '${FAF_ANDROID_VERSION_NAME}'.")
endif()
message(STATUS "faf_android: versionName ${FAF_ANDROID_VERSION_NAME}")

# Everything ends up in one shared object. Hidden visibility everywhere keeps
# the dynamic symbol table to the one entry point NativeActivity looks up.
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_POLICY_DEFAULT_CMP0063 NEW)

# add_subdirectory(... SYSTEM) (CMake 3.25+) turns the dependencies' include
# directories into system includes, so this project's warning level does not
# apply to their headers.
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.25)
  set(_faf_system_subdir SYSTEM)
else()
  set(_faf_system_subdir "")
endif()

# --- port/native: data path script, VFS, DDS decode, command line ------------------

add_subdirectory("${FAF_REPO_ROOT}/port/native" "${CMAKE_BINARY_DIR}/port-native" EXCLUDE_FROM_ALL)

# --- DiligentCore -------------------------------------------------------------

set(DILIGENT_CORE_DIR "${FAF_REPO_ROOT}/dependencies/DiligentCore")
if(NOT EXISTS "${DILIGENT_CORE_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "DiligentCore is missing in ${DILIGENT_CORE_DIR}. "
    "Run: powershell -ExecutionPolicy Bypass -File scripts/port/bootstrap_android.ps1")
endif()

# Diligent's ThirdParty/abseil-cpp FetchContent-declares abseil-cpp at
# configure time, although nothing linked here uses it. bootstrap_android.ps1
# pre-fetches the pinned commit into dependencies/abseil-cpp; use it so the
# configure step needs no network. An explicit -DFETCHCONTENT_SOURCE_DIR_ABSEIL-CPP wins.
set(FAF_ABSEIL_LOCAL_DIR "${FAF_REPO_ROOT}/dependencies/abseil-cpp")
if(NOT FETCHCONTENT_SOURCE_DIR_ABSEIL-CPP AND EXISTS "${FAF_ABSEIL_LOCAL_DIR}/CMakeLists.txt")
  set(FETCHCONTENT_SOURCE_DIR_ABSEIL-CPP "${FAF_ABSEIL_LOCAL_DIR}"
    CACHE PATH "Local abseil-cpp source tree (skips the download)" FORCE)
endif()
if(FETCHCONTENT_SOURCE_DIR_ABSEIL-CPP)
  message(STATUS "faf_android: abseil-cpp from ${FETCHCONTENT_SOURCE_DIR_ABSEIL-CPP}")
else()
  message(STATUS "faf_android: no local abseil-cpp; Diligent's configure downloads it "
    "(scripts/port/bootstrap_android.ps1 pre-fetches it for offline builds)")
endif()

# Backends: Vulkan (preferred) and OpenGL ES (fallback), both static. HLSL
# stays enabled because the runtime compiles its inline HLSL shaders at run
# time (glslang for Vulkan, Diligent's HLSL->GLSL converter for GLES).
set(DILIGENT_NO_VULKAN OFF CACHE BOOL "Vulkan backend (required)" FORCE)
set(DILIGENT_NO_OPENGL OFF CACHE BOOL "OpenGL ES backend (required: Vulkan fallback)" FORCE)
set(DILIGENT_NO_HLSL OFF CACHE BOOL "The runtime's shaders are HLSL" FORCE)
set(DILIGENT_NO_GLSLANG OFF CACHE BOOL "Vulkan compiles HLSL with glslang" FORCE)
set(DILIGENT_BUILD_TESTS OFF CACHE BOOL "No Diligent tests" FORCE)
set(DILIGENT_USE_OPENXR OFF CACHE BOOL "OpenXR is not used")
set(DILIGENT_NO_WEBGPU ON CACHE BOOL "WebGPU is not used")
set(DILIGENT_NO_ARCHIVER ON CACHE BOOL "The render state archiver is not used")
set(DILIGENT_NO_SUPER_RESOLUTION ON CACHE BOOL "Super resolution is not used")
# Diligent's install() rules predate CMake 3.31; nothing here is installed, so
# take the new DESTINATION normalisation instead of a warning per rule.
set(CMAKE_POLICY_DEFAULT_CMP0177 NEW)

add_subdirectory("${DILIGENT_CORE_DIR}" "${CMAKE_BINARY_DIR}/DiligentCore" EXCLUDE_FROM_ALL ${_faf_system_subdir})

# --- galplay (M7a1, release 0.5.0): the menu trace through the port's gal backend ----------
#
# GameActivity's mode "menu-replay" (src/GalPlay.h) replays a galtrace of FAF's main menu
# (port/graphics/trace) through the port's own Diligent gal backend (port/graphics/diligent) on
# Vulkan. Four parts, all linked statically into libfaf_android.so:
# - faf::gal_fx: the effect front end (port/graphics/fx), portable C++17;
# - galtrace_format: the trace reader (port/graphics/trace's own target), portable C++17;
# - faf_galplay_diligent: the backend's TUs that include Diligent and never a gal header
#   (DiligentHost, ShaderCompileDiligent, EffectsDiligentGpu, PipelineDiligent; the backend keeps the
#   two header sets apart, PipelineDiligent.h), and the image decoders; plain NDK flags;
# - faf_galplay_engine: everything that sees gpg::gal types: the rest of the backend, galplay's player
#   (trace/play/GalPlayer.cpp), the engine's gal TUs it needs (src/sdk) and the Android stand-ins
#   for the engine TUs that cannot build here (diligent/android/GalDevicePortable.cpp). These compile
#   like the engine sweep compiles src/sdk for Android (port/engine/compile_flags.txt,
#   scripts/port/engine_sweep.py: the shim port/engine/shim, the project's defines and include
#   directories, the MSVC semantics flags), plus FAF_PORT_GRAPHICS(_DILIGENT).
# GalPlay.cpp (the app side: data, window, pacing, files) has no engine types and builds with the
# runtime's own flags. galplay_cli is the same replay as a command-line program for the emulator
# and devices over adb (no window: the frames are read back offscreen); it is not in the APK.

add_subdirectory("${FAF_REPO_ROOT}/port/graphics/fx" "${CMAKE_BINARY_DIR}/port-graphics-fx" EXCLUDE_FROM_ALL)
add_subdirectory("${FAF_REPO_ROOT}/port/graphics/trace" "${CMAKE_BINARY_DIR}/port-graphics-trace" EXCLUDE_FROM_ALL)

set(FAF_SDK_DIR "${FAF_REPO_ROOT}/src/sdk")
set(FAF_GRAPHICS_DIR "${FAF_REPO_ROOT}/port/graphics")

add_library(faf_galplay_diligent STATIC
  "${FAF_GRAPHICS_DIR}/diligent/DiligentHost.cpp"
  "${FAF_GRAPHICS_DIR}/diligent/EffectsDiligentGpu.cpp"
  "${FAF_GRAPHICS_DIR}/diligent/PipelineDiligent.cpp"
  "${FAF_GRAPHICS_DIR}/diligent/ShaderCompileDiligent.cpp"
  "${FAF_GRAPHICS_DIR}/diligent/Texture2DPortable.cpp"
  "${FAF_GRAPHICS_DIR}/diligent/android/ImageDecodePortable.cpp"
)
set_target_properties(faf_galplay_diligent PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_include_directories(faf_galplay_diligent PRIVATE "${FAF_REPO_ROOT}" "${DILIGENT_CORE_DIR}"
  "${DILIGENT_CORE_DIR}/ThirdParty/Vulkan-Headers/include")
target_compile_definitions(faf_galplay_diligent PRIVATE PLATFORM_ANDROID=1 VULKAN_SUPPORTED=1 GLES_SUPPORTED=1)
target_compile_options(faf_galplay_diligent PRIVATE -pthread)
target_link_libraries(faf_galplay_diligent PUBLIC
  faf::gal_fx
  Diligent-GraphicsEngineVk-static
  Diligent-Common
  Diligent-PlatformInterface
  android
  log
)
foreach(_faf_spirv_cross spirv-cross-glsl spirv-cross-core)
  # The GL route's SPIRV-Cross step (ShaderCompileDiligent.cpp) is compiled in, not used on Android.
  if(TARGET ${_faf_spirv_cross})
    target_link_libraries(faf_galplay_diligent PUBLIC ${_faf_spirv_cross})
  endif()
endforeach()

add_library(faf_galplay_engine STATIC
  # the Diligent gal backend's engine-side TUs
  "${FAF_GRAPHICS_DIR}/diligent/D3D9Oracle.cpp"
  "${FAF_GRAPHICS_DIR}/diligent/DeviceDiligent.cpp"
  "${FAF_GRAPHICS_DIR}/diligent/EffectsDiligent.cpp"
  "${FAF_GRAPHICS_DIR}/diligent/ResourcesDiligent.cpp"
  "${FAF_GRAPHICS_DIR}/diligent/SelfTestDiligent.cpp"
  "${FAF_GRAPHICS_DIR}/diligent/android/GalDevicePortable.cpp"
  # galplay's player
  "${FAF_GRAPHICS_DIR}/trace/play/GalPlayer.cpp"
  # the engine's gal contexts and what they use
  "${FAF_SDK_DIR}/gpg/gal/ContextInterfaces.cpp"
  "${FAF_SDK_DIR}/gpg/gal/Effect.cpp"
  "${FAF_SDK_DIR}/gpg/gal/GalInterfaces.cpp"
  "${FAF_SDK_DIR}/gpg/gal/StringUtils.cpp"
  "${FAF_SDK_DIR}/gpg/core/containers/String.cpp"
  "${FAF_SDK_DIR}/legacy/containers/String.cpp"
  "${FAF_SDK_DIR}/legacy/math/X87Math.cpp"
  "${FAF_SDK_DIR}/moho/math/Vector4f.cpp"
  # the engine-side half of galplay
  src/GalPlayEngine.cpp
  src/GalPlayEngine.h
)
set_target_properties(faf_galplay_engine PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
# The shim first: it stands in for <windows.h> and the MSVC CRT headers the engine includes.
target_include_directories(faf_galplay_engine BEFORE PRIVATE "${FAF_REPO_ROOT}/port/engine/shim")
target_include_directories(faf_galplay_engine PRIVATE
  "${FAF_SDK_DIR}"
  "${FAF_REPO_ROOT}/dependencies/LuaPlus_Build1081/Src/LuaPlus/include"
  "${FAF_REPO_ROOT}/dependencies/boost_1_34_1"
  "${FAF_REPO_ROOT}/dependencies/zlib-1.2.3/include"
  "${FAF_REPO_ROOT}/dependencies/WildMagic3p8/Foundation"
  "${FAF_REPO_ROOT}/dependencies/WildMagic3p8/Foundation/Math"
  "${FAF_REPO_ROOT}/dependencies/WildMagic3p8/Foundation/System"
  "${FAF_REPO_ROOT}"
  src
)
target_compile_definitions(faf_galplay_engine PRIVATE
  # main.vcxproj's defines as the engine sweep keeps them (WIN32/_WINDOWS dropped)
  WIN32_LEAN_AND_MEAN NOMINMAX LUAPLUS_LIB BOOST_ALL_NO_LIB BOOST_NO_AUTO_PTR _CRT_SECURE_NO_WARNINGS
  _UNICODE UNICODE
  _LIBCPP_ENABLE_CXX17_REMOVED_AUTO_PTR _LIBCPP_ENABLE_CXX17_REMOVED_UNARY_BINARY_FUNCTION
  # the graphics build's switches (port/graphics/port_graphics.props)
  FAF_PORT_GRAPHICS=1 FAF_PORT_GRAPHICS_DILIGENT=1
)
target_compile_options(faf_galplay_engine PRIVATE
  -fms-extensions -fsigned-char -ffp-contract=off -fno-fast-math -fno-strict-aliasing -pthread
  "SHELL:-include ${FAF_SDK_DIR}/platform/X64LayoutAsserts.h"
  # the recovered engine code's MSVC idioms, not this build's business
  -Wno-microsoft-cast -Wno-ignored-attributes -Wno-unused-value -Wno-switch -Wno-nonportable-include-path
)
# LINK_ONLY: Diligent's usage requirements (its interface include directories, whose header names
# would shadow the engine's) stay on the Diligent side.
target_link_libraries(faf_galplay_engine PRIVATE faf::gal_fx PUBLIC galtrace_format "$<LINK_ONLY:faf_galplay_diligent>")

# --- libfaf_android.so ---------------------------------------------------------------

set(NATIVE_APP_GLUE_DIR "${CMAKE_ANDROID_NDK}/sources/android/native_app_glue")
if(NOT EXISTS "${NATIVE_APP_GLUE_DIR}/android_native_app_glue.c")
  message(FATAL_ERROR "android_native_app_glue.c not found in ${NATIVE_APP_GLUE_DIR}")
endif()

add_library(faf_android SHARED
  src/AndroidArgs.cpp
  src/AndroidArgs.h
  src/AndroidMain.cpp
  src/CrashHandler.cpp
  src/CrashHandler.h
  src/GalPlay.cpp
  src/GalPlay.h
  src/GameRuntime.cpp
  src/GameRuntime.h
  src/Log.cpp
  src/Log.h
  src/MenuReplayMode.cpp
  src/MenuReplayMode.h
  src/Renderer.cpp
  src/Renderer.h
  src/RunStatus.cpp
  src/RunStatus.h
  "${NATIVE_APP_GLUE_DIR}/android_native_app_glue.c"
)
target_compile_features(faf_android PRIVATE cxx_std_20)
set_target_properties(faf_android PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(faf_android PRIVATE src)
# GalPlay.cpp includes the engine-free headers of port/graphics (DiligentHost.h, ShaderCompileDiligent.h,
# the trace format) by their repository paths.
target_include_directories(faf_android PRIVATE "${FAF_REPO_ROOT}")
target_include_directories(faf_android SYSTEM PRIVATE "${NATIVE_APP_GLUE_DIR}")
target_compile_definitions(faf_android PRIVATE FAF_ANDROID_VERSION_NAME="${FAF_ANDROID_VERSION_NAME}")

# Warnings for the runtime's own C++ (the NDK's glue is C and not ours).
target_compile_options(faf_android PRIVATE
  "$<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic;-Wshadow;-Wconversion;-Wno-sign-conversion>"
)

target_link_libraries(faf_android PRIVATE
  faf::port_core
  faf_galplay_engine
  galtrace_format
  Diligent-GraphicsEngineVk-static
  Diligent-GraphicsEngineOpenGL-static
  Diligent-Common
  Diligent-PlatformInterface
  android
  log
  EGL
  GLESv3
)

# The dynamic symbol table holds ANativeActivity_onCreate and nothing else:
# - the version script makes every other symbol local,
# - --exclude-libs,ALL keeps symbols of the static archives (Diligent, the
#   port core, libc++) out of it even where they are declared with default
#   visibility (Diligent's GetEngineFactory* entry points are),
# - --gc-sections drops what nothing references (the NDK passes it for
#   release builds too; repeated here because it is part of the contract),
# - 16 KB segment alignment so the library loads on 16 KB page devices
#   (Android 15+), and --as-needed so only libraries that are really used
#   end up in DT_NEEDED. libvulkan.so is not one of them: Diligent loads it
#   with dlopen (volk), so devices without Vulkan still load the library and
#   fall back to OpenGL ES.
set(FAF_ANDROID_VERSION_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/faf_android.map")
file(WRITE "${FAF_ANDROID_VERSION_SCRIPT}.in"
  "/* Generated by port/android/CMakeLists.txt. */\n"
  "{\n  global:\n    ANativeActivity_onCreate;\n  local:\n    *;\n};\n")
configure_file("${FAF_ANDROID_VERSION_SCRIPT}.in" "${FAF_ANDROID_VERSION_SCRIPT}" COPYONLY)
target_link_options(faf_android PRIVATE
  "LINKER:--version-script=${FAF_ANDROID_VERSION_SCRIPT}"
  "LINKER:--exclude-libs,ALL"
  "LINKER:--gc-sections"
  "LINKER:--as-needed"
  "LINKER:-z,max-page-size=16384"
)
set_property(TARGET faf_android APPEND PROPERTY LINK_DEPENDS "${FAF_ANDROID_VERSION_SCRIPT}")

# --- galplay_cli (M7a1): the menu replay without a window, for adb ---------------------------
# Not built by default and never packaged: `cmake --build <dir> --target galplay_cli`. Runs what the
# activity's mode "menu-replay" runs (GalPlay.cpp, galplay_run with GalPlayOptions::headless), and
# writes the readback frames as the frame harness's BMPs.
add_executable(galplay_cli EXCLUDE_FROM_ALL
  src/GalPlayCli.cpp
  src/GalPlay.cpp
  src/GalPlay.h
  src/Log.cpp
  src/Log.h
)
target_compile_features(galplay_cli PRIVATE cxx_std_20)
set_target_properties(galplay_cli PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(galplay_cli PRIVATE src "${FAF_REPO_ROOT}")
target_compile_options(galplay_cli PRIVATE
  "$<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic;-Wshadow;-Wconversion;-Wno-sign-conversion>"
)
target_link_libraries(galplay_cli PRIVATE
  faf::port_core
  faf_galplay_engine
  galtrace_format
  Diligent-GraphicsEngineVk-static
  Diligent-Common
  Diligent-PlatformInterface
  android
  log
)
target_link_options(galplay_cli PRIVATE "LINKER:--gc-sections" "LINKER:-z,max-page-size=16384")
