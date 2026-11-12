#pragma once

// The order pins of the touch scheme Griff (android/TOUCH_UI.md, "Griff"; buildstage/griff-v3-spec.md, T3.2): at each place
// an order of Griff's gestures went to, a drop in the colour of its kind for a moment, beside FAF's own blip, which goes
// sooner and does not tell the kinds apart. The program keeps them at world places and shows them through the map's camera
// each frame (src/host/GriffAndroid.cpp); here their kinds, their life and their limit, for their tests
// (tests/wx/GriffPinsTests.cpp).

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace griffpins
{
  /// How long a pin stays, the last part of it fading out, and how many there are at most: a new one takes the oldest's place.
  constexpr std::int64_t kLifeMilliseconds = 1500;
  constexpr std::int64_t kFadeMilliseconds = 400;
  constexpr std::size_t kMax = 64;

  /// A pin's colour, as the activity draws it (GriffLayer): the mockup's own colour for a move, its attack colour for an
  /// attack and an attack-move, its patrol colour, and its build colour for a building and for helping (assist, repair,
  /// reclaim).
  enum class Kind
  {
    kMove = 1,
    kAttack = 2,
    kPatrol = 3,
    kBuild = 4,
  };

  /// The kind of an order FAF's UI gave (its CommandType; `attackMove`: a script order that is FAF's attack-move).
  [[nodiscard]] inline Kind KindOf(const std::string& type, const bool attackMove)
  {
    if (attackMove || type == "Attack" || type == "FormAttack" || type == "AggressiveMove" || type == "FormAggressiveMove"
        || type == "Capture") {
      return Kind::kAttack;
    }
    if (type == "Patrol" || type == "FormPatrol") {
      return Kind::kPatrol;
    }
    if (type == "BuildMobile" || type == "Guard" || type == "Repair" || type == "Reclaim") {
      return Kind::kBuild;
    }
    return Kind::kMove;
  }

  /// How much of a pin shows at this age: all of it, then less over its last kFadeMilliseconds, none from its end.
  [[nodiscard]] inline float Alpha(const std::int64_t age)
  {
    if (age < 0 || age >= kLifeMilliseconds) {
      return 0;
    }
    const std::int64_t left = kLifeMilliseconds - age;
    return left >= kFadeMilliseconds ? 1.0f : static_cast<float>(left) / static_cast<float>(kFadeMilliseconds);
  }

  struct Pin
  {
    float x = 0;
    float y = 0;
    float z = 0;
    Kind kind = Kind::kMove;
    std::int64_t born = 0;
  };

  /// The pins, at most kMax: a new one takes the oldest's place.
  class Pins
  {
  public:
    void Add(const float x, const float y, const float z, const Kind kind, const std::int64_t now)
    {
      mPins[mNext] = Pin{x, y, z, kind, now};
      mNext = (mNext + 1) % kMax;
      if (mCount < kMax) {
        ++mCount;
      }
    }

    /// Those that show at `now`, the oldest first.
    [[nodiscard]] std::vector<Pin> Live(const std::int64_t now) const
    {
      std::vector<Pin> live;
      for (std::size_t at = 0; at < mCount; ++at) {
        const Pin& pin = mPins[(mNext + kMax - mCount + at) % kMax];
        if (Alpha(now - pin.born) > 0) {
          live.push_back(pin);
        }
      }
      return live;
    }

    void Clear()
    {
      mCount = 0;
    }

  private:
    std::array<Pin, kMax> mPins{};
    std::size_t mNext = 0;
    std::size_t mCount = 0;
  };
} // namespace griffpins
