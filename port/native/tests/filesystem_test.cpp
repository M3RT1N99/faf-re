// fs:: helpers: path normalisation, _findfirst-style wildcards and ordering,
// and case-insensitive resolution against a real directory tree.

#include <string>
#include <vector>

#include "faf/port/FileSystem.h"

#include "FileSystemDetail.h"
#include "TestSupport.h"

namespace fs = faf::port::fs;
using faf::test::TempDir;
using faf::test::WriteFile;

FAF_TEST(NormalizePathFoldsSeparatorsAndDots)
{
  FAF_CHECK_EQ(
    fs::NormalizePath("C:\\ProgramData\\FAForever\\bin\\..\\gamedata\\"),
    std::string("C:/ProgramData/FAForever/gamedata")
  );
  FAF_CHECK_EQ(fs::NormalizePath("/a//b/./c/"), std::string("/a/b/c"));
  FAF_CHECK_EQ(fs::NormalizePath("a/b/../../c"), std::string("c"));
  FAF_CHECK_EQ(fs::NormalizePath("../x/./y"), std::string("../x/y"));
  FAF_CHECK_EQ(fs::NormalizePath("a/.."), std::string("."));
  FAF_CHECK_EQ(fs::NormalizePath(""), std::string(""));
  FAF_CHECK_EQ(fs::NormalizePath("/"), std::string("/"));
  FAF_CHECK_EQ(fs::NormalizePath("/.."), std::string("/"));
  FAF_CHECK_EQ(fs::NormalizePath("C:/../.."), std::string("C:/"));
  FAF_CHECK_EQ(fs::NormalizePath("c:\\"), std::string("c:/"));
  FAF_CHECK_EQ(fs::NormalizePath("/sdcard/Android/data/x/files/faf/bin/../fa_path.lua"),
               std::string("/sdcard/Android/data/x/files/faf/fa_path.lua"));
}

FAF_TEST(PathHelpers)
{
  using namespace fs::detail;
  FAF_CHECK_EQ(ParentPath("C:/a/b.lua"), std::string("C:/a"));
  FAF_CHECK_EQ(ParentPath("C:/a"), std::string("C:/"));
  FAF_CHECK_EQ(ParentPath("/a"), std::string("/"));
  FAF_CHECK_EQ(ParentPath("a"), std::string("."));
  FAF_CHECK_EQ(std::string(FileName("C:/a/b.lua")), std::string("b.lua"));
  FAF_CHECK_EQ(JoinPath("/", "x"), std::string("/x"));
  FAF_CHECK_EQ(JoinPath("C:/a", "x"), std::string("C:/a/x"));
  FAF_CHECK_EQ(JoinPath("", "x"), std::string("x"));
  FAF_CHECK(EqualsNoCase("Textures.SCD", "textures.scd"));
  FAF_CHECK(!EqualsNoCase("textures.scd", "textures.sc"));
}

FAF_TEST(WildcardMatchIsCaseInsensitive)
{
  FAF_CHECK(fs::WildcardMatch("*.nx2", "lua.NX2"));
  FAF_CHECK(fs::WildcardMatch("*", ""));
  FAF_CHECK(fs::WildcardMatch("*", "."));
  FAF_CHECK(fs::WildcardMatch("?ua.nx2", "lua.nx2"));
  FAF_CHECK(!fs::WildcardMatch("?ua.nx2", "ua.nx2"));
  FAF_CHECK(fs::WildcardMatch("XGG.*", "xgg.xwb"));
  FAF_CHECK(fs::WildcardMatch("SCMP_*", "scmp_009"));
  FAF_CHECK(!fs::WildcardMatch("*.scd", "textures.scd.bak"));
  FAF_CHECK(fs::WildcardMatch("a*b*c", "aXXbYYc"));
  FAF_CHECK(!fs::WildcardMatch("a*b*c", "aXXbYY"));
  FAF_CHECK(fs::WildcardMatch("mod_info.lua", "MOD_INFO.LUA"));
  FAF_CHECK(!fs::WildcardMatch("", "x"));
  FAF_CHECK(fs::WildcardMatch("", ""));
  // Backtracking stays linear-ish: this must return quickly.
  FAF_CHECK(!fs::WildcardMatch("*a*a*a*a*a*a*a*a*b", std::string(200, 'a')));
}

FAF_TEST(FindFilesUsesNtfsOrderAndDotEntries)
{
  TempDir dir;
  WriteFile(dir / "gamedata/b.nx2", "b");
  WriteFile(dir / "gamedata/A.nx2", "a");
  WriteFile(dir / "gamedata/_x.nx2", "x");
  WriteFile(dir / "gamedata/c.txt", "c");
  WriteFile(dir / "gamedata/noext", "n");
  faf::test::MakeDirectory(dir / "gamedata/Sub");

  // Upper-case collation: 'A' < 'B' < '_' (0x5F).
  FAF_CHECK_EQ(fs::FindFiles(dir / "gamedata/*.nx2"), (std::vector<std::string>{"A.nx2", "b.nx2", "_x.nx2"}));
  FAF_CHECK_EQ(fs::FindFiles(dir / "gamedata\\*.NX2"), (std::vector<std::string>{"A.nx2", "b.nx2", "_x.nx2"}));

  const std::vector<std::string> everything{".", "..", "A.nx2", "b.nx2", "c.txt", "noext", "Sub", "_x.nx2"};
  FAF_CHECK_EQ(fs::FindFiles(dir / "gamedata/*"), everything);
  FAF_CHECK_EQ(fs::FindFiles(dir / "gamedata/*.*"), everything); // "*.*" also matches "noext"

  // A plain name returns its on-disk spelling (init_faf.lua's mod_info.lua check).
  FAF_CHECK_EQ(fs::FindFiles(dir / "gamedata/C.TXT"), (std::vector<std::string>{"c.txt"}));
  FAF_CHECK(fs::FindFiles(dir / "gamedata/missing.lua").empty());
  FAF_CHECK(fs::FindFiles(dir / "nowhere/*").empty());
  FAF_CHECK(fs::FindFiles(dir / "gamedata/").empty());
  // The directory part resolves case-insensitively.
  FAF_CHECK_EQ(fs::FindFiles(dir / "GAMEDATA/sub/*"), (std::vector<std::string>{".", ".."}));
}

FAF_TEST(ResolveFindsOnDiskSpelling)
{
  TempDir dir;
  WriteFile(dir / "Gas Powered Games/Supreme Commander/Game.prefs", "prefs");

  // Public resolver: any spelling that opens the file.
  const auto resolved = fs::ResolveCaseInsensitive(dir / "gas powered games/supreme COMMANDER/game.PREFS");
  FAF_REQUIRE(resolved.has_value());
  FAF_CHECK(fs::IsRegularFile(*resolved));

  // Listing resolver (what a case-sensitive file system gets): the real spelling.
  const auto spelled = fs::detail::Resolve(dir / "gas powered games/supreme COMMANDER/game.PREFS", false);
  FAF_REQUIRE(spelled.has_value());
  FAF_CHECK_EQ(*spelled, dir / "Gas Powered Games/Supreme Commander/Game.prefs");

  FAF_CHECK(!fs::ResolveCaseInsensitive(dir / "gas powered games/missing/game.prefs").has_value());
  FAF_CHECK(!fs::detail::Resolve(dir / "Gas Powered Games/Game.prefs/x", false).has_value());
  FAF_CHECK(fs::IsDirectory(dir / "GAS POWERED GAMES"));
  FAF_CHECK(!fs::IsRegularFile(dir / "GAS POWERED GAMES"));
  FAF_CHECK(fs::IsRegularFile(dir / "gas powered games/../Gas Powered Games/supreme commander/GAME.PREFS"));
}

FAF_TEST(CreateDirectoriesReusesExistingCase)
{
  TempDir dir;
  faf::test::MakeDirectory(dir / "Gas Powered Games");
  FAF_CHECK(fs::CreateDirectories(dir / "gas powered games/Supreme Commander Forged Alliance/cache"));
  FAF_CHECK(fs::IsDirectory(dir / "Gas Powered Games/Supreme Commander Forged Alliance/cache"));
  // Only one "Gas Powered Games" exists afterwards (matters on case-sensitive storage).
  FAF_CHECK_EQ(fs::FindFiles(dir / "*"), (std::vector<std::string>{".", "..", "Gas Powered Games"}));
  FAF_CHECK(fs::CreateDirectories(dir / "Gas Powered Games")); // existing: true
  WriteFile(dir / "file", "x");
  FAF_CHECK(!fs::CreateDirectories(dir / "file/sub")); // a file in the way
}

FAF_TEST(ReadWholeFileResolvesCase)
{
  TempDir dir;
  WriteFile(dir / "Bin/Init_FAF.lua", std::string("path = {}\r\n\xEF\xBB\xBF"));
  std::vector<unsigned char> bytes;
  std::string error;
  FAF_REQUIRE(fs::ReadWholeFile(dir / "bin/init_faf.lua", bytes, &error));
  FAF_CHECK_EQ(bytes.size(), std::size_t{14});
  FAF_CHECK_EQ(static_cast<int>(bytes[13]), 0xBF);

  FAF_CHECK(!fs::ReadWholeFile(dir / "bin/missing.lua", bytes, &error));
  FAF_CHECK(bytes.empty());
  FAF_CHECK(error.find("missing.lua") != std::string::npos);

  WriteFile(dir / "empty", "");
  FAF_CHECK(fs::ReadWholeFile(dir / "EMPTY", bytes, &error));
  FAF_CHECK(bytes.empty());
}
