// CommandLine: CFG_GetArgOption semantics on the launcher's argv tokens.

#include "faf/port/CommandLine.h"

#include "TestHarness.h"

using faf::port::CommandLine;

namespace {
  CommandLine Launch()
  {
    return CommandLine({
      "/init",
      "/sdcard/Android/data/io.github.m3rt1n99.fafre/files/faf/bin/init_faf.lua",
      "/nobugreport",
      "/log",
      "/sdcard/Android/data/io.github.m3rt1n99.fafre/files/logs/game.sclog",
      "/RENDERER",
      "vulkan",
      "/gpgnet",
      "127.0.0.1:7237",
      "/mean",
      "1500",
      "/deviation",
      "500",
      "/init",
      "second.lua",
    });
  }
} // namespace

FAF_TEST(HasMatchesCaseInsensitively)
{
  const CommandLine args = Launch();
  FAF_CHECK(args.Has("/nobugreport"));
  FAF_CHECK(args.Has("/NoBugReport"));
  FAF_CHECK(args.Has("/renderer"));
  FAF_CHECK(!args.Has("/nomovie"));
  FAF_CHECK(!args.Has("nobugreport")); // the '/' is part of the option
  FAF_CHECK(!args.Has(""));
  FAF_CHECK(!CommandLine().Has("/init"));
}

FAF_TEST(ValuesFollowTheFirstOccurrence)
{
  const CommandLine args = Launch();
  // Values may start with '/', as Android paths do.
  FAF_CHECK_EQ(
    *args.Value("/init"), std::string("/sdcard/Android/data/io.github.m3rt1n99.fafre/files/faf/bin/init_faf.lua")
  );
  FAF_CHECK_EQ(*args.Value("/renderer"), std::string("vulkan"));
  FAF_CHECK_EQ(*args.Value("/gpgnet"), std::string("127.0.0.1:7237"));

  const auto two = args.Values("/mean", 2);
  FAF_REQUIRE(two.has_value());
  FAF_CHECK_EQ(two->size(), std::size_t{2});
  FAF_CHECK_EQ((*two)[0], std::string("1500"));
  FAF_CHECK_EQ((*two)[1], std::string("/deviation"));

  const auto none = args.Values("/nobugreport", 0);
  FAF_REQUIRE(none.has_value());
  FAF_CHECK(none->empty());
}

FAF_TEST(MissingValuesGiveNullopt)
{
  const CommandLine args({"/log", "/a.sclog", "/savereplay"});
  FAF_CHECK(!args.Value("/savereplay").has_value());
  FAF_CHECK(!args.Values("/log", 3).has_value());
  FAF_CHECK(!args.Value("/replay").has_value());
  FAF_CHECK(args.Has("/savereplay"));
  FAF_CHECK_EQ(args.Args().size(), std::size_t{3});
}
