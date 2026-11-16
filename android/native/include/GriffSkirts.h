#pragma once

// The ground each building takes, as FAF's build mode shows it, for Griff's draft (android/TOUCH_UI.md, "The draft"). While
// FAF's build mode is armed, the map's view draws the skirt of each building the selection's orders will build, of each
// structure in view, brighter beside a building placed that gains adjacency from it, and of each building placed
// (CUIWorldView.cpp DrawCommandGraph). The draft ends FAF's build mode, as a finger's tap makes a plan there, not an order:
// by its build-time patch (patches/src/moho/ui/CUIWorldView.cpp) the map's view draws the same for the draft's plans, the
// line a finger draws and the plan it aims, which the app gives (src/host/GriffAndroid.cpp).

#include <cstdint>
#include <vector>

namespace moho
{
  struct RMeshBlueprint;
  struct RUnitBlueprint;
} // namespace moho

namespace griffskirts
{
  /// FAF's build preview's colours (CWldSessionTypes.cpp SelectBuildPreviewColor): a place it takes, which is also the colour
  /// of a structure's skirt, and one it refuses; and a building's that gains adjacency, and of the structure it gains it from
  /// (CUIWorldView.cpp DrawBuildDragAdjacencyHighlights).
  constexpr std::uint32_t kFree = 0xD800D800u;
  constexpr std::uint32_t kRefused = 0xD8D80000u;
  constexpr std::uint32_t kAdjacent = 0xFF00FF00u;

  /// A building of the draft's: its blueprint, its place in the world and its colour, kFree or kRefused.
  struct Building
  {
    const moho::RUnitBlueprint* blueprint = nullptr;
    float x = 0;
    float y = 0;
    float z = 0;
    std::uint32_t color = kFree;
  };

  /// The draft as the map's view draws it: the mesh of the building picked, as far out as which a structure shows its skirt,
  /// as for the building of FAF's build mode; and the draft's buildings.
  struct Draft
  {
    const moho::RMeshBlueprint* mesh = nullptr;
    std::vector<Building> buildings;
  };

  /// What the app gives of its draft as the map's view draws, on the program's thread: false without a draft.
  inline bool (*gDraft)(Draft& draft) = nullptr;
} // namespace griffskirts
