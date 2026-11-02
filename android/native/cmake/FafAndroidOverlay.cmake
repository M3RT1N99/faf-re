# The engine files the Android build compiles changed, without changing them in the repository.
#
# patches/<path>.patch is a git diff of the engine file <path> (relative to the repository root).
# At configure time the file is copied to ${FAF_ANDROID_OVERLAY}/<path>, with LF line endings as
# the repository keeps it, and the patch is applied to the copy. A patch that no longer applies
# (the engine changed those lines) stops the configure with the file's name; the patch is then
# made again against the new file (README.md, "Patches").
#
# A patched source replaces the original in the targets (faf_android_overlay_sources); a patched
# header is found first because ${FAF_ANDROID_OVERLAY}/src comes ahead of src/ on the include
# path. A file that includes a patched header by its bare name, from the header's own folder, is
# compiled from the overlay too (FAF_ANDROID_OVERLAY_SIBLINGS), so it sees the patched header.

set(FAF_ANDROID_OVERLAY "${CMAKE_BINARY_DIR}/overlay")
set(_faf_overlay_work "${CMAKE_BINARY_DIR}/overlay-work")

find_package(Git REQUIRED)

# Copies <path> from the repository into the overlay, applying `patch` when one is given.
function(_faf_android_overlay_file path patch)
  file(READ "${FAF_REPO}/${path}" _text)
  string(REPLACE "\r\n" "\n" _text "${_text}")
  file(REMOVE_RECURSE "${_faf_overlay_work}")
  file(WRITE "${_faf_overlay_work}/${path}" "${_text}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${FAF_REPO}/${path}")
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
message(STATUS "android/native: ${_faf_patch_count} engine files patched")

# `out`: the files of `sources` (absolute paths), with each overlaid one replaced by its copy. A
# copy finds the original's neighbours (its own headers) through its folder in the repository.
function(faf_android_overlay_sources out)
  set(_result "")
  foreach(_file IN LISTS ARGN)
    file(RELATIVE_PATH _relative "${FAF_REPO}" "${_file}")
    if(_relative IN_LIST _faf_android_overlay_paths)
      set(_copy "${FAF_ANDROID_OVERLAY}/${_relative}")
      get_filename_component(_folder "${_file}" DIRECTORY)
      set_source_files_properties("${_copy}" PROPERTIES INCLUDE_DIRECTORIES "${_folder}")
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
