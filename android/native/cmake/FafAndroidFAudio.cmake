# FAudio, under the XACT engine (src/audio/XactEngineAndroid.cpp) and X3DAudio
# (src/audio/AudioAndroid.cpp): release 26.11, a tag of FAudio's repository, pinned by SHA256 and
# cached in dependencies/.downloads (the engine's download cache), built from source as a static
# library of this build. The game uses FACT, its reimplementation of XACT 3, F3DAudio and the mixer
# under them. The library leaves out FAudio's SDL and Win32 platform files, which
# src/audio/FAudioPlatformAndroid.c replaces, and XNA_Song.c, a player of files the game has none of.
#
# FAudio.patch mends what the game's banks run into in FACT, and one crash (tests/audio has them all):
# - A category's volume does not come down its hierarchy. FACTAudioEngine_SetVolume gave each child
#   its own volume once more, whatever the parent's: the sound options' "World" reached no unit or
#   weapon, each change made those quieter for good, and "Global" (SND_Mute) muted nothing. It also
#   left out the authored volumes until the first call.
# - A cue that fails its instance limit gives back an instance it never counted, as does one stopped
#   or destroyed while prepared. Past the limit, a cue went on playing and its count wrapped below
#   zero, after which it failed about 250 times. 1805 of the game's 3171 cues have such a limit.
# - Playing a cue index out of range uses a pointer that nothing set, where preparing it says so.
#
# ../CMakeLists.txt includes this. It gives the target `faudio` and puts FAudio's licence in
# ${CMAKE_BINARY_DIR}/licenses, from where the app puts it into its assets.

set(_faf_faudio_tag "26.11")
set(_faf_faudio_sha256 "8fcd197678ad1ab5c95f0901c3770d8d76c1d8b1da819f6873db599807ad7d03")
set(_faf_faudio_patch "${CMAKE_CURRENT_LIST_DIR}/FAudio.patch")
set(_faf_faudio_root "${CMAKE_BINARY_DIR}/faudio")
set(FAF_FAUDIO_SOURCE "${_faf_faudio_root}/source")

# --- The sources -------------------------------------------------------------------------------
# A stamp in the tree records the pin and the patch it was made from; another pin or patch makes the
# tree again.
function(_faf_faudio_source)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_faf_faudio_patch}")
  file(SHA256 "${_faf_faudio_patch}" _patch_sha256)

  set(_stamp "${FAF_FAUDIO_SOURCE}/.faf-download")
  set(_pin "${_faf_faudio_tag} ${_faf_faudio_sha256} ${_patch_sha256}\n")
  if(EXISTS "${_stamp}")
    file(READ "${_stamp}" _have)
    if(_have STREQUAL _pin)
      return()
    endif()
  endif()

  set(_downloads "${FAF_REPO}/dependencies/.downloads")
  set(_archive "${_downloads}/FAudio-${_faf_faudio_tag}.tar.gz")
  set(_have "")
  if(EXISTS "${_archive}")
    file(SHA256 "${_archive}" _have)
  endif()
  if(NOT _have STREQUAL _faf_faudio_sha256)
    set(_url "https://github.com/FNA-XNA/FAudio/archive/refs/tags/${_faf_faudio_tag}.tar.gz")
    message(STATUS "android/native: downloading ${_url}")
    file(MAKE_DIRECTORY "${_downloads}")
    file(DOWNLOAD "${_url}" "${_archive}" EXPECTED_HASH SHA256=${_faf_faudio_sha256} STATUS _status TLS_VERIFY ON)
    list(GET _status 0 _code)
    if(NOT _code EQUAL 0)
      message(FATAL_ERROR "android/native: download failed: ${_url}: ${_status}")
    endif()
  endif()

  set(_scratch "${_faf_faudio_root}/extract")
  file(REMOVE_RECURSE "${_scratch}" "${FAF_FAUDIO_SOURCE}")
  file(MAKE_DIRECTORY "${_faf_faudio_root}")
  file(ARCHIVE_EXTRACT INPUT "${_archive}" DESTINATION "${_scratch}")
  file(RENAME "${_scratch}/FAudio-${_faf_faudio_tag}" "${FAF_FAUDIO_SOURCE}")
  file(REMOVE_RECURSE "${_scratch}")

  # As cmake/FafAndroidOverlay.cmake applies a patch: GIT_CEILING_DIRECTORIES keeps git from taking
  # the repository around the build folder for the target of the patch.
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "GIT_CEILING_DIRECTORIES=${CMAKE_BINARY_DIR}"
            "${GIT_EXECUTABLE}" -c core.autocrlf=false apply --unsafe-paths --whitespace=nowarn "${_faf_faudio_patch}"
    WORKING_DIRECTORY "${FAF_FAUDIO_SOURCE}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _output)
  if(NOT _result EQUAL 0)
    message(FATAL_ERROR "android/native: cmake/FAudio.patch does not apply to FAudio ${_faf_faudio_tag}:\n${_output}")
  endif()
  file(WRITE "${_stamp}" "${_pin}")
endfunction()
_faf_faudio_source()

# --- The library -------------------------------------------------------------------------------
set(_faf_faudio_sources "")
foreach(_file F3DAudio FACT3D FACT FACT_internal FAPOBase FAPOFX FAPOFX_echo FAPOFX_eq FAPOFX_masteringlimiter
              FAPOFX_reverb FAudio FAudioFX_reverb FAudioFX_collector FAudioFX_volumemeter FAudio_internal
              FAudio_internal_simd FAudio_operationset)
  list(APPEND _faf_faudio_sources "${FAF_FAUDIO_SOURCE}/src/${_file}.c")
endforeach()

# Out of the default build: what links it builds it.
add_library(faudio STATIC EXCLUDE_FROM_ALL ${_faf_faudio_sources} "${FAF_ANDROID_DIR}/src/audio/FAudioPlatformAndroid.c")
target_include_directories(faudio SYSTEM PUBLIC "${FAF_FAUDIO_SOURCE}/include")
target_include_directories(faudio PRIVATE "${FAF_FAUDIO_SOURCE}/src" "${FAF_ANDROID_DIR}/include/faudio")
# FAudio_internal.h has the C library's functions on its Win32 side and SDL's on the other:
# FAUDIO_WIN32_PLATFORM picks the first, include/faudio has the one Windows function that side names.
# The weak symbols are for the AAudio function of a later Android than the app's minimum.
target_compile_definitions(faudio PRIVATE
  FAUDIO_WIN32_PLATFORM FAUDIO_DISABLE_DEBUGCONFIGURATION _GNU_SOURCE __ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__)
target_link_libraries(faudio PUBLIC aaudio log)

configure_file("${FAF_FAUDIO_SOURCE}/LICENSE" "${CMAKE_BINARY_DIR}/licenses/FAudio.txt" COPYONLY)
