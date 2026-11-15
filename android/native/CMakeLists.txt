# The Android build (README.md): the engine as it builds for Android, and the game client on top,
# without changing a file of the engine.
#
#   cmake --preset android-arm64 && cmake --build --preset android-arm64       (CMakePresets.json)
#
# The engine comes in as the repository's root project does for an Android toolchain
# (cmake/FafLinux.cmake: the headless simulation, the sim-math tests). The client is the engine's
# own POSIX client on SDL3, as cmake/FafLinuxClient.cmake builds it for Linux: the same files,
# dependencies and pins, built with the NDK. What differs for Android is here:
#
#   src/          what Android needs beyond the engine's POSIX client: the app's entry (src/app),
#                 the Vulkan surface's coming and going and BC textures on the CPU for GPUs without
#                 them (src/gal), the in-engine peer links off (src/net)
#   patches/      the engine's files compiled changed, and SDL's (cmake/FafAndroidOverlay.cmake)
#   cmake/        DXC built from source (FafAndroidDxc.cmake); ffmpeg/: FFmpeg built with the NDK.
#                 The Linux client takes Microsoft's Linux DXC and runs FFmpeg's configure and make,
#                 neither of which an Android cross build can use.
#
# Targets, with everything the app packages in ${CMAKE_BINARY_DIR}/bin:
#
#   headless, faf_sim_math_tests   the engine's programs (cmake/FafLinux.cmake)
#   faf_dxc                        libdxcompiler.so, DXC for the effects (cmake/FafAndroidDxc.cmake)
#   fafgame                        libfafgame.so: the engine's client and simulation, SDL3 among them,
#                                  which the app's activity (an SDLActivity) loads and starts
#   faf_movie_tests                the engine's movie player on real movies (tests/movie)
#   faf_griff_tests                the touch scheme Griff's rules (tests/griff)

cmake_minimum_required(VERSION 3.25)
# ASM: the engine's x86_64 build assembles the x87 helpers (cmake/FafLinux.cmake), and CMake wants
# a language its subprojects use enabled here as well.
project(faf_android LANGUAGES C CXX ASM)

if(NOT FAF_ANDROID)
  message(FATAL_ERROR "android/native needs an Android toolchain: use a preset of android/native/CMakePresets.json.")
endif()

get_filename_component(FAF_REPO "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(FAF_ANDROID_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

# What the app packages, in one folder (android/gradle.properties, fafNativeDir).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_subdirectory("${FAF_REPO}" engine)

# The engine's language settings (cmake/FafLinux.cmake sets them for its own directory), read from
# its simulation target so they follow it.
foreach(_property CXX_STANDARD CXX_STANDARD_REQUIRED CXX_EXTENSIONS C_STANDARD)
  get_target_property(_value faf_sim ${_property})
  set(CMAKE_${_property} ${_value})
endforeach()
# The engine's settings for its whole folder (the root CMakeLists.txt's FAF_RETAIL_LAYOUT, which decides
# the legacy containers' layout; cmake/FafLinux.cmake's profile and sanitizer options): this build's
# objects link with the engine's, so they are compiled as those are.
foreach(_property COMPILE_DEFINITIONS COMPILE_OPTIONS)
  get_directory_property(_value DIRECTORY "${FAF_REPO}" ${_property})
  if(_value)
    set_property(DIRECTORY APPEND PROPERTY ${_property} ${_value})
  endif()
endforeach()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# DXC, the effects' shader compiler (cmake/FafAndroidDxc.cmake): libdxcompiler.so for the ABI in
# ${CMAKE_BINARY_DIR}/bin, its licences in ${CMAKE_BINARY_DIR}/licenses, both for the app.
include("${FAF_ANDROID_DIR}/cmake/FafAndroidDxc.cmake")

# FFmpeg, under the movie player (ffmpeg/CMakeLists.txt): static libraries of this build, its
# licence in ${CMAKE_BINARY_DIR}/licenses for the app.
add_subdirectory(ffmpeg "${CMAKE_BINARY_DIR}/ffmpeg" EXCLUDE_FROM_ALL SYSTEM)

# --- The client's dependencies ---------------------------------------------------------------
# Those of cmake/FafLinuxClient.cmake, at its pins (cmake/FafDependencies.cmake), but DXC's Linux
# release (x86_64 only; cmake/FafAndroidDxc.cmake builds DXC) and the in-engine peer links' own
# (src/net).
set(FAF_DEPENDENCIES_ONLY DirectX-Headers SDL3 FAudio DirectXTex DirectXMath stb freetype msdfgen imgui)
include("${FAF_REPO}/cmake/FafDependencies.cmake")

# The files compiled changed (patches/), the engine's and its dependencies', which are here now, and
# those that include a patched header by its bare name from the header's folder.
set(FAF_ANDROID_OVERLAY_SIBLINGS src/gpg/gal/backends/vulkan/RayTracingVulkan.cpp)
include("${FAF_ANDROID_DIR}/cmake/FafAndroidOverlay.cmake")

# SDL3, static, as the Linux client builds it. On Android it is the window (the activity's surface),
# the input, the audio output and the program's start: its activity (org.libsdl.app.SDLActivity, whose
# Java sources the app takes from the same release) calls the library's entry on a thread of its own.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${FAF_DEPENDENCIES_DIR}/SDL3" "${CMAKE_BINARY_DIR}/deps/SDL3" EXCLUDE_FROM_ALL SYSTEM)
# Its patched files (patches/dependencies/SDL3, patches/README.md).
faf_android_overlay_target(SDL3-static)

# Dear ImGui with its SDL3 backends, for the developer windows (src/platform/sdl/ToolWindow.cpp).
add_subdirectory("${FAF_REPO}/cmake/deps/imgui" "${CMAKE_BINARY_DIR}/deps/imgui")

# FAudio, static, over that SDL3 (its CMakeLists takes SDL3_INCLUDE_DIRS and SDL3_LIBRARIES): XACT
# (FACT) and X3DAudio (F3DAudio) under platform/posix/XactOverFact.cpp.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SDL3_INCLUDE_DIRS "${FAF_DEPENDENCIES_DIR}/SDL3/include")
set(SDL3_LIBRARIES SDL3-static)
add_subdirectory("${FAF_DEPENDENCIES_DIR}/FAudio" "${CMAKE_BINARY_DIR}/deps/FAudio" EXCLUDE_FROM_ALL SYSTEM)
target_compile_options(FAudio-static PRIVATE -w)

# MSDF text and CD3DFont's GDI calls (platform/posix/GdiOverFreeType.cpp): FreeType and msdfgen,
# by their recipes.
add_subdirectory("${FAF_REPO}/cmake/deps/freetype" "${CMAKE_BINARY_DIR}/deps/freetype")
add_subdirectory("${FAF_REPO}/cmake/deps/msdfgen" "${CMAKE_BINARY_DIR}/deps/msdfgen")

# Lowercase spellings of POSIX stand-in headers the client includes, as cmake/FafLinuxClient.cmake
# writes them, next to the engine's <windows.h> (cmake/FafLinux.cmake).
file(WRITE "${CMAKE_BINARY_DIR}/win32-alias/shlobj.h" "#pragma once\n#include <ShlObj.h>\n")

# The licences of what this build links from others' sources, for the app's assets: the client's
# libraries, and those the simulation brings (the net channel's Monocypher, Zstandard for saves).
foreach(_licence
    "SDL3/LICENSE.txt|SDL3.txt" "FAudio/LICENSE|FAudio.txt" "freetype/LICENSE.TXT|FreeType.txt"
    "freetype/docs/FTL.TXT|FreeType-FTL.txt" "msdfgen/LICENSE.txt|msdfgen.txt" "DirectXTex/LICENSE|DirectXTex.txt"
    "DirectXMath/LICENSE|DirectXMath.txt" "DirectX-Headers/LICENSE|DirectX-Headers.txt" "stb/LICENSE|stb.txt"
    "imgui/LICENSE.txt|DearImGui.txt" "monocypher/LICENCE.md|Monocypher.txt" "zstd/LICENSE|Zstandard.txt")
  string(REPLACE "|" ";" _licence "${_licence}")
  list(GET _licence 0 _from)
  list(GET _licence 1 _to)
  configure_file("${FAF_DEPENDENCIES_DIR}/${_from}" "${CMAKE_BINARY_DIR}/licenses/${_to}" COPYONLY)
endforeach()

# --- The client's files ----------------------------------------------------------------------------
# cmake/FafLinuxClient.cmake's choice: the engine's sources that the simulation leaves out
# (src/sim_sources.cmake), minus the Windows-only ones, plus the portable files in the Windows
# backends' folders that the Vulkan backend shares.
include("${FAF_REPO}/src/sources.cmake")
include("${FAF_REPO}/src/sim_sources.cmake")
set(_faf_client_windows_only
  "^gpg/gal/backends/(d3d9|d3d10|d3d11|d3d12|dxgi)/"
  "/FX11/"
  "^gpg/gal/(DxgiAdapterInfo|D3DErrorToWideString|StateCache)\\.cpp$"
  "^moho/movie/sfd/DirectSoundMovieSink\\.cpp$"
  "^gpg/core/diagnostics/CrashReportDialog\\.cpp$"
  "^platform/windows/"
  "^gpg/gal/MatrixPortable\\.cpp$"
  "^moho/app/WinMain\\.cpp$"
  # Built apart, with DirectX-Headers' Win32 adapter (faf_client_imaging below).
  "^gpg/gal/backends/common/ImageCodecDirectXTex\\.cpp$"
  # Android's own beyond the Linux client's: the Vulkan backend's surface, whose window can lose it
  # while the program runs (src/gal/VulkanSurfaceAndroid.cpp), and the in-engine peer links, whose
  # libraries this build does not make yet (src/net/NetlinkPeersAndroid.cpp).
  "^gpg/gal/backends/vulkan/VulkanSurface\\.cpp$"
  "^moho/net/NetlinkPeers\\.cpp$"
  # The movie player, whose decoder is on FFmpeg's headers: faf_movie_player has it.
  "^moho/movie/sfd/(IndependentSfdBackend|SfdMetadata|FfmpegSfdDecoder)\\.cpp$")
set(_faf_client_shared_backend_files
  "^gpg/gal/backends/dxgi/DxgiFormats\\.cpp$"
  "^gpg/gal/backends/d3d11/(D3D11VertexLayouts|EffectSourceD3D11)\\.cpp$"
  "^gpg/gal/backends/d3d9/D3D9BaseClasses\\.cpp$"
  "^gpg/gal/backends/d3d10/(Float16)?HardwareVertexFormatterD3D10\\.cpp$")
set(_faf_client_files "")
foreach(_src IN LISTS FAF_ENGINE_SOURCES)
  set(_client FALSE)
  foreach(_pattern IN LISTS _faf_client_only_patterns)
    if(_src MATCHES "${_pattern}")
      set(_client TRUE)
      break()
    endif()
  endforeach()
  if(_client)
    foreach(_pattern IN LISTS _faf_sim_patterns _faf_client_windows_only)
      if(_src MATCHES "${_pattern}")
        set(_client FALSE)
        break()
      endif()
    endforeach()
    foreach(_pattern IN LISTS _faf_client_shared_backend_files)
      if(_src MATCHES "${_pattern}")
        set(_client TRUE)
        break()
      endif()
    endforeach()
  endif()
  if(_client)
    if(IS_ABSOLUTE "${_src}")
      list(APPEND _faf_client_files "${_src}")
    else()
      list(APPEND _faf_client_files "${FAF_REPO}/src/${_src}")
    endif()
  endif()
endforeach()
# The POSIX client's platform files (cmake/FafLinuxClient.cmake): the Win32 window calls, XACT on
# FACT, the POSIX half of the SDL3 window layer, movie sound on SDL3, CD3DFont's GDI calls on FreeType.
foreach(_file Win32WindowPosix XactOverFact WindowSdlPosix SdlMovieSink GdiOverFreeType)
  list(APPEND _faf_client_files "${FAF_REPO}/src/platform/posix/${_file}.cpp")
endforeach()
# The program's entry, in WinMain's place: SDL_main on Android (patches/README.md).
list(APPEND _faf_client_files "${FAF_REPO}/src/platform/posix/ClientMainPosix.cpp")
faf_android_overlay_sources(_faf_client_files ${_faf_client_files})
list(LENGTH _faf_client_files _faf_client_count)
message(STATUS "android/native: faf_client ${_faf_client_count} translation units")

# --- Objects -----------------------------------------------------------------------------------
# The simulation is the engine's own faf_sim, but for its files that patches/ changes: those are
# compiled here from the overlay with faf_sim's settings, and faf_sim's objects of them are left out.
get_target_property(_faf_sim_sources faf_sim SOURCES)
set(_faf_sim_patched "")
set(_faf_sim_patched_objects "")
foreach(_file IN LISTS _faf_sim_sources)
  file(RELATIVE_PATH _relative "${FAF_REPO}" "${_file}")
  if(_relative IN_LIST _faf_android_overlay_paths)
    list(APPEND _faf_sim_patched "${_file}")
    # Its object, as $<TARGET_OBJECTS:faf_sim> names it: .../faf_sim.dir/./src/<path>.o
    string(REGEX REPLACE "([.+])" "\\\\\\1" _escaped "${_relative}")
    list(APPEND _faf_sim_patched_objects "/faf_sim\\.dir/(\\./)?${_escaped}\\.o$")
    # The settings the engine gives single files, which CMake keeps per directory.
    foreach(_property COMPILE_DEFINITIONS COMPILE_OPTIONS)
      get_source_file_property(_value "${_file}" TARGET_DIRECTORY faf_sim ${_property})
      if(_value)
        set_property(SOURCE "${_file}" APPEND PROPERTY ${_property} ${_value})
      endif()
    endforeach()
  endif()
endforeach()
get_target_property(_faf_sim_libraries faf_sim LINK_LIBRARIES)
list(LENGTH _faf_sim_patched _faf_sim_patched_count)
message(STATUS "android/native: faf_sim with ${_faf_sim_patched_count} of its files patched")
if(_faf_sim_patched_count EQUAL 0)
  set(FAF_ANDROID_SIM_OBJECTS "$<TARGET_OBJECTS:faf_sim>")
else()
  faf_android_overlay_sources(_faf_sim_patched ${_faf_sim_patched})
  add_library(faf_sim_patched OBJECT ${_faf_sim_patched})
  foreach(_property INCLUDE_DIRECTORIES COMPILE_DEFINITIONS COMPILE_OPTIONS)
    get_target_property(_value faf_sim ${_property})
    if(_value)
      set_property(TARGET faf_sim_patched APPEND PROPERTY ${_property} ${_value})
    endif()
  endforeach()
  target_link_libraries(faf_sim_patched PUBLIC ${_faf_sim_libraries})
  target_include_directories(faf_sim_patched BEFORE PRIVATE "${FAF_ANDROID_OVERLAY}/src")
  add_dependencies(faf_sim_patched faf_build_info)
  list(JOIN _faf_sim_patched_objects "|" _faf_sim_patched_regex)
  set(FAF_ANDROID_SIM_OBJECTS
    "$<FILTER:$<TARGET_OBJECTS:faf_sim>,EXCLUDE,${_faf_sim_patched_regex}>" "$<TARGET_OBJECTS:faf_sim_patched>")
endif()

# The client's settings: the simulation's (faf_sim_settings: its definitions, warnings, the POSIX
# Win32 layer), the patched headers first, Vulkan's headers and DirectX-Headers' format numbers
# (<dxgiformat.h>), as cmake/FafLinuxClient.cmake gives them.
add_library(faf_android_settings INTERFACE)
target_include_directories(faf_android_settings BEFORE INTERFACE "${FAF_ANDROID_OVERLAY}/src")
target_include_directories(faf_android_settings INTERFACE
  "${FAF_DEPENDENCIES_DIR}/vulkan/include"
  "${FAF_DEPENDENCIES_DIR}/DirectX-Headers/include/directx")
target_link_libraries(faf_android_settings INTERFACE faf_sim_settings ${_faf_sim_libraries})
# Hidden symbols: the library exports its entries alone (exports.map), and the linker can drop what
# nothing reaches, as it does in the simulation's programs.
target_compile_options(faf_android_settings INTERFACE -fvisibility=hidden -fvisibility-inlines-hidden)

# DXC's headers bring their own Win32 types, which clash with the POSIX <Windows.h> of the client's
# files, so the one file that includes them is built on its own, as cmake/FafLinuxClient.cmake
# builds it, against the headers of the DXC this build makes.
add_library(faf_client_dxc OBJECT "${FAF_REPO}/src/platform/posix/dxc/DxcGlue.cpp")
target_include_directories(faf_client_dxc PRIVATE "${FAF_REPO}/src" "${FAF_ANDROID_DXC_SOURCE}/include")
target_compile_options(faf_client_dxc PRIVATE -pthread -Wall "SHELL:-include platform/PlatformDefines.h"
  -Wundef-prefix=FAFRE_PLATFORM_ -Werror=undef-prefix -fvisibility=hidden -fvisibility-inlines-hidden)

# Textures: DirectXTex's CPU code on DirectXMath and DirectX-Headers, and the engine's side of it with
# stb for PNG, BMP and JPG, built apart as cmake/FafLinuxClient.cmake builds them.
set(_faf_dxtex "${FAF_DEPENDENCIES_DIR}/DirectXTex/DirectXTex")
set(_faf_dx_headers "${FAF_DEPENDENCIES_DIR}/DirectX-Headers/include")
add_library(faf_directxtex_android STATIC
  "${_faf_dxtex}/BC.cpp"
  "${_faf_dxtex}/BC4BC5.cpp"
  "${_faf_dxtex}/BC6HBC7.cpp"
  "${_faf_dxtex}/DirectXTexCompress.cpp"
  "${_faf_dxtex}/DirectXTexConvert.cpp"
  "${_faf_dxtex}/DirectXTexDDS.cpp"
  "${_faf_dxtex}/DirectXTexHDR.cpp"
  "${_faf_dxtex}/DirectXTexImage.cpp"
  "${_faf_dxtex}/DirectXTexMipmaps.cpp"
  "${_faf_dxtex}/DirectXTexMisc.cpp"
  "${_faf_dxtex}/DirectXTexNormalMaps.cpp"
  "${_faf_dxtex}/DirectXTexPMAlpha.cpp"
  "${_faf_dxtex}/DirectXTexResize.cpp"
  "${_faf_dxtex}/DirectXTexTGA.cpp"
  "${_faf_dxtex}/DirectXTexUtil.cpp")
target_include_directories(faf_directxtex_android SYSTEM PUBLIC
  "${FAF_REPO}/src/platform/posix/sal"
  "${_faf_dxtex}"
  "${FAF_DEPENDENCIES_DIR}/DirectXMath/Inc"
  "${_faf_dx_headers}"
  "${_faf_dx_headers}/directx"
  "${_faf_dx_headers}/wsl/stubs")
# include/DirectXTexAndroid.h: aligned_alloc below Android 9.
target_compile_options(faf_directxtex_android PRIVATE -w -pthread
  "SHELL:-include ${FAF_REPO}/src/platform/PlatformDefines.h" "SHELL:-include ${FAF_ANDROID_DIR}/include/DirectXTexAndroid.h"
  -fvisibility=hidden -fvisibility-inlines-hidden)

add_library(faf_client_imaging OBJECT
  "${FAF_REPO}/src/gpg/gal/backends/common/ImageCodecDirectXTex.cpp"
  "${FAF_REPO}/src/platform/posix/imaging/StbImage.cpp")
target_include_directories(faf_client_imaging PRIVATE
  "${FAF_REPO}/src/platform/posix/winadapter"
  "${FAF_REPO}/src"
  "${FAF_DEPENDENCIES_DIR}/stb")
target_link_libraries(faf_client_imaging PUBLIC faf_directxtex_android)
target_compile_options(faf_client_imaging PRIVATE -pthread -Wall "SHELL:-include platform/PlatformDefines.h"
  -Wundef-prefix=FAFRE_PLATFORM_ -Werror=undef-prefix -fvisibility=hidden -fvisibility-inlines-hidden)

# The movie player apart from the rest: FFmpeg's headers are for its decoder, and the movie test takes
# it without the client.
set(_faf_movie_player_files "")
foreach(_file IndependentSfdBackend SfdMetadata FfmpegSfdDecoder)
  list(APPEND _faf_movie_player_files "${FAF_REPO}/src/moho/movie/sfd/${_file}.cpp")
endforeach()
faf_android_overlay_sources(_faf_movie_player_files ${_faf_movie_player_files})
add_library(faf_movie_player OBJECT ${_faf_movie_player_files})
target_link_libraries(faf_movie_player PUBLIC faf_android_settings)
# FFmpeg's headers. Whoever takes these objects links the libraries itself.
target_link_libraries(faf_movie_player PRIVATE FFmpeg::FFmpeg)

add_library(faf_client OBJECT ${_faf_client_files}
  src/app/GameMainAndroid.cpp
  src/app/ScreenKeyboardAndroid.cpp
  src/app/UiPackageAndroid.cpp
  src/gal/BlockCompressionAndroid.cpp
  src/gal/VulkanSurfaceAndroid.cpp
  src/net/NetlinkPeersAndroid.cpp)
target_link_libraries(faf_client PUBLIC faf_android_settings FAudio-static SDL3::SDL3-static faf_imgui_sdl faf_freetype
  faf_msdfgen)
# Device.cpp includes the patched DeviceVulkan.h by its path; the header finds the neighbours it
# includes by their bare names here. The patched UI file takes the choice of a finger's target from
# include/ (TapTarget.h).
target_include_directories(faf_client PRIVATE "${FAF_REPO}/src/gpg/gal/backends/vulkan" "${FAF_ANDROID_DIR}/include")
add_dependencies(faf_client faf_build_info)

# --- libfafgame.so ---------------------------------------------------------------------------------
# The whole engine: the simulation's objects first, as the Linux client links them (their static
# initializers run in link order, and the client's register factories for the sim's types).
add_library(fafgame SHARED ${FAF_ANDROID_SIM_OBJECTS}
  $<TARGET_OBJECTS:faf_client> $<TARGET_OBJECTS:faf_client_dxc> $<TARGET_OBJECTS:faf_client_imaging>
  $<TARGET_OBJECTS:faf_movie_player>)
target_link_libraries(fafgame PRIVATE faf_android_settings faf_directxtex_android FAudio-static SDL3::SDL3-static
  faf_imgui_sdl faf_freetype faf_msdfgen FFmpeg::FFmpeg android log ${CMAKE_DL_LIBS})
# The library exports what Java and SDL's activity look up in it (exports.map): the JNI entry points,
# SDL's main and the app's. The static libraries' symbols stay local, and what nothing reaches is
# dropped. The Windows import libraries the sources name (#pragma comment(lib, "psapi.lib")) do not
# exist here.
target_link_options(fafgame PRIVATE "-Wl,--version-script=${FAF_ANDROID_DIR}/exports.map" -Wl,--error-limit=0
  -Wl,--gc-sections -Wl,--no-dependent-libraries)
set_property(TARGET fafgame APPEND PROPERTY LINK_DEPENDS "${FAF_ANDROID_DIR}/exports.map")

# --- Tests -------------------------------------------------------------------------------------
# faf_movie_tests (tests/movie): the engine's movie player and decoder on real movies, on a device, for
# comparing the ABIs and for seeing what a movie costs. It plays no sound: the player's sink is SDL's
# (platform/posix/SdlMovieSink.cpp), which it never opens. What the player takes of the engine's POSIX
# layer (the movie's path, the threads' floating-point settings) comes from the simulation's objects
# through an archive, so that only what it reaches is linked.
add_library(faf_sim_archive STATIC EXCLUDE_FROM_ALL $<TARGET_OBJECTS:faf_sim>)
add_executable(faf_movie_tests tests/movie/MovieTests.cpp "${FAF_REPO}/src/platform/posix/SdlMovieSink.cpp"
  $<TARGET_OBJECTS:faf_movie_player>)
target_compile_features(faf_movie_tests PRIVATE cxx_std_20)
target_include_directories(faf_movie_tests PRIVATE "${FAF_REPO}/src")
target_link_libraries(faf_movie_tests PRIVATE faf_android_settings faf_sim_archive ${_faf_sim_libraries} FFmpeg::FFmpeg
  SDL3::SDL3-static)
target_link_options(faf_movie_tests PRIVATE -Wl,--gc-sections)

# faf_griff_tests (tests/griff): the rules of the touch scheme Griff (../TOUCH_UI.md), headers of their
# own (include/), and the choice of the small control a finger's tap meant. Linked statically, so it runs
# on the emulator and on Linux hosts alike (CI's arm64 runner).
add_executable(faf_griff_tests
  tests/griff/Main.cpp
  tests/griff/GriffDraftTests.cpp
  tests/griff/GriffPinsTests.cpp
  tests/griff/GriffPlaceTests.cpp
  tests/griff/GriffTapTests.cpp
  tests/griff/GriffUndoTests.cpp
  tests/griff/ListDragTests.cpp
  tests/griff/TapTargetTests.cpp)
target_include_directories(faf_griff_tests PRIVATE "${FAF_ANDROID_DIR}/include")
target_compile_features(faf_griff_tests PRIVATE cxx_std_20)
target_link_options(faf_griff_tests PRIVATE -static -Wl,--gc-sections)
