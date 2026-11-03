# FreeType, the rasteriser under the GDI font functions (src/win32/GdiFonts.cpp, README.md "The
# fonts"): release 2.14.3, the tag VER-2-14-3 of FreeType's GitHub mirror, pinned by SHA256 and
# cached in dependencies/.downloads (the engine's download cache), built from source as a static
# library of this build: FreeType's own CMake project, as a subproject. The game's fonts are
# TrueType files, so FreeType needs none of its optional libraries: no zlib, bzip2, libpng,
# HarfBuzz or Brotli.
#
# ../CMakeLists.txt includes this, and so does the Windows compare tool (tests/gdifont); both set
# FAF_REPO first. It gives the target `freetype` and puts FreeType's licence, the FreeType License
# (the one of its two licences this build takes) with the overview of the licences of its parts, in
# ${CMAKE_BINARY_DIR}/licenses, from where the app puts them into its assets.

set(_faf_freetype_tag "VER-2-14-3")
set(_faf_freetype_sha256 "dc49de6b01a266eef4876a4dd34d9842c475d3e28ff2eff63bd2fb760ab56261")
set(_faf_freetype_root "${CMAKE_BINARY_DIR}/freetype")
set(FAF_FREETYPE_SOURCE "${_faf_freetype_root}/source")

# --- The sources -------------------------------------------------------------------------------
# A stamp in the tree records the pin it was made from; another pin makes the tree again.
function(_faf_freetype_source)
  set(_stamp "${FAF_FREETYPE_SOURCE}/.faf-download")
  if(EXISTS "${_stamp}")
    file(READ "${_stamp}" _have)
    if(_have STREQUAL "${_faf_freetype_tag} ${_faf_freetype_sha256}\n")
      return()
    endif()
  endif()

  set(_downloads "${FAF_REPO}/dependencies/.downloads")
  set(_archive "${_downloads}/freetype-${_faf_freetype_tag}.tar.gz")
  set(_have "")
  if(EXISTS "${_archive}")
    file(SHA256 "${_archive}" _have)
  endif()
  if(NOT _have STREQUAL _faf_freetype_sha256)
    set(_url "https://github.com/freetype/freetype/archive/refs/tags/${_faf_freetype_tag}.tar.gz")
    message(STATUS "android/native: downloading ${_url}")
    file(MAKE_DIRECTORY "${_downloads}")
    file(DOWNLOAD "${_url}" "${_archive}" EXPECTED_HASH SHA256=${_faf_freetype_sha256} STATUS _status TLS_VERIFY ON)
    list(GET _status 0 _code)
    if(NOT _code EQUAL 0)
      message(FATAL_ERROR "android/native: download failed: ${_url}: ${_status}")
    endif()
  endif()

  set(_scratch "${_faf_freetype_root}/extract")
  file(REMOVE_RECURSE "${_scratch}" "${FAF_FREETYPE_SOURCE}")
  file(MAKE_DIRECTORY "${_faf_freetype_root}")
  file(ARCHIVE_EXTRACT INPUT "${_archive}" DESTINATION "${_scratch}")
  file(RENAME "${_scratch}/freetype-${_faf_freetype_tag}" "${FAF_FREETYPE_SOURCE}")
  file(REMOVE_RECURSE "${_scratch}")
  file(WRITE "${_stamp}" "${_faf_freetype_tag} ${_faf_freetype_sha256}\n")
endfunction()
_faf_freetype_source()

# --- The library -------------------------------------------------------------------------------
# FreeType's options are plain variables here, for its directory alone (its options honour them,
# CMP0077). Out of the default build: what links it builds it.
block(SCOPE_FOR VARIABLES)
  set(BUILD_SHARED_LIBS OFF)
  foreach(_dependency ZLIB BZIP2 PNG HARFBUZZ BROTLI)
    set(FT_DISABLE_${_dependency} ON)
  endforeach()
  set(SKIP_INSTALL_ALL ON)
  add_subdirectory("${FAF_FREETYPE_SOURCE}" "${_faf_freetype_root}/build" EXCLUDE_FROM_ALL SYSTEM)
endblock()

configure_file("${FAF_FREETYPE_SOURCE}/docs/FTL.TXT" "${CMAKE_BINARY_DIR}/licenses/FreeType.txt" COPYONLY)
configure_file("${FAF_FREETYPE_SOURCE}/LICENSE.TXT" "${CMAKE_BINARY_DIR}/licenses/FreeType-LICENSE.txt" COPYONLY)
