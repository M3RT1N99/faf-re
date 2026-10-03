# faf_port_lua: the LuaPlus 1081 C core (Lua 5.0.1) built from a patched copy.
#
# dependencies/LuaPlus_Build1081 is shared with the engine build and is never
# edited here. At configure time its Src/LuaPlus tree (plus faf_lua_config.h)
# is copied into <binary dir>/luaplus and
# dependencies/patches/luaplus_build1081_faf_android.patch is applied to the
# copy. A stamp over the patch and every source file decides whether the copy
# is current, so editing either re-stages on the next configure (both are
# CMAKE_CONFIGURE_DEPENDS) and an unchanged tree is left alone.

include_guard(GLOBAL)

set(FAF_PORT_LUAPLUS_DIR "${FAF_PORT_REPO_ROOT}/dependencies/LuaPlus_Build1081"
  CACHE PATH "LuaPlus Build 1081 checkout (read-only; a patched copy is built)")
set(FAF_PORT_LUAPLUS_PATCH "${FAF_PORT_REPO_ROOT}/dependencies/patches/luaplus_build1081_faf_android.patch")

# The C core and the libraries the data-path scripts use. The C++ wrapper
# (LuaObject, LuaState, ...) is not needed and has real LP64 breaks.
set(_faf_lua_sources
  src/lapi.c src/lcode.c src/ldebug.c src/ldo.c src/ldump.c src/lfunc.c src/lgc.c src/llex.c src/lmem.c
  src/lobject.c src/lopcodes.c src/lparser.c src/lstate.c src/lstring.c src/ltable.c src/ltm.c src/lundump.c
  src/lvm.c src/lzio.c
  src/lib/lauxlib.c src/lib/lbaselib.c src/lib/liolib.c src/lib/lmathlib.c src/lib/lstrlib.c src/lib/ltablib.c
  LuaPlusAddons.c
)

function(_faf_port_stage_luaplus out_root)
  set(src_root "${FAF_PORT_LUAPLUS_DIR}/Src/LuaPlus")
  set(stage "${CMAKE_CURRENT_BINARY_DIR}/luaplus")

  if(NOT EXISTS "${src_root}/src/llex.c" OR NOT EXISTS "${FAF_PORT_LUAPLUS_DIR}/faf_lua_config.h")
    message(FATAL_ERROR
      "LuaPlus 1081 sources not found in ${FAF_PORT_LUAPLUS_DIR} (need Src/LuaPlus and faf_lua_config.h).\n"
      "dependencies/ is not in git; restore LuaPlus_Build1081 there or set FAF_PORT_LUAPLUS_DIR.")
  endif()
  if(NOT EXISTS "${FAF_PORT_LUAPLUS_PATCH}")
    message(FATAL_ERROR "Missing ${FAF_PORT_LUAPLUS_PATCH}")
  endif()

  file(GLOB_RECURSE inputs LIST_DIRECTORIES false RELATIVE "${src_root}"
    "${src_root}/*.c" "${src_root}/*.h" "${src_root}/*.cpp" "${src_root}/*.inl")
  list(SORT inputs)
  set(fingerprint "stage-v1;${FAF_PORT_LUAPLUS_DIR}")
  file(SHA256 "${FAF_PORT_LUAPLUS_PATCH}" patch_hash)
  file(SHA256 "${FAF_PORT_LUAPLUS_DIR}/faf_lua_config.h" config_hash)
  list(APPEND fingerprint "${patch_hash}" "${config_hash}")
  set(depends "${FAF_PORT_LUAPLUS_PATCH}" "${FAF_PORT_LUAPLUS_DIR}/faf_lua_config.h")
  foreach(rel IN LISTS inputs)
    file(SHA256 "${src_root}/${rel}" hash)
    list(APPEND fingerprint "${rel}=${hash}")
    list(APPEND depends "${src_root}/${rel}")
  endforeach()
  string(SHA256 fingerprint "${fingerprint}")
  set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${depends})

  set(stamp "${stage}/.faf_port_stamp")
  set(current "")
  if(EXISTS "${stamp}")
    file(READ "${stamp}" current)
  endif()
  if(NOT current STREQUAL fingerprint)
    message(STATUS "faf_port_lua: staging a patched copy of ${src_root}")
    file(REMOVE_RECURSE "${stage}")
    file(MAKE_DIRECTORY "${stage}/Src")
    file(COPY "${src_root}" DESTINATION "${stage}/Src")
    file(COPY "${FAF_PORT_LUAPLUS_DIR}/faf_lua_config.h" DESTINATION "${stage}")

    # A fully applied luaplus_build1081_faf_required.patch turns the object
    # layouts into the 32-bit engine's (pack(4) TObject, __int64 fields), which
    # neither builds with clang nor works on LP64. The port needs the stock
    # layouts; fail with the reason instead of a pile of compile errors.
    file(STRINGS "${stage}/Src/LuaPlus/src/lobject.h" engine_layout REGEX "reserved68")
    if(engine_layout)
      file(REMOVE_RECURSE "${stage}")
      message(FATAL_ERROR
        "${src_root}/src/lobject.h carries the engine's 32-bit object layout "
        "(luaplus_build1081_faf_required.patch). The Android core needs the stock layout; "
        "point FAF_PORT_LUAPLUS_DIR at an unpatched LuaPlus_Build1081 tree.")
    endif()

    _faf_port_apply_patch("${stage}" "${FAF_PORT_LUAPLUS_PATCH}")
    file(WRITE "${stamp}" "${fingerprint}")
  endif()
  set(${out_root} "${stage}" PARENT_SCOPE)
endfunction()

# Applies a -p1 unified diff inside `dir`. git apply is preferred: it is exact
# about CR bytes (the LuaPlus sources are CRLF and the patch carries the CRs)
# and fails as a whole instead of leaving half-patched files. The build tree
# usually sits inside this repository's work tree, where git apply would take
# the patch paths relative to the repository root, so discovery is stopped at
# the staging directory with GIT_CEILING_DIRECTORIES.
function(_faf_port_apply_patch dir patch)
  find_package(Git QUIET)
  find_program(FAF_PORT_PATCH_EXECUTABLE patch)
  get_filename_component(ceiling "${dir}" DIRECTORY)

  set(saved_ceiling "$ENV{GIT_CEILING_DIRECTORIES}")
  set(saved_git_dir "$ENV{GIT_DIR}")
  set(saved_work_tree "$ENV{GIT_WORK_TREE}")
  set(ENV{GIT_CEILING_DIRECTORIES} "${ceiling}")
  unset(ENV{GIT_DIR})
  unset(ENV{GIT_WORK_TREE})

  if(GIT_EXECUTABLE)
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" apply --whitespace=nowarn "${patch}"
      WORKING_DIRECTORY "${dir}"
      RESULT_VARIABLE result
      OUTPUT_VARIABLE output
      ERROR_VARIABLE output)
    set(tool "git apply")
  elseif(FAF_PORT_PATCH_EXECUTABLE)
    execute_process(
      COMMAND "${FAF_PORT_PATCH_EXECUTABLE}" -p1 --binary --forward --batch -i "${patch}"
      WORKING_DIRECTORY "${dir}"
      RESULT_VARIABLE result
      OUTPUT_VARIABLE output
      ERROR_VARIABLE output)
    set(tool "patch")
  else()
    set(result 1)
    set(output "neither git nor patch was found")
    set(tool "(none)")
  endif()

  set(ENV{GIT_CEILING_DIRECTORIES} "${saved_ceiling}")
  if(saved_git_dir)
    set(ENV{GIT_DIR} "${saved_git_dir}")
  endif()
  if(saved_work_tree)
    set(ENV{GIT_WORK_TREE} "${saved_work_tree}")
  endif()

  if(NOT result EQUAL 0)
    file(REMOVE_RECURSE "${dir}")
    message(FATAL_ERROR
      "Applying ${patch} with ${tool} failed:\n${output}\n"
      "The LuaPlus tree in ${FAF_PORT_LUAPLUS_DIR} differs from the one the patch was cut against; "
      "see dependencies/patches/luaplus_build1081_faf_android.md.")
  endif()
endfunction()

function(faf_port_add_luaplus)
  _faf_port_stage_luaplus(stage)
  set(lua_root "${stage}/Src/LuaPlus")

  set(sources "")
  foreach(rel IN LISTS _faf_lua_sources)
    list(APPEND sources "${lua_root}/${rel}")
  endforeach()

  add_library(faf_port_lua STATIC ${sources})
  add_library(faf::port_lua ALIAS faf_port_lua)
  set_target_properties(faf_port_lua PROPERTIES
    C_STANDARD 99
    C_EXTENSIONS ON
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET hidden
  )

  # lua.h includes "../LuaLink.h", so the include dir must stay inside the tree.
  target_include_directories(faf_port_lua SYSTEM
    PUBLIC
      "${lua_root}/include"
      "${stage}"
      "${CMAKE_CURRENT_SOURCE_DIR}/src/lua"
    PRIVATE
      "${lua_root}"
      "${lua_root}/src"
      "${lua_root}/src/lib"
  )
  # LUAPLUS_LIB: static library, no __declspec(dllimport) or #pragma comment(lib).
  # LUAPLUS_HAS_WCHAR_T: lua_WChar = wchar_t, as in the engine's LuaPlus build.
  target_compile_definitions(faf_port_lua PUBLIC LUAPLUS_LIB LUAPLUS_HAS_WCHAR_T)

  set(prefix "${CMAKE_CURRENT_SOURCE_DIR}/src/lua/faf_port_lua_config.h")
  if(MSVC)
    target_compile_options(faf_port_lua PRIVATE "/FI${prefix}" /W0)
    target_compile_definitions(faf_port_lua PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
  else()
    # 2004 code: '#endif _WIN32_WCE' trailers, K&R-era casts, an ignored
    # __stdcall on non-x86 targets. Its warnings are not actionable here.
    target_compile_options(faf_port_lua PRIVATE -include "${prefix}" -w)
    # Lua keeps a string's characters right behind its header and reads them
    # as (char *)(ts + 1). With _FORTIFY_SOURCE=2 (the NDK default) bionic
    # takes that pointer as the end of a zero-sized object, so the first
    # strchr on a weak table's __mode - luaopen_base creates one - aborts with
    # "FORTIFY: strchr: prevented read past end of buffer". Only this vendored
    # C core uses the pattern; our own code keeps FORTIFY.
    target_compile_options(faf_port_lua PRIVATE -U_FORTIFY_SOURCE)
    if(WIN32)
      target_compile_definitions(faf_port_lua PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
    endif()
  endif()

  if(NOT WIN32)
    target_link_libraries(faf_port_lua PUBLIC m)
  endif()
endfunction()
