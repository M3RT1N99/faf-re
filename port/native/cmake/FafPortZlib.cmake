# zlib for faf_port_core: the platform's on Android/Linux (the NDK sysroot
# ships zlib.h and libz as a stable API), the vendored 1.2.3 sources on Windows
# where no system zlib exists. Defines FAF_PORT_ZLIB_TARGET.

include_guard(GLOBAL)

if(WIN32)
  set(_faf_port_system_zlib_default OFF)
else()
  set(_faf_port_system_zlib_default ON)
endif()
option(FAF_PORT_USE_SYSTEM_ZLIB "Link the system zlib (find_package(ZLIB)) instead of building dependencies/zlib-1.2.3"
  ${_faf_port_system_zlib_default})
set(FAF_PORT_ZLIB_DIR "${FAF_PORT_REPO_ROOT}/dependencies/zlib-1.2.3"
  CACHE PATH "zlib sources built when FAF_PORT_USE_SYSTEM_ZLIB is OFF (read-only)")

if(FAF_PORT_USE_SYSTEM_ZLIB)
  find_package(ZLIB REQUIRED)
  set(FAF_PORT_ZLIB_TARGET ZLIB::ZLIB)
else()
  set(_faf_zlib_sources adler32.c compress.c crc32.c deflate.c infback.c inffast.c inflate.c inftrees.c trees.c
    uncompr.c zutil.c)
  foreach(_faf_zlib_source IN LISTS _faf_zlib_sources)
    if(NOT EXISTS "${FAF_PORT_ZLIB_DIR}/${_faf_zlib_source}")
      message(FATAL_ERROR "zlib source ${FAF_PORT_ZLIB_DIR}/${_faf_zlib_source} not found; "
        "set FAF_PORT_ZLIB_DIR or FAF_PORT_USE_SYSTEM_ZLIB=ON")
    endif()
  endforeach()
  list(TRANSFORM _faf_zlib_sources PREPEND "${FAF_PORT_ZLIB_DIR}/")

  add_library(faf_port_zlib STATIC ${_faf_zlib_sources})
  # zlib 1.2.3 uses K&R function definitions, which C23 removed.
  set_target_properties(faf_port_zlib PROPERTIES C_STANDARD 99 C_EXTENSIONS ON POSITION_INDEPENDENT_CODE ON)
  # The root zconf.h is the stock one; dependencies/zlib-1.2.3/include holds
  # the engine's patched copy, which this build does not need.
  target_include_directories(faf_port_zlib SYSTEM PUBLIC "${FAF_PORT_ZLIB_DIR}")
  if(MSVC)
    target_compile_options(faf_port_zlib PRIVATE /W0)
    target_compile_definitions(faf_port_zlib PRIVATE _CRT_SECURE_NO_DEPRECATE _CRT_NONSTDC_NO_DEPRECATE)
  else()
    target_compile_options(faf_port_zlib PRIVATE -w)
  endif()
  set(FAF_PORT_ZLIB_TARGET faf_port_zlib)
endif()
