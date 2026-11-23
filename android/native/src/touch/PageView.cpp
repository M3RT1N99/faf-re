#include "touch/PageView.h"

#include <algorithm>
#include <optional>

#include "touch/Numbers.h"

namespace
{
  /// How far from the keyboard and the screen's top what is brought into view stays, in the picture's pixels.
  constexpr float kShowMargin = 8;

  int ToPicture(const float place, const int viewSize, const int pictureSize)
  {
    if (viewSize <= 0 || pictureSize <= 0) {
      return touch::Round(place);
    }
    return std::max(0, std::min(pictureSize - 1, touch::Round(place * pictureSize / viewSize)));
  }

  float ToScreen(const float place, const int viewSize, const int pictureSize)
  {
    return viewSize <= 0 || pictureSize <= 0 ? place : place * viewSize / pictureSize;
  }
} // namespace

void touch::PageView::SetScreen(const int width, const int height)
{
  mScreenWidth = width;
  mScreenHeight = height;
  Settle();
}

void touch::PageView::SetPicture(const int width, const int height)
{
  mPictureWidth = width;
  mPictureHeight = height;
  Settle();
}

bool touch::PageView::SetPage(const int left, const int top, const int right, const int bottom)
{
  if (mPage && left == mLeft && top == mTop && right == mRight && bottom == mBottom) {
    return false;
  }
  std::optional<float> stood;
  const int under = CoveredIndex(left, top, right, bottom);
  if (under >= 0) {
    stood = mCovered[static_cast<std::size_t>(under)].viewTop;
    mCovered.erase(mCovered.begin() + under, mCovered.end());
  } else if (mPage) {
    mCovered.push_back(CoveredPage{mLeft, mTop, mRight, mBottom, mViewTop});
  }
  mPage = true;
  mLeft = left;
  mTop = top;
  mRight = right;
  mBottom = bottom;
  mViewTop = stood.has_value() ? *stood : MostTop();
  Settle();
  return true;
}

int touch::PageView::CoveredIndex(const int left, const int top, const int right, const int bottom) const
{
  for (int index = static_cast<int>(mCovered.size()) - 1; index >= 0; --index) {
    if (mCovered[static_cast<std::size_t>(index)].Is(left, top, right, bottom)) {
      return index;
    }
  }
  return -1;
}

bool touch::PageView::ClearPage()
{
  const bool had = mPage;
  mPage = false;
  mCovered.clear();
  Settle();
  return had;
}

bool touch::PageView::Zoomed() const
{
  return mPage && mRight > mLeft && mScreenWidth > 0 && mPictureWidth > 0 && mPictureHeight > 0 &&
    mRight - mLeft < mPictureWidth;
}

float touch::PageView::Scale() const
{
  if (Zoomed()) {
    return static_cast<float>(mScreenWidth) / static_cast<float>(mRight - mLeft);
  }
  return mPictureHeight > 0 ? static_cast<float>(mScreenHeight) / static_cast<float>(mPictureHeight) : 1;
}

float touch::PageView::Zoom() const
{
  return mPictureHeight > 0 && mScreenHeight > 0 ? Scale() * mPictureHeight / mScreenHeight : 1;
}

int touch::PageView::ViewWidth() const
{
  return Zoomed() ? Round(mPictureWidth * Scale()) : mScreenWidth;
}

int touch::PageView::ViewHeight() const
{
  return Zoomed() ? Round(mPictureHeight * Scale()) : mScreenHeight;
}

float touch::PageView::ViewLeft() const
{
  return Zoomed() ? static_cast<float>(-Round(mLeft * Scale())) : 0;
}

float touch::PageView::ViewTop() const
{
  return Zoomed() ? mViewTop : 0;
}

void touch::PageView::SetKeyboard(const int pixels)
{
  mKeyboard = pixels;
  Settle();
}

void touch::PageView::ScrollBy(const float dy)
{
  if (Zoomed()) {
    mViewTop += dy;
    Settle();
  }
}

void touch::PageView::Reveal(const int top, const int bottom)
{
  const float margin = kShowMargin * Scale();
  const float screenBottom = mViewTop + bottom * Scale();
  const float screenTop = mViewTop + top * Scale();
  const float lowest = mScreenHeight - mKeyboard - margin;
  if (screenBottom > lowest) {
    ScrollBy(lowest - screenBottom);
  } else if (screenTop < margin) {
    ScrollBy(margin - screenTop);
  }
}

float touch::PageView::PageHeight() const
{
  return (mBottom - mTop) * Scale();
}

float touch::PageView::MostTop() const
{
  const float room = mScreenHeight - mKeyboard - PageHeight();
  return (room > 0 ? room / 2 : 0) - mTop * Scale();
}

float touch::PageView::LeastTop() const
{
  const float room = mScreenHeight - mKeyboard - PageHeight();
  return room > 0 ? MostTop() : room - mTop * Scale();
}

void touch::PageView::Settle()
{
  if (Zoomed()) {
    mViewTop = std::max(LeastTop(), std::min(MostTop(), mViewTop));
  }
}

int touch::PageView::PictureX(const float x) const
{
  return ToPicture(x - ViewLeft(), ViewWidth(), mPictureWidth);
}

int touch::PageView::PictureY(const float y) const
{
  return ToPicture(y - ViewTop(), ViewHeight(), mPictureHeight);
}

float touch::PageView::ScreenX(const float x) const
{
  return ToScreen(x, ViewWidth(), mPictureWidth) + ViewLeft();
}

float touch::PageView::ScreenY(const float y) const
{
  return ToScreen(y, ViewHeight(), mPictureHeight) + ViewTop();
}
