#pragma once

// What the touch layer gives the touch scheme Griff's half in the engine (GriffAndroid.cpp), and what it reads of it: the
// earlier host's natives of the app's Game class, as C++ the layer on SDL's fingers calls (FingersSdl.cpp). A call gives a task
// on the program's thread (HostSdl.h) in the order of the input, as the natives did, and a read takes what the program last
// published. Places are the picture's pixels. Any thread, unless it says otherwise.

#include <array>
#include <cstdint>
#include <vector>

namespace griffinput
{
  /// What the program says to the layer (Said): what a tap was, a group stored, the tick's clicks over, a drag it made a pan or
  /// a formation of, an order's record for the undo and its end, whether the draft's rest is on FAF's UI, whether the
  /// handle's rest distributes.
  enum Said : int
  {
    kSaidTap = 1,
    kSaidGroupStored = 3,
    kSaidCommitted = 4,
    kSaidPan = 5,
    kSaidOrdered = 6,
    kSaidUndone = 7,
    kSaidFormation = 8,
    kSaidDraftRest = 9,
    kSaidDistributes = 10,
  };

  /// A hint where FAF prints its own messages (griff.lua Hint): the layer's, a stroke given up.
  constexpr int kHintCancelled = 13;

  /// The undo's button as FAF's UI draws it (SetHud): none, offered, or tapped and waiting.
  constexpr int kHudUndoNone = 0;
  constexpr int kHudUndoOffered = 1;
  constexpr int kHudUndoWaiting = 2;
  /// With a finger on the draft's tick, its cross and the undo's button: the looks' bits above the rail's two of each button.
  constexpr int kHudHeldTick = 16;
  constexpr int kHudHeldCross = 32;
  constexpr int kHudHeldUndo = 64;
  /// HudPlaces: five numbers for each of Box, Plus, the undo's button, the draft's tick and its cross.
  constexpr int kHudPiece = 5;
  constexpr int kHudPieces = 5;

  /// Griff's sizes in the picture's pixels: the handle's radius, how far from its middle a finger is on it, its room from the
  /// group's units and how far from its middle its arrow reaches; the tap help's reach and what counts as a small control; how
  /// far beside a unit a tap picks it, without troops and with them; the speed a second under which the camera's glide stops;
  /// how far from a finger the draft finds a deposit.
  struct Sizes
  {
    float radius = 0;
    float touchRadius = 0;
    float gap = 0;
    float arrowReach = 0;
    float tapReach = 0;
    float tapSmallSide = 0;
    float nearReach = 0;
    float nearReachWithTroops = 0;
    float glideStop = 0;
    float depositReach = 0;
  };

  /// The scheme is on or off, with its sizes.
  void SetGriff(bool on, const Sizes& sizes);

  /// Where the layer's own pieces lie over the picture, boxes of left, top, right and bottom: the handle keeps clear of them,
  /// and a gesture finds none of the game's under them.
  void SetCovers(const int* boxes, int count);

  /// The handle as the last frame placed it, with the free map's left, top and right at its middle, and the stroke last
  /// answered for with what its end is on (stroke::kEnd).
  struct Handle
  {
    bool shown = false;
    float x = 0;
    float y = 0;
    float groupX = 0;
    float groupY = 0;
    int count = 0;
    /// It points at the group from elsewhere.
    bool off = false;
    float freeLeft = 0;
    float freeTop = 0;
    float freeRight = 0;
    int stroke = 0;
    int strokeEnd = 0;
  };

  /// The handle now; `shown` false when there is none, no room for it, or the scheme is off.
  [[nodiscard]] Handle HandleNow();

  /// What the program said since the last call, three numbers each: a Said, and its two numbers.
  [[nodiscard]] std::vector<std::array<int, 3>> TakeSaid();

  /// Who hears that the program said something: called on the program's thread, after the word, outside any of the layer's
  /// calls (a task of its own). Set once, before the program's input.
  void SetListener(void (*heard)());

  // --- A finger's gestures (TouchInput::Mouse) ------------------------------------------------------------------------------

  void Down(int x, int y, bool box);
  void SmartTap(int x, int y, int rail, int step, int baseX, int baseY, int tap);
  void Deselect();
  /// The camera goes to the selection, by a flight or at once.
  void ToGroup(bool fly);
  void HandleRest(int rest);
  void Distribute();
  /// A route's waypoints, `count` places as x, y pairs; `rail`: the kRail bits that count for it; `patrol`: a loop.
  void Route(const int* places, int count, int rail, bool patrol);
  void StrokeHover(int x, int y, int stroke);
  void Drag(int x, int y, int nowX, int nowY, bool rested, int rail, int drag);
  /// A pan's camera glides on at this velocity in the picture's pixels a second.
  void DragEnd(int x, int y, float velocityX, float velocityY);
  void DragGiveUp(int x, int y);
  void RestTap(int x, int y, int rail);
  void Rest(int x, int y);
  void RestGone();
  void Pinch(bool start, int x, int y, int distance);
  void PinchEnd(float velocityX, float velocityY);
  /// Plus held, or let go: Shift's key, in the order of the clicks.
  void HoldShift(bool down);

  // --- The draft --------------------------------------------------------------------------------------------------------

  /// The draft as the last frame drew it (touch::Draft's numbers).
  [[nodiscard]] std::vector<float> DraftShown();
  void DraftTap(int x, int y, int plan);
  void DraftLine(int phase, int x, int y);
  void DraftMove(int phase, int fromX, int fromY, int x, int y);
  void DraftAim(int phase, int x, int y);
  void DraftRest(int x, int y, int rest);
  void DraftBox(int x0, int y0, int x1, int y1);
  /// The tick, with Plus counting or not.
  void DraftCommit(bool plus);
  void DraftDrop();

  // --- FAF's UI's pieces (griffhud.lua) -----------------------------------------------------------------------------------

  /// A tap on the cross, Griff's Esc.
  void Escape();
  /// Whether the cross shows, as the last frame found Esc.
  [[nodiscard]] bool Escapes();
  /// What the player opened of FAF's that shows (touch::Opened's numbers).
  [[nodiscard]] std::vector<float> Opened();
  void Hint(int kind, int number);
  /// How the rail's Box and Plus look (touch::Rail::Look, two bits each, Box's first) with kHudHeld bits, the undo's button
  /// (kHudUndo) and the part of its time left, which griffhud.lua draws.
  void SetHud(int looks, int undo, float undoLeft);
  /// Where griffhud.lua drew Box, Plus, the undo's button, the draft's tick and its cross: left, top, right, bottom and 1 where
  /// something the player opened of FAF's lies over it or FAF's UI is hidden, for each.
  [[nodiscard]] std::array<float, kHudPiece * kHudPieces> HudPlaces();
  /// The undo's button was tapped for record `number`.
  void Undo(int number);

  /// Whether the program's clicks run, the draft's tick's or the undo's: the fingers rest meanwhile. Clicks that have waited
  /// for a frame past their patience count as over.
  [[nodiscard]] bool Clicking();
  /// How many runs of the program's clicks have begun.
  [[nodiscard]] int ClickRuns();
} // namespace griffinput
