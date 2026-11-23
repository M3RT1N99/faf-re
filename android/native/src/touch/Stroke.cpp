#include "touch/Stroke.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "touch/Numbers.h"

namespace
{
  constexpr int kEndPlace = 15;
} // namespace

double touch::stroke::Length(const float* const xs, const float* const ys, const int count)
{
  double length = 0;
  for (int i = 1; i < count; i++) {
    length += Hypot(xs[i] - xs[i - 1], ys[i] - ys[i - 1]);
  }
  return length;
}

int touch::stroke::Classify(const float* const xs, const float* const ys, const int count, const float millimetre)
{
  if (count < 2) {
    return kCancel;
  }
  if (Hypot(xs[count - 1] - xs[0], ys[count - 1] - ys[0]) >= kLoopMm * millimetre) {
    return kRoute;
  }
  return Length(xs, ys, count) > kPatrolMm * millimetre ? kPatrol : kCancel;
}

int touch::stroke::Meaning(const int end, const int mode)
{
  if (mode == kDistribute) {
    return kSaysDistribute;
  }
  const int place = end & kEndPlace;
  if (place == kEndPanel) {
    return kSaysCancel;
  }
  if (place == kEndMinimap) {
    return kSaysMinimap;
  }
  if (mode == kCancel) {
    return kSaysCancel;
  }
  if (mode == kPatrol && (place == 0 || (end & kEndPatrolOffered) != 0)) {
    return kSaysPatrol;
  }
  if (place == kEndAttack) {
    return kSaysAttack;
  }
  if ((end & kEndAttackMove) != 0) {
    return kSaysAttackMove;
  }
  switch (place) {
    case kEndGuard:
    case kEndRepair:
      return (end & kEndEngineers) != 0 ? kSaysAssist : kSaysGuard;
    case kEndReclaim:
      return kSaysReclaim;
    case kEndCapture:
      return kSaysCapture;
    case kEndOther:
      return kSaysOrder;
    default:
      return kSaysRoute;
  }
}

int touch::stroke::Waypoints(
  const float* const xs,
  const float* const ys,
  const int count,
  const float step,
  const float tolerance,
  float* const outX,
  float* const outY,
  const int room
)
{
  if (count < 2 || room <= 0) {
    return 0;
  }
  std::vector<bool> keep(static_cast<std::size_t>(count), false);
  keep[0] = true;
  keep[count - 1] = true;
  // Each split takes one segment off and puts two on: 2 + 2 * splits entries at most, and there are fewer splits than points.
  std::vector<int> stack;
  stack.reserve(2 * static_cast<std::size_t>(count) + 4);
  stack.push_back(0);
  stack.push_back(count - 1);
  while (!stack.empty()) {
    const int b = stack.back();
    stack.pop_back();
    const int a = stack.back();
    stack.pop_back();
    if (b - a < 2) {
      continue;
    }
    const double ax = xs[a];
    const double ay = ys[a];
    const double dx = xs[b] - ax;
    const double dy = ys[b] - ay;
    const double length = Hypot(dx, dy);
    int far = -1;
    double farDistance = tolerance;
    for (int i = a + 1; i < b; i++) {
      const double px = xs[i] - ax;
      const double py = ys[i] - ay;
      const double distance = length > 0 ? std::abs(px * dy - py * dx) / length : Hypot(px, py);
      if (distance > farDistance) {
        farDistance = distance;
        far = i;
      }
    }
    if (far >= 0) {
      keep[far] = true;
      stack.push_back(a);
      stack.push_back(far);
      stack.push_back(far);
      stack.push_back(b);
    }
  }
  int n = 0;
  int from = 0;
  for (int i = 1; i < count && n < room; i++) {
    if (!keep[i]) {
      continue;
    }
    const float ax = xs[from];
    const float ay = ys[from];
    const float bx = xs[i];
    const float by = ys[i];
    const int pieces = std::max(1, Round(static_cast<float>(Hypot(bx - ax, by - ay)) / step));
    for (int k = 1; k <= pieces && n < room; k++) {
      outX[n] = ax + (bx - ax) * k / pieces;
      outY[n] = ay + (by - ay) * k / pieces;
      n++;
    }
    from = i;
  }
  outX[n - 1] = xs[count - 1];
  outY[n - 1] = ys[count - 1];
  return n;
}
