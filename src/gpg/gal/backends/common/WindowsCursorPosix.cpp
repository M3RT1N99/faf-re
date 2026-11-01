// Engine addition (android/CLIENT_PLAN.md, C1): WindowsCursor.h without the Win32 cursor, in
// place of WindowsCursor.cpp. There is no system cursor to set: touch has none, and a mouse's
// pointer icon comes with the Android host's input (C3).

#include "WindowsCursor.h"

namespace gpg::gal
{
  WindowsCursor::~WindowsCursor()
  {
    Destroy();
  }

  void WindowsCursor::Destroy() noexcept
  {
    mPreviousCursor = nullptr;
    mCursorIcon = nullptr;
  }

  HCURSOR WindowsCursor::SetCursor(const CursorContext&)
  {
    return nullptr;
  }

  HCURSOR WindowsCursor::InitCursor()
  {
    return nullptr;
  }

  int WindowsCursor::ShowCursor(const bool show)
  {
    // Win32's display counter: 0 shown, -1 hidden.
    return show ? 0 : -1;
  }
} // namespace gpg::gal
