#pragma once

// How the game's picture lies on the screen (android/TOUCH_UI.md). Without a page it fills the screen. With a page, a part of
// the picture that FAF's Lua has declared, the picture is larger: the page's width fills the screen's, and its height is what a
// finger scrolls through. All in the screen's pixels, unless it says the picture's; the picture has a size of its own, which
// this does not change, as the game goes on drawing at it.

#include <vector>

namespace touch
{
  class PageView
  {
  public:
    void SetScreen(int width, int height);
    void SetPicture(int width, int height);

    /// A page in the picture's pixels; false if it is the one there already. A page opens at its top, over the one there. One
    /// that was under it comes back where it stood, and what stood over it has closed: when that opens again it opens at its
    /// top.
    bool SetPage(int left, int top, int right, int bottom);
    /// No page; false if there was none.
    bool ClearPage();

    /// Whether the view is larger than the screen.
    [[nodiscard]] bool Zoomed() const;
    /// The screen's pixels to a picture's pixel.
    [[nodiscard]] float Scale() const;
    /// How much larger than without a page the picture is.
    [[nodiscard]] float Zoom() const;
    [[nodiscard]] int ViewWidth() const;
    [[nodiscard]] int ViewHeight() const;
    [[nodiscard]] float ViewLeft() const;
    [[nodiscard]] float ViewTop() const;

    /// The part of the screen that the keyboard covers, from the bottom: a page can go up that much further.
    void SetKeyboard(int pixels);
    /// The finger went `dy`, positive downwards.
    void ScrollBy(float dy);
    /// Moves the page as far as it takes to show these lines of the picture above the keyboard.
    void Reveal(int top, int bottom);

    /// From the screen's pixels to the picture's. Always on the picture: a finger can leave it, and a mouse button that goes up
    /// off the game's window is lost.
    [[nodiscard]] int PictureX(float x) const;
    [[nodiscard]] int PictureY(float y) const;
    /// From the picture's pixels to the screen's, for what is drawn over the game.
    [[nodiscard]] float ScreenX(float x) const;
    [[nodiscard]] float ScreenY(float y) const;

  private:
    /// A page that another has opened over.
    struct CoveredPage
    {
      int left;
      int top;
      int right;
      int bottom;
      float viewTop;

      [[nodiscard]] bool Is(const int otherLeft, const int otherTop, const int otherRight, const int otherBottom) const
      {
        return left == otherLeft && top == otherTop && right == otherRight && bottom == otherBottom;
      }
    };

    /// Where this page is among those under the one now open, or -1.
    [[nodiscard]] int CoveredIndex(int left, int top, int right, int bottom) const;
    [[nodiscard]] float PageHeight() const;
    /// The view's top with the page's top at the top of the screen, as far down as it goes.
    [[nodiscard]] float MostTop() const;
    /// The view's top with the page's bottom above the keyboard, as far up as it goes.
    [[nodiscard]] float LeastTop() const;
    void Settle();

    int mScreenWidth = 0;
    int mScreenHeight = 0;
    int mPictureWidth = 0;
    int mPictureHeight = 0;
    bool mPage = false;
    int mLeft = 0;
    int mTop = 0;
    int mRight = 0;
    int mBottom = 0;
    /// Where the view's top is on the screen, which the scroll moves.
    float mViewTop = 0;
    int mKeyboard = 0;
    /// The pages under the one now open, the newest last, and where they stood.
    std::vector<CoveredPage> mCovered;
  };
} // namespace touch
