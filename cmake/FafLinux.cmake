# Engine addition (dedicated-server plan, Phase D): the Linux build.
#
# Included by the top-level CMakeLists.txt, in place of the Windows project,
# when the toolchain is cmake/toolchains/linux-clang-x64.cmake. Linux builds
# the headless simulation only (plan D2, G6): no window, render device, audio,
# movie, wx or Win32 code.
#
#   cmake --preset linux-clang-x64-release && cmake --build --preset linux-clang-x64-release
#
# The Android toolchains (cmake/toolchains/android-*.cmake, FAF_ANDROID) build the
# same targets with the NDK against bionic, for arm64-v8a and x86_64:
#
#   cmake --preset android-arm64-release && cmake --build --preset android-arm64-release

if(NOT FAF_LINUX)
  message(FATAL_ERROR "cmake/FafLinux.cmake needs cmake/toolchains/linux-clang-x64.cmake or android-*.cmake.")
endif()
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "GNU")
  message(FATAL_ERROR "The Linux build expects clang with the GNU driver: use the linux-clang-x64-release preset.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "The Linux build is 64-bit only.")
endif()
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
  set(FAF_TARGET_ARCH arm64)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
  set(FAF_TARGET_ARCH x64)
else()
  message(FATAL_ERROR "The Linux build targets x86_64 and arm64, not ${CMAKE_SYSTEM_PROCESSOR}.")
endif()
# The platform half of the names below (build info, the sim-math tests' known list).
if(FAF_ANDROID)
  set(FAF_LINUX_PLATFORM android)
else()
  set(FAF_LINUX_PLATFORM linux)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}")

# --- Build-wide settings ------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Release optimises as the Windows builds' /O2 does: CMake appends its GNU
# default (-O3 -DNDEBUG) after the toolchain's -O2, so the later -O3 would win.
foreach(_lang C CXX ASM)
  string(REPLACE "-O3" "-O2" CMAKE_${_lang}_FLAGS_RELEASE "${CMAKE_${_lang}_FLAGS_RELEASE}")
  string(REPLACE "-O2 -DNDEBUG -O2 -DNDEBUG" "-O2 -DNDEBUG" CMAKE_${_lang}_FLAGS_RELEASE "${CMAKE_${_lang}_FLAGS_RELEASE}")
endforeach()

if(FAF_ANDROID)
  # Android's executables link bionic dynamically, as every program on a device does (the
  # emulator's ARM translation runs no other kind). FAF_ANDROID_STATIC_EXECUTABLES links it
  # statically, so the same file also runs on a Linux host of its architecture, which has
  # no Android dynamic linker: CI runs the tests there.
  option(FAF_ANDROID_STATIC_EXECUTABLES "Link bionic statically, to run the executables on a Linux host" OFF)
  if(FAF_ANDROID_STATIC_EXECUTABLES)
    string(APPEND CMAKE_EXE_LINKER_FLAGS " -static")
  endif()
endif()

# --- Locations ------------------------------------------------------------------------
set(FAF_DEPENDENCIES_DIR "${PROJECT_SOURCE_DIR}/dependencies" CACHE PATH
    "Third-party tree (dependencies/ in the repository)")

include(FafCodegen)

# --- Toolchain check ------------------------------------------------------------------
# tests/linux/ToolchainCheck.cpp: data model, char signedness, no FMA
# contraction, exceptions through extern "C", threads, default x87 and SSE
# control words. Run it after a toolchain change; `ldd` on it shows the C++
# library is linked statically.
add_executable(faf-linux-toolchain-check "${PROJECT_SOURCE_DIR}/tests/linux/ToolchainCheck.cpp")
target_link_libraries(faf-linux-toolchain-check PRIVATE faf_codegen)

# --- Language dialect ---------------------------------------------------------------------
# clang-cl compiles the Windows build with -fms-extensions -fms-compatibility
# -fms-compatibility-version=19.33 -fms-volatile, also under /permissive-
# (`clang-cl -### /permissive-`). Linux takes the Microsoft keywords and
# intrinsics (-fms-extensions) and volatile semantics (-fms-volatile), not
# -fms-compatibility: without a Windows target it stops defining __GNUC__,
# __STDC__, __EXCEPTIONS and __GXX_RTTI, which glibc, libc++ and boost read.
# Code that relies on Microsoft lookup rules (unqualified names from dependent
# bases) is fixed in the source instead. clang defines no _MSC_VER or _WIN32
# here, so platform code takes its POSIX branches.
set(FAF_LINUX_MS_DIALECT -fms-extensions -fms-volatile)

# --- Dependencies -----------------------------------------------------------------------
# Wild Magic Foundation (the sim's math) and zlib 1.2.3, with the Windows
# builds' source lists.
add_subdirectory("${PROJECT_SOURCE_DIR}/cmake/deps/wildmagic" "${CMAKE_BINARY_DIR}/deps/wildmagic")
add_subdirectory("${PROJECT_SOURCE_DIR}/cmake/deps/zlib" "${CMAKE_BINARY_DIR}/deps/zlib")

# --- faf_sim ----------------------------------------------------------------------------
# The simulation's translation units (src/sim_sources.cmake), in the engine's
# order, as an object library: every object is linked, so every static
# initialiser runs, as in the Windows executable.
include("${PROJECT_SOURCE_DIR}/src/sources.cmake")
include("${PROJECT_SOURCE_DIR}/src/sim_sources.cmake")

# boost.thread 1.34.1 on pthreads: the Win32 TLS hooks are left out.
list(FILTER FAF_SIM_SOURCES EXCLUDE REGEX "/libs/thread/src/tss_(dll|hooks|pe)\\.cpp$")
# The Windows platform services (call stacks, crash-report paths, message
# boxes); the POSIX implementation of the same functions is plan WPL.5.
list(FILTER FAF_SIM_SOURCES EXCLUDE REGEX "^moho/misc/WinPlatform\\.cpp$")
# The crash diagnostics on dbghelp, vectored exception handlers, minidumps and debug
# registers; gpg/core/diagnostics/posix/DiagnosticsPosix.cpp implements their headers.
list(FILTER FAF_SIM_SOURCES EXCLUDE REGEX "^gpg/core/diagnostics/(CrashHooks|CrashReport|ProbeWatch)\\.cpp$")
# The lockstep transport: Winsock sockets driven by WSAEventSelect. Linux replays and
# serves games without it (the server has its own channel, plan D3); moho/net/Common.cpp's
# connector factories fail here as on a Windows host without Winsock.
list(FILTER FAF_SIM_SOURCES EXCLUDE REGEX "^moho/net/(CNetDatagramSocketImpl|CNetTCPBuf|CNetTCPConnection|CNetTCPConnector|CNetTCPServerImpl|CNetUDPConnection|CNetUDPConnector|INetDatagramSocket|INetTCPServer|INetTCPSocket)\\.cpp$")

set(_faf_sim_files "")
foreach(_src IN LISTS FAF_SIM_SOURCES)
  if(IS_ABSOLUTE "${_src}")
    list(APPEND _faf_sim_files "${_src}")
  else()
    list(APPEND _faf_sim_files "${PROJECT_SOURCE_DIR}/src/${_src}")
  endif()
endforeach()
list(LENGTH _faf_sim_files _faf_sim_count)
message(STATUS "faf_sim: ${_faf_sim_count} translation units")

file(WRITE "${CMAKE_BINARY_DIR}/win32-alias/windows.h" "#pragma once\n#include <Windows.h>\n")

# The wire format of server-authoritative sessions (remote/sources.cmake, FAF_REMOTE_SOURCES),
# after the engine's sources as in the Windows server-mode build: headless's wire checks
# use it, and the Linux game server will.
include("${PROJECT_SOURCE_DIR}/src/remote/sources.cmake")
# The client half too (FAF_REMOTE_CLIENT_SOURCES), as Windows' server-mode faf_engine links it:
# sim-side server-mode code (SetFocusArmy's driver lookup) calls into it.
foreach(_src IN LISTS FAF_REMOTE_SOURCES FAF_REMOTE_CLIENT_SOURCES)
  list(APPEND _faf_sim_files "${PROJECT_SOURCE_DIR}/src/${_src}")
endforeach()

add_library(faf_sim OBJECT ${_faf_sim_files})
# Its compile and link settings, which targets built from a few sim sources
# (tests/sim_math) share without depending on every faf_sim object.
add_library(faf_sim_settings INTERFACE)
target_link_libraries(faf_sim PUBLIC faf_sim_settings)

# The git commit for gpg/core/diagnostics/BuildInfo.cpp, read on every build.
include(FafBuildInfo)
faf_build_info(faf_sim ${FAF_TARGET_ARCH}-${FAF_LINUX_PLATFORM})

if(FAF_TARGET_ARCH STREQUAL "x64")
  # The x64 half of legacy/math/X87Math.cpp, in GNU syntax for System V (the
  # Windows build assembles X87Math64.asm, plan WPL.10).
  enable_language(ASM)
  target_sources(faf_sim PRIVATE "${PROJECT_SOURCE_DIR}/src/legacy/math/X87Math64.S")
endif()
# gpg::gal::Math without D3DX (Windows' faf_main uses gpg/gal/Matrix.cpp, plan WPR.12).
target_sources(faf_sim PRIVATE "${PROJECT_SOURCE_DIR}/src/gpg/gal/MatrixPortable.cpp")
# The Win32 calls the shared code makes, on POSIX (plan WPL.5).
target_sources(faf_sim PRIVATE "${PROJECT_SOURCE_DIR}/src/platform/posix/Win32Shim.cpp")
# Stack walks for the diagnostics below, on any C library (bionic before API 33 has no
# backtrace(3)).
target_sources(faf_sim PRIVATE "${PROJECT_SOURCE_DIR}/src/platform/posix/Backtrace.cpp")
# The crash diagnostics' headers on POSIX (plan WPL.5).
target_sources(faf_sim PRIVATE "${PROJECT_SOURCE_DIR}/src/gpg/core/diagnostics/posix/DiagnosticsPosix.cpp")
# moho/misc/WinPlatform.cpp's platform services (call stacks, error text, message boxes) and
# the type_info of client classes the sim names (plan WPL.5).
target_sources(faf_sim PRIVATE
  "${PROJECT_SOURCE_DIR}/src/moho/misc/posix/PlatformPosix.cpp"
  "${PROJECT_SOURCE_DIR}/src/platform/posix/ClientTypeInfoPosix.cpp")
# Every thread's x87 control word and MXCSR as on Windows x64 (plan WPL.9): the main thread's
# from a constructor, the others' from pthread_create, wrapped in every program that links the
# sim.
target_sources(faf_sim PRIVATE "${PROJECT_SOURCE_DIR}/src/platform/posix/FpEnvPosix.cpp")
target_link_options(faf_sim_settings INTERFACE "-Wl,--wrap=pthread_create")

# The Windows build's engine definitions without the Win32 ones (WIN32,
# _WINDOWS, WIN32_LEAN_AND_MEAN, NOMINMAX, UNICODE, _CRT_*).
target_compile_definitions(faf_sim_settings INTERFACE
  LUAPLUS_LIB LUA_NUMBER=float
  BOOST_ALL_NO_LIB BOOST_NO_AUTO_PTR
  # The server-mode build, as Windows' faf_engine with FAF_SERVER_MODE (src/CMakeLists.txt):
  # the Linux programs are the server and the replay runner.
  FAF_SERVER_MODE=1)
target_include_directories(faf_sim_settings INTERFACE
  "${PROJECT_SOURCE_DIR}/src"
  # <Windows.h> and <intrin.h> for the shared code (plan WPL.5), and <windows.h>, the
  # spelling some files use (the file system is case-sensitive here).
  "${PROJECT_SOURCE_DIR}/src/platform/posix/win32"
  "${CMAKE_BINARY_DIR}/win32-alias"
  "${FAF_DEPENDENCIES_DIR}/boost_1_34_1"
  "${FAF_DEPENDENCIES_DIR}/zlib-1.2.3")
target_link_libraries(faf_sim_settings INTERFACE faf_codegen)
target_link_libraries(faf_sim PUBLIC Wm3Foundation zlib123)
# dlopen/dlsym (Win32Shim's LoadLibrary) and dladdr (symbol names in call stacks): part of
# glibc's libc since 2.34, libdl on bionic.
target_link_libraries(faf_sim_settings INTERFACE ${CMAKE_DL_LIBS})

set(_faf_sim_options
  # The recovered tree's x86 layout asserts, off as on Windows x64 (see the
  # header).
  "SHELL:-include platform/X64LayoutAsserts.h"
  # The Windows names the shared code uses (plan WPL.5).
  "SHELL:-include platform/PosixCompat.h"
  ${FAF_LINUX_MS_DIALECT}
  # boost.thread checks for the compiler's threading mode (_REENTRANT).
  -pthread
  # The Windows build's diagnostics settings (src/CMakeLists.txt).
  -Wno-microsoft
  -Wno-unused-parameter -Wno-unused-variable -Wno-unused-private-field
  -Wno-unused-function -Wno-unused-local-typedef -Wno-unused-but-set-variable
  -Wno-unused-lambda-capture -Wno-unused-value -Wno-unused-command-line-argument
  -Wno-missing-field-initializers -Wno-sign-compare -Wno-switch
  -Wno-invalid-offsetof -Wno-keyword-macro -Wno-unknown-pragmas
  -Wno-ignored-pragma-intrinsic -Wno-pragma-pack -Wno-ignored-attributes
  -Wno-deprecated-declarations -Wno-deprecated-enum-enum-conversion
  -Wno-nonportable-include-path -Wno-reorder-ctor
  -Wno-delete-non-abstract-non-virtual-dtor -Wno-overloaded-virtual
  -Wno-inconsistent-missing-override -Wno-missing-braces -Wno-braced-scalar-init
  -Wno-char-subscripts -Wno-parentheses -Wno-dangling-else
  -Wno-tautological-constant-out-of-range-compare
  -Wno-tautological-undefined-compare -Wno-undefined-var-template
  -Wno-writable-strings -Wno-comment -Wno-extra-tokens -Wno-nontrivial-memcall
  -Wno-mismatched-tags -Wno-extra-qualification
  # Options clang 18 does not know yet (the Windows build uses a newer clang).
  -Wno-unknown-warning-option)
target_compile_options(faf_sim_settings INTERFACE "$<$<COMPILE_LANGUAGE:C,CXX>:${_faf_sim_options}>")

# boost.thread's own sources, as on Windows (src/CMakeLists.txt).
foreach(_src IN LISTS _faf_sim_files)
  if(_src MATCHES "/libs/thread/src/")
    # Its thread.cpp and tss.cpp use std::auto_ptr, which libc++ keeps behind this opt-in.
    set_property(SOURCE "${_src}" APPEND PROPERTY COMPILE_DEFINITIONS BOOST_THREAD_BUILD_LIB=1 _LIBCPP_ENABLE_CXX17_REMOVED_AUTO_PTR=1)
    set_property(SOURCE "${_src}" APPEND PROPERTY COMPILE_OPTIONS -w)
  endif()
endforeach()

# --- faf_sim_math_tests (plan WPL.23) ---------------------------------------------------
# The golden sim-math tests (tests/sim_math) on every faf_sim object, as Windows runs
# them on faf_engine: every golden case, the pathfinder's and the random stream's too.
add_executable(faf_sim_math_tests "${PROJECT_SOURCE_DIR}/tests/sim_math/SimMathTests.cpp")
target_link_libraries(faf_sim_math_tests PRIVATE faf_sim)
set(_faf_sim_math_args "${PROJECT_SOURCE_DIR}/tests/sim_math/golden.txt")
set(_faf_sim_math_known "${PROJECT_SOURCE_DIR}/tests/sim_math/known_${FAF_LINUX_PLATFORM}-${FAF_TARGET_ARCH}.txt")
if(EXISTS "${_faf_sim_math_known}")
  list(APPEND _faf_sim_math_args --known "${_faf_sim_math_known}")
endif()
# A cross build (Android) runs its tests where the target runs: a device, the emulator,
# a host of its architecture (CI), or CMAKE_CROSSCOMPILING_EMULATOR.
if(NOT CMAKE_CROSSCOMPILING OR CMAKE_CROSSCOMPILING_EMULATOR)
  add_custom_command(TARGET faf_sim_math_tests POST_BUILD
    COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR} "$<TARGET_FILE:faf_sim_math_tests>" ${_faf_sim_math_args}
    COMMENT "Checking the simulation's arithmetic against tests/sim_math/golden.txt"
    VERBATIM)
endif()

# --- faf-sim-link -----------------------------------------------------------------------
# Every faf_sim object linked into one executable with no entry point of its
# own, to list what the simulation references outside itself (plan WPL.3):
# `cmake --build ... --target faf-sim-link` fails with that list until it is
# empty.
add_executable(faf-sim-link "${PROJECT_SOURCE_DIR}/tests/linux/SimLinkMain.cpp")
target_link_libraries(faf-sim-link PRIVATE faf_sim)
target_link_options(faf-sim-link PRIVATE -Wl,--error-limit=0)

# --- headless (plan WPL.7) --------------------------------------------------------------
# The headless replay runner on Linux: headless/sources.cmake on every faf_sim object, as on
# Windows (a plain Lua state for sync packets and launch configs, no wx entry, no
# userInit.lua). Linux has one architecture, so the file is `headless`, without the
# Windows x64 build's `_x64`.
include("${PROJECT_SOURCE_DIR}/src/headless/sources.cmake")
set(_faf_headless_files "")
foreach(_src IN LISTS FAF_HEADLESS_SOURCES)
  list(APPEND _faf_headless_files "${PROJECT_SOURCE_DIR}/src/${_src}")
endforeach()
add_executable(headless ${_faf_headless_files})
target_link_libraries(headless PRIVATE faf_sim)
target_compile_definitions(headless PRIVATE FAF_SERVER_MODE=1)
target_link_options(headless PRIVATE -Wl,--error-limit=0)

# The libm gate (plan WPL.13): the faf_sim objects call no host libm transcendental outside
# tests/linux/libm_allowlist.txt; headless is not built while one does. It reads the objects
# with the toolchain's nm and objdump (the NDK's llvm-nm and llvm-objdump for Android).
# The object list goes in a response file: on a Windows host (Android builds) it is longer
# than a command line may be.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/faf_sim_objects.rsp" CONTENT "$<JOIN:$<TARGET_OBJECTS:faf_sim>,\n>\n")
add_custom_target(faf_sim_libm_gate
  COMMAND "${CMAKE_COMMAND}" -E env "FAF_NM=${CMAKE_NM}" "FAF_OBJDUMP=${CMAKE_OBJDUMP}"
          "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/linux/LibmGate.py"
          "${PROJECT_SOURCE_DIR}/tests/linux/libm_allowlist.txt" "@${CMAKE_BINARY_DIR}/faf_sim_objects.rsp"
  DEPENDS faf_sim
  COMMENT "Checking faf_sim's libm transcendental calls against tests/linux/libm_allowlist.txt"
  VERBATIM)
add_dependencies(headless faf_sim_libm_gate)

# --- faf_registration_dump_sim (plan WPL.4) ----------------------------------------------
# What the static initialisers of the Linux faf_sim objects registered (tests/registry): its
# sections must equal Windows' faf_registration_dump_sim (src/CMakeLists.txt).
add_executable(faf_registration_dump_sim EXCLUDE_FROM_ALL "${PROJECT_SOURCE_DIR}/tests/registry/RegistrationDump.cpp")
target_link_libraries(faf_registration_dump_sim PRIVATE faf_sim)
