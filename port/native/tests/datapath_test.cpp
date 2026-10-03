// RunDataPathScript and CheckLuaSyntax on synthetic scripts, plus the
// LuaPlus patch hunks (dependencies/patches/luaplus_build1081_faf_android.patch)
// through the raw Lua API.

#include <algorithm>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "faf/port/DataPath.h"

#include "LuaCore.h"
#include "TestSupport.h"

using faf::port::CheckLuaSyntax;
using faf::port::DataPathOptions;
using faf::port::DataPathResult;
using faf::port::RunDataPathScript;
using faf::test::BuildZip;
using faf::test::Exists;
using faf::test::TempDir;
using faf::test::WriteFile;

namespace {
  /// A miniature install: <root>/faf/bin/init.lua, <root>/faf/fa_path.lua,
  /// two .scd files, a shader cache entry and a documents dir.
  struct Layout
  {
    TempDir dir;
    std::string root = dir.Path();

    Layout()
    {
      WriteFile(dir / "faf/fa_path.lua", "fa_path = \"" + root + "/scfa\"\nGameVersion = \"3839\"\n");
      WriteFile(dir / "scfa/gamedata/b.scd", BuildZip({{"b.lua", "b", faf::test::ZipMethod::Stored}}));
      WriteFile(dir / "scfa/gamedata/A.scd", BuildZip({{"a.lua", "a", faf::test::ZipMethod::Stored}}));
      WriteFile(dir / "scfa/gamedata/readme.txt", "not mounted");
      WriteFile(dir / "localappdata/Gas Powered Games/Supreme Commander Forged Alliance/cache/shader_1234", "old");
      faf::test::MakeDirectory(dir / "documents/maps");
      WriteFile(
        dir / "faf/bin/init.lua",
        // A UTF-8 BOM, '!=', `continue`, string methods, case-mismatched
        // dofile/io.open paths, and every binding init_faf.lua uses.
        "\xEF\xBB\xBF-- data-path script for the unit test\r\n"
        "dofile(InitFileDir .. '/../FA_Path.lua')\r\n"
        "LOG('fa_path', fa_path, 3839)\r\n"
        "path = {}\r\n"
        "local n = 0\r\n"
        "for _, entry in io.dir(fa_path .. '\\\\gamedata\\\\*.scd') do\r\n"
        "  if entry == '.' or entry == '..' then continue end\r\n"
        "  n = n + 1\r\n"
        "  path[n] = { dir = fa_path .. '/gamedata/' .. entry, mountpoint = '/' }\r\n"
        "end\r\n"
        "if n != 2 then error('expected 2 archives, got ' .. n) end\r\n"
        "local version = '3839.1'\r\n"
        "if version:find('.', 1, true) != 5 or ('abc'):upper() != 'ABC' then error('string methods') end\r\n"
        "local cache = SHGetFolderPath('LOCAL_APPDATA') .. 'gas powered games/Supreme Commander Forged "
        "Alliance/cache'\r\n"
        "for _, file in io.dir(cache .. '/*') do\r\n"
        "  if file != '.' and file != '..' then os.remove(cache .. '/' .. file) end\r\n"
        "end\r\n"
        "local h = io.open(InitFileDir .. '/../FA_PATH.LUA', 'rb')\r\n"
        "if not h then error('io.open did not resolve the name') end\r\n"
        "local firstLine = h:read('*l')\r\n"
        "h:close()\r\n"
        "local w = io.open(InitFileDir .. '/written.txt', 'w')\r\n"
        "if w then w:write('x') w:close() end\r\n"
        "path[n + 1] = { dir = SHGetFolderPath('PERSONAL') .. 'maps', mountpoint = '/maps' }\r\n"
        "path[n + 2] = { mountpoint = '/no-dir' }\r\n"
        "path[n + 3] = 'not a table'\r\n"
        "path[n + 4] = { dir = SHGetFolderPath('DESKTOP') .. 'x' }\r\n"
        "hook = { '/schook' }\r\n"
        "protocols = { 'http', 'https', 42 }\r\n"
        "print('done', LaunchDir == InitFileDir, firstLine ~= nil)\r\n"
      );
    }

    DataPathOptions Options(const bool allowWrites, std::vector<std::string>* const log) const
    {
      DataPathOptions options;
      options.scriptPath = dir / "faf/bin/init.lua";
      options.folders.localAppData = dir / "localappdata";
      options.folders.personal = dir / "documents";
      options.allowWrites = allowWrites;
      if (log != nullptr) {
        options.log = [log](const std::string_view line) {
          log->emplace_back(line);
        };
      }
      return options;
    }
  };

  bool Contains(const std::vector<std::string>& lines, const std::string_view text)
  {
    return std::any_of(lines.begin(), lines.end(), [&](const std::string& line) {
      return line.find(text) != std::string::npos;
    });
  }

  DataPathResult RunScript(const TempDir& dir, const std::string& source, std::vector<std::string>* log = nullptr)
  {
    WriteFile(dir / "script.lua", source);
    DataPathOptions options;
    options.scriptPath = dir / "script.lua";
    options.folders.localAppData = dir / "appdata";
    options.allowWrites = false;
    if (log != nullptr) {
      options.log = [log](const std::string_view line) {
        log->emplace_back(line);
      };
    }
    return RunDataPathScript(options);
  }
} // namespace

FAF_TEST(RunsDataPathScriptWithoutWrites)
{
  Layout layout;
  std::vector<std::string> log;
  const DataPathResult result = RunDataPathScript(layout.Options(false, &log));
  FAF_REQUIRE(result.ok);
  FAF_CHECK(result.error.empty());

  FAF_REQUIRE(result.mounts.size() == 4);
  FAF_CHECK_EQ(result.mounts[0].dir, layout.root + "/scfa/gamedata/A.scd");
  FAF_CHECK_EQ(result.mounts[1].dir, layout.root + "/scfa/gamedata/b.scd");
  FAF_CHECK_EQ(result.mounts[0].mountpoint, std::string("/"));
  FAF_CHECK_EQ(result.mounts[2].dir, layout.root + "/documents/maps");
  FAF_CHECK_EQ(result.mounts[2].mountpoint, std::string("/maps"));
  FAF_CHECK_EQ(result.mounts[3].dir, layout.root + "/localappdata/x"); // DESKTOP -> LOCAL_APPDATA
  FAF_CHECK_EQ(result.mounts[3].mountpoint, std::string("/"));         // missing mountpoint
  FAF_CHECK_EQ(result.hooks, (std::vector<std::string>{"/schook"}));
  FAF_CHECK_EQ(result.protocols, (std::vector<std::string>{"http", "https"}));
  FAF_CHECK_EQ(result.initFileDir, layout.root + "/faf/bin");
  FAF_CHECK_EQ(result.launchDir, result.initFileDir);
  FAF_CHECK_EQ(result.logLines, std::size_t{2});
  FAF_CHECK(result.scriptMilliseconds > 0.0);

  FAF_CHECK(Contains(log, "fa_path\t" + layout.root + "/scfa\t3839"));
  FAF_CHECK(Contains(log, "done\ttrue\ttrue"));
  FAF_CHECK(Contains(log, "os.remove("));
  FAF_CHECK(Contains(log, "io.open("));
  // Nothing on disk changed.
  FAF_CHECK(Exists(layout.dir / "localappdata/Gas Powered Games/Supreme Commander Forged Alliance/cache/shader_1234"));
  FAF_CHECK(!Exists(layout.dir / "faf/bin/written.txt"));
}

FAF_TEST(RunsDataPathScriptWithWrites)
{
  Layout layout;
  const DataPathResult result = RunDataPathScript(layout.Options(true, nullptr));
  FAF_REQUIRE(result.ok);
  FAF_CHECK_EQ(result.mounts.size(), std::size_t{4});
  // init_faf.lua's shader cache cleanup, through a case-mismatched path.
  FAF_CHECK(!Exists(layout.dir / "localappdata/Gas Powered Games/Supreme Commander Forged Alliance/cache/shader_1234"));
  FAF_CHECK(Exists(layout.dir / "faf/bin/written.txt"));
}

FAF_TEST(RunsShareNoState)
{
  // Two layouts, run alternately and then concurrently: each run must see
  // only its own folders and log lines.
  Layout first;
  Layout second;
  for (int round = 0; round < 2; ++round) {
    std::vector<std::string> logA;
    std::vector<std::string> logB;
    const DataPathResult a = RunDataPathScript(first.Options(false, &logA));
    const DataPathResult b = RunDataPathScript(second.Options(false, &logB));
    FAF_REQUIRE(a.ok && b.ok);
    FAF_CHECK_EQ(a.mounts[2].dir, first.root + "/documents/maps");
    FAF_CHECK_EQ(b.mounts[2].dir, second.root + "/documents/maps");
    FAF_CHECK(Contains(logA, first.root) && !Contains(logA, second.root));
    FAF_CHECK(Contains(logB, second.root) && !Contains(logB, first.root));
  }

  std::vector<DataPathResult> results(8);
  std::vector<std::vector<std::string>> logs(results.size());
  std::vector<std::thread> threads;
  for (std::size_t i = 0; i < results.size(); ++i) {
    threads.emplace_back([&, i] {
      results[i] = RunDataPathScript((i % 2 ? second : first).Options(false, &logs[i]));
    });
  }
  for (std::thread& thread : threads) {
    thread.join();
  }
  for (std::size_t i = 0; i < results.size(); ++i) {
    const Layout& expected = i % 2 ? second : first;
    FAF_REQUIRE(results[i].ok);
    FAF_CHECK_EQ(results[i].mounts[0].dir, expected.root + "/scfa/gamedata/A.scd");
    FAF_CHECK_EQ(logs[i].size(), std::size_t{4}); // 2 LOG lines + 2 skipped-write notices
  }
}

FAF_TEST(ScriptErrorsCarryLocationAndTraceback)
{
  TempDir dir;
  DataPathResult result = RunScript(dir, "local function explode()\n  error('boom')\nend\nexplode()\n");
  FAF_CHECK(!result.ok);
  FAF_CHECK(result.error.find("script.lua:2: boom") != std::string::npos);
  FAF_CHECK(result.error.find("stack traceback:") != std::string::npos);
  FAF_CHECK(result.error.find("in function `explode'") != std::string::npos);

  result = RunScript(dir, "path = {}\nx = = 1\n");
  FAF_CHECK(!result.ok);
  FAF_CHECK(result.error.find("script.lua:2:") != std::string::npos);

  result = RunScript(dir, "os.execute('echo hi')\n");
  FAF_CHECK(!result.ok);
  FAF_CHECK(result.error.find("os.execute is not available") != std::string::npos);
  FAF_CHECK(!RunScript(dir, "os.exit(1)\n").ok);
  FAF_CHECK(!RunScript(dir, "SHGetFolderPath('NOT_A_FOLDER')\n").ok);
  FAF_CHECK(!RunScript(dir, "SHGetFolderPath()\n").ok);
  FAF_CHECK(!RunScript(dir, "dofile(InitFileDir .. '/missing.lua')\n").ok);
  FAF_CHECK(!RunScript(dir, "io.output(InitFileDir .. '/out.txt')\n").ok);
  FAF_CHECK(RunScript(dir, "if os.setlocale('de_DE') ~= nil then error('locale changed') end\n").ok);
  FAF_CHECK(RunScript(dir, "local f, msg = loadfile(InitFileDir .. '/missing.lua')\nassert(f == nil and msg)\n").ok);
  FAF_CHECK(RunScript(dir, "assert(towstring == nil, 'wide strings must stay unreachable')\n").ok);

  DataPathOptions options;
  options.scriptPath = dir / "does-not-exist.lua";
  result = RunDataPathScript(options);
  FAF_CHECK(!result.ok);
  FAF_CHECK(result.error.find("not found") != std::string::npos);

  // No LOCAL_APPDATA configured: the binding refuses instead of returning "/".
  WriteFile(dir / "folders.lua", "SHGetFolderPath('LOCAL_APPDATA')\n");
  options.scriptPath = dir / "folders.lua";
  result = RunDataPathScript(options);
  FAF_CHECK(!result.ok);
  FAF_CHECK(result.error.find("no directory configured") != std::string::npos);
}

FAF_TEST(NumbersAreTheGamesFloats)
{
  TempDir dir;
  std::vector<std::string> log;
  // float lua_Number, and '^' is bitwise XOR as in the game's VM (OPR_POW
  // compiles to kOpBXor in src/sdk/lua); FAF scripts use math.pow for powers.
  const DataPathResult result =
    RunScript(dir, "LOG(16777217, 1/3, 16777216 + 1 == 16777216, 2^24, math.pow(2, 24))\n", &log);
  FAF_REQUIRE(result.ok);
  FAF_REQUIRE(log.size() == 1);
  FAF_CHECK_EQ(log[0], std::string("16777216\t0.333333343\ttrue\t26\t16777216"));
}

FAF_TEST(CheckLuaSyntaxAcceptsFafDialect)
{
  std::string error;
  const auto ok = [&](const std::string_view source) {
    return CheckLuaSyntax(source.data(), source.size(), "test.lua", &error);
  };
  FAF_CHECK(ok("return 1 != 2"));
  FAF_CHECK(ok("\xEF\xBB\xBFlocal x = 1"));
  FAF_CHECK(ok("\xEF\xBB\xBF"));                               // a file that is only a BOM
  FAF_CHECK(ok("for i = 1, 2 do if i == 1 then continue end end"));
  FAF_CHECK(ok("local t = {} for k, v in t do end"));
  FAF_CHECK(ok("local s = '\xC3\xA4\xFF\x80' -- \xE4 in a comment"));
  FAF_CHECK(ok("local s = L\"x\""));                           // the name L called with "x", as in the engine
  FAF_CHECK(ok(""));
  FAF_CHECK(CheckLuaSyntax(nullptr, 0, "empty", &error));

  FAF_CHECK(!ok("x = 1 \xC3\xA4"));                             // high bytes outside strings
  FAF_CHECK(error.find("invalid character") != std::string::npos);
  FAF_CHECK(!ok("\xEF\xBB x = 1"));                             // a broken BOM
  FAF_CHECK(!ok("x = !y"));
  FAF_CHECK(!ok("x = = 1"));
  FAF_CHECK(error.rfind("test.lua:1:", 0) == 0);
  FAF_CHECK(!CheckLuaSyntax("x = = 1", 7, "=custom", &error));
  FAF_CHECK(error.rfind("custom:1:", 0) == 0);
}

FAF_TEST(LoadFileHandlesBomsAndHighBytes)
{
  // luaL_loadfile itself (getF / getWF / the UTF-16 BOM test in the patch);
  // the port's dofile reads through fs::ReadWholeFile instead.
  TempDir dir;
  const std::u16string utf16 = u"﻿return 40 + 2 != 1";
  std::string utf16Bytes;
  for (const char16_t unit : utf16) {
    utf16Bytes.push_back(static_cast<char>(unit & 0xFF));
    utf16Bytes.push_back(static_cast<char>(unit >> 8));
  }
  WriteFile(dir / "utf16.lua", utf16Bytes);
  WriteFile(dir / "utf8bom.lua", "\xEF\xBB\xBFreturn 7");
  WriteFile(dir / "latin1.lua", "-- \xE4\xF6\xFC\nreturn 'caf\xE9'");

  lua_State* const L = lua_open();
  FAF_REQUIRE(L != nullptr);
  const auto run = [&](const std::string& path) -> std::string {
    lua_settop(L, 0);
    if (luaL_loadfile(L, path.c_str()) != 0 || lua_pcall(L, 0, 1, 0) != 0) {
      return std::string("error: ") + (lua_tostring(L, -1) ? lua_tostring(L, -1) : "?");
    }
    if (lua_type(L, -1) == LUA_TBOOLEAN) {
      return lua_toboolean(L, -1) ? "true" : "false";
    }
    return lua_tostring(L, -1) ? lua_tostring(L, -1) : "nil";
  };
  FAF_CHECK_EQ(run(dir / "utf16.lua"), std::string("true"));
  FAF_CHECK_EQ(run(dir / "utf8bom.lua"), std::string("7"));
  FAF_CHECK_EQ(run(dir / "latin1.lua"), std::string("caf\xE9"));
  lua_close(L);
}
