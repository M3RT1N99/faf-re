// How far past the map's edges the main map's camera may go (include/CameraClear.h): what the game's panels along the view's
// edges cover, on the phone's picture (1647x768 at the HUD's factor 1.6, the panels as the T3 probe and the walk on the
// emulator found them); and how near the camera's focus may come to each edge of the map for the view clear of them.

#include <limits>
#include <vector>

#include <CameraClear.h>

#include "Check.h"

namespace
{
  using cameraclear::Allowed;
  using cameraclear::ClearCorners;
  using cameraclear::Covered;
  using cameraclear::Ground;
  using cameraclear::Larger;
  using cameraclear::Margins;
  using cameraclear::Measure;
  using cameraclear::Panels;
  using cameraclear::Rect;
  using cameraclear::Rules;
  using cameraclear::Spans;

  constexpr Rect kView{0, 0, 1647, 768};
  /// The app's (GriffAndroid.cpp): pieces overlapping by more than 0.4 % of the view's height, panels within 1 % of an edge
  /// and up to 2 % past it, covering at most half of the view.
  constexpr Rules kRules{3.072f, 7.68f, 15.36f, 0.5f};

  /// FAF's panels in a skirmish on the S22 Ultra at 1.6 with a unit selected, about (tests/wx/GriffPlaceTests.cpp), and the
  /// construction panel.
  const std::vector<Rect> kPanels{
    {0, 4, 537, 116},       // the economy, at the top and the left: the top's
    {0, 124, 312, 236},     // the multifunction panel
    {679, 0, 967, 108},     // the tabs
    {1101, 0, 1647, 149},   // the score, at the top and the right: the top's
    {1525, 150, 1647, 296}, // the avatars
    {1547, 285, 1647, 471}, // the control groups' slots
    {0, 390, 555, 578},     // the unit view
    {0, 576, 651, 768},     // the orders, at the left and the bottom: the bottom's
    {975, 545, 1647, 768},  // the construction panel, at the bottom and the right: the bottom's
  };

  [[nodiscard]] bool Near(const float actual, const float expected)
  {
    return actual > expected - 1e-4f && actual < expected + 1e-4f;
  }
} // namespace

WXT_TEST(CameraClearEachPanelCountsForTheEdgeItLiesAlong)
{
  const Margins margins = Covered(kPanels, kView, kRules);
  WXT_CHECK(Near(margins.left, 555.0f / 1647));
  WXT_CHECK(Near(margins.top, 149.0f / 768));
  WXT_CHECK(Near(margins.right, (1647.0f - 1525) / 1647));
  WXT_CHECK(Near(margins.bottom, (768.0f - 545) / 768));
}

WXT_TEST(CameraClearPiecesThatOverlapAreOnePanel)
{
  const std::vector<Rect> pieces{
    {-2, 390, 46, 578},     // the unit view's bracket at the screen's edge
    {46, 390, 555, 578},    // and its body beside it: one panel only with what overlaps both
    {40, 400, 60, 410},     // such as its icon over the seam
    {-1, 576, 651, 771},    // the orders panel, which the unit view stands on by 2 pixels
    {1101, 3, 1657, 149},   // the score, a line above
    {1525, 150, 1651, 296}, // the avatars
    {1600, 100, 1601, 100}, // a piece without area, which covers nothing
  };
  const std::vector<Rect> panels = Panels(pieces, 3.072f);
  WXT_CHECK_EQ(panels.size(), static_cast<std::size_t>(4));
  WXT_CHECK_EQ(panels[0].left, -2.0f);
  WXT_CHECK_EQ(panels[0].right, 555.0f);
  WXT_CHECK_EQ(panels[0].bottom, 578.0f);
  WXT_CHECK_EQ(panels[1].top, 576.0f);
  WXT_CHECK_EQ(panels[2].left, 1101.0f);
  WXT_CHECK_EQ(panels[2].bottom, 149.0f);
  WXT_CHECK_EQ(panels[3].top, 150.0f);
  // With no overlap needed, what meets by a line is one panel.
  WXT_CHECK_EQ(Panels(pieces, 0).size(), static_cast<std::size_t>(3));
  WXT_CHECK(Panels({}, 3.072f).empty());
}

WXT_TEST(CameraClearThePhonesHudMakesTheFreeAreaBetweenItsPanels)
{
  // FAF's panels over the map as the walk found them in a skirmish on the emulator at the phone's picture and 1.6, the
  // commander selected, each gathered from its pieces: the economy, the multifunction panel with Griff's row, which it
  // overlaps by 4 pixels, the tabs, the score, the avatars with the control groups' slots, the unit view, and along the
  // bottom the orders panel with the construction panel. Each edge of the map comes out between them: under the score, left
  // of the avatars, right of the unit view and above the construction panel.
  const std::vector<Rect> pieces{
    {-11, 4, 537, 116},  {-9, 124, 312, 236},   {16, 232, 300, 305},     {679, -5, 967, 108},  {1101, 3, 1657, 149},
    {1525, 150, 1651, 296}, {1550, 285, 1647, 439}, {-2, 390, 555, 578}, {-1, 576, 651, 771},  {641, 541, 1647, 775},
  };
  const Margins margins = Measure(pieces, kView, kRules);
  WXT_CHECK(Near(margins.left, 555.0f / 1647));
  WXT_CHECK(Near(margins.top, 149.0f / 768));
  WXT_CHECK(Near(margins.right, (1647.0f - 1525) / 1647));
  WXT_CHECK(Near(margins.bottom, (768.0f - 541) / 768));
}

WXT_TEST(CameraClearAPanelOnItsWayInCountsForNone)
{
  // FAF slides the score in from the right as a game begins: past the edge it would count for the right one, which it is
  // not, and the larger margin stays for the game.
  const std::vector<Rect> sliding{{1490, 3, 2047, 149}, {-400, 4, 149, 116}};
  WXT_CHECK(Covered(sliding, kView, kRules).None());
  // Its brackets reach a little past the edge, the panel in its place.
  const Margins placed = Covered({{1101, 3, 1657, 149}}, kView, kRules);
  WXT_CHECK(Near(placed.top, 149.0f / 768));
  WXT_CHECK_EQ(placed.right, 0.0f);
}

WXT_TEST(CameraClearWhatStandsAtNoEdgeOrCoversTheViewCountsForNone)
{
  const std::vector<Rect> covers{
    {921, 265, 1190, 524}, // the minimap's window
    {0, 0, 1647, 768},     // a question box's shadow over the screen
    {0, 0, 1000, 768},     // a dialog over the left of the screen: more than half from each edge it touches
    {8, 300, 200, 400},    // the rail's row, 8 pixels from the edge
    {100, 100, 100, 200},  // a box without area
  };
  const Margins margins = Covered(covers, kView, kRules);
  WXT_CHECK(margins.None());
  WXT_CHECK(Covered(kPanels, Rect{0, 0, 0, 0}, kRules).None());
}

WXT_TEST(CameraClearMarginsKeepTheLargerAtEachEdge)
{
  const Margins larger = Larger(Margins{0.1f, 0.3f, 0, 0.2f}, Margins{0.2f, 0.1f, 0.05f, 0});
  WXT_CHECK_EQ(larger.left, 0.2f);
  WXT_CHECK_EQ(larger.top, 0.3f);
  WXT_CHECK_EQ(larger.right, 0.05f);
  WXT_CHECK_EQ(larger.bottom, 0.2f);
  WXT_CHECK(Margins{}.None());
  WXT_CHECK(!larger.None());
}

WXT_TEST(CameraClearTheClearViewsCornersAreWithinTheMargins)
{
  const std::vector<cameraclear::Corner> corners = ClearCorners(Margins{0.25f, 0.2f, 0.1f, 0.3f});
  WXT_CHECK_EQ(corners.size(), static_cast<std::size_t>(4));
  WXT_CHECK_EQ(corners[0].x, 0.25f);
  WXT_CHECK_EQ(corners[0].y, 0.2f);
  WXT_CHECK_EQ(corners[1].x, 0.9f);
  WXT_CHECK_EQ(corners[1].y, 0.2f);
  WXT_CHECK_EQ(corners[2].x, 0.25f);
  WXT_CHECK_EQ(corners[2].y, 0.7f);
  WXT_CHECK_EQ(corners[3].x, 0.9f);
  WXT_CHECK_EQ(corners[3].y, 0.7f);
}

WXT_TEST(CameraClearTheFocusComesAsNearTheEdgeAsTheNearestCornerTowardsIt)
{
  // A tilted camera looking north (screen up is the world's -z): the far corners of the clear view lie wider apart than the
  // near ones. The engine keeps 300 of the focus over the map each way.
  const std::vector<Ground> corners{{true, -260, -120}, {true, 280, -120}, {true, -200, 90}, {true, 210, 90}};
  const Spans spans = Allowed(300, 300, corners);
  WXT_CHECK_EQ(spans.minX, 200.0f);
  WXT_CHECK_EQ(spans.maxX, 210.0f);
  WXT_CHECK_EQ(spans.minZ, 120.0f);
  WXT_CHECK_EQ(spans.maxZ, 90.0f);
}

WXT_TEST(CameraClearTheFocusNeverGoesFartherInThanTheEnginesClamp)
{
  // Where the engine lets the edge come farther in than the clear view needs, it stays: the clamp only ever lets go.
  const std::vector<Ground> corners{{true, -260, -120}, {true, 280, -120}, {true, -200, 90}, {true, 210, 90}};
  const Spans spans = Allowed(100, 50, corners);
  WXT_CHECK_EQ(spans.minX, 100.0f);
  WXT_CHECK_EQ(spans.maxX, 100.0f);
  WXT_CHECK_EQ(spans.minZ, 50.0f);
  WXT_CHECK_EQ(spans.maxZ, 50.0f);
}

WXT_TEST(CameraClearACornerWithoutGroundKeepsTheEnginesClamp)
{
  // Above the horizon, as a camera turned up far could show it.
  const std::vector<Ground> corners{{false, 0, 0}, {true, 280, -120}, {true, -200, 90}, {true, 210, 90}};
  const Spans spans = Allowed(300, 300, corners);
  WXT_CHECK_EQ(spans.minX, 300.0f);
  WXT_CHECK_EQ(spans.maxX, 300.0f);
  WXT_CHECK_EQ(spans.minZ, 300.0f);
  WXT_CHECK_EQ(spans.maxZ, 300.0f);
}

WXT_TEST(CameraClearAnEdgeNoCornerLiesTowardsKeepsTheEnginesSpan)
{
  // A clear view all on one side of the focus, which no margins make, leaves the other side as the engine has it.
  const std::vector<Ground> corners{{true, 10, -120}, {true, 280, -120}, {true, 10, 90}, {true, 210, 90}};
  const Spans spans = Allowed(300, 300, corners);
  WXT_CHECK_EQ(spans.minX, 300.0f);
  WXT_CHECK_EQ(spans.maxX, 10.0f);
}
