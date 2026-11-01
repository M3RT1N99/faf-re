# The game client on Android (android/CLIENT_PLAN.md, C1): every engine translation unit the
# simulation build leaves out, but the Windows-only ones, with the simulation's own, as one
# shared library, libfafgame.so, which an Android activity loads and starts. Included by
# cmake/FafLinux.cmake with FAF_ANDROID_CLIENT=ON; the targets above stay as they are.
#
#   faf_game_sim   the simulation's objects, compiled with the DirectX stand-ins
#   faf_gal        the renderer (gpg/gal): the Vulkan backend with the parts it shares
#   faf_client     the rest of the client
#   fafgame        libfafgame.so: all three
#   fafgalsmoke    libfafgalsmoke.so: the renderer test (src/platform/android/GalSmoke.cpp)

if(NOT FAF_LINUX_PLATFORM STREQUAL "android")
  message(FATAL_ERROR "FAF_ANDROID_CLIENT needs an Android toolchain (the android-*-release presets)")
endif()

# The client's files: the engine's, without the simulation's (built below from _faf_sim_files).
set(_faf_client_sources "")
foreach(_src IN LISTS FAF_ENGINE_SOURCES)
  if(NOT _src IN_LIST _faf_sim_sources_unfiltered)
    list(APPEND _faf_client_sources "${_src}")
  endif()
endforeach()

# What Android has no use for, or has its own of:
set(_faf_client_windows_only
  # The Direct3D 9 to 12 backends and DXGI; the Vulkan backend takes the Direct3D 11 backend's
  # vertex layouts and effect source translator and DXGI's format tables (added back below).
  "^gpg/gal/backends/(d3d9|d3d10|d3d11|d3d12|dxgi)/"
  # Effects 11, the Direct3D 11 backend's effect framework.
  "/FX11/"
  # gal's matrix math on D3DX (MatrixPortable.cpp, in faf_game_sim as in the simulation), the
  # Direct3D 9 backend's state caches, DXGI's adapter list.
  "^gpg/gal/(Matrix|StateCache|DxgiAdapterInfo)\\.cpp$"
  # The Vulkan backend's Win32 surface, the backends' Win32 cursor, DXC and D3DX: their Android
  # halves below.
  "^gpg/gal/backends/vulkan/VulkanSurface\\.cpp$"
  "^gpg/gal/backends/common/(WindowsCursor|ImageCodec)\\.cpp$"
  "^gpg/gal/effects/ShaderCompiler\\.cpp$"
  # The crash dialog (CrashReport.cpp's hook stays unset, as in the simulation build).
  "^gpg/core/diagnostics/CrashReportDialog\\.cpp$"
  # The wx script debugger's windows and the wx tool windows (emitter editor, reflection
  # editor, directory dialogs, URLs).
  "^moho/misc/(ScrDebugWindow|ScrFileCtrl|ScrSourceCtrl|ScrWatchCtrl|ScrGotoDialog|ScrPauseEvent|TreeData)\\.cpp$"
  "^moho/app/(WEmitterWx|WRefEditDialog|FrameDumpCommands|SnapshotDirectoryDialog|WxUrl)\\.cpp$")
foreach(_pattern IN LISTS _faf_client_windows_only)
  list(FILTER _faf_client_sources EXCLUDE REGEX "${_pattern}")
endforeach()
list(APPEND _faf_client_sources
  gpg/gal/backends/d3d11/D3D11VertexLayouts.cpp
  gpg/gal/backends/d3d11/EffectSourceD3D11.cpp
  gpg/gal/backends/dxgi/DxgiFormats.cpp
  gpg/gal/backends/vulkan/VulkanSurfaceAndroid.cpp
  gpg/gal/backends/common/WindowsCursorPosix.cpp)

# The renderer apart from the rest: the test library takes it without the client.
set(_faf_gal_files "")
set(_faf_client_files "")
foreach(_src IN LISTS _faf_client_sources)
  if(IS_ABSOLUTE "${_src}")
    set(_file "${_src}")
  else()
    set(_file "${PROJECT_SOURCE_DIR}/src/${_src}")
  endif()
  # MeshVertex.cpp holds the mesh batches' vertex formatters, which live in the Direct3D 9 and
  # 10 backends' files: it goes with the client.
  if(_src MATCHES "^gpg/gal/" AND NOT _src MATCHES "^gpg/gal/MeshVertex\\.cpp$")
    list(APPEND _faf_gal_files "${_file}")
  else()
    list(APPEND _faf_client_files "${_file}")
  endif()
endforeach()
# DXC's and D3DX's Android halves differ between the game and the test (FAF_GAL_SMOKE_TEST), so
# each library compiles its own.
set(_faf_gal_stubs
  "${PROJECT_SOURCE_DIR}/src/gpg/gal/effects/ShaderCompilerPosix.cpp"
  "${PROJECT_SOURCE_DIR}/src/gpg/gal/backends/common/ImageCodecPosix.cpp")
list(APPEND _faf_client_files ${_faf_gal_stubs})
list(LENGTH _faf_gal_files _faf_gal_count)
list(LENGTH _faf_client_files _faf_client_count)
message(STATUS "faf_gal: ${_faf_gal_count} translation units; faf_client: ${_faf_client_count}")

set(_faf_directx_standins "${PROJECT_SOURCE_DIR}/src/platform/posix/directx")
# Hidden symbols: a library exports its JNI entry points (JNIEXPORT) alone, and the linker can drop
# what nothing reaches, as it does in the simulation's executables.
set(_faf_library_compile_options -fvisibility=hidden -fvisibility-inlines-hidden)

# The simulation's objects compiled once more with the stand-ins too: a few of its headers read
# them where they are on the include path (gpg/gal/D3D9Utils.h, moho/render/ID3DTextureSheet.h),
# so every object of the library sees the same RECT. ClientTypeInfoPosix.cpp stands in for the
# client classes' type_info in the simulation's programs; here the classes are real.
get_target_property(_faf_game_sim_sources faf_sim SOURCES)
list(FILTER _faf_game_sim_sources EXCLUDE REGEX "/platform/posix/ClientTypeInfoPosix\\.cpp$")
add_library(faf_game_sim OBJECT ${_faf_game_sim_sources})
target_link_libraries(faf_game_sim PUBLIC faf_sim_settings)
target_include_directories(faf_game_sim BEFORE PRIVATE "${_faf_directx_standins}")
target_compile_options(faf_game_sim PRIVATE ${_faf_library_compile_options})
get_target_property(_faf_sim_libraries faf_sim LINK_LIBRARIES)
target_link_libraries(faf_game_sim PUBLIC ${_faf_sim_libraries})
faf_build_info(faf_game_sim ${FAF_TARGET_ARCH}-${FAF_LINUX_PLATFORM})

add_library(faf_gal OBJECT ${_faf_gal_files})
target_link_libraries(faf_gal PUBLIC faf_sim_settings Wm3Foundation zlib123)
target_include_directories(faf_gal BEFORE PRIVATE "${_faf_directx_standins}")
target_compile_options(faf_gal PRIVATE ${_faf_library_compile_options})

# Out of the default build until every client file compiles and libfafgame.so links (C1: wx-lite,
# the XACT stand-in, the GDI font, the vertex formatters); name the targets to build them.
add_library(faf_client OBJECT EXCLUDE_FROM_ALL ${_faf_client_files})
target_link_libraries(faf_client PUBLIC faf_sim_settings Wm3Foundation zlib123)
target_include_directories(faf_client BEFORE PRIVATE "${_faf_directx_standins}")
target_compile_options(faf_client PRIVATE ${_faf_library_compile_options})

# The libraries export only their JNI entry points (JNIEXPORT): the static libraries' symbols stay
# local, and what nothing reaches is dropped, as the linker drops it from the simulation's
# programs (WildMagic's ContBox3 brings ContMinBox, whose ConvexHull3 the Linux build leaves out).
set(_faf_android_library_options -Wl,--error-limit=0 -Wl,--exclude-libs,ALL -Wl,--gc-sections)

# libfafgame.so: the activity's native half, the whole engine.
add_library(fafgame SHARED EXCLUDE_FROM_ALL $<TARGET_OBJECTS:faf_game_sim> $<TARGET_OBJECTS:faf_gal> $<TARGET_OBJECTS:faf_client>)
target_link_libraries(fafgame PRIVATE faf_game_sim android log)
target_link_options(fafgame PRIVATE ${_faf_android_library_options})

# libfafgalsmoke.so: the renderer on the phone's Vulkan without the client (GalSmoke.cpp).
add_library(faf_gal_smoke OBJECT ${_faf_gal_stubs} "${PROJECT_SOURCE_DIR}/src/platform/android/GalSmoke.cpp")
target_link_libraries(faf_gal_smoke PUBLIC faf_sim_settings Wm3Foundation zlib123)
target_include_directories(faf_gal_smoke BEFORE PRIVATE "${_faf_directx_standins}")
target_compile_options(faf_gal_smoke PRIVATE ${_faf_library_compile_options})
target_compile_definitions(faf_gal_smoke PRIVATE FAF_GAL_SMOKE_TEST=1)
# Without the client, the client classes' type_info the simulation names come from the stand-in,
# as in the simulation's programs.
add_library(fafgalsmoke SHARED $<TARGET_OBJECTS:faf_game_sim> $<TARGET_OBJECTS:faf_gal> $<TARGET_OBJECTS:faf_gal_smoke>
  "${PROJECT_SOURCE_DIR}/src/platform/posix/ClientTypeInfoPosix.cpp")
# The test lists the phone's Vulkan devices itself (libvulkan); the backend loads it on its own.
target_link_libraries(fafgalsmoke PRIVATE faf_game_sim android log vulkan)
target_link_options(fafgalsmoke PRIVATE ${_faf_android_library_options})
