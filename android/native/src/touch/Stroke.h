#pragma once

// A finger's stroke from the selection's handle (touch scheme Griff, buildstage/touch-concept-v2.md): what lifting it gives,
// its waypoints, and what the screen says it gives.

namespace touch::stroke
{
  /// What lifting the stroke gives (the mockup's classifyStroke): a route out, a patrol by a loop, or nothing; or, which no
  /// stroke's shape gives, FAF's Distribute orders, the lift of the handle's rest where the program said its selection can.
  constexpr int kRoute = 0;
  constexpr int kPatrol = 1;
  constexpr int kCancel = 2;
  constexpr int kDistribute = 3;
  /// A stroke that ends this near its start is a loop: a patrol if it is longer than kPatrolMm, else taken back.
  constexpr float kLoopMm = 8;
  constexpr float kPatrolMm = 40;

  /// What the stroke's end is on, as the program says (GriffAndroid.cpp StrokeAnswerTask): FAF's right click there, which the
  /// world view decides by what the cursor is over, a panel or the minimap; 0 while it has not said.
  constexpr int kEndMove = 1;
  constexpr int kEndAttack = 2;
  constexpr int kEndGuard = 3;
  constexpr int kEndRepair = 4;
  constexpr int kEndReclaim = 5;
  constexpr int kEndCapture = 6;
  constexpr int kEndOther = 7;
  constexpr int kEndPanel = 8;
  constexpr int kEndMinimap = 9;
  /// With it: engineers are selected; FAF offers its patrol for the selection (not for factories alone); FAF's attack-move is
  /// armed (its orders panel's button), whose waypoints the route gives.
  constexpr int kEndEngineers = 16;
  constexpr int kEndPatrolOffered = 32;
  constexpr int kEndAttackMove = 64;

  /// What the screen says lifting gives, the mockup's labels (strokeMeaning) and FAF's own for what it decides otherwise.
  constexpr int kSaysRoute = 0;
  constexpr int kSaysAttackMove = 1;
  constexpr int kSaysCancel = 2;
  constexpr int kSaysMinimap = 3;
  constexpr int kSaysPatrol = 4;
  constexpr int kSaysAttack = 5;
  constexpr int kSaysAssist = 6;
  constexpr int kSaysGuard = 7;
  constexpr int kSaysReclaim = 8;
  constexpr int kSaysCapture = 9;
  constexpr int kSaysOrder = 10;
  /// FAF's own words for its key action G, key_desc_distribute_orders (keydescriptions.lua).
  constexpr int kSaysDistribute = 11;

  /// The length of the first `count` points of xs/ys.
  [[nodiscard]] double Length(const float* xs, const float* ys, int count);

  /// kRoute, kPatrol or kCancel for the first `count` points, `millimetre` pixels to a mm.
  [[nodiscard]] int Classify(const float* xs, const float* ys, int count, float millimetre);

  /// What lifting gives, in the mockup's order, by what the end is on (kEnd and its flags, 0 unknown) and the stroke's mode:
  /// a panel gives nothing, the minimap its place; then a loop; then FAF's order there: an attack, and with FAF's attack-move
  /// armed an attack-move, assist with engineers or guard, reclaim; a plain move is the route. A loop is a patrol where FAF
  /// offers one; for factories alone it is their rally route, as any stroke. The handle's rest is FAF's Distribute orders,
  /// whatever lies under it.
  [[nodiscard]] int Meaning(int end, int mode);

  /// Writes the waypoints of the first `count` points of xs/ys into outX/outY, which have room for `room`, and returns how
  /// many: about one every `step` along the straight, and every corner the finger drew, ending on the stroke's end; not its
  /// start. The corners stay (Ramer-Douglas-Peucker within `tolerance`): far out, `step` is a long way on the map, and a
  /// route that cut a corner would go elsewhere. When out has too little room, the last waypoint is the stroke's end all the
  /// same.
  int Waypoints(const float* xs, const float* ys, int count, float step, float tolerance, float* outX, float* outY, int room);
} // namespace touch::stroke
