// VirtualFileSystem + the zip reader, on archives the test builds itself:
// stored/deflate entries, FAF's zero-length deflate entries, data-descriptor
// skipping, zip64, mount order, directory mounts, and corrupt archives.

#include <algorithm>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "faf/port/Vfs.h"

#include "TestSupport.h"

using faf::port::MountSpec;
using faf::port::VirtualFileSystem;
using faf::test::BuildZip;
using faf::test::TempDir;
using faf::test::WriteFile;
using faf::test::ZipEntrySpec;
using faf::test::ZipMethod;

namespace {
  std::string Pattern(const std::size_t size, const unsigned seed)
  {
    std::string out(size, '\0');
    unsigned state = seed;
    for (char& c : out) {
      state = state * 1103515245u + 12345u;
      c = static_cast<char>("local x = 1 -- abcdefghij\n"[(state >> 16) % 26]);
    }
    return out;
  }

  std::string Read(const VirtualFileSystem& vfs, const std::string& path)
  {
    std::vector<std::uint8_t> bytes;
    std::string error;
    if (!vfs.Read(path, bytes, &error)) {
      return "<error: " + error + ">";
    }
    return std::string(bytes.begin(), bytes.end());
  }

  std::size_t FindCentralHeader(const std::vector<std::uint8_t>& zip, const std::size_t ordinal)
  {
    std::size_t seen = 0;
    for (std::size_t i = 0; i + 4 <= zip.size(); ++i) {
      if (zip[i] == 0x50 && zip[i + 1] == 0x4b && zip[i + 2] == 0x01 && zip[i + 3] == 0x02 && seen++ == ordinal) {
        return i;
      }
    }
    return std::string::npos;
  }
} // namespace

FAF_TEST(ReadsStoredDeflateAndEmptyEntries)
{
  TempDir dir;
  const std::string config = Pattern(5000, 1);
  const std::string texture(70000, '\x7f');
  WriteFile(
    dir / "lua.nx2",
    BuildZip({
      {"lua/", "", ZipMethod::Deflate},
      {"lua/system/config.lua", config, ZipMethod::Deflate},
      {"Textures/UI/Common/Splash.dds", texture, ZipMethod::Stored},
      {"lua/empty.lua", "", ZipMethod::EmptyDeflateNoStream},
      {"lua/emptyblock.lua", "", ZipMethod::Deflate},
      {"lua/descriptor.lua", "skipped", ZipMethod::Deflate, true},
      {"LUA/System/Config.lua", "duplicate loses", ZipMethod::Stored},
    })
  );

  VirtualFileSystem vfs;
  std::string error;
  FAF_REQUIRE(vfs.Mount({dir / "lua.nx2", "/"}, &error) == 1);
  FAF_CHECK(error.empty());
  FAF_REQUIRE(vfs.Mounts().size() == 1);
  FAF_CHECK(vfs.Mounts()[0].isArchive);
  FAF_CHECK_EQ(vfs.Mounts()[0].mountpoint, std::string("/"));
  FAF_CHECK_EQ(vfs.Mounts()[0].entryCount, std::size_t{6}); // the data-descriptor entry is dropped

  FAF_CHECK_EQ(Read(vfs, "/lua/system/config.lua"), config);
  FAF_CHECK_EQ(Read(vfs, "/LUA/SYSTEM/CONFIG.LUA"), config);
  FAF_CHECK_EQ(Read(vfs, "\\lua\\system\\config.lua"), config);
  FAF_CHECK_EQ(Read(vfs, "/textures/ui/common/splash.dds"), texture);
  FAF_CHECK_EQ(Read(vfs, "/lua/empty.lua"), std::string());
  FAF_CHECK_EQ(Read(vfs, "/lua/emptyblock.lua"), std::string());
  FAF_CHECK(!vfs.Find("/lua/descriptor.lua").found);
  FAF_CHECK(!vfs.Find("/lua/").found);
  FAF_CHECK(!vfs.Find("/lua").found);
  FAF_CHECK(!vfs.Find("/").found);

  const faf::port::FileLocation location = vfs.Find("/lua/system/config.lua");
  FAF_CHECK(location.found);
  FAF_CHECK(location.compressed);
  FAF_CHECK_EQ(location.size, std::uint64_t{config.size()});
  FAF_CHECK_EQ(location.entryName, std::string("lua/system/config.lua"));
  FAF_CHECK(!vfs.Find("/textures/ui/common/splash.dds").compressed);

  const std::vector<std::string> entries = vfs.ArchiveEntries(0);
  FAF_CHECK_EQ(entries.size(), std::size_t{5});
  std::vector<std::uint8_t> bytes;
  FAF_CHECK(vfs.ReadArchiveEntry(0, "Lua/System/Config.lua", bytes));
  FAF_CHECK_EQ(bytes.size(), config.size());
  FAF_CHECK(!vfs.ReadArchiveEntry(0, "lua/descriptor.lua", bytes, &error));
  FAF_CHECK(!vfs.ReadArchiveEntry(5, "lua/system/config.lua", bytes, &error));
}

FAF_TEST(FirstMountWinsAndMountpointsNest)
{
  TempDir dir;
  WriteFile(dir / "faf/lua.nx2", BuildZip({{"lua/ui/uiutil.lua", "faf", ZipMethod::Deflate}}));
  WriteFile(
    dir / "scfa/gamedata/lua.scd",
    BuildZip(
      {{"lua/ui/uiutil.lua", "scfa", ZipMethod::Stored}, {"lua/ui/only_scfa.lua", "scfa-only", ZipMethod::Stored}}
    )
  );
  WriteFile(dir / "vault/maps/SCMP_009/SCMP_009_scenario.lua", "scenario");
  WriteFile(dir / "scfa/fonts/ARIAL.TTF", "font");

  VirtualFileSystem vfs;
  FAF_CHECK_EQ(vfs.Mount({dir / "faf/lua.nx2", "/"}), std::size_t{1});
  FAF_CHECK_EQ(vfs.Mount({dir / "scfa/gamedata/lua.scd", "/"}), std::size_t{1});
  FAF_CHECK_EQ(vfs.Mount({dir / "vault/maps/scmp_009", "/maps/SCMP_009"}), std::size_t{1});
  FAF_CHECK_EQ(vfs.Mount({dir / "scfa\\fonts", "/fonts"}), std::size_t{1});

  FAF_CHECK_EQ(Read(vfs, "/lua/ui/uiutil.lua"), std::string("faf"));
  FAF_CHECK_EQ(vfs.Find("/lua/ui/uiutil.lua").mountIndex, std::size_t{0});
  FAF_CHECK_EQ(Read(vfs, "/lua/ui/only_scfa.lua"), std::string("scfa-only"));
  FAF_CHECK_EQ(vfs.Mounts()[2].mountpoint, std::string("/maps/scmp_009/"));
  FAF_CHECK_EQ(Read(vfs, "/maps/scmp_009/scmp_009_scenario.lua"), std::string("scenario"));
  FAF_CHECK_EQ(Read(vfs, "/fonts/arial.ttf"), std::string("font"));
  const faf::port::FileLocation font = vfs.Find("/fonts/arial.ttf");
  FAF_CHECK(font.found);
  FAF_CHECK(font.entryName.empty());
  FAF_CHECK_EQ(font.size, std::uint64_t{4});
  FAF_CHECK(!vfs.Find("/maps/scmp_0090/x").found);

  FAF_CHECK_EQ(vfs.List("/lua/ui"), (std::vector<std::string>{"uiutil.lua", "only_scfa.lua"}));
  FAF_CHECK_EQ(vfs.List("/LUA/UI/", "only*"), (std::vector<std::string>{"only_scfa.lua"}));
  FAF_CHECK_EQ(vfs.List("/fonts"), (std::vector<std::string>{"ARIAL.TTF"}));
  FAF_CHECK(vfs.List("/maps").empty()); // the map mount is deeper than /maps
}

FAF_TEST(DirectoryMountsStayInside)
{
  TempDir dir;
  WriteFile(dir / "secret.txt", "secret");
  WriteFile(dir / "mounted/inner/file.txt", "file");
  VirtualFileSystem vfs;
  FAF_REQUIRE(vfs.Mount({dir / "mounted", "/maps/x"}) == 1);
  FAF_CHECK_EQ(Read(vfs, "/maps/x/inner/file.txt"), std::string("file"));
  FAF_CHECK(!vfs.Find("/maps/x/../secret.txt").found);
  FAF_CHECK(!vfs.Find("/maps/x/inner/../../secret.txt").found);
  FAF_CHECK(!vfs.Find("/maps/x/inner/../inner/file.txt").found);
  FAF_CHECK(vfs.List("/maps/x/inner/..").empty());
}

FAF_TEST(WildcardMountsExpandInNtfsOrder)
{
  TempDir dir;
  WriteFile(dir / "gamedata/b.scd", BuildZip({{"x.lua", "b", ZipMethod::Stored}}));
  WriteFile(dir / "gamedata/A.scd", BuildZip({{"x.lua", "a", ZipMethod::Stored}}));
  WriteFile(dir / "gamedata/not_a_zip.scd", "this is not a zip archive at all");
  WriteFile(dir / "gamedata/readme.txt", "ignored");

  VirtualFileSystem vfs;
  std::string error;
  FAF_CHECK_EQ(vfs.Mount({dir / "bin/../gamedata/*.scd", "/"}, &error), std::size_t{2});
  FAF_CHECK(error.find("not_a_zip.scd") != std::string::npos);
  FAF_REQUIRE(vfs.Mounts().size() == 2);
  FAF_CHECK(vfs.Mounts()[0].diskPath.ends_with("/A.scd"));
  FAF_CHECK(vfs.Mounts()[1].diskPath.ends_with("/b.scd"));
  FAF_CHECK_EQ(Read(vfs, "/x.lua"), std::string("a"));

  error = "stale";
  FAF_CHECK_EQ(vfs.Mount({dir / "missing/dir", "/"}, &error), std::size_t{0});
  FAF_CHECK(error.empty()); // missing paths are skipped, not errors
  FAF_CHECK_EQ(vfs.Mount({dir / "missing/*.scd", "/"}, &error), std::size_t{0});
  FAF_CHECK_EQ(vfs.Mount({"", "/"}, &error), std::size_t{0});
}

FAF_TEST(ReadsZip64AndCommentedArchives)
{
  TempDir dir;
  const std::string data = Pattern(100000, 7);
  WriteFile(
    dir / "zip64.nx2",
    BuildZip({{"a/stored.bin", data, ZipMethod::Stored}, {"a/deflated.lua", data, ZipMethod::Deflate}}, true)
  );
  WriteFile(
    dir / "comment.scd", BuildZip({{"c.lua", "commented", ZipMethod::Stored}}, false, "PK\x05\x06 not an end record")
  );

  VirtualFileSystem vfs;
  std::string error;
  FAF_REQUIRE(vfs.Mount({dir / "zip64.nx2", "/"}, &error) == 1);
  FAF_REQUIRE(vfs.Mount({dir / "comment.scd", "/"}, &error) == 1);
  FAF_CHECK_EQ(Read(vfs, "/a/stored.bin"), data);
  FAF_CHECK_EQ(Read(vfs, "/a/deflated.lua"), data);
  FAF_CHECK_EQ(Read(vfs, "/c.lua"), std::string("commented"));
}

FAF_TEST(RejectsEntriesThatLieAboutTheirSize)
{
  TempDir dir;
  const std::string data = Pattern(4000, 3);
  std::vector<std::uint8_t> zip =
    BuildZip({{"too_big.lua", data, ZipMethod::Deflate}, {"ok.lua", "ok", ZipMethod::Stored}});

  // Declared one byte smaller than the stream inflates to.
  std::vector<std::uint8_t> smaller = zip;
  const std::size_t header = FindCentralHeader(smaller, 0);
  FAF_REQUIRE(header != std::string::npos);
  const auto declared = static_cast<std::uint32_t>(data.size() - 1);
  std::memcpy(&smaller[header + 24], &declared, 4); // little-endian host (x86, arm64)
  WriteFile(dir / "smaller.nx2", smaller);

  // Declared far larger than deflate can produce from the compressed bytes.
  std::vector<std::uint8_t> huge = zip;
  const std::uint32_t absurd = 0xF0000000u;
  std::memcpy(&huge[header + 24], &absurd, 4);
  WriteFile(dir / "huge.nx2", huge);

  for (const char* name : {"smaller.nx2", "huge.nx2"}) {
    VirtualFileSystem vfs;
    FAF_REQUIRE(vfs.Mount({dir / name, "/"}) == 1);
    std::vector<std::uint8_t> bytes;
    std::string error;
    FAF_CHECK(!vfs.Read("/too_big.lua", bytes, &error));
    FAF_CHECK(!error.empty());
    FAF_CHECK(bytes.empty());
    FAF_CHECK_EQ(Read(vfs, "/ok.lua"), std::string("ok"));
  }
}

FAF_TEST(TruncatedAndCorruptArchivesFailCleanly)
{
  // Every prefix of a valid archive, and every byte set to 0xFF or with its
  // top bit flipped: opening and reading must fail with a message or succeed -
  // never crash or read out of bounds (run under ASan to see the latter).
  TempDir dir;
  const std::string text = Pattern(900, 11);
  const std::vector<std::uint8_t> zip = BuildZip({
    {"lua/a.lua", text, ZipMethod::Deflate},
    {"lua/b.lua", text.substr(0, 300), ZipMethod::Stored},
    {"lua/", "", ZipMethod::Deflate},
    {"lua/c.lua", "", ZipMethod::EmptyDeflateNoStream},
    {"lua/d.lua", "descriptor", ZipMethod::Deflate, true},
  });
  const std::string path = dir / "fuzz.nx2";

  std::size_t opened = 0;
  const auto exercise = [&](const std::vector<std::uint8_t>& bytes) {
    WriteFile(path, bytes);
    VirtualFileSystem vfs;
    std::string error;
    if (vfs.Mount({path, "/"}, &error) == 0) {
      FAF_CHECK(!error.empty());
      return;
    }
    ++opened;
    std::vector<std::uint8_t> out;
    for (const std::string& entry : vfs.ArchiveEntries(0)) {
      if (!vfs.ReadArchiveEntry(0, entry, out, &error)) {
        FAF_CHECK(out.empty());
      }
    }
    (void)vfs.List("/lua");
    (void)vfs.Find("/lua/a.lua");
  };

  for (std::size_t length = 0; length < zip.size(); ++length) {
    exercise(std::vector<std::uint8_t>(zip.begin(), zip.begin() + static_cast<std::ptrdiff_t>(length)));
  }
  for (std::size_t i = 0; i < zip.size(); ++i) {
    for (const bool saturate : {true, false}) {
      std::vector<std::uint8_t> corrupt = zip;
      corrupt[i] = saturate ? std::uint8_t{0xFF} : static_cast<std::uint8_t>(corrupt[i] ^ 0x80);
      exercise(corrupt);
    }
  }
  FAF_CHECK(opened > 0);
}

FAF_TEST(ConcurrentReadsShareOneArchive)
{
  TempDir dir;
  std::vector<ZipEntrySpec> entries;
  for (unsigned i = 0; i < 16; ++i) {
    entries.push_back(
      {"f" + std::to_string(i) + ".lua", Pattern(20000 + i * 97, i), i % 2 ? ZipMethod::Stored : ZipMethod::Deflate}
    );
  }
  WriteFile(dir / "shared.nx2", BuildZip(entries));
  VirtualFileSystem vfs;
  FAF_REQUIRE(vfs.Mount({dir / "shared.nx2", "/"}) == 1);

  std::vector<int> mismatches(8, 0);
  std::vector<std::thread> threads;
  for (std::size_t t = 0; t < mismatches.size(); ++t) {
    threads.emplace_back([&, t] {
      std::vector<std::uint8_t> bytes;
      for (unsigned round = 0; round < 100; ++round) {
        const ZipEntrySpec& entry = entries[(round + t) % entries.size()];
        if (!vfs.Read("/" + entry.name, bytes) || std::string(bytes.begin(), bytes.end()) != entry.data) {
          ++mismatches[t];
        }
      }
    });
  }
  for (std::thread& thread : threads) {
    thread.join();
  }
  for (const int count : mismatches) {
    FAF_CHECK_EQ(count, 0);
  }
}
