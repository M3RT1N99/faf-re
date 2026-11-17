// The ground buildings take beside the touch scheme Griff's draft (include/GriffSkirts.h): which colour a structure's skirt
// shows and which of the draft's buildings it lights, by the rule FAF's build mode has for its build drag's meshes
// (CUIWorldView.cpp DrawBuildDragAdjacencyHighlights). Whether a building gains adjacency from a structure is the engine's
// (AreSkirtsAdjacent and FAF's OnDetectAdjacencyBonus); here each test says which do.

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <utility>
#include <vector>

#include <GriffSkirts.h>

#include "Check.h"

namespace
{
  using griffskirts::Building;
  using griffskirts::kAdjacent;
  using griffskirts::kFree;
  using griffskirts::kRefused;
  using griffskirts::Light;

  /// The draft's buildings in these colours, each at the x of its index, by which a test says which gain adjacency.
  std::vector<Building> Buildings(const std::initializer_list<std::uint32_t> colors)
  {
    std::vector<Building> buildings;
    for (const std::uint32_t color : colors) {
      Building building;
      building.x = static_cast<float>(buildings.size());
      building.color = color;
      buildings.push_back(building);
    }
    return buildings;
  }

  /// A structure that the buildings at these x gain adjacency from.
  auto Beside(std::vector<float> xs)
  {
    return [xs = std::move(xs)](const Building& building) {
      return std::find(xs.begin(), xs.end(), building.x) != xs.end();
    };
  }
} // namespace

WXT_TEST(GriffSkirtsAStructureBesideAFreePlanShowsBrightAndLightsIt)
{
  std::vector<Building> buildings = Buildings({kFree});
  WXT_CHECK_EQ(Light(buildings, Beside({0})), kAdjacent);
  WXT_CHECK_EQ(buildings[0].color, kAdjacent);
}

WXT_TEST(GriffSkirtsAStructureThatNoPlanGainsFromShowsAFreePlacesColour)
{
  std::vector<Building> buildings = Buildings({kFree, kFree});
  WXT_CHECK_EQ(Light(buildings, Beside({})), kFree);
  WXT_CHECK_EQ(buildings[0].color, kFree);
  WXT_CHECK_EQ(buildings[1].color, kFree);
}

WXT_TEST(GriffSkirtsAPlanOnTakenGroundLightsNothingAndIsNotAsked)
{
  // FAF passes over a mesh in the refused colour before it asks gamemain.lua whether it gains adjacency.
  std::vector<Building> buildings = Buildings({kRefused});
  int asked = 0;
  const auto gains = [&asked](const Building&) {
    ++asked;
    return true;
  };
  WXT_CHECK_EQ(Light(buildings, gains), kFree);
  WXT_CHECK_EQ(buildings[0].color, kRefused);
  WXT_CHECK_EQ(asked, 0);
}

WXT_TEST(GriffSkirtsAStructureLightsTheFirstPlanThatGainsFromItAlone)
{
  std::vector<Building> buildings = Buildings({kRefused, kFree, kFree, kFree});
  WXT_CHECK_EQ(Light(buildings, Beside({0, 2, 3})), kAdjacent);
  WXT_CHECK_EQ(buildings[0].color, kRefused);
  WXT_CHECK_EQ(buildings[1].color, kFree);
  WXT_CHECK_EQ(buildings[2].color, kAdjacent);
  WXT_CHECK_EQ(buildings[3].color, kFree);
}

WXT_TEST(GriffSkirtsAPlanIsLitByTheFirstStructureBesideItAlone)
{
  // Three structures beside the first plan, the second also beside the second plan: the camera's first lights the first plan;
  // the second passes over it and lights the second plan; the third, beside the first plan alone, stays in a free place's
  // colour.
  std::vector<Building> buildings = Buildings({kFree, kFree});
  WXT_CHECK_EQ(Light(buildings, Beside({0})), kAdjacent);
  WXT_CHECK_EQ(Light(buildings, Beside({0, 1})), kAdjacent);
  WXT_CHECK_EQ(buildings[1].color, kAdjacent);
  WXT_CHECK_EQ(Light(buildings, Beside({0})), kFree);
  WXT_CHECK_EQ(buildings[0].color, kAdjacent);
}
