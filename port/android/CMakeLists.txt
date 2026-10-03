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
  src/GameRuntime.cpp
  src/GameRuntime.h
  src/Log.cpp
  src/Log.h
  src/Renderer.cpp
  src/Renderer.h
  src/RunStatus.cpp
  src/RunStatus.h
  "${NATIVE_APP_GLUE_DIR}/android_native_app_glue.c"
)
target_compile_features(faf_android PRIVATE cxx_std_20)
set_target_properties(faf_android PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(faf_android PRIVATE src)
target_include_directories(faf_android SYSTEM PRIVATE "${NATIVE_APP_GLUE_DIR}")
target_compile_definitions(faf_android PRIVATE FAF_ANDROID_VERSION_NAME="${FAF_ANDROID_VERSION_NAME}")

# Warnings for the runtime's own C++ (the NDK's glue is C and not ours).
target_compile_options(faf_android PRIVATE
  "$<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic;-Wshadow;-Wconversion;-Wno-sign-conversion>"
)

target_link_libraries(faf_android PRIVATE
  faf::port_core
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
