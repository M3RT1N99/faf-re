# The Android build (README.md): the engine as it builds for Android, and the game client on top,
# without changing a file of the engine.
#
#   cmake --preset android-arm64 && cmake --build --preset android-arm64       (CMakePresets.json)
#
# The engine comes in as the repository's root project does for an Android toolchain
# (cmake/FafLinux.cmake: the headless simulation, the sim-math tests). On top:
#
#   include/      headers ahead of the engine's: the Win32 names the client needs beyond the
#                 engine's POSIX layer (win32/), DirectX type stand-ins (directx/), stand-ins for
#                 the Windows-only backends' headers that shared code includes (shadow/)
#   src/          the Android implementations: Win32 additions, the Vulkan surface, the cursor,
#                 the effects' preprocessor and shader compiler, the texture codec stand-in, the
#                 renderer test (smoke/)
#   patches/      the engine files compiled changed (cmake/FafAndroidOverlay.cmake)
#
# Targets, with everything the app packages in ${CMAKE_BINARY_DIR}/bin:
#
#   headless, faf_sim_math_tests   the engine's programs (cmake/FafLinux.cmake)
#   faf_game_sim                   the simulation's objects, compiled with the client's headers
#   faf_android_platform           src/win32 and src/gal: what the Vulkan backend stands on here
#   faf_android_dxc                src/gal/DxcAndroid.cpp, the one file on DXC's own headers
#   faf_dxc                        libdxcompiler.so, DXC for the effects (cmake/FafAndroidDxc.cmake)
#   freetype                       FreeType, under the GDI font functions (cmake/FafAndroidFreetype.cmake)
#   faudio                         FAudio, under the XACT engine and X3DAudio (cmake/FafAndroidFAudio.cmake)
#   faf_audio                      src/audio: the XACT engine on FACT, X3DAudio on F3DAudio
#   faf_gal                        the renderer (gpg/gal): the Vulkan backend with what it shares
#   faf_client                     the rest of the client
#   fafgame                        libfafgame.so, all of it
#   fafgalsmoke                    libfafgalsmoke.so, the renderer test (src/smoke)

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
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
  set(FAF_TARGET_ARCH arm64)
else()
  set(FAF_TARGET_ARCH x64)
endif()

# The files compiled changed (patches/), and those that include a patched header by its bare name
# from the header's folder.
set(FAF_ANDROID_OVERLAY_SIBLINGS src/moho/render/ID3DTextureSheet.cpp)
include("${FAF_ANDROID_DIR}/cmake/FafAndroidOverlay.cmake")

# DXC, the effects' shader compiler (cmake/FafAndroidDxc.cmake): libdxcompiler.so for the ABI in
# ${CMAKE_BINARY_DIR}/bin, its licences in ${CMAKE_BINARY_DIR}/licenses, both for the app.
include("${FAF_ANDROID_DIR}/cmake/FafAndroidDxc.cmake")

# FreeType, under the GDI font functions (cmake/FafAndroidFreetype.cmake): a static library of this
# build, its licences in ${CMAKE_BINARY_DIR}/licenses for the app.
include("${FAF_ANDROID_DIR}/cmake/FafAndroidFreetype.cmake")

# FAudio, under the XACT engine and X3DAudio (cmake/FafAndroidFAudio.cmake): a static library of this
# build, its licence in ${CMAKE_BINARY_DIR}/licenses for the app.
include("${FAF_ANDROID_DIR}/cmake/FafAndroidFAudio.cmake")

# src/gal/ShaderCompilerAndroid.cpp gives DXC the arguments of the engine's
# gpg/gal/effects/ShaderCompiler.cpp, which this build leaves out. When upstream changes that file,
# the configure stops here, so that the Android half follows it (and this hash with it) instead of
# compiling the effects differently. The hash is of the file with LF line ends.
file(READ "${FAF_REPO}/src/gpg/gal/effects/ShaderCompiler.cpp" _faf_shader_compiler)
string(REPLACE "\r\n" "\n" _faf_shader_compiler "${_faf_shader_compiler}")
string(SHA256 _faf_shader_compiler_hash "${_faf_shader_compiler}")
if(NOT _faf_shader_compiler_hash STREQUAL "fa8fedad15b0a1eb40acc48b0123cba2577829f4cd88bb1ca5bb454a0d30ed39")
  message(FATAL_ERROR "android/native: src/gpg/gal/effects/ShaderCompiler.cpp changed (SHA256 "
    "${_faf_shader_compiler_hash}): bring src/gal/ShaderCompilerAndroid.cpp in line with it, then renew "
    "the hash in CMakeLists.txt.")
endif()

# --- The client's files --------------------------------------------------------------------
# Every translation unit of the engine that the simulation build leaves out (src/sources.cmake,
# src/sim_sources.cmake before cmake/FafLinux.cmake's POSIX filters), but the Windows-only ones.
include("${FAF_REPO}/src/sources.cmake")
include("${FAF_REPO}/src/sim_sources.cmake")
set(_faf_client_sources "")
foreach(_src IN LISTS FAF_ENGINE_SOURCES)
  if(NOT _src IN_LIST FAF_SIM_SOURCES)
    list(APPEND _faf_client_sources "${_src}")
  endif()
endforeach()

set(_faf_client_windows_only
  # The Direct3D 9 to 12 backends and DXGI. The Vulkan backend takes the Direct3D 11 backend's
  # vertex layouts and effect source translator and DXGI's format tables (added back below).
  "^gpg/gal/backends/(d3d9|d3d10|d3d11|d3d12|dxgi)/"
  # Effects 11, the Direct3D 11 backend's effect framework.
  "/FX11/"
  # gal's matrix math on D3DX (MatrixPortable.cpp, a simulation file, has the same functions),
  # the Direct3D 9 backend's state caches, DXGI's adapter list.
  "^gpg/gal/(Matrix|StateCache|DxgiAdapterInfo)\\.cpp$"
  # The Vulkan backend's Win32 surface, the backends' Win32 cursor, D3DX and DXC: their Android
  # halves are in src/gal.
  "^gpg/gal/backends/vulkan/VulkanSurface\\.cpp$"
  "^gpg/gal/backends/common/(WindowsCursor|ImageCodec)\\.cpp$"
  "^gpg/gal/effects/ShaderCompiler\\.cpp$"
  # The crash dialog (CrashReport.cpp's hook stays unset, as in the simulation build).
  "^gpg/core/diagnostics/CrashReportDialog\\.cpp$"
  # The frame sampler: it suspends the main thread and reads its context, which Android has no
  # call for. src/diag has its frame mark.
  "^gpg/core/diagnostics/FrameSampler\\.cpp$"
  # The XACT engine DLL's clock imports, patched in its PE image: src/audio/AudioAndroid.cpp has
  # the one function, for a DLL that does not exist here.
  "^moho/audio/XactClock\\.cpp$"
  # The Windows program's entry and its process setup (CRT hooks, the allocation log, crash tests,
  # accessibility keys, Media Center): on Android the host starts the engine.
  "^moho/app/WinMain\\.cpp$"
  # The in-engine peer links on netlink (libjuice, libdatachannel), which is not built for Android
  # yet (C5): src/net has them off.
  "^moho/net/NetlinkPeers\\.cpp$"
  # The movie decoder on FFmpeg, which is not built for Android yet: src/movie has the decoder that
  # finds no FFmpeg.
  "^moho/movie/sfd/FfmpegSfdDecoder\\.cpp$"
  # The wx script debugger's windows and the wx tool windows (emitter editor, reflection editor,
  # directory dialogs): wx-lite leaves them out (WX_LITE.md). The console commands that open the
  # editors say so in the log (src/app, patches/src/moho/console).
  "^moho/misc/(ScrDebugWindow|ScrFileCtrl|ScrSourceCtrl|ScrWatchCtrl|ScrGotoDialog|ScrPauseEvent|TreeData)\\.cpp$"
  "^moho/app/(WEmitterWx|WRefEditDialog|FrameDumpCommands|SnapshotDirectoryDialog)\\.cpp$")
foreach(_pattern IN LISTS _faf_client_windows_only)
  list(FILTER _faf_client_sources EXCLUDE REGEX "${_pattern}")
endforeach()
list(APPEND _faf_client_sources
  gpg/gal/backends/d3d11/D3D11VertexLayouts.cpp
  gpg/gal/backends/d3d11/EffectSourceD3D11.cpp
  gpg/gal/backends/dxgi/DxgiFormats.cpp)

# The renderer apart from the rest: the renderer test takes it without the client.
# MeshVertex.cpp holds the mesh batches' vertex formatters, which live in the Direct3D 9 and 10
# backends' files: it goes with the client.
set(_faf_gal_files "")
set(_faf_client_files "")
foreach(_src IN LISTS _faf_client_sources)
  if(IS_ABSOLUTE "${_src}")
    set(_file "${_src}")
  else()
    set(_file "${FAF_REPO}/src/${_src}")
  endif()
  if(_src MATCHES "^gpg/gal/" AND NOT _src MATCHES "^gpg/gal/MeshVertex\\.cpp$")
    list(APPEND _faf_gal_files "${_file}")
  else()
    list(APPEND _faf_client_files "${_file}")
  endif()
endforeach()
faf_android_overlay_sources(_faf_gal_files ${_faf_gal_files})
faf_android_overlay_sources(_faf_client_files ${_faf_client_files})
list(LENGTH _faf_gal_files _faf_gal_count)
list(LENGTH _faf_client_files _faf_client_count)
message(STATUS "android/native: faf_gal ${_faf_gal_count} translation units, faf_client ${_faf_client_count}")

# --- Settings ---------------------------------------------------------------------------------
# The engine's (faf_sim_settings: its definitions, warnings, the POSIX Win32 layer behind these),
# with this build's headers ahead of the engine's: the patched files first, then the stand-ins.
add_library(faf_android_settings INTERFACE)
target_include_directories(faf_android_settings BEFORE INTERFACE
  "${FAF_ANDROID_OVERLAY}/src"
  "${FAF_ANDROID_DIR}/include/shadow"
  "${FAF_ANDROID_DIR}/include/win32"
  "${FAF_ANDROID_DIR}/include/directx")
# Hidden symbols: a library exports its JNI entry points (JNIEXPORT) alone, and the linker can drop
# what nothing reaches, as it does in the simulation's programs.
target_compile_options(faf_android_settings INTERFACE -fvisibility=hidden -fvisibility-inlines-hidden)
# Wild Magic is headers only (cmake/deps/wildmagic): its queries are engine code.
target_link_libraries(faf_android_settings INTERFACE faf_sim_settings Wm3Headers zlib123)

# The libraries export only their JNI entry points: the static libraries' symbols stay local, and
# what nothing reaches is dropped, as the linker drops it from the simulation's programs. The
# Windows import libraries the sources name (#pragma comment(lib, "psapi.lib")) do not exist here.
set(_faf_android_library_options
  -Wl,--error-limit=0 -Wl,--exclude-libs,ALL -Wl,--gc-sections -Wl,--no-dependent-libraries)

# --- Objects -----------------------------------------------------------------------------------
# The simulation's objects compiled once more with this build's headers: a few of its headers
# read them (gpg/gal/D3D9Utils.h, moho/render/ID3DTextureSheet.h), so every object of a library
# sees the same RECT. ClientTypeInfoPosix.cpp stands in for the client classes' type_info in the
# simulation's programs; here the classes are real.
get_target_property(_faf_game_sim_sources faf_sim SOURCES)
list(FILTER _faf_game_sim_sources EXCLUDE REGEX "/platform/posix/ClientTypeInfoPosix\\.cpp$")
# The settings the engine gives single files (boost.thread's sources: cmake/FafLinux.cmake). CMake
# keeps source properties per directory, so they are read from the engine's and set here too.
foreach(_file IN LISTS _faf_game_sim_sources)
  foreach(_property COMPILE_DEFINITIONS COMPILE_OPTIONS)
    get_source_file_property(_value "${_file}" TARGET_DIRECTORY faf_sim ${_property})
    if(_value)
      set_property(SOURCE "${_file}" APPEND PROPERTY ${_property} ${_value})
    endif()
  endforeach()
endforeach()
faf_android_overlay_sources(_faf_game_sim_sources ${_faf_game_sim_sources})
add_library(faf_game_sim OBJECT ${_faf_game_sim_sources})
target_link_libraries(faf_game_sim PUBLIC faf_android_settings)
get_target_property(_faf_sim_libraries faf_sim LINK_LIBRARIES)
target_link_libraries(faf_game_sim PUBLIC ${_faf_sim_libraries})
# The engine's build info (cmake/FafBuildInfo.cmake), which its faf_sim set up: the same header.
# faf_build_info() itself reads variables of the engine's directory scope, so it is not called.
add_dependencies(faf_game_sim faf_build_info)
target_include_directories(faf_game_sim PRIVATE "${CMAKE_BINARY_DIR}/generated")

add_library(faf_android_platform OBJECT
  src/win32/Win32Android.cpp
  src/win32/User32Android.cpp
  src/win32/ExceptionsAndroid.cpp
  src/win32/DbgHelpAndroid.cpp
  src/win32/FindFileAndroid.cpp
  src/win32/GdiAndroid.cpp
  src/win32/GdiFonts.cpp
  src/win32/PsapiAndroid.cpp
  src/gal/D3DCompilerAndroid.cpp
  src/gal/FxcPreprocessor.cpp
  src/gal/GalInterfacesAndroid.cpp
  src/gal/BlockCompressionAndroid.cpp
  src/gal/ImageCodecAndroid.cpp
  src/gal/ImageFilesAndroid.cpp
  src/gal/ShaderCompilerAndroid.cpp
  src/gal/VulkanSurfaceAndroid.cpp
  src/gal/WindowsCursorAndroid.cpp)
target_link_libraries(faf_android_platform PUBLIC faf_android_settings)
# FreeType's headers, for GdiFonts.cpp. Whoever takes these objects links the library itself.
target_link_libraries(faf_android_platform PRIVATE freetype)

# DxcAndroid.cpp calls DXC through DXC's own headers, which declare the Windows types themselves
# (dxc/WinAdapter.h). It is compiled without this build's settings, whose Win32 stand-ins and the
# engine's forced POSIX header would declare them again, and sees nothing of the engine.
add_library(faf_android_dxc OBJECT src/gal/DxcAndroid.cpp)
target_include_directories(faf_android_dxc PRIVATE "${FAF_ANDROID_DXC_SOURCE}/include")
target_compile_options(faf_android_dxc PRIVATE -fvisibility=hidden -fvisibility-inlines-hidden)

add_library(faf_gal OBJECT ${_faf_gal_files})
target_link_libraries(faf_gal PUBLIC faf_android_settings)
# Device.cpp includes the patched DeviceVulkan.h by its path; the header finds the neighbours it
# includes by their bare names here.
target_include_directories(faf_gal PRIVATE "${FAF_REPO}/src/gpg/gal/backends/vulkan")

# wx-lite (WX_LITE.md): the part of wxWindows 2.4.2 the client uses, on the message layer.
add_library(faf_wx_lite OBJECT
  src/wx/app.cpp
  src/wx/common.cpp
  src/wx/controls.cpp
  src/wx/event.cpp
  src/wx/gdi.cpp
  src/wx/sizer.cpp
  src/wx/toplevel.cpp
  src/wx/url.cpp
  src/wx/window.cpp)
target_include_directories(faf_wx_lite PUBLIC "${FAF_ANDROID_DIR}/include")
target_link_libraries(faf_wx_lite PUBLIC faf_android_settings)

add_library(faf_client OBJECT ${_faf_client_files}
  src/app/EntityDialogAndroid.cpp
  src/diag/FrameSamplerAndroid.cpp
  src/movie/FfmpegSfdDecoderAndroid.cpp
  src/gal/D3D9Unavailable.cpp
  src/gal/D3D12SwitchesAndroid.cpp
  src/gal/D3DXMathAndroid.cpp
  src/gal/GalAvailabilityAndroid.cpp
  src/gal/MeshFormattersAndroid.cpp
  src/net/DatagramSocketAndroid.cpp
  src/net/NetlinkPeersAndroid.cpp)
target_link_libraries(faf_client PUBLIC faf_android_settings faf_wx_lite)
# As the Windows build gives CLobby.cpp _HAS_AUTO_PTR_ETC (src/CMakeLists.txt): boost's string
# algorithms there use std::unary_function, which libc++ keeps behind this opt-in from C++17 on.
set_property(SOURCE "${FAF_REPO}/src/moho/net/CLobby.cpp" APPEND PROPERTY COMPILE_DEFINITIONS
  _LIBCPP_ENABLE_CXX17_REMOVED_UNARY_BINARY_FUNCTION=1)

# The XACT engine and X3DAudio apart from the rest: the audio test takes them without the client.
add_library(faf_audio OBJECT src/audio/AudioAndroid.cpp src/audio/XactEngineAndroid.cpp)
target_link_libraries(faf_audio PUBLIC faf_android_settings)
# FAudio's headers. Whoever takes these objects links the library itself.
target_link_libraries(faf_audio PRIVATE faudio)

# The host (WX_LITE.md): the program's start, and the activity's surface, input and lifecycle as
# Win32 messages.
add_library(faf_host OBJECT src/host/GameHost.cpp src/host/KeysAndroid.cpp)
target_link_libraries(faf_host PUBLIC faf_android_settings faf_wx_lite)

# --- Libraries ---------------------------------------------------------------------------------
# libfafgame.so: the activity's native half, the whole engine and its host.
add_library(fafgame SHARED
  $<TARGET_OBJECTS:faf_game_sim> $<TARGET_OBJECTS:faf_android_platform> $<TARGET_OBJECTS:faf_android_dxc>
  $<TARGET_OBJECTS:faf_gal> $<TARGET_OBJECTS:faf_wx_lite> $<TARGET_OBJECTS:faf_client> $<TARGET_OBJECTS:faf_audio>
  $<TARGET_OBJECTS:faf_host>)
target_link_libraries(fafgame PRIVATE faf_game_sim freetype faudio android log ${CMAKE_DL_LIBS})
target_link_options(fafgame PRIVATE ${_faf_android_library_options})

# libfafgalsmoke.so: the renderer on the phone's Vulkan without the client (src/smoke). Without
# the client, the client classes' type_info the simulation names come from the stand-in, as in
# the simulation's programs.
add_library(fafgalsmoke SHARED
  $<TARGET_OBJECTS:faf_game_sim> $<TARGET_OBJECTS:faf_android_platform> $<TARGET_OBJECTS:faf_android_dxc>
  $<TARGET_OBJECTS:faf_gal>
  src/smoke/GalSmoke.cpp
  src/smoke/GalSmokeDoubles.cpp
  "${FAF_REPO}/src/platform/posix/ClientTypeInfoPosix.cpp")
target_link_libraries(fafgalsmoke PRIVATE faf_game_sim faf_android_settings freetype android log vulkan ${CMAKE_DL_LIBS})
target_link_options(fafgalsmoke PRIVATE ${_faf_android_library_options})

# --- Tests -------------------------------------------------------------------------------------
# faf_wx_lite_tests (WX_LITE.md, Checks): wx-lite's event system, the message layer, the
# translation of the layer's messages into wx events, and GDI's fonts on FreeType. Linked
# statically, it runs on the emulator and on Linux hosts alike (CI's arm64 runner), where liblog
# and libandroid do not exist: the test stands in for the three functions the layer calls of theirs
# (tests/wx/NdkDoubles.cpp). The simulation's objects come from an archive, so that only what the
# test reaches is linked (the engine's POSIX layer).
add_library(faf_game_sim_archive STATIC EXCLUDE_FROM_ALL $<TARGET_OBJECTS:faf_game_sim>)
add_executable(faf_wx_lite_tests
  tests/wx/Main.cpp
  tests/wx/EventTests.cpp
  tests/wx/GdiTests.cpp
  tests/wx/MessageLayerTests.cpp
  tests/wx/WindowTests.cpp
  tests/wx/NdkDoubles.cpp
  $<TARGET_OBJECTS:faf_wx_lite>
  $<TARGET_OBJECTS:faf_android_platform>
  $<TARGET_OBJECTS:faf_android_dxc>)
target_include_directories(faf_wx_lite_tests PRIVATE "${FAF_ANDROID_DIR}/include")
target_link_libraries(faf_wx_lite_tests PRIVATE faf_android_settings faf_game_sim_archive freetype ${_faf_sim_libraries} ${CMAKE_DL_LIBS})
target_link_options(faf_wx_lite_tests PRIVATE -static -Wl,--gc-sections -Wl,--no-dependent-libraries)

# faf_audio_tests (tests/audio): the game's XACT engine with the game's banks, the mix heard through
# the platform's observer, on a device: it needs Android's libaaudio, so it is not static. Like the
# game, it takes the engine's POSIX layer from the simulation's objects.
add_executable(faf_audio_tests tests/audio/AudioTests.cpp $<TARGET_OBJECTS:faf_audio>)
target_link_libraries(faf_audio_tests PRIVATE faf_android_settings faudio faf_game_sim_archive ${_faf_sim_libraries} ${CMAKE_DL_LIBS})
target_link_options(faf_audio_tests PRIVATE -Wl,--gc-sections -Wl,--no-dependent-libraries)
