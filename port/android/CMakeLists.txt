cmake_minimum_required(VERSION 3.22)
project(faf_android LANGUAGES C CXX)

if(NOT ANDROID)
  message(FATAL_ERROR "Configure this project with the Android NDK toolchain")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(DILIGENT_BUILD_TESTS OFF CACHE BOOL "Disable upstream test targets" FORCE)
set(DILIGENT_USE_OPENXR OFF CACHE BOOL "OpenXR is not used by the Android bootstrap" FORCE)
set(DILIGENT_NO_HLSL OFF CACHE BOOL "Keep the Vulkan HLSL compiler for engine effects" FORCE)
set(DILIGENT_NO_GLSLANG OFF CACHE BOOL "Keep GLSL shader compilation available" FORCE)

set(FAF_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(DILIGENT_CORE_DIR "${FAF_REPO_ROOT}/dependencies/DiligentCore")
if(NOT EXISTS "${DILIGENT_CORE_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "DiligentCore missing. Run scripts/port/bootstrap_android.ps1 first.")
endif()

add_subdirectory("${DILIGENT_CORE_DIR}" "${CMAKE_BINARY_DIR}/DiligentCore")

set(NATIVE_APP_GLUE "${CMAKE_ANDROID_NDK}/sources/android/native_app_glue")
add_library(faf_android SHARED
  src/main.cpp
  "${NATIVE_APP_GLUE}/android_native_app_glue.c"
)
target_include_directories(faf_android PRIVATE "${NATIVE_APP_GLUE}")
target_link_libraries(faf_android PRIVATE
  Diligent-Common
  Diligent-GraphicsEngineVk-shared
  Diligent-PlatformInterface
  Diligent-TargetPlatform
  android
  log
)

set_target_properties(faf_android PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN YES
)
