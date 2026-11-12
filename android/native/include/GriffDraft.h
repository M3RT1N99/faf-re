#pragma once

// The draft of the touch scheme Griff (android/TOUCH_UI.md, "Griff"; buildstage/griff-v3-spec.md, T2): buildings planned on
// the map before anything is ordered, each a plan at a world place, ordered by FAF's build mode and a click per plan when the
// player ticks them (src/host/GriffAndroid.cpp). Here the rules alone, for their tests (tests/wx/GriffDraftTests.cpp): where a
// plan lands, which ground a plan takes, the line from the newest plan, the move of the whole draft, the limit, and the pixel
// a building's click goes to. What FAF itself says of a place (its own build check: structures it sees, queued buildings, the
// ground) the caller gives as `Free`, a callable of (const Building&, float x, float z). World places are FAF's x and z.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace griffdraft
{
  /// The mockup's limit: a draft holds this many plans, and a line has no more places than that.
  constexpr std::size_t kMaxPlans = 30;

  /// What a building must stand on: nothing, or a resource deposit of a kind, where FAF builds it only.
  enum class Deposit
  {
    kNone,
    kMass,
    kHydrocarbon,
  };

  /// What the draft takes of a building's blueprint: its footprint in whole cells and its skirt (FAF's Physics.SkirtOffset and
  /// SkirtSize; a size of 0 is the footprint's, as FAF's GetSkirtRect has it), the ground no other building may take.
  struct Building
  {
    std::string id;
    int footprintX = 1;
    int footprintZ = 1;
    float skirtOffsetX = 0;
    float skirtOffsetZ = 0;
    float skirtSizeX = 0;
    float skirtSizeZ = 0;
    Deposit deposit = Deposit::kNone;

    /// The skirt's breadth along x and z: a line of these buildings steps by it, so that their skirts touch, as FAF's own
    /// drag line puts them for its adjacency.
    [[nodiscard]] float SpacingX() const
    {
      return skirtSizeX == 0 ? static_cast<float>(footprintX) : skirtSizeX;
    }

    [[nodiscard]] float SpacingZ() const
    {
      return skirtSizeZ == 0 ? static_cast<float>(footprintZ) : skirtSizeZ;
    }
  };

  struct Rect
  {
    float x0 = 0;
    float z0 = 0;
    float x1 = 0;
    float z1 = 0;

    /// Strictly, as FAF's Rect2::Overlaps: skirts that touch take no ground of each other.
    [[nodiscard]] bool Overlaps(const Rect& other) const
    {
      return x0 < other.x1 && other.x0 < x1 && z0 < other.z1 && other.z0 < z1;
    }
  };

  /// Where FAF places a building of this footprint size for a world coordinate: the footprint's first cell the nearest whole
  /// one (RUnitBlueprint::GetFootprintRect rounds to the nearest, ties to even), its middle half the footprint on.
  [[nodiscard]] inline float Snap(const float at, const int size)
  {
    const float half = static_cast<float>(size) * 0.5f;
    return static_cast<float>(std::lrintf(at - half)) + half;
  }

  /// The ground a building there takes (RUnitBlueprint::GetSkirtRect).
  [[nodiscard]] inline Rect SkirtRect(const Building& building, const float x, const float z)
  {
    const float lowerX = static_cast<float>(std::lrintf(x - static_cast<float>(building.footprintX) * 0.5f));
    const float lowerZ = static_cast<float>(std::lrintf(z - static_cast<float>(building.footprintZ) * 0.5f));
    Rect rect;
    rect.x0 = building.skirtSizeX == 0 ? lowerX : lowerX + building.skirtOffsetX;
    rect.z0 = building.skirtSizeZ == 0 ? lowerZ : lowerZ + building.skirtOffsetZ;
    rect.x1 = rect.x0 + building.SpacingX();
    rect.z1 = rect.z0 + building.SpacingZ();
    return rect;
  }

  struct Place
  {
    float x = 0;
    float z = 0;
  };

  /// tan 22.5 degrees: a line's axis counts when its part of the way, in the building's steps, is at least this much of the
  /// other's, so a line goes in one of 8 directions (the mockup's snapLine).
  constexpr float kDirection = 0.414f;

  /// The places of a line of buildings after `from` towards `to`: in 8 directions, whole steps of the building's skirt in
  /// each axis, as many as the way is long in steps, rounded, and none before half a step, nor more than a draft holds: the
  /// preview shows no more, the lift adds only what it showed, and FAF's build check is asked for each place at each move of
  /// the finger. Each on FAF's grid.
  [[nodiscard]] inline std::vector<Place> Line(const Building& building, const Place& from, const Place& to)
  {
    std::vector<Place> places;
    const float stepX = building.SpacingX();
    const float stepZ = building.SpacingZ();
    if (!(stepX > 0 && stepZ > 0)) {
      return places;
    }
    const float dx = to.x - from.x;
    const float dz = to.z - from.z;
    // The way in steps of the building, as comparisons that hold: a place that is no number makes no line.
    const float acrossX = std::fabs(dx) / stepX;
    const float acrossZ = std::fabs(dz) / stepZ;
    const float steps = (std::max)(acrossX, acrossZ);
    if (!(steps >= 0.5f)) {
      return places;
    }
    const float unitX = acrossX >= acrossZ * kDirection ? (dx < 0 ? -1.0f : 1.0f) : 0.0f;
    const float unitZ = acrossZ >= acrossX * kDirection ? (dz < 0 ? -1.0f : 1.0f) : 0.0f;
    const long most = static_cast<long>(kMaxPlans);
    const long count = steps < static_cast<float>(most) ? std::lround(steps) : most;
    for (long step = 1; step <= count; ++step) {
      const float along = static_cast<float>(step);
      places.push_back(Place{Snap(from.x + unitX * stepX * along, building.footprintX),
                             Snap(from.z + unitZ * stepZ * along, building.footprintZ)});
    }
    return places;
  }

  struct Plan
  {
    int id = 0;
    /// Which of the draft's buildings it is.
    std::size_t building = 0;
    float x = 0;
    float z = 0;
  };

  /// A place a deposit's building could take there, the deposit's middle (FAF's ResourceDeposit: its cells' rectangle, whose
  /// middle cell is the place, floored), with that middle on the screen (the picture's pixels).
  struct Candidate
  {
    float screenX = 0;
    float screenY = 0;
    Place place;
  };

  /// The deposit FAF's ghost puts a deposit's building on for the world point under the cursor (CUIWorldView::UpdateSelection,
  /// CSimResources::FindClosestDeposit): from the building's first cell there, floored, the deposit whose middle cell is
  /// nearest, the first of the nearest; its index in `deposits`, or -1 without one. FAF looks only within its snap's reach of
  /// the cursor, which a click at a deposit's middle always is.
  [[nodiscard]] inline int SnappedDeposit(const std::vector<Candidate>& deposits, const Building& building, const float x, const float z)
  {
    const float fromX = std::floor(x - static_cast<float>(building.footprintX) * 0.5f);
    const float fromZ = std::floor(z - static_cast<float>(building.footprintZ) * 0.5f);
    int nearest = -1;
    float best = (std::numeric_limits<float>::max)();
    for (std::size_t at = 0; at < deposits.size(); ++at) {
      const float dx = fromX - std::floor(deposits[at].place.x);
      const float dz = fromZ - std::floor(deposits[at].place.z);
      const float distance = std::sqrt(dx * dx + dz * dz);
      if (distance < best) {
        best = distance;
        nearest = static_cast<int>(at);
      }
    }
    return nearest;
  }

  /// Whether FAF's click at this world point under the cursor builds `building` at `place` (a plan's, or an order's given
  /// again): its first cell there by Snap on both axes, as FAF's build click places it (IssueBuildDragOrders); a deposit's
  /// building on the deposit FAF's ghost snaps it to, which must be the one at `place`, of `deposits` (all of its kind; none
  /// known, its cell as any building's).
  [[nodiscard]] inline bool LandsAt(const Building& building, const std::vector<Candidate>& deposits, const Place& place, const float x,
                                    const float z)
  {
    if (building.deposit != Deposit::kNone && !deposits.empty()) {
      int at = -1;
      float best = (std::numeric_limits<float>::max)();
      for (std::size_t candidate = 0; candidate < deposits.size(); ++candidate) {
        const float distance = std::hypot(deposits[candidate].place.x - place.x, deposits[candidate].place.z - place.z);
        if (distance < best) {
          best = distance;
          at = static_cast<int>(candidate);
        }
      }
      return SnappedDeposit(deposits, building, x, z) == at;
    }
    return Snap(x, building.footprintX) == place.x && Snap(z, building.footprintZ) == place.z;
  }

  /// The pixel a building's click goes to, whose middle the camera shows at `x`, `y` on the picture: of the four pixels about
  /// it, the nearest first, the first whose click lands where it should (`lands(pixelX, pixelY)`: LandsAt for the world point
  /// under that pixel); false where none does, as where a pixel spans more than a cell, which a closer camera makes good.
  /// FAF takes a click at a whole pixel and places by the world point under it, so the pixel nearest the middle falls into a
  /// neighbouring cell once a pixel spans more than a world unit; under a pitched or turned camera, or between two deposits, a
  /// pixel across the middle can land where the nearest does not.
  template <typename Lands>
  [[nodiscard]] bool ClickPixel(const float x, const float y, Lands&& lands, int& clickX, int& clickY)
  {
    if (!std::isfinite(x) || !std::isfinite(y)) {
      return false;
    }
    struct Pixel
    {
      float x;
      float y;
      float away;
    };
    std::array<Pixel, 4> pixels{};
    std::size_t count = 0;
    for (const float pixelX : {std::floor(x), std::floor(x) + 1}) {
      for (const float pixelY : {std::floor(y), std::floor(y) + 1}) {
        pixels[count++] = Pixel{pixelX, pixelY, std::hypot(pixelX - x, pixelY - y)};
      }
    }
    std::stable_sort(pixels.begin(), pixels.end(), [](const Pixel& one, const Pixel& other) { return one.away < other.away; });
    for (const Pixel& pixel : pixels) {
      const int pixelX = static_cast<int>(pixel.x);
      const int pixelY = static_cast<int>(pixel.y);
      if (lands(pixelX, pixelY)) {
        clickX = pixelX;
        clickY = pixelY;
        return true;
      }
    }
    return false;
  }

  enum class Placed
  {
    kPlaced,
    /// No deposit of the building's kind free within reach.
    kNoFreeDeposit,
    /// The ground there is a plan's, or FAF's own check refuses it.
    kTaken,
    kFull,
  };

  /// The draft: the building picked for the next plans, and the plans in the order they were made, each with its own building.
  class Draft
  {
  public:
    static constexpr std::size_t kMaxPlans = griffdraft::kMaxPlans;

    [[nodiscard]] bool Open() const
    {
      return mPicked < mBuildings.size();
    }

    /// The draft opens with a building, or takes another for the next plans; the plans keep theirs.
    void Pick(const Building& building)
    {
      const auto known =
        std::find_if(mBuildings.begin(), mBuildings.end(), [&building](const Building& other) { return other.id == building.id; });
      if (known != mBuildings.end()) {
        *known = building;
        mPicked = static_cast<std::size_t>(known - mBuildings.begin());
        return;
      }
      mBuildings.push_back(building);
      mPicked = mBuildings.size() - 1;
    }

    /// No draft: no building, no plans.
    void Close()
    {
      mBuildings.clear();
      mPlans.clear();
      mMoving.clear();
      mPicked = 0;
    }

    /// The building of the next plans; only while open.
    [[nodiscard]] const Building& Picked() const
    {
      return mBuildings[mPicked];
    }

    [[nodiscard]] const Building& BuildingOf(const Plan& plan) const
    {
      return mBuildings[plan.building];
    }

    [[nodiscard]] const std::vector<Plan>& Plans() const
    {
      return mPlans;
    }

    [[nodiscard]] bool Full() const
    {
      return mPlans.size() >= kMaxPlans;
    }

    /// Whether a building there would take ground of a plan, but the one numbered `except`.
    [[nodiscard]] bool OnPlan(const Building& building, const float x, const float z, const int except = 0) const
    {
      const Rect rect = SkirtRect(building, x, z);
      return std::any_of(mPlans.begin(), mPlans.end(), [&](const Plan& plan) {
        return plan.id != except && SkirtRect(BuildingOf(plan), plan.x, plan.z).Overlaps(rect);
      });
    }

    /// Whether a plan's ground is taken: by another plan's, or by what FAF's own check refuses.
    template <typename Free>
    [[nodiscard]] bool Taken(const Plan& plan, Free&& free) const
    {
      const Building& building = BuildingOf(plan);
      return OnPlan(building, plan.x, plan.z, plan.id) || !free(building, plan.x, plan.z);
    }

    /// Where a plan of the picked building at this world place would land: on FAF's grid.
    [[nodiscard]] Place Snapped(const float x, const float z) const
    {
      const Building& building = Picked();
      return Place{Snap(x, building.footprintX), Snap(z, building.footprintZ)};
    }

    /// A plan of the picked building at this world place, on FAF's grid, unless its ground is taken or the draft is full.
    template <typename Free>
    Placed PlaceAt(const float x, const float z, Free&& free)
    {
      const griffdraft::Place at = Snapped(x, z);
      const Building& building = Picked();
      if (OnPlan(building, at.x, at.z) || !free(building, at.x, at.z)) {
        return Placed::kTaken;
      }
      return Add(mPicked, at) ? Placed::kPlaced : Placed::kFull;
    }

    /// The deposit nearest to a place on the screen within `reach` whose ground is free, for the picked building: its index
    /// in `candidates`, or none.
    template <typename Free>
    [[nodiscard]] int NearestFree(const std::vector<Candidate>& candidates, const float x, const float y, const float reach, Free&& free) const
    {
      const Building& building = Picked();
      int best = -1;
      float bestDistance = reach;
      for (std::size_t at = 0; at < candidates.size(); ++at) {
        const Candidate& candidate = candidates[at];
        const float distance = std::hypot(candidate.screenX - x, candidate.screenY - y);
        if (distance <= bestDistance && !OnPlan(building, candidate.place.x, candidate.place.z)
            && free(building, candidate.place.x, candidate.place.z)) {
          best = static_cast<int>(at);
          bestDistance = distance;
        }
      }
      return best;
    }

    /// A plan of the picked building on the free deposit nearest to a place on the screen within `reach`.
    template <typename Free>
    Placed PlaceOnDeposit(const std::vector<Candidate>& candidates, const float x, const float y, const float reach, Free&& free)
    {
      const int nearest = NearestFree(candidates, x, y, reach, free);
      if (nearest < 0) {
        return Placed::kNoFreeDeposit;
      }
      return Add(mPicked, candidates[static_cast<std::size_t>(nearest)].place) ? Placed::kPlaced : Placed::kFull;
    }

    /// The deposits given, in their order, that are free get a plan of the picked building each: how many did, and in `full`
    /// whether the draft filled before the last.
    template <typename Free>
    std::size_t PlaceOnDeposits(const std::vector<Candidate>& candidates, Free&& free, bool& full)
    {
      full = false;
      std::size_t placed = 0;
      for (const Candidate& candidate : candidates) {
        const Building& building = Picked();
        if (OnPlan(building, candidate.place.x, candidate.place.z) || !free(building, candidate.place.x, candidate.place.z)) {
          continue;
        }
        if (!Add(mPicked, candidate.place)) {
          full = true;
          break;
        }
        ++placed;
      }
      return placed;
    }

    /// Removes the plan numbered `id`; false when there is none.
    bool Remove(const int id)
    {
      const auto plan = std::find_if(mPlans.begin(), mPlans.end(), [id](const Plan& other) { return other.id == id; });
      if (plan == mPlans.end()) {
        return false;
      }
      mPlans.erase(plan);
      return true;
    }

    /// The newest plan, from which a line goes: none when there is none or it stands on a deposit, as a line of extractors
    /// has nowhere to go.
    [[nodiscard]] const Plan* LineStart() const
    {
      if (mPlans.empty() || BuildingOf(mPlans.back()).deposit != Deposit::kNone) {
        return nullptr;
      }
      return &mPlans.back();
    }

    /// The line from the newest plan towards this world place, each place with whether its ground is free.
    template <typename Free>
    [[nodiscard]] std::vector<std::pair<griffdraft::Place, bool>> LineTo(const float x, const float z, Free&& free) const
    {
      std::vector<std::pair<griffdraft::Place, bool>> line;
      const Plan* const start = LineStart();
      if (start == nullptr) {
        return line;
      }
      const Building& building = BuildingOf(*start);
      for (const griffdraft::Place& at : Line(building, griffdraft::Place{start->x, start->z}, griffdraft::Place{x, z})) {
        line.emplace_back(at, !OnPlan(building, at.x, at.z) && free(building, at.x, at.z));
      }
      return line;
    }

    /// The line's free places become plans, in order; false when the draft filled before the line's end.
    template <typename Free>
    bool AddLine(const float x, const float z, Free&& free)
    {
      const Plan* const start = LineStart();
      if (start == nullptr) {
        return true;
      }
      const std::size_t building = start->building;
      for (const griffdraft::Place& at :
           Line(mBuildings[building], griffdraft::Place{start->x, start->z}, griffdraft::Place{x, z})) {
        if (OnPlan(mBuildings[building], at.x, at.z) || !free(mBuildings[building], at.x, at.z)) {
          continue;
        }
        if (!Add(building, at)) {
          return false;
        }
      }
      return true;
    }

    /// The whole draft starts to move: where each plan is now is where the move counts from.
    void MoveBegin()
    {
      mMoving.clear();
      for (const Plan& plan : mPlans) {
        mMoving.push_back(griffdraft::Place{plan.x, plan.z});
      }
    }

    [[nodiscard]] bool Moving() const
    {
      return !mMoving.empty() && mMoving.size() == mPlans.size();
    }

    /// Every plan this far from where the move began, in whole world units, which keep each on FAF's grid; plans on deposits
    /// stay on theirs.
    void MoveBy(const float dx, const float dz)
    {
      if (!Moving()) {
        return;
      }
      const float wholeX = std::round(dx);
      const float wholeZ = std::round(dz);
      for (std::size_t at = 0; at < mPlans.size(); ++at) {
        if (BuildingOf(mPlans[at]).deposit != Deposit::kNone) {
          continue;
        }
        mPlans[at].x = mMoving[at].x + wholeX;
        mPlans[at].z = mMoving[at].z + wholeZ;
      }
    }

    void MoveEnd()
    {
      mMoving.clear();
    }

    /// The move given up: every plan back where it was.
    void MoveAbort()
    {
      if (Moving()) {
        for (std::size_t at = 0; at < mPlans.size(); ++at) {
          mPlans[at].x = mMoving[at].x;
          mPlans[at].z = mMoving[at].z;
        }
      }
      mMoving.clear();
    }

    /// The plans that can be built now, in their order: FAF's own check passes, and none takes ground of one before it, as
    /// each order then holds its ground for those after it.
    template <typename Free>
    [[nodiscard]] std::vector<Plan> Buildable(Free&& free) const
    {
      std::vector<Plan> buildable;
      for (const Plan& plan : mPlans) {
        const Building& building = BuildingOf(plan);
        const Rect rect = SkirtRect(building, plan.x, plan.z);
        const bool onEarlier = std::any_of(buildable.begin(), buildable.end(), [&](const Plan& earlier) {
          return SkirtRect(BuildingOf(earlier), earlier.x, earlier.z).Overlaps(rect);
        });
        if (!onEarlier && free(building, plan.x, plan.z)) {
          buildable.push_back(plan);
        }
      }
      return buildable;
    }

  private:
    bool Add(const std::size_t building, const griffdraft::Place& at)
    {
      if (Full()) {
        return false;
      }
      mPlans.push_back(Plan{mNextId, building, at.x, at.z});
      // Numbers the activity holds while the program removes: never one twice in a game.
      mNextId = mNextId == 0x7FFFFFFF ? 1 : mNextId + 1;
      return true;
    }

    std::vector<Building> mBuildings;
    std::size_t mPicked = 0;
    std::vector<Plan> mPlans;
    /// Where the plans were when the move began; empty while none moves.
    std::vector<griffdraft::Place> mMoving;
    int mNextId = 1;
  };
} // namespace griffdraft
