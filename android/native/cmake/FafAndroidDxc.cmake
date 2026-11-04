# DXC, the effects' shader compiler (README.md, "The shader compiler"): libdxcompiler.so, built for
# the build's ABI from the sources of the release the Windows build downloads
# (cmake/FafDependencies.cmake: v1.9.2609, dxc_2026_09_29.zip). Microsoft publishes no Android
# build of it.
#
# At configure time the release's commit and the three submodules its build needs are downloaded,
# pinned by SHA256, into dependencies/.downloads (the engine's download cache) and put together in
# FAF_ANDROID_DXC_SOURCE. Two builds of them run as external projects:
#
#   faf_dxc_tablegen  llvm-tblgen and clang-tblgen for the build host. DXC's build runs them, so a
#                     cross build takes them from a native build, as DXC's own ARM64 builds do
#                     (utils/hct/hctbuild.cmd: LLVM_TABLEGEN, CLANG_TABLEGEN). On a Windows host
#                     they are built with CMake's default generator, Visual Studio's, which finds
#                     its compiler without a developer prompt; elsewhere with this build's.
#   faf_dxc           libdxcompiler.so for the target, with this build's toolchain file (the NDK,
#                     the API level, the C++ library), into ${CMAKE_BINARY_DIR}/bin, where the app
#                     packages it.
#
# Both follow DXC's build for Linux (cmake/caches/PredefinedParams.cmake) without its tests, with
# the release's version (utils/version/version.inc, HLSL_ENABLE_FIXED_VER) and its generated
# sources as released (HLSL_DISABLE_SOURCE_GENERATION). The library exports what dxcompiler.dll
# exports (DXCompiler.def: DxcCreateInstance, DxcCreateInstance2): the static libraries' symbols,
# the C++ library's among them, stay local, as in this build's own libraries. Its debug information
# is line tables, which symbolize a crash.
#
# The client's files that call DXC compile against its headers in FAF_ANDROID_DXC_SOURCE
# (faf_android_dxc, ../CMakeLists.txt). Its licences go to ${CMAKE_BINARY_DIR}/licenses, from where
# the app puts them into its assets.

include(ExternalProject)

set(_faf_dxc_root "${CMAKE_BINARY_DIR}/dxc")
set(FAF_ANDROID_DXC_SOURCE "${_faf_dxc_root}/source")

# <folder in the source tree>|<GitHub repository>|<commit>|<SHA256 of GitHub's archive of it>:
# the release's commit (tag v1.9.2609) and its submodules at that commit, but googletest (tests).
set(_faf_dxc_archives
  ".|microsoft/DirectXShaderCompiler|01b62ad47db7dee3dd33f90e7339cf9e860b3f93|c48c223df13ba425fd9beda9843754defbcd379a3811a6a0e590110be57168c7"
  "external/DirectX-Headers|microsoft/DirectX-Headers|980971e835876dc0cde415e8f9bc646e64667bf7|b5a4b6d8806ff7f29f19879f83d015dbe8740676d4ca0b48647a789cc7773c4e"
  "external/SPIRV-Headers|KhronosGroup/SPIRV-Headers|496543121ce6419f23d6fa5d7194ba66c36212d2|a9bb9c48713245eacf97cc539b6f1d45405a92d8813f8b82e635f0a085ee9898"
  "external/SPIRV-Tools|KhronosGroup/SPIRV-Tools|ef96ed763b43b59b33b31b362f09a02b729fa1c9|82c62146083fd558735a3171cf97cfc47903ca7d368482e87f94bd44883c0f00")

# The licences the APK carries: <file in the source tree>|<name in the assets>.
set(_faf_dxc_licenses
  "LICENSE.TXT|DirectXShaderCompiler.txt"
  "ThirdPartyNotices.txt|DirectXShaderCompiler-ThirdPartyNotices.txt"
  "external/DirectX-Headers/LICENSE|DirectX-Headers.txt"
  "external/SPIRV-Headers/LICENSE|SPIRV-Headers.txt"
  "external/SPIRV-Tools/LICENSE|SPIRV-Tools.txt")

# --- The sources -------------------------------------------------------------------------------
# A stamp in the tree records the pins it was made from; another pin makes the tree again.
function(_faf_dxc_source)
  string(SHA256 _pins "${_faf_dxc_archives}")
  set(_stamp "${FAF_ANDROID_DXC_SOURCE}/.faf-download")
  if(EXISTS "${_stamp}")
    file(READ "${_stamp}" _have)
    if(_have STREQUAL "${_pins}\n")
      return()
    endif()
  endif()

  set(_downloads "${FAF_REPO}/dependencies/.downloads")
  set(_scratch "${_faf_dxc_root}/extract")
  file(REMOVE_RECURSE "${FAF_ANDROID_DXC_SOURCE}")
  file(MAKE_DIRECTORY "${_downloads}" "${_faf_dxc_root}")
  foreach(_entry IN LISTS _faf_dxc_archives)
    string(REPLACE "|" ";" _entry "${_entry}")
    list(GET _entry 0 _place)
    list(GET _entry 1 _repository)
    list(GET _entry 2 _commit)
    list(GET _entry 3 _sha256)
    get_filename_component(_name "${_repository}" NAME)
    set(_archive "${_downloads}/${_name}-${_commit}.tar.gz")
    set(_have "")
    if(EXISTS "${_archive}")
      file(SHA256 "${_archive}" _have)
    endif()
    if(NOT _have STREQUAL _sha256)
      set(_url "https://github.com/${_repository}/archive/${_commit}.tar.gz")
      message(STATUS "android/native: downloading ${_url}")
      file(DOWNLOAD "${_url}" "${_archive}" EXPECTED_HASH SHA256=${_sha256} STATUS _status TLS_VERIFY ON)
      list(GET _status 0 _code)
      if(NOT _code EQUAL 0)
        message(FATAL_ERROR "android/native: download failed: ${_url}: ${_status}")
      endif()
    endif()
    file(REMOVE_RECURSE "${_scratch}")
    file(ARCHIVE_EXTRACT INPUT "${_archive}" DESTINATION "${_scratch}")
    if(_place STREQUAL ".")
      set(_target "${FAF_ANDROID_DXC_SOURCE}")
    else()
      # DXC's archive has the submodule's folder, empty.
      set(_target "${FAF_ANDROID_DXC_SOURCE}/${_place}")
      file(REMOVE_RECURSE "${_target}")
    endif()
    file(RENAME "${_scratch}/${_name}-${_commit}" "${_target}")
  endforeach()
  file(REMOVE_RECURSE "${_scratch}")
  file(WRITE "${_stamp}" "${_pins}\n")
endfunction()
_faf_dxc_source()

foreach(_entry IN LISTS _faf_dxc_licenses)
  string(REPLACE "|" ";" _entry "${_entry}")
  list(GET _entry 0 _from)
  list(GET _entry 1 _to)
  configure_file("${FAF_ANDROID_DXC_SOURCE}/${_from}" "${CMAKE_BINARY_DIR}/licenses/${_to}" COPYONLY)
endforeach()

# --- The builds --------------------------------------------------------------------------------
set(_faf_dxc_options
  -C "${FAF_ANDROID_DXC_SOURCE}/cmake/caches/PredefinedParams.cmake"
  -DHLSL_INCLUDE_TESTS=OFF
  -DSPIRV_BUILD_TESTS=OFF
  -DLLVM_INCLUDE_TESTS=OFF
  -DCLANG_INCLUDE_TESTS=OFF
  # No revision from version control (the tree has none, and the repository around the build
  # folder is not DXC's): the release's version, its generated sources.
  -DLLVM_APPEND_VC_REV=OFF
  -DHLSL_ENABLE_FIXED_VER=ON
  -DHLSL_DISABLE_SOURCE_GENERATION=ON
  -DHLSL_AUTOCRLF=OFF
  # What DXC's generators write stays unformatted: a host's clang-format would read this repository's
  # .clang-format from above the build folder, and an older one refuses it and stops the build.
  -DCLANG_FORMAT_EXE=
  # A newer compiler's warnings in SPIRV-Tools do not stop the build.
  -DSPIRV_WERROR=OFF)

set(_faf_dxc_tablegen "${_faf_dxc_root}/tablegen")
set(_faf_dxc_tablegen_build "${_faf_dxc_root}/tablegen-build")
if(CMAKE_HOST_WIN32)
  set(_faf_dxc_host_generator "")
else()
  set(_faf_dxc_host_generator -G "${CMAKE_GENERATOR}" -DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM})
endif()
ExternalProject_Add(faf_dxc_tablegen
  SOURCE_DIR "${FAF_ANDROID_DXC_SOURCE}"
  BINARY_DIR "${_faf_dxc_tablegen_build}"
  STAMP_DIR "${_faf_dxc_root}/stamp"
  TMP_DIR "${_faf_dxc_root}/tmp"
  DOWNLOAD_COMMAND ""
  UPDATE_COMMAND ""
  # A plain configure for the host: no CMAKE_GENERATOR from the environment, which would override
  # the default generator, and none of this build's settings.
  CONFIGURE_COMMAND "${CMAKE_COMMAND}" -E env --unset=CMAKE_GENERATOR --unset=CMAKE_GENERATOR_PLATFORM
                    "${CMAKE_COMMAND}" ${_faf_dxc_host_generator} -S <SOURCE_DIR> -B <BINARY_DIR>
                    -DCMAKE_BUILD_TYPE=Release ${_faf_dxc_options} -DENABLE_SPIRV_CODEGEN=OFF
  BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --config Release --parallel --target llvm-tblgen clang-tblgen
  INSTALL_COMMAND "${CMAKE_COMMAND}" -DFROM=<BINARY_DIR> -DTO=${_faf_dxc_tablegen}
                  -DSUFFIX=${CMAKE_HOST_EXECUTABLE_SUFFIX} -P "${FAF_ANDROID_DIR}/cmake/FafAndroidDxcTablegen.cmake"
  USES_TERMINAL_CONFIGURE TRUE
  USES_TERMINAL_BUILD TRUE)

set(_faf_dxc_tablegen_args
  # An existing native build: DXC's cross build does not configure one of its own (CrossCompile.cmake).
  -DLLVM_NATIVE_BUILD=${_faf_dxc_tablegen_build}
  -DLLVM_TABLEGEN=${_faf_dxc_tablegen}/llvm-tblgen${CMAKE_HOST_EXECUTABLE_SUFFIX}
  -DCLANG_TABLEGEN=${_faf_dxc_tablegen}/clang-tblgen${CMAKE_HOST_EXECUTABLE_SUFFIX})
set(_faf_dxc_toolchain_args -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE} -DFAF_ANDROID_NDK=${FAF_ANDROID_NDK})
if(FAF_ANDROID_ALLOW_UNPINNED_NDK)
  list(APPEND _faf_dxc_toolchain_args -DFAF_ANDROID_ALLOW_UNPINNED_NDK=ON)
endif()
ExternalProject_Add(faf_dxc
  SOURCE_DIR "${FAF_ANDROID_DXC_SOURCE}"
  BINARY_DIR "${_faf_dxc_root}/build"
  STAMP_DIR "${_faf_dxc_root}/stamp"
  TMP_DIR "${_faf_dxc_root}/tmp"
  DOWNLOAD_COMMAND ""
  UPDATE_COMMAND ""
  CMAKE_ARGS
    ${_faf_dxc_options}
    ${_faf_dxc_toolchain_args}
    ${_faf_dxc_tablegen_args}
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}
    # After the toolchain's -g: line tables, not full debug information.
    -DCMAKE_C_FLAGS=-gline-tables-only
    -DCMAKE_CXX_FLAGS=-gline-tables-only
    -DCMAKE_SHARED_LINKER_FLAGS=-Wl,--exclude-libs,ALL
  BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --target dxcompiler
  INSTALL_COMMAND "${CMAKE_COMMAND}" -E copy_if_different <BINARY_DIR>/lib/libdxcompiler.so "${CMAKE_BINARY_DIR}/bin/libdxcompiler.so"
  BUILD_BYPRODUCTS "${CMAKE_BINARY_DIR}/bin/libdxcompiler.so"
  DEPENDS faf_dxc_tablegen
  USES_TERMINAL_CONFIGURE TRUE
  USES_TERMINAL_BUILD TRUE)
