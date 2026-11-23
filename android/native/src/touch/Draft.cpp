#include "touch/Draft.h"

#include <algorithm>

#include "touch/Numbers.h"

bool touch::Draft::Update(const float* const values, int count, const Screen& screen)
{
  count = std::clamp(count, 0, kValues);
  if (SameAsShown(values, count)) {
    return false;
  }
  std::copy(values, values + count, mShown.begin());
  mShownCount = count;
  mOpen = count > kOpenAt && values[kOpenAt] != 0;
  if (!mOpen || count < kHead) {
    mPicksDeposit = false;
    mPlans = 0;
    mLine = 0;
    mAim = 0;
    mRestsOnMap = false;
    return true;
  }
  mPicksDeposit = values[kPicksDepositAt] != 0;
  mRestsOnMap = values[kRestsOnMapAt] != 0;
  const int room = (count - kHead) / kItem;
  mPlans = std::max(0, std::min(ToInt(values[kPlanCountAt]), room));
  mLine = std::max(0, std::min(ToInt(values[kLineCountAt]), room - mPlans));
  mAim = std::max(0, std::min(ToInt(values[kAimCountAt]), room - mPlans - mLine));
  for (int item = 0; item < mPlans + mLine + mAim; ++item) {
    const int from = kHead + item * kItem;
    const int to = item * kItem;
    mItems[to + kIdAt] = values[from + kIdAt];
    mItems[to + kXAt] = screen.X(values[from + kXAt]);
    mItems[to + kYAt] = screen.Y(values[from + kYAt]);
    mItems[to + kLeftAt] = screen.X(values[from + kLeftAt]);
    mItems[to + kTopAt] = screen.Y(values[from + kTopAt]);
    mItems[to + kRightAt] = screen.X(values[from + kRightAt]);
    mItems[to + kBottomAt] = screen.Y(values[from + kBottomAt]);
    mItems[to + kFlagsAt] = values[from + kFlagsAt];
  }
  return true;
}

bool touch::Draft::SameAsShown(const float* const values, const int count) const
{
  if (count != mShownCount) {
    return false;
  }
  for (int at = 0; at < count; ++at) {
    if (!SameFloat(values[at], mShown[at])) {
      return false;
    }
  }
  return true;
}

float touch::Draft::Value(const int item, const int at) const
{
  return mItems[item * kItem + at];
}

int touch::Draft::Id(const int item) const
{
  return ToInt(Value(item, kIdAt));
}

float touch::Draft::X(const int item) const
{
  return Value(item, kXAt);
}

float touch::Draft::Y(const int item) const
{
  return Value(item, kYAt);
}

float touch::Draft::Left(const int item) const
{
  return Value(item, kLeftAt);
}

float touch::Draft::Top(const int item) const
{
  return Value(item, kTopAt);
}

float touch::Draft::Right(const int item) const
{
  return Value(item, kRightAt);
}

float touch::Draft::Bottom(const int item) const
{
  return Value(item, kBottomAt);
}

bool touch::Draft::Has(const int item, const int flag) const
{
  return (ToInt(Value(item, kFlagsAt)) & flag) != 0;
}

int touch::Draft::PlanAt(const float fingerX, const float fingerY, const float least) const
{
  for (int plan = mPlans - 1; plan >= 0; --plan) {
    if (!Has(plan, kFree)) {
      continue;
    }
    const float middleX = X(plan);
    const float middleY = Y(plan);
    const float left = std::min(Left(plan), middleX - least);
    const float right = std::max(Right(plan), middleX + least);
    const float top = std::min(Top(plan), middleY - least);
    const float bottom = std::max(Bottom(plan), middleY + least);
    if (fingerX >= left && fingerX <= right && fingerY >= top && fingerY <= bottom) {
      return Id(plan);
    }
  }
  return 0;
}

int touch::Draft::PlanNumbered(const int id) const
{
  for (int plan = 0; plan < mPlans; ++plan) {
    if (Id(plan) == id) {
      return plan;
    }
  }
  return -1;
}

bool touch::Draft::Newest(const int id) const
{
  const int plan = PlanNumbered(id);
  return plan >= 0 && Has(plan, kNewest);
}

bool touch::Draft::OnDeposit(const int id) const
{
  const int plan = PlanNumbered(id);
  return plan >= 0 && Has(plan, kOnDeposit);
}

int touch::Draft::LoupeFrom(const int finger, const int side, const int extent)
{
  return std::max(0, std::min(extent - side, finger - side / 2));
}
