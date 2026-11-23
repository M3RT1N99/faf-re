#pragma once

// The touch scheme Griff's draft as the screen shows it (buildstage/griff-v3-spec.md, T2): buildings planned on the map before
// anything is ordered. The program holds the plans at world places (include/GriffDraft.h) and gives each frame where they are
// on the picture (GriffAndroid.cpp PublishDraft); here, in the screen's pixels: whether the draft is open and whether its
// building stands on a deposit, each plan's place, the box its ground takes and its flags, the line a finger would add and the
// plan a resting finger would place. And which plan a finger is on. FAF's UI draws them (griffmap.lua), the tick and the cross
// too (griffhud.lua).

#include <array>

namespace touch
{
  class Draft
  {
  public:
    /// How many numbers the program gives at most.
    static constexpr int kValues = 512;
    /// A place's flags, as the program gives them.
    static constexpr int kNewest = 1;
    static constexpr int kOnDeposit = 2;
    static constexpr int kTaken = 4;
    /// Its middle on the free map: not under a panel, which a finger there would be on.
    static constexpr int kFree = 8;
    /// A plan's box on the screen is at least this wide, for the eye as for a finger (the mockup's 2.3 mm).
    static constexpr float kLeastMm = 2.3f;

    /// From the picture's pixels to the screen's.
    class Screen
    {
    public:
      virtual ~Screen() = default;
      [[nodiscard]] virtual float X(float pictureX) const = 0;
      [[nodiscard]] virtual float Y(float pictureY) const = 0;
    };

    /// The program's numbers, `count` of them (at most kValues: the rest are no places it gives), in the picture's pixels:
    /// what is shown from now on. Whether anything changed. Numbers that are not whole places are left out.
    bool Update(const float* values, int count, const Screen& screen);

    [[nodiscard]] bool Open() const
    {
      return mOpen;
    }

    /// Whether the building of the next plans stands on a deposit: a box with Box gives each free deposit in it a plan.
    [[nodiscard]] bool PicksDeposit() const
    {
      return mPicksDeposit;
    }

    [[nodiscard]] int Plans() const
    {
      return mPlans;
    }

    [[nodiscard]] int LineCount() const
    {
      return mLine;
    }

    [[nodiscard]] bool AimShown() const
    {
      return mAim > 0;
    }

    /// Whether the finger that rests is on the map, not on FAF's UI: the loupe is for the map.
    [[nodiscard]] bool RestsOnMap() const
    {
      return mRestsOnMap;
    }

    /// The plans are places 0 to Plans() - 1, the line's follow, then the aim's.
    [[nodiscard]] int LineItem(const int at) const
    {
      return mPlans + at;
    }

    [[nodiscard]] int AimItem() const
    {
      return mPlans + mLine;
    }

    [[nodiscard]] int Id(int item) const;
    [[nodiscard]] float X(int item) const;
    [[nodiscard]] float Y(int item) const;
    [[nodiscard]] float Left(int item) const;
    [[nodiscard]] float Top(int item) const;
    [[nodiscard]] float Right(int item) const;
    [[nodiscard]] float Bottom(int item) const;
    [[nodiscard]] bool Has(int item, int flag) const;

    /// The plan a finger here is on, the newest first (the mockup's planAt): within its box, the ground it takes, whose edge
    /// the map shows as FAF's build mode does, at least `least` each way from its middle; nothing beyond, where a tap or a rest
    /// places a building against it. Only one whose middle is on the free map, as a finger on a panel over a plan is on the
    /// panel. Its number, or 0.
    [[nodiscard]] int PlanAt(float fingerX, float fingerY, float least) const;

    /// Whether the plan numbered `id` is the newest, from which a drag draws a line.
    [[nodiscard]] bool Newest(int id) const;

    /// Whether the plan numbered `id` stands on a deposit: it neither starts a line nor moves with the draft.
    [[nodiscard]] bool OnDeposit(int id) const;

    /// Where the loupe's copy of the game's picture begins on one axis: the square of `side` pixels about the finger at
    /// `finger`, kept on the picture of `extent` pixels; in the picture's pixels. Within half the side of an edge its middle is
    /// not the finger's, and the loupe shows the copy where it was taken.
    [[nodiscard]] static int LoupeFrom(int finger, int side, int extent);

  private:
    // The program's numbers (GriffAndroid.cpp PublishDraft): a head, then each place's.
    static constexpr int kOpenAt = 0;
    static constexpr int kPicksDepositAt = 1;
    static constexpr int kPlanCountAt = 2;
    static constexpr int kLineCountAt = 3;
    static constexpr int kAimCountAt = 4;
    static constexpr int kRestsOnMapAt = 5;
    static constexpr int kHead = 6;
    static constexpr int kItem = 8;
    static constexpr int kIdAt = 0;
    static constexpr int kXAt = 1;
    static constexpr int kYAt = 2;
    static constexpr int kLeftAt = 3;
    static constexpr int kTopAt = 4;
    static constexpr int kRightAt = 5;
    static constexpr int kBottomAt = 6;
    static constexpr int kFlagsAt = 7;

    [[nodiscard]] bool SameAsShown(const float* values, int count) const;
    /// The plan numbered `id`'s place among the plans, or -1.
    [[nodiscard]] int PlanNumbered(int id) const;
    [[nodiscard]] float Value(int item, int at) const;

    std::array<float, kValues> mShown{};
    int mShownCount = 0;
    bool mOpen = false;
    bool mPicksDeposit = false;
    int mPlans = 0;
    int mLine = 0;
    int mAim = 0;
    bool mRestsOnMap = false;
    /// The places in the screen's pixels: the plans, then the line's, then the aim's, kItem numbers each.
    std::array<float, kValues> mItems{};
  };
} // namespace touch
