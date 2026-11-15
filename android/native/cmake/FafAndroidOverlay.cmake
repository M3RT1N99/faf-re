# The engine files the Android build compiles changed, without changing them in the repository.
#
# patches/<path>.patch is a git diff of the engine file <path> (relative to the repository root;
# dependencies/<name>/... for a dependency's file, which is in FAF_DEPENDENCIES_DIR once the
# configure has fetched it). At configure time the file is copied to ${FAF_ANDROID_OVERLAY}/<path>,
# with LF line endings as the repository keeps it, and the patch is applied to the copy. A patch
# that no longer applies (the engine changed those lines) stops the configure with the file's name;
# the patch is then made again against the new file (README.md, "Patches").
#
# A patched source replaces the original in the targets (faf_android_overlay_sources, and
# faf_android_overlay_target for a dependency's target, which stops the configure for a patch of
# the dependency's that replaces none of its sources); a patched header is found first because
# ${FAF_ANDROID_OVERLAY}/src comes ahead of src/ on the include path. A file that includes a
# patched header by its bare name, from the header's own folder, is compiled from the overlay too
# (FAF_ANDROID_OVERLAY_SIBLINGS), so it sees the patched header.

set(FAF_ANDROID_OVERLAY "${CMAKE_BINARY_DIR}/overlay")
set(_faf_overlay_work "${CMAKE_BINARY_DIR}/overlay-work")

find_package(Git REQUIRED)

# `out`: where the file at `path` is, the repository's or a dependency's. cmake/FafDependencies.cmake
# fetches the dependencies into FAF_DEPENDENCIES_DIR, which a cache variable may put elsewhere.
function(_faf_android_original path out)
  if(path MATCHES "^dependencies/(.*)$")
    set(${out} "${FAF_DEPENDENCIES_DIR}/${CMAKE_MATCH_1}" PARENT_SCOPE)
  else()
    set(${out} "${FAF_REPO}/${path}" PARENT_SCOPE)
  endif()
endfunction()

# Copies <path> into the overlay, applying `patch` when one is given.
function(_faf_android_overlay_file path patch)
  _faf_android_original("${path}" _original)
  file(READ "${_original}" _text)
  string(REPLACE "\r\n" "\n" _text "${_text}")
  file(REMOVE_RECURSE "${_faf_overlay_work}")
  file(WRITE "${_faf_overlay_work}/${path}" "${_text}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_original}")
  if(patch)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${patch}")
    file(READ "${patch}" _diff)
    string(REPLACE "\r\n" "\n" _diff "${_diff}")
    file(WRITE "${_faf_overlay_work}/change.patch" "${_diff}")
    # Applied as GNU patch would: GIT_CEILING_DIRECTORIES keeps git from taking the repository
    # around the build folder for the target of the patch.
    execute_process(
      COMMAND "${CMAKE_COMMAND}" -E env "GIT_CEILING_DIRECTORIES=${CMAKE_BINARY_DIR}"
              "${GIT_EXECUTABLE}" -c core.autocrlf=false apply --unsafe-paths --whitespace=nowarn change.patch
      WORKING_DIRECTORY "${_faf_overlay_work}"
      RESULT_VARIABLE _result
      OUTPUT_VARIABLE _output
      ERROR_VARIABLE _output)
    if(NOT _result EQUAL 0)
      message(FATAL_ERROR "android/native/patches: ${path}.patch no longer applies to ${path}:\n${_output}\n"
                          "Make the patch again against the engine's file (android/native/README.md, \"Patches\").")
    endif()
  endif()
  # Written only when it changed, so the objects of unchanged files stay built.
  get_filename_component(_folder "${FAF_ANDROID_OVERLAY}/${path}" DIRECTORY)
  file(MAKE_DIRECTORY "${_folder}")
  file(COPY_FILE "${_faf_overlay_work}/${path}" "${FAF_ANDROID_OVERLAY}/${path}" ONLY_IF_DIFFERENT)
endfunction()

file(GLOB_RECURSE _faf_android_patches CONFIGURE_DEPENDS RELATIVE "${FAF_ANDROID_DIR}/patches" "${FAF_ANDROID_DIR}/patches/*.patch")
set(_faf_android_overlay_paths "")
foreach(_patch IN LISTS _faf_android_patches)
  string(REGEX REPLACE "\\.patch$" "" _path "${_patch}")
  _faf_android_overlay_file("${_path}" "${FAF_ANDROID_DIR}/patches/${_patch}")
  list(APPEND _faf_android_overlay_paths "${_path}")
endforeach()
foreach(_path IN LISTS FAF_ANDROID_OVERLAY_SIBLINGS)
  _faf_android_overlay_file("${_path}" "")
  list(APPEND _faf_android_overlay_paths "${_path}")
endforeach()
file(REMOVE_RECURSE "${_faf_overlay_work}")
list(LENGTH _faf_android_patches _faf_patch_count)
message(STATUS "android/native: ${_faf_patch_count} files patched")

# `out`: the files of `sources` (absolute paths), with each overlaid one replaced by its copy. A
# copy finds the original's neighbours (its own headers) through its folder in the repository.
function(faf_android_overlay_sources out)
  set(_result "")
  foreach(_file IN LISTS ARGN)
    file(RELATIVE_PATH _relative "${FAF_REPO}" "${_file}")
    if(_relative IN_LIST _faf_android_overlay_paths)
      set(_copy "${FAF_ANDROID_OVERLAY}/${_relative}")
      get_filename_component(_folder "${_file}" DIRECTORY)
      # For quoted includes only: a neighbour may have a system header's name (src/platform/shell has a string.h), which
      # the folder on the include path would put in that header's place.
      set_property(SOURCE "${_copy}" APPEND PROPERTY COMPILE_OPTIONS "-iquote${_folder}")
      # Whatever single-file settings the original has here, the copy takes along.
      foreach(_property COMPILE_DEFINITIONS COMPILE_OPTIONS)
        get_source_file_property(_value "${_file}" ${_property})
        if(_value)
          set_property(SOURCE "${_copy}" APPEND PROPERTY ${_property} ${_value})
        endif()
      endforeach()
      list(APPEND _result "${_copy}")
    else()
      list(APPEND _result "${_file}")
    endif()
  endforeach()
  set(${out} "${_result}" PARENT_SCOPE)
endfunction()

# The sources of `target`, the target of the dependency in FAF_DEPENDENCIES_DIR/<dependency>, made in
# another folder, with each overlaid one replaced by its copy, which finds the original's neighbours
# for its quoted includes. A patch of the dependency's that replaces none of them stops the
# configure: the target would build that file unchanged, and nothing else would say so.
function(faf_android_overlay_target target dependency)
  get_target_property(_sources ${target} SOURCES)
  get_target_property(_source_dir ${target} SOURCE_DIR)
  set(_result "")
  set(_replaced "")
  foreach(_file IN LISTS _sources)
    # What the target compiles: a header among its sources (for an IDE) is still included from its own folder. A
    # generator expression's file is none of the patched ones by its name.
    if(_file MATCHES "\\.(c|cc|cpp|cxx|m|mm|s|S)$" AND NOT _file MATCHES "^\\$<")
      get_filename_component(_absolute "${_file}" ABSOLUTE BASE_DIR "${_source_dir}")
      file(RELATIVE_PATH _relative "${FAF_DEPENDENCIES_DIR}" "${_absolute}")
      if("dependencies/${_relative}" IN_LIST _faf_android_overlay_paths)
        get_filename_component(_folder "${_absolute}" DIRECTORY)
        set(_file "${FAF_ANDROID_OVERLAY}/dependencies/${_relative}")
        set_property(SOURCE "${_file}" TARGET_DIRECTORY ${target} APPEND PROPERTY COMPILE_OPTIONS "-iquote${_folder}")
        list(APPEND _replaced "dependencies/${_relative}")
      endif()
    endif()
    list(APPEND _result "${_file}")
  endforeach()
  set_property(TARGET ${target} PROPERTY SOURCES ${_result})
  foreach(_path IN LISTS _faf_android_overlay_paths)
    if(_path MATCHES "^dependencies/${dependency}/" AND NOT _path IN_LIST _replaced)
      message(FATAL_ERROR "android/native/patches: ${_path}.patch replaces none of ${target}'s sources, which would "
                          "build the file unchanged: it is not one of them by that path (a header of a dependency "
                          "is not overlaid).")
    endif()
  endforeach()
endfunction()
