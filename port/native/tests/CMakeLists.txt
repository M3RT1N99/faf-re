# Unit tests (no game data needed) and the integration test against a local
# FAF install (skipped when absent).

if(WIN32)
  set(_faf_port_default_install "C:/ProgramData/FAForever")
else()
  set(_faf_port_default_install "")
endif()
set(FAF_PORT_TEST_FAF_INSTALL "${_faf_port_default_install}" CACHE PATH
  "FAF client install for the integration test (read-only; empty = skip)")

# Scratch space for the tests. Keep it short on Windows: the fixtures nest a
# few levels deep, and without LongPathsEnabled paths stop at 260 characters.
set(FAF_PORT_TEST_TMP "${CMAKE_CURRENT_BINARY_DIR}/test-tmp" CACHE PATH "Scratch directory for the tests")
file(MAKE_DIRECTORY "${FAF_PORT_TEST_TMP}")

function(faf_port_add_test name)
  add_executable(${name} TestMain.cpp TestHarness.h TestSupport.h ${ARGN})
  target_link_libraries(${name} PRIVATE faf_port_core)
  # Tests reach the internal headers (fs::detail, LuaCore.h, NativeFile.h).
  target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/src")
  faf_port_host_executable(${name})
  add_test(NAME faf_port.${name} COMMAND ${name})
  set_tests_properties(faf_port.${name} PROPERTIES
    ENVIRONMENT "FAF_PORT_TEST_TMP=${FAF_PORT_TEST_TMP}"
    SKIP_RETURN_CODE 77
    TIMEOUT 300
  )
endfunction()

faf_port_add_test(filesystem_test filesystem_test.cpp)
faf_port_add_test(commandline_test commandline_test.cpp)
faf_port_add_test(zip_vfs_test zip_vfs_test.cpp)
faf_port_add_test(image_test image_test.cpp)
faf_port_add_test(datapath_test datapath_test.cpp)

faf_port_add_test(integration_test integration_test.cpp)
target_link_libraries(integration_test PRIVATE faf_port_datacheck)
set_tests_properties(faf_port.integration_test PROPERTIES
  ENVIRONMENT "FAF_PORT_TEST_TMP=${FAF_PORT_TEST_TMP};FAF_PORT_TEST_FAF_INSTALL=${FAF_PORT_TEST_FAF_INSTALL}"
  LABELS integration
)

# The tool itself, end to end, when an install is there at configure time.
if(FAF_PORT_BUILD_TOOLS AND FAF_PORT_TEST_FAF_INSTALL AND EXISTS "${FAF_PORT_TEST_FAF_INSTALL}/bin/init_faf.lua")
  add_test(NAME faf_port.datacheck_tool
    COMMAND faf_datacheck
      --init "${FAF_PORT_TEST_FAF_INSTALL}/bin/init_faf.lua"
      --localappdata "${FAF_PORT_TEST_TMP}/datacheck/localappdata"
      --documents "${FAF_PORT_TEST_TMP}/datacheck/documents"
      --no-writes --check-lua --json
  )
  # Exit code 0 = every check passed.
  set_tests_properties(faf_port.datacheck_tool PROPERTIES LABELS integration TIMEOUT 300)
endif()
