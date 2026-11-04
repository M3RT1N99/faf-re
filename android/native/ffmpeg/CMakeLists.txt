# FFmpeg's avutil, swresample, swscale, avcodec and avformat as static libraries for the Android
# build, in the engine's configuration (dependencies/ffmpeg/faf/generate.sh), built with the NDK's
# clang from the submodule's sources. dependencies/ffmpeg/faf/CMakeLists.txt does the same for
# clang-cl and rejects anything else; the targets are named as there, so that what links them does
# not care which of the two it is under.
#
# The sources and flags come from <abi>/sources.cmake and the configuration headers from <abi>/, both
# written by generate.sh here. Nothing here changes FFmpeg's sources.
#
# Use: add_subdirectory(ffmpeg <binary dir>/ffmpeg EXCLUDE_FROM_ALL SYSTEM), then link FFmpeg::FFmpeg.
# A directory of its own, because it clears the configurations' C flags for itself. It puts FFmpeg's
# licence in ${CMAKE_BINARY_DIR}/licenses, from where the app puts it into its assets.

cmake_minimum_required(VERSION 3.25)
project(ffmpeg_faf_android LANGUAGES C ASM)

if(NOT ANDROID OR NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
  message(FATAL_ERROR "android/native/ffmpeg: <abi>/ was generated for the NDK's clang; configure with an Android "
                      "toolchain (android/native/CMakePresets.json).")
endif()

get_filename_component(FFMPEG_FAF_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../dependencies/ffmpeg" ABSOLUTE)
if(NOT EXISTS "${FFMPEG_FAF_SOURCE_DIR}/configure")
  message(FATAL_ERROR "android/native/ffmpeg: the submodule dependencies/ffmpeg is not checked out: "
                      "git submodule update --init dependencies/ffmpeg")
endif()

set(FFMPEG_FAF_CONFIG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/${ANDROID_ABI}")
if(NOT EXISTS "${FFMPEG_FAF_CONFIG_DIR}/sources.cmake")
  message(FATAL_ERROR "android/native/ffmpeg: no configuration for the ABI ${ANDROID_ABI}: add it to "
                      "android/native/ffmpeg/generate.sh and run sh android/native/ffmpeg/generate.sh ${ANDROID_ABI}")
endif()

# Upstream moves the submodule, and what is checked in here describes the old one: stop and say so, as
# android/native/CMakeLists.txt does for ShaderCompiler.cpp, instead of building one FFmpeg's sources
# with another's configuration. The hashes are of the files with LF line ends.
function(_ffmpeg_faf_check_stamp)
  file(STRINGS "${FFMPEG_FAF_SOURCE_DIR}/RELEASE" _release LIMIT_COUNT 1)
  set(_expected "release ${_release}\n")
  foreach(_file generate.sh gen_sources.py)
    file(READ "${FFMPEG_FAF_SOURCE_DIR}/faf/${_file}" _text)
    string(REPLACE "\r\n" "\n" _text "${_text}")
    string(SHA256 _hash "${_text}")
    string(APPEND _expected "faf/${_file} ${_hash}\n")
  endforeach()
  set(_have "(none)")
  if(EXISTS "${FFMPEG_FAF_CONFIG_DIR}/stamp.txt")
    file(READ "${FFMPEG_FAF_CONFIG_DIR}/stamp.txt" _have)
    string(REPLACE "\r\n" "\n" _have "${_have}")
  endif()
  if(NOT _have STREQUAL _expected)
    message(FATAL_ERROR "android/native/ffmpeg: ${ANDROID_ABI}/ was not made from this dependencies/ffmpeg; run "
                        "sh android/native/ffmpeg/generate.sh and take what it writes.\n"
                        "The folder's stamp:\n${_have}\nThe submodule:\n${_expected}")
  endif()
endfunction()
_ffmpeg_faf_check_stamp()

include("${FFMPEG_FAF_CONFIG_DIR}/sources.cmake")

# configure names these two whenever they exist, for MediaCodec, which this configuration has not; left
# in, the program would load libmediandk.so and use nothing of it.
list(REMOVE_ITEM FFMPEG_FAF_AVUTIL_SYSTEM_LIBS android mediandk)

# FFmpeg's own optimisation and flags in every configuration (FFMPEG_FAF_COMPILE_OPTIONS carries
# configure's -O3): FFmpeg relies on dead-code elimination for disabled components and does not link
# unoptimised. No NDEBUG either, as in FFmpeg's build. The C standard is FFmpeg's as well, not the
# engine's that a parent project sets (android/native/CMakeLists.txt).
foreach(_config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  set(CMAKE_C_FLAGS_${_config} "")
  set(CMAKE_ASM_FLAGS_${_config} "")
endforeach()
unset(CMAKE_C_STANDARD)
unset(CMAKE_C_EXTENSIONS)

# The public header FFmpeg's build generates (libavutil/avconfig.h), on its own so that users of
# FFmpeg::FFmpeg do not see config.h.
configure_file("${FFMPEG_FAF_CONFIG_DIR}/libavutil/avconfig.h"
               "${CMAKE_CURRENT_BINARY_DIR}/include/libavutil/avconfig.h" COPYONLY)

# Dependencies between the libraries (configure's <lib>_deps).
set(_ffmpeg_deps_avutil "")
set(_ffmpeg_deps_swresample avutil)
set(_ffmpeg_deps_swscale avutil)
set(_ffmpeg_deps_avcodec avutil)
set(_ffmpeg_deps_avformat avcodec avutil)

set(FFMPEG_FAF_LIBRARIES "")
foreach(_lib avutil swresample swscale avcodec avformat)
  string(TOUPPER "${_lib}" _LIB)
  set(_sources "${FFMPEG_FAF_${_LIB}_SOURCES}")
  list(TRANSFORM _sources PREPEND "${FFMPEG_FAF_SOURCE_DIR}/")
  add_library(ffmpeg_${_lib} STATIC ${_sources})
  set_target_properties(ffmpeg_${_lib} PROPERTIES OUTPUT_NAME ${_lib} FOLDER "dependencies/ffmpeg")
  # FFmpeg's compile commands: -I. -I$(SRC_PATH) $(CPPFLAGS), then $(CFLAGS) for C and $(ASFLAGS) for
  # the NEON files, plus what ffbuild/library.mak adds for library objects.
  target_include_directories(ffmpeg_${_lib} PRIVATE
    "${FFMPEG_FAF_CONFIG_DIR}" "${FFMPEG_FAF_SOURCE_DIR}" ${FFMPEG_FAF_INCLUDE_DIRECTORIES})
  target_compile_definitions(ffmpeg_${_lib} PRIVATE
    ${FFMPEG_FAF_DEFINITIONS} HAVE_AV_CONFIG_H BUILDING_${_lib})
  target_compile_options(ffmpeg_${_lib} PRIVATE
    "$<$<COMPILE_LANGUAGE:C>:${FFMPEG_FAF_COMPILE_OPTIONS}>"
    "$<$<COMPILE_LANGUAGE:ASM>:${FFMPEG_FAF_ASM_OPTIONS}>")
  # For users FFmpeg's root comes after the system's headers: on this case-insensitive build host its
  # file VERSION would otherwise answer libc++'s #include <version>.
  target_include_directories(ffmpeg_${_lib} INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/include")
  target_compile_options(ffmpeg_${_lib} INTERFACE "-idirafter${FFMPEG_FAF_SOURCE_DIR}")
  foreach(_dep IN LISTS _ffmpeg_deps_${_lib})
    target_link_libraries(ffmpeg_${_lib} PUBLIC ffmpeg_${_dep})
  endforeach()
  target_link_libraries(ffmpeg_${_lib} PUBLIC ${FFMPEG_FAF_${_LIB}_SYSTEM_LIBS})
  list(APPEND FFMPEG_FAF_LIBRARIES ffmpeg_${_lib})
endforeach()

add_library(ffmpeg_faf INTERFACE)
add_library(FFmpeg::FFmpeg ALIAS ffmpeg_faf)
target_link_libraries(ffmpeg_faf INTERFACE
  ffmpeg_avformat ffmpeg_avcodec ffmpeg_swscale ffmpeg_swresample ffmpeg_avutil)

# For a parent project that wants to add options to every FFmpeg library.
if(NOT PROJECT_IS_TOP_LEVEL)
  set(FFMPEG_FAF_LIBRARIES "${FFMPEG_FAF_LIBRARIES}" PARENT_SCOPE)
endif()

# FFmpeg's licence as this configuration has it (config.h: LGPL version 2.1 or later).
configure_file("${FFMPEG_FAF_SOURCE_DIR}/COPYING.LGPLv2.1" "${CMAKE_BINARY_DIR}/licenses/FFmpeg.txt" COPYONLY)
