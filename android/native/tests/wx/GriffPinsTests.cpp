// The touch scheme Griff's order pins (include/GriffPins.h): each kind's colour, a pin's life and fade, and the limit.

#include <cstddef>
#include <cstdint>
#include <vector>

#include <GriffPins.h>

#include "Check.h"

namespace
{
  using griffpins::Alpha;
  using griffpins::Kind;
  using griffpins::KindOf;
  using griffpins::Pin;
  using griffpins::Pins;
} // namespace

WXT_TEST(GriffPinsShowWholeThenFadeOverTheirLast400Milliseconds)
{
  WXT_CHECK(Alpha(0) == 1.0f);
  WXT_CHECK(Alpha(1100) == 1.0f);
  WXT_CHECK(Alpha(1300) == 0.5f);
  WXT_CHECK(Alpha(1499) > 0.0f && Alpha(1499) < 0.01f);
  WXT_CHECK(Alpha(1500) == 0.0f);
  WXT_CHECK(Alpha(-1) == 0.0f);
}

WXT_TEST(GriffPinsLiveFor1500Milliseconds)
{
  Pins pins;
  pins.Add(1, 2, 3, Kind::kMove, 1000);
  WXT_CHECK_EQ(pins.Live(1000).size(), std::size_t{1});
  WXT_CHECK_EQ(pins.Live(2499).size(), std::size_t{1});
  WXT_CHECK(pins.Live(2500).empty());
  pins.Clear();
  WXT_CHECK(pins.Live(1000).empty());
}

WXT_TEST(GriffPinsAreAtMost64TheNewestInTheOldestsPlace)
{
  Pins pins;
  for (std::int64_t born = 0; born < 65; ++born) {
    pins.Add(static_cast<float>(born), 0, 0, Kind::kMove, born);
  }
  const std::vector<Pin> live = pins.Live(100);
  WXT_CHECK_EQ(live.size(), std::size_t{64});
  if (live.size() == 64) {
    WXT_CHECK_EQ(live.front().born, std::int64_t{1});
    WXT_CHECK_EQ(live.back().born, std::int64_t{64});
  }
}

WXT_TEST(GriffPinsColourEachKindOfOrder)
{
  WXT_CHECK_EQ(KindOf("Move", false), Kind::kMove);
  WXT_CHECK_EQ(KindOf("FormMove", false), Kind::kMove);
  WXT_CHECK_EQ(KindOf("Script", true), Kind::kAttack);
  WXT_CHECK_EQ(KindOf("AggressiveMove", false), Kind::kAttack);
  WXT_CHECK_EQ(KindOf("Attack", false), Kind::kAttack);
  WXT_CHECK_EQ(KindOf("Capture", false), Kind::kAttack);
  WXT_CHECK_EQ(KindOf("Patrol", false), Kind::kPatrol);
  WXT_CHECK_EQ(KindOf("BuildMobile", false), Kind::kBuild);
  WXT_CHECK_EQ(KindOf("Guard", false), Kind::kBuild);
  WXT_CHECK_EQ(KindOf("Repair", false), Kind::kBuild);
  WXT_CHECK_EQ(KindOf("Reclaim", false), Kind::kBuild);
}
