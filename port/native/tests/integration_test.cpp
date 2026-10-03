// Integration: FAF's real init_faf.lua from a local FAF client install, run
// with writes disabled and a sandboxed LOCAL_APPDATA, mounted and checked the
// way the device does it. Skipped (exit 77) when no install is present.
//
// $FAF_PORT_TEST_FAF_INSTALL overrides the install directory (CMake passes
// FAF_PORT_TEST_FAF_INSTALL, default C:/ProgramData/FAForever on Windows).

#include <cstdio>
#include <cstdlib>
#include <string>

#include "faf/port/FileSystem.h"
#include "faf/port/Vfs.h"

#include "DataCheck.h"
#include "TestSupport.h"

namespace datacheck = faf::port::datacheck;

FAF_TEST(InitFafMountsTheLocalInstall)
{
  const char* const configured = std::getenv("FAF_PORT_TEST_FAF_INSTALL");
  const std::string install = faf::port::fs::NormalizePath(configured != nullptr ? configured : "");
  const std::string script = install + "/bin/init_faf.lua";
  if (install.empty() || !faf::port::fs::IsRegularFile(script)) {
    faf::test::Skip("no FAF install at '" + install + "' (set FAF_PORT_TEST_FAF_INSTALL)");
  }

  faf::test::TempDir sandbox;
  datacheck::Options options;
  options.scriptPath = script;
  options.localAppData = sandbox / "localappdata";
  options.documents = sandbox / "documents";
  options.allowWrites = false; // never touch the real install
  options.checkLua = true;

  faf::port::VirtualFileSystem vfs;
  const datacheck::Report report = datacheck::Run(options, vfs);
  for (const std::string& problem : report.problems) {
    std::printf("problem: %s\n", problem.c_str());
  }
  FAF_REQUIRE(report.dataPath.ok);

  std::printf(
    "init_faf.lua: %zu path entries in %.1f ms; %zu mounts (%zu archives, %zu entries) in %.1f ms; "
    "lua.nx2: %zu parsed, %zu failed\n",
    report.dataPath.mounts.size(),
    report.dataPath.scriptMilliseconds,
    report.mounts,
    report.archives,
    report.archiveEntries,
    report.mountMilliseconds,
    report.lua.parsed,
    report.lua.failed
  );
  FAF_CHECK(report.mounts >= 700);
  FAF_CHECK(report.archives >= 15);
  FAF_CHECK(report.mountErrors.empty());

  for (const datacheck::Lookup& lookup : report.lookups) {
    std::printf(
      "  %-4s %-56s %s\n", lookup.ok ? "ok" : "FAIL", lookup.vfsPath.c_str(), lookup.location.diskPath.c_str()
    );
    FAF_CHECK(lookup.ok);
  }

  FAF_CHECK(report.lua.ran);
  FAF_CHECK(report.lua.parsed >= 1000);
  FAF_CHECK_EQ(report.lua.failed, std::size_t{0});
  for (const std::string& error : report.lua.errors) {
    std::printf("  lua: %s\n", error.c_str());
  }

  const datacheck::Extraction splash =
    datacheck::ExtractToPng(vfs, datacheck::SplashCandidates()[0], sandbox / "splash.png");
  FAF_CHECK(splash.ok);
  FAF_CHECK_EQ(splash.format, std::string("DXT5"));
  FAF_CHECK_EQ(splash.width, 1024);
  FAF_CHECK_EQ(splash.height, 768);
  FAF_CHECK(faf::port::fs::IsRegularFile(sandbox / "splash.png"));
}
