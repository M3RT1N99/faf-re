// The device's classes of cores and where the game's threads go by them (include/CoreClasses.h): the rating sysfs gives each
// CPU, the classes, the plan for the sim's thread and the others, its line in the report, what /proc says of a thread, and
// the sim's speed lines.

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <CoreClasses.h>

#include "Check.h"

namespace
{
  using coreclasses::CpuSet;
  using coreclasses::Source;

  /// The text of one file for each CPU from cpu0 on, `count` times `rating`, then the next.
  std::vector<std::string> Texts(const std::vector<std::pair<int, const char*>>& runs)
  {
    std::vector<std::string> texts;
    for (const auto& [count, rating] : runs) {
      texts.insert(texts.end(), static_cast<std::size_t>(count), rating);
    }
    return texts;
  }

  /// A thread's stat line as Linux writes it, `fields` of them, the name with a space and a parenthesis in it.
  std::string StatLine(const int processor, const int fields = 52)
  {
    std::string line = "4321 (Sim (2) x)";
    for (int field = 3; field <= fields; ++field) {
      std::string value = "0";
      if (field == 3) {
        value = "R";
      } else if (field == 14) {
        value = "150"; // utime
      } else if (field == 15) {
        value = "25"; // stime
      } else if (field == 19) {
        value = "-10"; // nice: a number the parse passes over
      } else if (field == 22) {
        value = "98765"; // starttime
      } else if (field == 39) {
        value = std::to_string(processor);
      }
      line += " " + value;
    }
    return line + "\n";
  }
} // namespace

WXT_TEST(ARatingIsTheNumberAloneWithItsLineEnd)
{
  WXT_CHECK_EQ(coreclasses::Rating("1024\n"), 1024U);
  WXT_CHECK_EQ(coreclasses::Rating("2803200"), 2803200U);
  WXT_CHECK_EQ(coreclasses::Rating(""), 0U);
  WXT_CHECK_EQ(coreclasses::Rating("0\n"), 0U);
  WXT_CHECK_EQ(coreclasses::Rating("max\n"), 0U);
  WXT_CHECK_EQ(coreclasses::Rating("12 34\n"), 0U);
  WXT_CHECK_EQ(coreclasses::Rating("-5\n"), 0U);
  WXT_CHECK_EQ(coreclasses::Rating("99999999999999999999\n"), 0U);
}

WXT_TEST(AThreeClassPhonePutsTheSimOnTheBigAndMidCoresAndTheRestOffTheLittleOnes)
{
  const coreclasses::Reading reading = coreclasses::Read(Texts({{4, "380\n"}, {3, "860\n"}, {1, "1024\n"}}), {});
  WXT_CHECK(reading.source == Source::kCapacity);
  WXT_CHECK_EQ(reading.classes.size(), 3U);
  WXT_CHECK_EQ(reading.classes[0].rating, 1024U);
  WXT_CHECK_EQ(reading.classes[0].cpus, CpuSet{0x80});
  WXT_CHECK_EQ(reading.classes[1].cpus, CpuSet{0x70});
  WXT_CHECK_EQ(reading.classes[2].cpus, CpuSet{0x0F});
  WXT_CHECK_EQ(reading.all, CpuSet{0xFF});

  const coreclasses::Plan plan = coreclasses::PlanFor(reading);
  WXT_CHECK(plan.Places());
  WXT_CHECK_EQ(plan.sim, CpuSet{0xF0});
  WXT_CHECK_EQ(plan.simFirst, CpuSet{0x80});
  WXT_CHECK_EQ(plan.others, CpuSet{0xF0});
  WXT_CHECK(plan.offLittle);
  WXT_CHECK_EQ(coreclasses::PlacementLine(reading, plan),
    "cores: big cpu7 (capacity 1024), mid cpu4-6 (capacity 860), little cpu0-3 (capacity 380) by cpu_capacity; the sim's "
    "thread on cpu4-7, starting on cpu7; the render thread, the issue thread and the workers on cpu4-7, off the little "
    "cores, which the engine sizes its worker pools by; FAF's init's mask is not taken");
}

WXT_TEST(WithoutCapacitiesTheRatedClocksTellTheClasses)
{
  // The Galaxy S22 Ultra's clocks as the app's replay test reported them: 4 at 1.82 GHz, 3 at 2.52 GHz, 1 at 2.80 GHz.
  const coreclasses::Reading reading = coreclasses::Read(
    Texts({{8, ""}}), Texts({{4, "1824000\n"}, {3, "2520000\n"}, {1, "2803200\n"}}));
  WXT_CHECK(reading.source == Source::kMaxFrequency);
  const coreclasses::Plan plan = coreclasses::PlanFor(reading);
  WXT_CHECK_EQ(plan.sim, CpuSet{0xF0});
  WXT_CHECK_EQ(plan.simFirst, CpuSet{0x80});
  WXT_CHECK_EQ(plan.others, CpuSet{0xF0});
  WXT_CHECK_EQ(coreclasses::PlacementLine(reading, plan),
    "cores: big cpu7 (2.80 GHz), mid cpu4-6 (2.52 GHz), little cpu0-3 (1.82 GHz) by cpuinfo_max_freq; the sim's thread on "
    "cpu4-7, starting on cpu7; the render thread, the issue thread and the workers on cpu4-7, off the little cores, which "
    "the engine sizes its worker pools by; FAF's init's mask is not taken");
}

WXT_TEST(ACpuWithoutItsRatingTakesTheOtherFileOrNone)
{
  const std::vector<std::string> capacities = Texts({{4, "380\n"}, {3, "860\n"}, {1, ""}});
  const std::vector<std::string> clocks = Texts({{4, "1824000\n"}, {3, "2520000\n"}, {1, "2803200\n"}});
  WXT_CHECK(coreclasses::Read(capacities, clocks).source == Source::kMaxFrequency);

  const coreclasses::Reading none = coreclasses::Read(capacities, Texts({{7, "1824000\n"}, {1, "unknown\n"}}));
  WXT_CHECK(none.source == Source::kNone);
  WXT_CHECK(none.classes.empty());
  WXT_CHECK_EQ(none.all, CpuSet{0});
  WXT_CHECK(!coreclasses::PlanFor(none).Places());
  WXT_CHECK_EQ(coreclasses::PlacementLine(none, coreclasses::PlanFor(none)),
    "cores: sysfs rates no CPU (cpu_capacity, cpufreq's cpuinfo_max_freq), so the threads go where FAF's init and Android "
    "put them");
  WXT_CHECK(coreclasses::Read({}, {}).source == Source::kNone);
}

WXT_TEST(OneClassKeepsFafsWay)
{
  const coreclasses::Reading reading = coreclasses::Read(Texts({{8, "1024\n"}}), {});
  WXT_CHECK_EQ(reading.classes.size(), 1U);
  const coreclasses::Plan plan = coreclasses::PlanFor(reading);
  WXT_CHECK(!plan.Places());
  WXT_CHECK_EQ(plan.others, CpuSet{0});
  WXT_CHECK_EQ(coreclasses::PlacementLine(reading, plan),
    "cores: cpu0-7 (capacity 1024) by cpu_capacity: one class, so the threads go where FAF's init and Android put them");
}

WXT_TEST(TwoClassesPutTheSimOnTheBigOnesAlone)
{
  const coreclasses::Reading reading = coreclasses::Read(Texts({{4, "500\n"}, {4, "1024\n"}}), {});
  const coreclasses::Plan plan = coreclasses::PlanFor(reading);
  WXT_CHECK_EQ(plan.sim, CpuSet{0xF0});
  WXT_CHECK_EQ(plan.simFirst, CpuSet{0xF0});
  WXT_CHECK_EQ(plan.others, CpuSet{0xF0});
  WXT_CHECK(plan.offLittle);
  WXT_CHECK_EQ(coreclasses::PlacementLine(reading, plan),
    "cores: big cpu4-7 (capacity 1024), little cpu0-3 (capacity 500) by cpu_capacity; the sim's thread on cpu4-7; the "
    "render thread, the issue thread and the workers on cpu4-7, off the little cores, which the engine sizes its worker "
    "pools by; FAF's init's mask is not taken");
}

WXT_TEST(FewBigCoresLeaveTheOtherThreadsOnEveryCore)
{
  const coreclasses::Reading reading = coreclasses::Read(Texts({{6, "400\n"}, {2, "1024\n"}}), {});
  const coreclasses::Plan plan = coreclasses::PlanFor(reading);
  WXT_CHECK_EQ(plan.sim, CpuSet{0xC0});
  WXT_CHECK_EQ(plan.others, CpuSet{0xFF});
  WXT_CHECK(!plan.offLittle);
  WXT_CHECK_EQ(coreclasses::PlacementLine(reading, plan),
    "cores: big cpu6-7 (capacity 1024), little cpu0-5 (capacity 400) by cpu_capacity; the sim's thread on cpu6-7; the "
    "render thread, the issue thread and the workers on every core, as 6 of the 8 are little; FAF's init's mask is not "
    "taken");
}

WXT_TEST(FourClassesGiveTheSimTheTwoFastestWhereverTheyLie)
{
  // Two mid classes of their own ratings, the faster one on lower CPUs than the slower.
  const coreclasses::Reading reading =
    coreclasses::Read(Texts({{3, "400\n"}, {2, "900\n"}, {2, "860\n"}, {1, "1024\n"}}), {});
  WXT_CHECK_EQ(reading.classes.size(), 4U);
  WXT_CHECK_EQ(reading.classes[1].cpus, CpuSet{0x18});
  WXT_CHECK_EQ(reading.classes[2].cpus, CpuSet{0x60});
  const coreclasses::Plan plan = coreclasses::PlanFor(reading);
  WXT_CHECK_EQ(plan.sim, CpuSet{0x98});
  WXT_CHECK_EQ(plan.simFirst, CpuSet{0x80});
  WXT_CHECK_EQ(plan.others, CpuSet{0xF8});
  WXT_CHECK_EQ(coreclasses::PlacementLine(reading, plan),
    "cores: cpu7 (capacity 1024), cpu3-4 (capacity 900), cpu5-6 (capacity 860), cpu0-2 (capacity 400) by cpu_capacity; "
    "the sim's thread on cpu3-4,7, starting on cpu7; the render thread, the issue thread and the workers on cpu3-7, off "
    "the little cores, which the engine sizes its worker pools by; FAF's init's mask is not taken");
}

WXT_TEST(MoreCpusThanASetHoldsGiveNoClasses)
{
  WXT_CHECK_EQ(coreclasses::Read(Texts({{64, "1024\n"}}), {}).all, ~CpuSet{0});
  WXT_CHECK(coreclasses::Read(Texts({{32, "400\n"}, {33, "1024\n"}}), {}).source == Source::kNone);
}

WXT_TEST(ACpuListIsTheKernels)
{
  WXT_CHECK_EQ(coreclasses::CpuList(0), "none");
  WXT_CHECK_EQ(coreclasses::CpuList(0xF0), "4-7");
  WXT_CHECK_EQ(coreclasses::CpuList(0x80), "7");
  WXT_CHECK_EQ(coreclasses::CpuList(0x75), "0,2,4-6");
  WXT_CHECK_EQ(coreclasses::CpuList(CpuSet{1} << 63), "63");
  WXT_CHECK_EQ(coreclasses::CpuList(~CpuSet{0}), "0-63");
}

WXT_TEST(OnlyCpusThePlanDidNotGiveMoveAThread)
{
  using coreclasses::Look;
  using coreclasses::LookAt;
  constexpr CpuSet kPlaced = 0xF0;
  WXT_CHECK(LookAt(0xF0, kPlaced, 0xF0) == Look::kOwn);
  // The kernel answers the active CPUs alone: a paused core, or one gone off, takes none of the plan's away.
  WXT_CHECK(LookAt(0x70, kPlaced, 0xF0) == Look::kOwn);
  WXT_CHECK(LookAt(0xF0, kPlaced, 0x70) == Look::kOwn);
  // Android set the CPUs to the top-app group's, or to the back's, which hold none of the plan's.
  WXT_CHECK(LookAt(0xFF, kPlaced, 0xF0) == Look::kMoved);
  WXT_CHECK(LookAt(0x0F, kPlaced, 0xF0) == Look::kMoved);
  // Where the back's group held the threads at the last look, they are held still, and the front's group moves them.
  WXT_CHECK(LookAt(0x0F, kPlaced, 0x0F) == Look::kHeld);
  WXT_CHECK(LookAt(0xFF, kPlaced, 0x0F) == Look::kMoved);
  // A thread on every CPU is never moved.
  WXT_CHECK(LookAt(0x0F, 0xFF, 0xFF) == Look::kOwn);
}

WXT_TEST(AThreadsStatGivesItsCpuItsTimeAndItsStart)
{
  const coreclasses::ThreadStat stat = coreclasses::ParseStat(StatLine(7));
  WXT_CHECK(stat.valid);
  WXT_CHECK_EQ(stat.processor, 7);
  WXT_CHECK_EQ(stat.cpuTicks, 175U);
  WXT_CHECK_EQ(stat.startTime, 98765U);
  WXT_CHECK_EQ(coreclasses::ParseStat(StatLine(0, 39)).processor, 0);

  WXT_CHECK(!coreclasses::ParseStat(StatLine(7, 38)).valid);
  WXT_CHECK(!coreclasses::ParseStat(StatLine(64)).valid);
  WXT_CHECK(!coreclasses::ParseStat("").valid);
  WXT_CHECK(!coreclasses::ParseStat("4321 (Sim) R 1 2").valid);
}

WXT_TEST(SchedstatsFirstNumberIsTheThreadsTimeOnACpu)
{
  std::uint64_t nanos = 0;
  WXT_CHECK(coreclasses::ParseRunNanos("123456789 2345 67\n", nanos));
  WXT_CHECK_EQ(nanos, 123456789U);
  WXT_CHECK(!coreclasses::ParseRunNanos("", nanos));
  WXT_CHECK(!coreclasses::ParseRunNanos("x 1 2\n", nanos));
}

WXT_TEST(ACpusClassIsTheOneThatHoldsIt)
{
  const coreclasses::Reading reading = coreclasses::Read(Texts({{4, "380\n"}, {3, "860\n"}, {1, "1024\n"}}), {});
  WXT_CHECK_EQ(coreclasses::ClassOf(reading.classes, 7), 0);
  WXT_CHECK_EQ(coreclasses::ClassOf(reading.classes, 5), 1);
  WXT_CHECK_EQ(coreclasses::ClassOf(reading.classes, 0), 2);
  WXT_CHECK_EQ(coreclasses::ClassOf(reading.classes, 8), -1);
  WXT_CHECK_EQ(coreclasses::ClassOf(reading.classes, -1), -1);
  WXT_CHECK_EQ(coreclasses::ClassOf(reading.classes, 64), -1);
}

WXT_TEST(ASpeedLineHasTheTicksASecondAndWhereTheSimsThreadRan)
{
  const coreclasses::Reading reading = coreclasses::Read(Texts({{4, "380\n"}, {3, "860\n"}, {1, "1024\n"}}), {});
  coreclasses::SpeedWindow window;
  window.seconds = 30.0;
  window.firstTick = 6120;
  window.lastTick = 6228;
  window.runNanos = 29'100'000'000;
  window.classNanos = {20'661'000'000, 8'439'000'000, 0};
  window.moved = true;
  window.headroom = 0.78f;
  WXT_CHECK_EQ(coreclasses::SpeedLine(window, reading.classes),
    "sim: 3.6 ticks a second over 30 s (ticks 6120 to 6228), in a replay's move; its thread on a CPU 97% of the time: big "
    "71%, mid 29%, little 0%; thermal headroom 0.78");

  // One class has no shares to tell, and a phone without a headroom gives none.
  const coreclasses::Reading one = coreclasses::Read(Texts({{8, "1024\n"}}), {});
  coreclasses::SpeedWindow plain;
  plain.seconds = 30.2;
  plain.lastTick = 302;
  plain.runNanos = 15'100'000'000;
  plain.classNanos = {15'100'000'000};
  plain.headroom = std::numeric_limits<float>::quiet_NaN();
  WXT_CHECK_EQ(coreclasses::SpeedLine(plain, one.classes),
    "sim: 10.0 ticks a second over 30 s (ticks 0 to 302); its thread on a CPU 50% of the time");
}
