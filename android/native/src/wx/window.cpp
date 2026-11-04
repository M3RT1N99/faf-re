// wx-lite (android/native/WX_LITE.md): wxWindowBase (src/common/wincmn.cpp) and the MSW port's
// wxWindow (src/msw/window.cpp) of wxWindows 2.4.2 on the message layer: the windows' native half
// is an HWND of include/win32/WinUser.h, whose window procedure hands each message to
// MSWWindowProc, which turns it into wx events as wxWindowMSW does.

#include <Windows.h>

#include <algorithm>
#include <unordered_map>
#include <vector>

#include "Private.h"
#include "wx/app.h"
#include "wx/control.h"
#include "wx/dcclient.h"
#include "wx/sizer.h"
#include "wx/toplevel.h"
#include "wx/window.h"

namespace
{
  /// The wx window of each HWND (wxWinHandleHash).
  std::unordered_map<WXHWND, wxWindow*>& Handles()
  {
    static std::unordered_map<WXHWND, wxWindow*>* const handles = new std::unordered_map<WXHWND, wxWindow*>;
    return *handles;
  }

  wxWindow* gWinBeingCreated = nullptr;

  /// The windows that had the capture before the current one (wxWindowBase::ms_winCaptureNext).
  std::vector<wxWindow*>& CaptureStack()
  {
    static std::vector<wxWindow*>* const stack = new std::vector<wxWindow*>;
    return *stack;
  }

  RECT WindowRectOf(const WXHWND hWnd)
  {
    RECT rect{};
    (void)::GetWindowRect(wxlite::ToHwnd(hWnd), &rect);
    return rect;
  }

  RECT ClientRectOf(const WXHWND hWnd)
  {
    RECT rect{};
    (void)::GetClientRect(wxlite::ToHwnd(hWnd), &rect);
    return rect;
  }

  wxString WindowTextOf(const WXHWND hWnd)
  {
    const HWND hwnd = wxlite::ToHwnd(hWnd);
    const int length = ::GetWindowTextLengthW(hwnd);
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    (void)::GetWindowTextW(hwnd, text.data(), length + 1);
    text.resize(static_cast<std::size_t>(length));
    return wxString(text);
  }

  wxWindow* FindWindowRecursively(const wxWindow* const parent, const wxString& label, const long id, const bool byId)
  {
    if (parent != nullptr) {
      if (byId ? parent->GetId() == id : parent->GetName() == label) {
        return const_cast<wxWindow*>(parent);
      }
      for (wxWindow* const win : parent->GetChildren()) {
        if (wxWindow* const found = FindWindowRecursively(win, label, id, byId)) {
          return found;
        }
      }
    }
    return nullptr;
  }

  wxWindow* FindWindowHelper(const wxWindow* const parent, const wxString& label, const long id, const bool byId)
  {
    if (parent != nullptr) {
      return FindWindowRecursively(parent, label, id, byId);
    }
    for (wxWindow* const win : wxTopLevelWindows) {
      if (wxWindow* const found = FindWindowRecursively(win, label, id, byId)) {
        return found;
      }
    }
    return nullptr;
  }

} // namespace

// --- Handles and the window procedure -------------------------------------------------------------

const wxChar* const wxlite::kCanvasClassName = wxT("wxWindowClass");
const wxChar* const wxlite::kCanvasClassNameNR = wxT("wxWindowClassNR");

wxlite::WindowCreationHook::WindowCreationHook(wxWindow* const winBeingCreated)
{
  gWinBeingCreated = winBeingCreated;
}

wxlite::WindowCreationHook::~WindowCreationHook()
{
  gWinBeingCreated = nullptr;
}

wxWindow* wxFindWinFromHandle(const WXHWND hWnd)
{
  const auto found = Handles().find(hWnd);
  return found != Handles().end() ? found->second : nullptr;
}

wxWindow* wxGetWindowFromHWND(const WXHWND hWnd)
{
  // A native control's window is its wx window here: there are no native controls.
  return hWnd != 0 ? wxFindWinFromHandle(hWnd) : nullptr;
}

void wxlite::AssociateWinWithHandle(const HWND hWnd, wxWindow* const win)
{
  wxCHECK_RET(hWnd != nullptr, wxT("attempt to add a NULL hWnd to window list ignored"));
  if (wxFindWinFromHandle(ToWx(hWnd)) == nullptr) {
    Handles()[ToWx(hWnd)] = win;
  }
}

void wxlite::RemoveHandleAssociation(wxWindow* const win)
{
  Handles().erase(win->GetHWND());
}

LRESULT wxlite::WindowProcedure(const HWND hWnd, const UINT message, const WPARAM wParam, const LPARAM lParam)
{
  wxWindow* wnd = wxFindWinFromHandle(ToWx(hWnd));

  // Is this the first message for a window being created? Then it is ours.
  if (!wnd && gWinBeingCreated) {
    AssociateWinWithHandle(hWnd, gWinBeingCreated);
    wnd = gWinBeingCreated;
    gWinBeingCreated = nullptr;
    wnd->SetHWND(ToWx(hWnd));
  }

  if (wnd) {
    return wnd->MSWWindowProc(message, wParam, lParam);
  }
  return ::DefWindowProcW(hWnd, message, wParam, lParam);
}

// --- Key codes ----------------------------------------------------------------------------------

int wxCharCodeMSWToWX(const int keySym)
{
  int id;
  switch (keySym) {
  case VK_CANCEL: id = WXK_CANCEL; break;
  case VK_BACK: id = WXK_BACK; break;
  case VK_TAB: id = WXK_TAB; break;
  case VK_CLEAR: id = WXK_CLEAR; break;
  case VK_RETURN: id = WXK_RETURN; break;
  case VK_SHIFT: id = WXK_SHIFT; break;
  case VK_CONTROL: id = WXK_CONTROL; break;
  case VK_MENU: id = WXK_MENU; break;
  case VK_PAUSE: id = WXK_PAUSE; break;
  case VK_CAPITAL: id = WXK_CAPITAL; break;
  case VK_SPACE: id = WXK_SPACE; break;
  case VK_ESCAPE: id = WXK_ESCAPE; break;
  case VK_PRIOR: id = WXK_PRIOR; break;
  case VK_NEXT: id = WXK_NEXT; break;
  case VK_END: id = WXK_END; break;
  case VK_HOME: id = WXK_HOME; break;
  case VK_LEFT: id = WXK_LEFT; break;
  case VK_UP: id = WXK_UP; break;
  case VK_RIGHT: id = WXK_RIGHT; break;
  case VK_DOWN: id = WXK_DOWN; break;
  case VK_SELECT: id = WXK_SELECT; break;
  case VK_PRINT: id = WXK_PRINT; break;
  case VK_EXECUTE: id = WXK_EXECUTE; break;
  case VK_INSERT: id = WXK_INSERT; break;
  case VK_DELETE: id = WXK_DELETE; break;
  case VK_HELP: id = WXK_HELP; break;
  case VK_NUMPAD0: id = WXK_NUMPAD0; break;
  case VK_NUMPAD1: id = WXK_NUMPAD1; break;
  case VK_NUMPAD2: id = WXK_NUMPAD2; break;
  case VK_NUMPAD3: id = WXK_NUMPAD3; break;
  case VK_NUMPAD4: id = WXK_NUMPAD4; break;
  case VK_NUMPAD5: id = WXK_NUMPAD5; break;
  case VK_NUMPAD6: id = WXK_NUMPAD6; break;
  case VK_NUMPAD7: id = WXK_NUMPAD7; break;
  case VK_NUMPAD8: id = WXK_NUMPAD8; break;
  case VK_NUMPAD9: id = WXK_NUMPAD9; break;
  case VK_MULTIPLY: id = WXK_NUMPAD_MULTIPLY; break;
  case VK_ADD: id = WXK_NUMPAD_ADD; break;
  case VK_SUBTRACT: id = WXK_NUMPAD_SUBTRACT; break;
  case VK_DECIMAL: id = WXK_NUMPAD_DECIMAL; break;
  case VK_DIVIDE: id = WXK_NUMPAD_DIVIDE; break;
  case VK_F1: id = WXK_F1; break;
  case VK_F2: id = WXK_F2; break;
  case VK_F3: id = WXK_F3; break;
  case VK_F4: id = WXK_F4; break;
  case VK_F5: id = WXK_F5; break;
  case VK_F6: id = WXK_F6; break;
  case VK_F7: id = WXK_F7; break;
  case VK_F8: id = WXK_F8; break;
  case VK_F9: id = WXK_F9; break;
  case VK_F10: id = WXK_F10; break;
  case VK_F11: id = WXK_F11; break;
  case VK_F12: id = WXK_F12; break;
  case VK_F13: id = WXK_F13; break;
  case VK_F14: id = WXK_F14; break;
  case VK_F15: id = WXK_F15; break;
  case VK_F16: id = WXK_F16; break;
  case VK_F17: id = WXK_F17; break;
  case VK_F18: id = WXK_F18; break;
  case VK_F19: id = WXK_F19; break;
  case VK_F20: id = WXK_F20; break;
  case VK_F21: id = WXK_F21; break;
  case VK_F22: id = WXK_F22; break;
  case VK_F23: id = WXK_F23; break;
  case VK_F24: id = WXK_F24; break;
  case VK_NUMLOCK: id = WXK_NUMLOCK; break;
  case VK_SCROLL: id = WXK_SCROLL; break;
  case VK_OEM_1: id = ';'; break;
  case VK_OEM_PLUS: id = '+'; break;
  case VK_OEM_COMMA: id = ','; break;
  case VK_OEM_MINUS: id = '-'; break;
  case VK_OEM_PERIOD: id = '.'; break;
  case VK_OEM_2: id = '/'; break;
  case VK_OEM_3: id = '~'; break;
  case VK_OEM_4: id = '['; break;
  case VK_OEM_5: id = '\\'; break;
  case VK_OEM_6: id = ']'; break;
  case VK_OEM_7: id = '\''; break;
  default: id = 0;
  }
  return id;
}

int wxCharCodeWXToMSW(const int id, bool* const isVirtual)
{
  *isVirtual = true;
  int keySym = 0;
  switch (id) {
  case WXK_CANCEL: keySym = VK_CANCEL; break;
  case WXK_CLEAR: keySym = VK_CLEAR; break;
  case WXK_SHIFT: keySym = VK_SHIFT; break;
  case WXK_CONTROL: keySym = VK_CONTROL; break;
  case WXK_MENU: keySym = VK_MENU; break;
  case WXK_PAUSE: keySym = VK_PAUSE; break;
  case WXK_PRIOR: keySym = VK_PRIOR; break;
  case WXK_NEXT: keySym = VK_NEXT; break;
  case WXK_END: keySym = VK_END; break;
  case WXK_HOME: keySym = VK_HOME; break;
  case WXK_LEFT: keySym = VK_LEFT; break;
  case WXK_UP: keySym = VK_UP; break;
  case WXK_RIGHT: keySym = VK_RIGHT; break;
  case WXK_DOWN: keySym = VK_DOWN; break;
  case WXK_SELECT: keySym = VK_SELECT; break;
  case WXK_PRINT: keySym = VK_PRINT; break;
  case WXK_EXECUTE: keySym = VK_EXECUTE; break;
  case WXK_INSERT: keySym = VK_INSERT; break;
  case WXK_DELETE: keySym = VK_DELETE; break;
  case WXK_HELP: keySym = VK_HELP; break;
  case WXK_NUMPAD0: keySym = VK_NUMPAD0; break;
  case WXK_NUMPAD1: keySym = VK_NUMPAD1; break;
  case WXK_NUMPAD2: keySym = VK_NUMPAD2; break;
  case WXK_NUMPAD3: keySym = VK_NUMPAD3; break;
  case WXK_NUMPAD4: keySym = VK_NUMPAD4; break;
  case WXK_NUMPAD5: keySym = VK_NUMPAD5; break;
  case WXK_NUMPAD6: keySym = VK_NUMPAD6; break;
  case WXK_NUMPAD7: keySym = VK_NUMPAD7; break;
  case WXK_NUMPAD8: keySym = VK_NUMPAD8; break;
  case WXK_NUMPAD9: keySym = VK_NUMPAD9; break;
  case WXK_NUMPAD_MULTIPLY: keySym = VK_MULTIPLY; break;
  case WXK_NUMPAD_ADD: keySym = VK_ADD; break;
  case WXK_NUMPAD_SUBTRACT: keySym = VK_SUBTRACT; break;
  case WXK_NUMPAD_DECIMAL: keySym = VK_DECIMAL; break;
  case WXK_NUMPAD_DIVIDE: keySym = VK_DIVIDE; break;
  case WXK_F1: keySym = VK_F1; break;
  case WXK_F2: keySym = VK_F2; break;
  case WXK_F3: keySym = VK_F3; break;
  case WXK_F4: keySym = VK_F4; break;
  case WXK_F5: keySym = VK_F5; break;
  case WXK_F6: keySym = VK_F6; break;
  case WXK_F7: keySym = VK_F7; break;
  case WXK_F8: keySym = VK_F8; break;
  case WXK_F9: keySym = VK_F9; break;
  case WXK_F10: keySym = VK_F10; break;
  case WXK_F11: keySym = VK_F11; break;
  case WXK_F12: keySym = VK_F12; break;
  case WXK_F13: keySym = VK_F13; break;
  case WXK_F14: keySym = VK_F14; break;
  case WXK_F15: keySym = VK_F15; break;
  case WXK_F16: keySym = VK_F16; break;
  case WXK_F17: keySym = VK_F17; break;
  case WXK_F18: keySym = VK_F18; break;
  case WXK_F19: keySym = VK_F19; break;
  case WXK_F20: keySym = VK_F20; break;
  case WXK_F21: keySym = VK_F21; break;
  case WXK_F22: keySym = VK_F22; break;
  case WXK_F23: keySym = VK_F23; break;
  case WXK_F24: keySym = VK_F24; break;
  case WXK_NUMLOCK: keySym = VK_NUMLOCK; break;
  case WXK_SCROLL: keySym = VK_SCROLL; break;
  default:
    *isVirtual = false;
    keySym = id;
    break;
  }
  return keySym;
}

wxWindow* wxFindWindowAtPointer(wxPoint& pt)
{
  pt = wxGetMousePosition();
  POINT point{pt.x, pt.y};
  HWND hWnd = ::WindowFromPoint(point);
  // The window under the pointer, or its nearest wx ancestor.
  while (hWnd != nullptr && wxFindWinFromHandle(wxlite::ToWx(hWnd)) == nullptr) {
    hWnd = ::GetParent(hWnd);
  }
  return hWnd != nullptr ? wxFindWinFromHandle(wxlite::ToWx(hWnd)) : nullptr;
}

// --- wxWindowBase -------------------------------------------------------------------------------

int wxWindowBase::ms_lastControlId = -200;

BEGIN_EVENT_TABLE(wxWindowBase, wxEvtHandler)
  EVT_INIT_DIALOG(wxWindowBase::OnInitDialog)
END_EVENT_TABLE()

void wxWindowBase::InitBase()
{
  // no window yet, no parent nor children
  m_parent = nullptr;
  m_windowId = -1;

  // no constraints on the minimal window size
  m_minWidth = m_minHeight = m_maxWidth = m_maxHeight = -1;

  // window is created hidden
  m_isShown = false;
  m_isEnabled = true;

  // the default event handler is just this window
  m_eventHandler = this;

  // no validator
  m_windowValidator = nullptr;

  // the colours of a dialog's face and its text, as wxSystemSettings gives them on Windows
  m_backgroundColour = wxColour(0xD4, 0xD0, 0xC8);
  m_foregroundColour = wxColour(0x00, 0x00, 0x00);
  m_hasBgCol = m_hasFgCol = false;

  // no style bits
  m_exStyle = m_windowStyle = 0;

  m_isBeingDeleted = false;

  // an optimization for the event processing: checking this flag is much faster than using
  // IsKindOf(CLASSINFO(wxWindow))
  m_isWindow = true;

  // no sizers
  m_windowSizer = nullptr;
  m_containingSizer = nullptr;
  m_autoLayout = false;
}

bool wxWindowBase::CreateBase(
  wxWindowBase* const parent,
  const wxWindowID id,
  const wxPoint& WXUNUSED(pos),
  const wxSize& WXUNUSED(size),
  const long style,
  const wxValidator& validator,
  const wxString& name
)
{
  // ids are limited to 16 bits under MSW so if you care about portability, it's not a good idea
  // to use ids out of this range (and negative ids are reserved for wxWindows own usage)
  m_windowId = id == -1 ? NewControlId() : id;

  SetName(name);
  SetWindowStyleFlag(style);
  SetParent(parent);
  SetValidator(validator);

  // if the parent window has wxWS_EX_VALIDATE_RECURSIVELY set, we should copy it to the children
  // as well
  if (parent && (parent->GetExtraStyle() & wxWS_EX_VALIDATE_RECURSIVELY)) {
    SetExtraStyle(GetExtraStyle() | wxWS_EX_VALIDATE_RECURSIVELY);
  }
  return true;
}

wxWindowBase::~wxWindowBase()
{
  // FIXME if these 2 cases result from programming errors in the user code we should probably
  //       assert here instead of silently fixing them

  // Just in case the window has been Closed, but we're then deleting immediately: don't leave
  // dangling pointers.
  wxPendingDelete.erase(std::remove(wxPendingDelete.begin(), wxPendingDelete.end(), this), wxPendingDelete.end());

  // Just in case we've loaded a top-level window via LoadNativeDialog but we weren't a dialog
  // class
  wxTopLevelWindows.erase(
    std::remove(wxTopLevelWindows.begin(), wxTopLevelWindows.end(), static_cast<wxWindow*>(this)), wxTopLevelWindows.end()
  );

  delete m_windowValidator;

  if (m_containingSizer) {
    m_containingSizer->Remove(static_cast<wxWindow*>(this));
  }
  delete m_windowSizer;
}

bool wxWindowBase::Destroy()
{
  delete this;
  return true;
}

bool wxWindowBase::Close(const bool force)
{
  wxCloseEvent event(wxEVT_CLOSE_WINDOW, m_windowId);
  event.SetEventObject(this);
  event.SetCanVeto(!force);

  // return FALSE if window wasn't closed because the application vetoed the close event
  return GetEventHandler()->ProcessEvent(event) && !event.GetVeto();
}

bool wxWindowBase::DestroyChildren()
{
  while (!GetChildren().empty()) {
    wxWindow* const child = GetChildren().front();
    child->Show(false);
    delete child;
    // the child removed itself from our list (RemoveChild in its destructor)
  }
  return true;
}

void wxWindowBase::Centre(int direction)
{
  // the position/size of the parent window or of the entire screen
  wxPoint posParent;
  int widthParent = 0;
  int heightParent = 0;

  wxWindow* parent = nullptr;
  if (!(direction & wxCENTRE_ON_SCREEN)) {
    // find the parent to centre this window on: it should be the immediate parent for the
    // controls but the top level parent for the top level windows (like dialogs)
    parent = GetParent();
    if (IsTopLevel()) {
      while (parent && !parent->IsTopLevel()) {
        parent = parent->GetParent();
      }
    }

    // there is no wxTopLevelWindow under wxMotif yet
    if (parent) {
      auto* const winTop = dynamic_cast<wxTopLevelWindow*>(parent);
      if (winTop && winTop->IsIconized()) {
        parent = nullptr;
      }
    }

    // did we find the parent?
    if (!parent) {
      // no other choice
      direction |= wxCENTRE_ON_SCREEN;
    }
  }

  if (direction & wxCENTRE_ON_SCREEN) {
    // centre with respect to the whole screen
    widthParent = ::GetSystemMetrics(SM_CXSCREEN);
    heightParent = ::GetSystemMetrics(SM_CYSCREEN);
  } else {
    if (IsTopLevel()) {
      // centre on the parent
      parent->GetSize(&widthParent, &heightParent);

      // adjust to the parents position
      posParent = parent->GetPosition();
    } else {
      // centre inside the parents client rectangle
      parent->GetClientSize(&widthParent, &heightParent);
    }
  }

  int width, height;
  GetSize(&width, &height);

  int xNew = -1, yNew = -1;

  if (direction & wxHORIZONTAL) {
    xNew = (widthParent - width) / 2;
  }
  if (direction & wxVERTICAL) {
    yNew = (heightParent - height) / 2;
  }

  xNew += posParent.x;
  yNew += posParent.y;

  // Base size of the visible dimensions of the display to take into account the taskbar
  const wxSize size(::GetSystemMetrics(SM_CXSCREEN), ::GetSystemMetrics(SM_CYSCREEN));

  // NB: in wxMSW, negative position may not neccessary mean "out of screen", but this code is
  //     wxMSW specific
  if (posParent.x + widthParent >= 0) { // if parent is (partially) on the main display
    if (xNew < 0) {
      xNew = 0;
    } else if (xNew + width > size.x) {
      xNew = size.x - width - 1;
    }
  }
  if (posParent.y + heightParent >= 0) { // if parent is (partially) on the main display
    if (yNew + height > size.y) {
      yNew = size.y - height - 1;
    }
    // Make certain that the title bar is initially visible always, even if this would push the
    // bottom of the dialog of the visible area of the display
    if (yNew < 0) {
      yNew = 0;
    }
  }

  // move the window to this position (keeping the old size but using SetSize() and not Move()
  // to allow xNew and/or yNew to be -1)
  SetSize(xNew, yNew, width, height, wxSIZE_ALLOW_MINUS_ONE);
}

void wxWindowBase::Fit()
{
  if (!GetChildren().empty()) {
    SetClientSize(DoGetBestSize());
  }
  //else: do nothing if we have no children
}

wxSize wxWindowBase::DoGetBestSize() const
{
  if (m_windowSizer) {
    return m_windowSizer->GetMinSize();
  }
  if (!GetChildren().empty()) {
    // our minimal acceptable size is such that all our windows fit inside
    int maxX = 0, maxY = 0;
    for (wxWindow* const win : GetChildren()) {
      if (win->IsTopLevel()) {
        // dialogs and frames lie in different top level windows - don't deal with them here
        continue;
      }
      int wx, wy, ww, wh;
      win->GetPosition(&wx, &wy);

      // if the window hadn't been positioned yet, assume that it is in the origin
      if (wx == -1) {
        wx = 0;
      }
      if (wy == -1) {
        wy = 0;
      }
      win->GetSize(&ww, &wh);
      if (wx + ww > maxX) {
        maxX = wx + ww;
      }
      if (wy + wh > maxY) {
        maxY = wy + wh;
      }
    }

    // for compatibility with the old versions and because it really looks slightly more pretty
    // like this, add a pad
    maxX += 7;
    maxY += 14;
    return wxSize(maxX, maxY);
  }
  // for a generic window there is no natural best size - just use the current one
  return GetSize();
}

wxPoint wxWindowBase::GetClientAreaOrigin() const
{
  return wxPoint(0, 0);
}

void wxWindowBase::SetSizeHints(const int minW, const int minH, const int maxW, const int maxH, int WXUNUSED(incW), int WXUNUSED(incH))
{
  m_minWidth = minW;
  m_maxWidth = maxW;
  m_minHeight = minH;
  m_maxHeight = maxH;
}

bool wxWindowBase::Show(const bool show)
{
  if (show != m_isShown) {
    m_isShown = show;
    return true;
  }
  return false;
}

bool wxWindowBase::Enable(const bool enable)
{
  if (enable != m_isEnabled) {
    m_isEnabled = enable;
    return true;
  }
  return false;
}

bool wxWindowBase::IsTopLevel() const
{
  return false;
}

void wxWindowBase::SetParent(wxWindowBase* const parent)
{
  m_parent = static_cast<wxWindow*>(parent);
}

void wxWindowBase::AddChild(wxWindowBase* const child)
{
  wxCHECK_RET(child, wxT("can't add a NULL child"));

  GetChildren().push_back(static_cast<wxWindow*>(child));
  child->SetParent(this);
}

void wxWindowBase::RemoveChild(wxWindowBase* const child)
{
  wxCHECK_RET(child, wxT("can't remove a NULL child"));

  wxWindowList& children = GetChildren();
  children.erase(std::remove(children.begin(), children.end(), static_cast<wxWindow*>(child)), children.end());
  child->SetParent(nullptr);
}

bool wxWindowBase::Reparent(wxWindowBase* const newParent)
{
  wxWindow* const oldParent = GetParent();
  if (newParent == oldParent) {
    // nothing done
    return false;
  }

  // unlink this window from the existing parent.
  if (oldParent) {
    oldParent->RemoveChild(this);
  } else {
    wxTopLevelWindows.erase(
      std::remove(wxTopLevelWindows.begin(), wxTopLevelWindows.end(), static_cast<wxWindow*>(this)), wxTopLevelWindows.end()
    );
  }

  // add it to the new one
  if (newParent) {
    newParent->AddChild(this);
  } else {
    wxTopLevelWindows.push_back(static_cast<wxWindow*>(this));
  }
  return true;
}

wxWindow* wxWindowBase::FindWindow(const long id)
{
  return FindWindowRecursively(static_cast<wxWindow*>(this), wxEmptyString, id, true);
}

wxWindow* wxWindowBase::FindWindow(const wxString& name)
{
  return FindWindowRecursively(static_cast<wxWindow*>(this), name, 0, false);
}

wxWindow* wxWindowBase::FindWindowById(const long id, const wxWindow* const parent)
{
  return FindWindowHelper(parent, wxEmptyString, id, true);
}

wxWindow* wxWindowBase::FindWindowByName(const wxString& name, const wxWindow* const parent)
{
  return FindWindowHelper(parent, name, 0, false);
}

void wxWindowBase::PushEventHandler(wxEvtHandler* const handler)
{
  wxEvtHandler* const handlerOld = GetEventHandler();
  handler->SetNextHandler(handlerOld);
  if (handlerOld) {
    GetEventHandler()->SetPreviousHandler(handler);
  }
  SetEventHandler(handler);
}

wxEvtHandler* wxWindowBase::PopEventHandler(const bool deleteHandler)
{
  wxEvtHandler* handlerA = GetEventHandler();
  if (handlerA) {
    wxEvtHandler* const handlerB = handlerA->GetNextHandler();
    handlerA->SetNextHandler(nullptr);
    if (handlerB) {
      handlerB->SetPreviousHandler(nullptr);
    }
    SetEventHandler(handlerB);
    if (deleteHandler) {
      delete handlerA;
      handlerA = nullptr;
    }
  }
  return handlerA;
}

bool wxWindowBase::RemoveEventHandler(wxEvtHandler* const handler)
{
  wxCHECK_MSG(handler, false, wxT("RemoveEventHandler(NULL) called"));

  wxEvtHandler* handlerPrev = nullptr;
  wxEvtHandler* handlerCur = GetEventHandler();
  while (handlerCur) {
    wxEvtHandler* const handlerNext = handlerCur->GetNextHandler();
    if (handlerCur == handler) {
      if (handlerPrev) {
        handlerPrev->SetNextHandler(handlerNext);
      } else {
        SetEventHandler(handlerNext);
      }
      if (handlerNext) {
        handlerNext->SetPreviousHandler(handlerPrev);
      }
      handler->SetNextHandler(nullptr);
      return true;
    }
    handlerPrev = handlerCur;
    handlerCur = handlerNext;
  }

  wxFAIL_MSG(wxT("where has the event handler gone?"));
  return false;
}

void wxWindowBase::SetValidator(const wxValidator& validator)
{
  delete m_windowValidator;
  m_windowValidator = static_cast<wxValidator*>(validator.Clone());
  if (m_windowValidator) {
    m_windowValidator->SetWindow(this);
  }
}

bool wxWindowBase::Validate()
{
  const bool recurse = (GetExtraStyle() & wxWS_EX_VALIDATE_RECURSIVELY) != 0;
  for (wxWindow* const child : m_children) {
    wxValidator* const validator = child->GetValidator();
    if (validator && !validator->Validate(static_cast<wxWindow*>(this))) {
      return false;
    }
    if (recurse && !child->Validate()) {
      return false;
    }
  }
  return true;
}

bool wxWindowBase::TransferDataToWindow()
{
  const bool recurse = (GetExtraStyle() & wxWS_EX_VALIDATE_RECURSIVELY) != 0;
  for (wxWindow* const child : m_children) {
    wxValidator* const validator = child->GetValidator();
    if (validator && !validator->TransferToWindow()) {
      return false;
    }
    if (recurse && !child->TransferDataToWindow()) {
      // warning already given
      return false;
    }
  }
  return true;
}

bool wxWindowBase::TransferDataFromWindow()
{
  const bool recurse = (GetExtraStyle() & wxWS_EX_VALIDATE_RECURSIVELY) != 0;
  for (wxWindow* const child : m_children) {
    wxValidator* const validator = child->GetValidator();
    if (validator && !validator->TransferFromWindow()) {
      // nop warning here because the application is supposed to give one itself - we don't know
      // here what might have gone wrongly
      return false;
    }
    if (recurse && !child->TransferDataFromWindow()) {
      // warning already given
      return false;
    }
  }
  return true;
}

void wxWindowBase::InitDialog()
{
  wxInitDialogEvent event(GetId());
  event.SetEventObject(this);
  GetEventHandler()->ProcessEvent(event);
}

void wxWindowBase::OnInitDialog(wxInitDialogEvent& WXUNUSED(event))
{
  TransferDataToWindow();
  // wxWindows 2.4.2 updates the UI here (UpdateWindowUI): wx-lite sends no wxEVT_UPDATE_UI, as
  // no window of the engine on Android has a handler for it.
}

void wxWindowBase::CaptureMouse()
{
  wxWindow* const winOld = GetCapture();
  if (winOld) {
    static_cast<wxWindowBase*>(winOld)->DoReleaseMouse();
    // save it on stack
    CaptureStack().push_back(winOld);
  }
  DoCaptureMouse();
}

void wxWindowBase::ReleaseMouse()
{
  wxASSERT_MSG(GetCapture() == this, wxT("attempt to release mouse, but this window hasn't captured it"));
  DoReleaseMouse();
  if (!CaptureStack().empty()) {
    wxWindow* const next = CaptureStack().back();
    CaptureStack().pop_back();
    static_cast<wxWindowBase*>(next)->DoCaptureMouse();
  }
}

wxWindow* wxWindowBase::GetCapture()
{
  const HWND hwnd = ::GetCapture();
  return hwnd ? wxFindWinFromHandle(wxlite::ToWx(hwnd)) : nullptr;
}

bool wxWindowBase::HasCapture() const
{
  return static_cast<const wxWindow*>(this) == GetCapture();
}

wxWindow* wxWindowBase::FindFocus()
{
  const HWND hWnd = ::GetFocus();
  return hWnd ? wxGetWindowFromHWND(wxlite::ToWx(hWnd)) : nullptr;
}

bool wxWindowBase::SetBackgroundColour(const wxColour& colour)
{
  if (!colour.Ok() || (colour == m_backgroundColour)) {
    return false;
  }
  m_backgroundColour = colour;
  m_hasBgCol = true;
  return true;
}

bool wxWindowBase::SetForegroundColour(const wxColour& colour)
{
  if (!colour.Ok() || (colour == m_foregroundColour)) {
    return false;
  }
  m_foregroundColour = colour;
  m_hasFgCol = true;
  return true;
}

bool wxWindowBase::Layout()
{
  // If there is a sizer, use it instead of the constraints
  if (GetSizer()) {
    int w, h;
    // A window's virtual size is its client size here: wx-lite has no scrolled windows.
    GetClientSize(&w, &h);
    GetSizer()->SetDimension(0, 0, w, h);
  }
  return true;
}

void wxWindowBase::SetSizer(wxSizer* const sizer, const bool deleteOld)
{
  if (m_windowSizer && deleteOld) {
    delete m_windowSizer;
  }
  m_windowSizer = sizer;
  SetAutoLayout(sizer != nullptr);
}

void wxWindowBase::SetSizerAndFit(wxSizer* const sizer, const bool deleteOld)
{
  SetSizer(sizer, deleteOld);
  sizer->SetSizeHints(static_cast<wxWindow*>(this));
}

// --- wxWindow (MSW) -------------------------------------------------------------------------------

BEGIN_EVENT_TABLE(wxWindow, wxWindowBase)
  EVT_ERASE_BACKGROUND(wxWindow::OnEraseBackground)
  EVT_IDLE(wxWindow::OnIdle)
END_EVENT_TABLE()

void wxWindow::Init()
{
  // generic
  InitBase();

  m_isBeingDeleted = false;
  m_mouseInWindow = false;
  m_lastKeydownProcessed = false;
  m_childrenDisabled = nullptr;
  m_hWnd = 0;

  // as all windows are created with WS_VISIBLE style...
  m_isShown = true;
}

wxWindow::~wxWindow()
{
  m_isBeingDeleted = true;

  for (wxWindow* win = GetParent(); win; win = win->GetParent()) {
    auto* const frame = dynamic_cast<wxTopLevelWindow*>(win);
    if (frame) {
      if (frame->GetLastFocus() == this) {
        frame->SetLastFocus(nullptr);
      }
      break;
    }
  }

  DestroyChildren();

  if (m_parent) {
    m_parent->RemoveChild(this);
  }

  if (m_hWnd) {
    // VZ: test temp removed to understand what really happens here
    (void)::DestroyWindow(wxlite::ToHwnd(m_hWnd));

    // remove hWnd <-> wxWindow association
    wxlite::RemoveHandleAssociation(this);
  }

  delete m_childrenDisabled;
}

bool wxWindow::Create(
  wxWindow* const parent, const wxWindowID id, const wxPoint& pos, const wxSize& size, const long style, const wxString& name
)
{
  wxCHECK_MSG(parent, false, wxT("can't create wxWindow without parent"));

  if (!CreateBase(parent, id, pos, size, style, wxDefaultValidator, name)) {
    return false;
  }

  parent->AddChild(this);

  WXDWORD exstyle;
  DWORD msflags = static_cast<DWORD>(MSWGetCreateWindowFlags(&exstyle));

  // all windows are created visible
  msflags |= WS_VISIBLE;

  return MSWCreate(wxlite::kCanvasClassName, nullptr, pos, size, msflags, exstyle);
}

void wxWindow::SetFocus()
{
  const HWND hWnd = wxlite::ToHwnd(GetHWND());
  wxCHECK_RET(hWnd, wxT("can't set focus to invalid window"));
  (void)::SetFocus(hWnd);
}

void wxWindow::SetFocusFromKbd()
{
  wxWindowBase::SetFocusFromKbd();
}

bool wxWindow::Enable(const bool enable)
{
  if (!wxWindowBase::Enable(enable)) {
    return false;
  }

  const HWND hWnd = wxlite::ToHwnd(GetHWND());
  if (hWnd) {
    ::EnableWindow(hWnd, enable ? TRUE : FALSE);
  }

  // the logic below doesn't apply to the top level windows -- otherwise showing a modal dialog
  // would result in total greying out (and disabling of all controls in it) of the parent
  // window
  if (IsTopLevel()) {
    return true;
  }

  // when the parent is disabled, all of its children should be disabled as well but when it is
  // enabled back, only those of the children which hadn't been already disabled in the beginning
  // should be enabled again, so we have to keep the list of those children
  for (wxWindow* const child : GetChildren()) {
    if (child->IsTopLevel()) {
      // the logic below doesn't apply to top level children
      continue;
    }
    if (enable) {
      // enable the child back unless it had been disabled before us
      if (!m_childrenDisabled
          || std::find(m_childrenDisabled->begin(), m_childrenDisabled->end(), child) == m_childrenDisabled->end()) {
        child->Enable();
      }
    } else { // we're being disabled
      if (child->IsEnabled()) {
        // disable it as children shouldn't stay enabled while the parent is not
        child->Disable();
      } else { // child already disabled, remember it
        if (!m_childrenDisabled) {
          m_childrenDisabled = new wxWindowList;
        }
        m_childrenDisabled->push_back(child);
      }
    }
  }

  if (enable && m_childrenDisabled) {
    // we don't need this list any more, don't keep unused memory
    delete m_childrenDisabled;
    m_childrenDisabled = nullptr;
  }
  return true;
}

bool wxWindow::Show(const bool show)
{
  if (!wxWindowBase::Show(show)) {
    return false;
  }

  const HWND hWnd = wxlite::ToHwnd(GetHWND());
  ::ShowWindow(hWnd, show ? SW_SHOW : SW_HIDE);

  if (show && IsTopLevel()) {
    ::BringWindowToTop(hWnd);
  }
  return true;
}

void wxWindow::Raise()
{
  ::BringWindowToTop(wxlite::ToHwnd(GetHWND()));
}

void wxWindow::Lower()
{
  ::SetWindowPos(wxlite::ToHwnd(GetHWND()), HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void wxWindow::SetTitle(const wxString& title)
{
  ::SetWindowTextW(wxlite::ToHwnd(GetHWND()), title.c_str());
}

wxString wxWindow::GetTitle() const
{
  return WindowTextOf(GetHWND());
}

void wxWindow::DoCaptureMouse()
{
  const HWND hWnd = wxlite::ToHwnd(GetHWND());
  if (hWnd) {
    ::SetCapture(hWnd);
  }
}

void wxWindow::DoReleaseMouse()
{
  (void)::ReleaseCapture();
}

void wxWindow::WarpPointer(int x, int y)
{
  ClientToScreen(&x, &y);
  (void)::SetCursorPos(x, y);
}

bool wxWindow::Reparent(wxWindowBase* const parent)
{
  if (!wxWindowBase::Reparent(parent)) {
    return false;
  }
  const HWND hWndChild = wxlite::ToHwnd(GetHWND());
  const HWND hWndParent = GetParent() ? wxlite::GetHwndOf(GetParent()) : nullptr;
  ::SetParent(hWndChild, hWndParent);
  return true;
}

void wxWindow::Clear()
{
  // wxWindows 2.4.2 fills the client area with the background colour; wx-lite's device contexts
  // draw nothing (wx/dc.h).
}

void wxWindow::Refresh(const bool eraseBack, const wxRect* const rect)
{
  const HWND hWnd = wxlite::ToHwnd(GetHWND());
  if (hWnd) {
    if (rect) {
      RECT mswRect;
      mswRect.left = rect->x;
      mswRect.top = rect->y;
      mswRect.right = rect->x + rect->width;
      mswRect.bottom = rect->y + rect->height;
      ::InvalidateRect(hWnd, &mswRect, eraseBack ? TRUE : FALSE);
    } else {
      ::InvalidateRect(hWnd, nullptr, eraseBack ? TRUE : FALSE);
    }
  }
}

void wxWindow::Update()
{
  (void)::UpdateWindow(wxlite::ToHwnd(GetHWND()));
}

void wxWindow::SetWindowStyleFlag(const long flags)
{
  const long flagsOld = GetWindowStyleFlag();
  if (flags == flagsOld) {
    return;
  }

  // update the internal variable
  wxWindowBase::SetWindowStyleFlag(flags);

  // now update the Windows style as well if needed - and if the window had been already created
  if (!GetHWND()) {
    return;
  }

  const HWND hWnd = wxlite::ToHwnd(GetHWND());
  WXDWORD exstyle, exstyleOld;
  const long style = static_cast<long>(MSWGetStyle(flags, &exstyle));
  const long styleOld = static_cast<long>(MSWGetStyle(flagsOld, &exstyleOld));

  if (style != styleOld) {
    // some flags (e.g. WS_VISIBLE or WS_DISABLED) should not be changed by this function so
    // instead of simply setting the style to the new value we clear the bits which were set in
    // styleOld but are set in the new one and set the ones which were not set before
    long styleReal = ::GetWindowLongW(hWnd, GWL_STYLE);
    styleReal &= ~styleOld;
    styleReal |= style;
    ::SetWindowLongW(hWnd, GWL_STYLE, styleReal);
  }

  // and the extended style
  if (exstyle != exstyleOld) {
    long exstyleReal = ::GetWindowLongW(hWnd, GWL_EXSTYLE);
    exstyleReal &= ~static_cast<long>(exstyleOld);
    exstyleReal |= static_cast<long>(exstyle);
    ::SetWindowLongW(hWnd, GWL_EXSTYLE, exstyleReal);

    // we must call SetWindowPos() to flash the cached extended style and also to make the
    // change to wxSTAY_ON_TOP style take effect: just setting the style simply doesn't work
    ::SetWindowPos(hWnd, (exstyleReal & WS_EX_TOPMOST) ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
  }
}

WXDWORD wxWindow::MSWGetStyle(const long flags, WXDWORD* const exstyle) const
{
  // translate the style
  WXDWORD style = WS_CHILD | WS_VISIBLE;

  if (flags & wxCLIP_CHILDREN) {
    style |= WS_CLIPCHILDREN;
  }
  if (flags & wxCLIP_SIBLINGS) {
    style |= WS_CLIPSIBLINGS;
  }
  if (flags & wxVSCROLL) {
    style |= WS_VSCROLL;
  }
  if (flags & wxHSCROLL) {
    style |= WS_HSCROLL;
  }

  const wxBorder border = static_cast<wxBorder>(flags & wxBORDER_MASK);

  // WS_BORDER is only required for wxBORDER_SIMPLE
  if (border & wxBORDER_SIMPLE) {
    style |= WS_BORDER;
  }

  // now deal with ext style if the caller wants it
  if (exstyle) {
    *exstyle = 0;

    if (flags & wxTRANSPARENT_WINDOW) {
      *exstyle |= WS_EX_TRANSPARENT;
    }

    switch (border) {
    default:
    case wxBORDER_NONE:
    case wxBORDER_SIMPLE:
    case wxBORDER_DEFAULT:
      break;
    case wxBORDER_STATIC:
      *exstyle |= WS_EX_STATICEDGE;
      break;
    case wxBORDER_RAISED:
    case wxBORDER_DOUBLE:
      *exstyle |= WS_EX_DLGMODALFRAME;
      break;
    case wxBORDER_SUNKEN:
      *exstyle |= WS_EX_CLIENTEDGE;
      style &= ~static_cast<WXDWORD>(WS_BORDER);
      break;
    }

    // wxUniv doesn't use Windows dialog navigation functions at all
    if ((flags & wxTAB_TRAVERSAL) && !IsTopLevel()) {
      // we need this to make the keyboard navigation work inside us
      *exstyle |= WS_EX_CONTROLPARENT;
    }
  }
  return style;
}

WXHWND wxWindow::MSWGetParent() const
{
  return m_parent ? m_parent->GetHWND() : WXHWND(0);
}

bool wxWindow::MSWCreate(
  const wxChar* const wclass,
  const wxChar* const title,
  const wxPoint& pos,
  const wxSize& size,
  const WXDWORD style,
  const WXDWORD extendedStyle
)
{
  // choose the position/size for the new window (MSWGetCreateWindowCoords: -1 is the default)
  int x, y, w, h;
  if (pos.x == -1) {
    x = y = CW_USEDEFAULT;
  } else {
    x = pos.x;
    y = pos.y == -1 ? 200 : pos.y;
  }
  if (size.x == -1) {
    w = h = CW_USEDEFAULT;
  } else {
    w = size.x;
    h = size.y == -1 ? 250 : size.y;
  }

  // controlId is menu handle for the top level windows, so set it to 0 unless we're creating a
  // child window
  const int controlId = (style & WS_CHILD) ? GetId() : 0;

  // for each class "Foo" we have we also have "FooNR" ("no repaint") class which differs only in
  // its default flags (DLGC_WANT* and CS_[HV]REDRAW)
  wxString className(wclass);
  if (GetWindowStyleFlag() & wxNO_FULL_REPAINT_ON_RESIZE) {
    className = wxString(wxlite::kCanvasClassNameNR);
  }

  // do create the window
  wxlite::WindowCreationHook hook(this);

  m_hWnd = wxlite::ToWx(::CreateWindowExW(
    static_cast<DWORD>(extendedStyle),
    className.c_str(),
    title ? title : wxT(""),
    static_cast<DWORD>(style),
    x,
    y,
    w,
    h,
    wxlite::ToHwnd(MSWGetParent()),
    reinterpret_cast<HMENU>(static_cast<LONG_PTR>(controlId)),
    nullptr,
    nullptr // no extra data
  ));

  if (!m_hWnd) {
    return false;
  }

  SubclassWin(m_hWnd);
  return true;
}

void wxWindow::SubclassWin(const WXHWND hWnd)
{
  // The window's class has wx-lite's window procedure already: there are no native windows to
  // subclass here, only the association to make.
  wxlite::AssociateWinWithHandle(wxlite::ToHwnd(hWnd), this);
}

void wxWindow::UnsubclassWin()
{
  wxlite::RemoveHandleAssociation(this);
}

wxWindow* wxWindow::FindItemByHWND(const WXHWND hWnd, const bool controlOnly) const
{
  for (wxWindow* const parent : GetChildren()) {
    // Do a recursive search.
    if (wxWindow* const found = parent->FindItemByHWND(hWnd)) {
      return found;
    }
    if (!controlOnly || dynamic_cast<const wxControl*>(parent) != nullptr) {
      if (parent->GetHWND() == hWnd) {
        return parent;
      }
    }
  }
  return nullptr;
}

bool wxWindow::IsMouseInWindow() const
{
  // get the mouse position
  POINT pt;
  ::GetCursorPos(&pt);

  // find the window which currently has the cursor and go up the window chain until we find this
  // window - or exhaust it
  HWND hwnd = ::WindowFromPoint(pt);
  while (hwnd && (hwnd != wxlite::ToHwnd(GetHWND()))) {
    hwnd = ::GetParent(hwnd);
  }
  return hwnd != nullptr;
}

void wxWindow::OnIdle(wxIdleEvent& WXUNUSED(event))
{
  // Check if we need to send a LEAVE event
  if (m_mouseInWindow) {
    // note that we should generate the leave event whether the window has or doesn't have mouse
    // capture
    if (!IsMouseInWindow()) {
      // Generate a LEAVE event
      m_mouseInWindow = false;

      // Excel/PowerPoint/Word modify the state of the mouse (like MK_SHIFT), so the next mouse
      // events don't contain this information
      int state = 0;
      if (wxlite::IsShiftDown()) {
        state |= MK_SHIFT;
      }
      if (wxlite::IsCtrlDown()) {
        state |= MK_CONTROL;
      }
      if (::GetKeyState(VK_LBUTTON)) {
        state |= MK_LBUTTON;
      }
      if (::GetKeyState(VK_MBUTTON)) {
        state |= MK_MBUTTON;
      }
      if (::GetKeyState(VK_RBUTTON)) {
        state |= MK_RBUTTON;
      }

      POINT pt;
      ::GetCursorPos(&pt);

      // we need to have client coordinates here for symmetry with wxEVT_ENTER_WINDOW
      const RECT rect = WindowRectOf(GetHWND());
      pt.x -= rect.left;
      pt.y -= rect.top;

      wxMouseEvent event2(wxEVT_LEAVE_WINDOW);
      InitMouseEvent(event2, pt.x, pt.y, static_cast<WXUINT>(state));

      (void)GetEventHandler()->ProcessEvent(event2);
    }
  }
}

bool wxWindow::ScrollLines(int WXUNUSED(lines))
{
  // no scroll bars in the message layer
  return false;
}

bool wxWindow::ScrollPages(int WXUNUSED(pages))
{
  return false;
}

void wxWindow::OnEraseBackground(wxEraseEvent& WXUNUSED(event))
{
  // wxWindows 2.4.2 fills the client area with the background colour here; wx-lite's device
  // contexts draw nothing (wx/dc.h).
}

void wxWindow::OnPaint(wxPaintEvent& event)
{
  // Not in the window's table, as in wxWindows 2.4.2: a window without a paint handler of its own
  // leaves WM_PAINT to DefWindowProc, which validates it.
  event.Skip();
}

// --- Geometry -----------------------------------------------------------------------------------

void wxWindow::DoGetSize(int* const x, int* const y) const
{
  const RECT rect = WindowRectOf(GetHWND());
  if (x) {
    *x = rect.right - rect.left;
  }
  if (y) {
    *y = rect.bottom - rect.top;
  }
}

void wxWindow::DoGetClientSize(int* const x, int* const y) const
{
  const RECT rect = ClientRectOf(GetHWND());
  if (x) {
    *x = rect.right;
  }
  if (y) {
    *y = rect.bottom;
  }
}

void wxWindow::DoGetPosition(int* const x, int* const y) const
{
  const RECT rect = WindowRectOf(GetHWND());

  POINT point;
  point.x = rect.left;
  point.y = rect.top;

  // we do the adjustments with respect to the parent only for the "real" children, not for the
  // dialogs/frames
  if (!IsTopLevel()) {
    wxWindow* const parent = GetParent();
    const HWND hParentWnd = parent ? wxlite::GetHwndOf(parent) : nullptr;

    // Since we now have the absolute screen coords, if there's a parent we must subtract its
    // top left corner
    if (hParentWnd) {
      ::ScreenToClient(hParentWnd, &point);
    }

    if (parent) {
      // We may be faking the client origin. So a window that's really at (0, 30) may appear
      // (to wxWin apps) to be at (0, 0).
      const wxPoint pt(parent->GetClientAreaOrigin());
      point.x -= pt.x;
      point.y -= pt.y;
    }
  }

  if (x) {
    *x = point.x;
  }
  if (y) {
    *y = point.y;
  }
}

void wxWindow::DoScreenToClient(int* const x, int* const y) const
{
  POINT pt{x ? *x : 0, y ? *y : 0};
  ::ScreenToClient(wxlite::ToHwnd(GetHWND()), &pt);
  if (x) {
    *x = pt.x;
  }
  if (y) {
    *y = pt.y;
  }
}

void wxWindow::DoClientToScreen(int* const x, int* const y) const
{
  POINT pt{x ? *x : 0, y ? *y : 0};
  ::ClientToScreen(wxlite::ToHwnd(GetHWND()), &pt);
  if (x) {
    *x = pt.x;
  }
  if (y) {
    *y = pt.y;
  }
}

void wxWindow::DoMoveWindow(const int x, const int y, int width, int height)
{
  // Moves the window to the new location and size (or the default ones)
  if (width < 0) {
    width = 0;
  }
  if (height < 0) {
    height = 0;
  }
  ::MoveWindow(wxlite::ToHwnd(GetHWND()), x, y, width, height, TRUE);
}

void wxWindow::DoSetSize(int x, int y, int width, int height, const int sizeFlags)
{
  // get the current size and position...
  int currentX, currentY;
  GetPosition(&currentX, &currentY);
  int currentW, currentH;
  GetSize(&currentW, &currentH);

  // ... and don't do anything (avoiding flicker) if it's already ok
  if (x == currentX && y == currentY && width == currentW && height == currentH) {
    return;
  }

  if (x == -1 && !(sizeFlags & wxSIZE_ALLOW_MINUS_ONE)) {
    x = currentX;
  }
  if (y == -1 && !(sizeFlags & wxSIZE_ALLOW_MINUS_ONE)) {
    y = currentY;
  }

  wxSize size(-1, -1);
  if (width == -1) {
    if (sizeFlags & wxSIZE_AUTO_WIDTH) {
      size = DoGetBestSize();
      width = size.x;
    } else {
      // just take the current one
      width = currentW;
    }
  }

  if (height == -1) {
    if (sizeFlags & wxSIZE_AUTO_HEIGHT) {
      if (size.x == -1) {
        size = DoGetBestSize();
      }
      //else: already called DoGetBestSize() above
      height = size.y;
    } else {
      // just take the current one
      height = currentH;
    }
  }

  DoMoveWindow(x, y, width, height);
}

void wxWindow::DoSetClientSize(const int width, const int height)
{
  // setting the client size is less obvious than it it could have been because in the result of
  // changing the total size the window scrollbar may [dis]appear and/or its menubar may [un]wrap
  // and this would change the client size again and so we must keep doing it until we get the
  // correct new size
  for (int i = 0; i < 4; i++) {
    RECT rectClient;
    ::GetClientRect(wxlite::ToHwnd(GetHWND()), &rectClient);

    // if the size is already ok, stop here (rectClient.left = top = 0)
    if ((rectClient.right == width || width == -1) && (rectClient.bottom == height || height == -1)) {
      break;
    }

    int widthClient = width;
    int heightClient = height;

    // Find the difference between the entire window (title bar and all) and the client area; add
    // this to the new client size to move the window
    RECT rectWin;
    ::GetWindowRect(wxlite::ToHwnd(GetHWND()), &rectWin);

    widthClient += rectWin.right - rectWin.left - rectClient.right;
    heightClient += rectWin.bottom - rectWin.top - rectClient.bottom;

    POINT point;
    point.x = rectWin.left;
    point.y = rectWin.top;

    // MoveWindow positions the child windows relative to the parent, so adjust if necessary
    if (!IsTopLevel()) {
      wxWindow* const parent = GetParent();
      if (parent) {
        ::ScreenToClient(wxlite::GetHwndOf(parent), &point);
      }
    }

    DoMoveWindow(point.x, point.y, widthClient, heightClient);
  }
}

int wxWindow::GetCharHeight() const
{
  return wxlite::kCharHeight;
}

int wxWindow::GetCharWidth() const
{
  return wxlite::kCharWidth;
}

// --- Messages -----------------------------------------------------------------------------------

long wxWindow::MSWDefWindowProc(const WXUINT nMsg, const WXWPARAM wParam, const WXLPARAM lParam)
{
  return static_cast<long>(::DefWindowProcW(wxlite::ToHwnd(GetHWND()), nMsg, wParam, lParam));
}

bool wxWindow::MSWShouldPreProcessMessage(WXMSG* WXUNUSED(pMsg))
{
  // preprocess all messages by default
  return true;
}

bool wxWindow::MSWProcessMessage(WXMSG* WXUNUSED(pMsg))
{
  // wxWindows 2.4.2 does the keyboard navigation of dialogs here (Tab, the arrows, Enter on the
  // default button, Escape on the cancel button, IsDialogMessage). On Android no wx dialog is
  // shown, so there is nothing to navigate.
  return false;
}

bool wxWindow::MSWTranslateMessage(WXMSG* WXUNUSED(pMsg))
{
  // the accelerators: wx-lite's windows have none
  return false;
}

void wxWindow::MSWDestroyWindow() {}

long wxWindow::MSWWindowProc(const WXUINT message, const WXWPARAM wParam, const WXLPARAM lParam)
{
  // did we process the message?
  bool processed = false;

  // the return value
  union {
    bool allow;
    WXLPARAM result;
  } rc;

  // for most messages we should return 0 when we do process the message
  rc.result = 0;

  switch (message) {
  case WM_CREATE: {
    bool mayCreate;
    processed = HandleCreate(reinterpret_cast<void*>(lParam), &mayCreate);
    if (processed) {
      // return 0 to allow window creation
      rc.result = mayCreate ? 0 : -1;
    }
  } break;

  case WM_DESTROY:
    // never set processed to TRUE and *always* pass WM_DESTROY to DefWindowProc() as Windows may
    // do some internal cleanup in it
    (void)HandleDestroy();
    break;

  case WM_MOVE:
    processed = HandleMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
    break;

  case WM_SIZE:
    switch (wParam) {
    case SIZE_MINIMIZED:
      // we're not interested in these messages at all
      break;
    case SIZE_MAXIMIZED:
    case SIZE_RESTORED:
    default:
      processed = HandleSize(LOWORD(lParam), HIWORD(lParam), static_cast<WXUINT>(wParam));
      break;
    }
    break;

  case WM_ACTIVATEAPP:
    wxTheApp->SetActive(wParam != 0, FindFocus());
    break;

  case WM_ACTIVATE:
    processed = HandleActivate(LOWORD(wParam), HIWORD(wParam) != 0, static_cast<WXHWND>(lParam));
    break;

  case WM_SETFOCUS:
    processed = HandleSetFocus(static_cast<WXHWND>(wParam));
    break;

  case WM_KILLFOCUS:
    processed = HandleKillFocus(static_cast<WXHWND>(wParam));
    break;

  case WM_PAINT:
    processed = HandlePaint();
    break;

  case WM_CLOSE:
    // don't let the DefWindowProc() destroy our window - we'll do it ourselves in ~wxWindow
    Close();
    processed = true;
    rc.result = TRUE;
    break;

  case WM_SHOWWINDOW:
    processed = HandleShow(wParam != 0, static_cast<int>(lParam));
    break;

  case WM_MOUSEMOVE:
    processed = HandleMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), static_cast<WXUINT>(wParam));
    break;

  case WM_MOUSEWHEEL:
    processed = HandleMouseWheel(wParam, lParam);
    break;

  case WM_LBUTTONDOWN:
  case WM_LBUTTONUP:
  case WM_LBUTTONDBLCLK:
  case WM_RBUTTONDOWN:
  case WM_RBUTTONUP:
  case WM_RBUTTONDBLCLK:
  case WM_MBUTTONDOWN:
  case WM_MBUTTONUP:
  case WM_MBUTTONDBLCLK: {
    // A disabled window gets no mouse events, and neither do the windows inside one (the mouse
    // event of a disabled child goes to its parent from WindowFromPoint already).
    for (wxWindow* win = this;;) {
      if (!win->IsEnabled()) {
        processed = true;
        break;
      }
      win = win->GetParent();
      if (!win || win->IsTopLevel()) {
        break;
      }
    }
    if (processed) {
      break;
    }

    // wxWindows 2.4.2 looks for a child window under the pointer here (FindWindowForMouseEvent),
    // for the native controls that do not take the mouse themselves; the message layer gives the
    // message to the window under the pointer already, so this is that window.
    const int x = GET_X_LPARAM(lParam);
    const int y = GET_Y_LPARAM(lParam);
    if (GetCapture() != this && message == WM_LBUTTONDOWN && AcceptsFocus()) {
      SetFocus();
    }
    processed = HandleMouseEvent(message, x, y, static_cast<WXUINT>(wParam));
  } break;

  case WM_SYSCOMMAND:
    switch (wParam & 0xFFFFFFF0) {
    case SC_MAXIMIZE:
      processed = HandleMaximize();
      break;
    case SC_MINIMIZE:
      processed = HandleMinimize();
      break;
    default:
      break;
    }
    break;

  case WM_COMMAND:
    processed = HandleCommand(LOWORD(wParam), HIWORD(wParam), static_cast<WXHWND>(lParam));
    break;

  case WM_SYSKEYDOWN:
  case WM_KEYDOWN:
    // If this has been processed by an event handler, return 0 now (we've handled it).
    m_lastKeydownProcessed = HandleKeyDown(static_cast<WORD>(wParam), lParam);
    if (m_lastKeydownProcessed) {
      processed = true;
    }

    if (!processed) {
      switch (wParam) {
      // we consider these message "not interesting" to OnChar, so just don't do anything more
      // with them
      case VK_SHIFT:
      case VK_CONTROL:
      case VK_MENU:
      case VK_CAPITAL:
      case VK_NUMLOCK:
      case VK_SCROLL:
        processed = true;
        break;

      // avoid duplicate messages to OnChar for these ASCII keys: they will be translated by
      // TranslateMessage() and received in WM_CHAR
      case VK_ESCAPE:
      case VK_SPACE:
      case VK_RETURN:
      case VK_BACK:
      case VK_TAB:
      case VK_ADD:
      case VK_SUBTRACT:
      case VK_MULTIPLY:
      case VK_DIVIDE:
      case VK_OEM_1:
      case VK_OEM_2:
      case VK_OEM_3:
      case VK_OEM_4:
      case VK_OEM_5:
      case VK_OEM_6:
      case VK_OEM_7:
      case VK_OEM_PLUS:
      case VK_OEM_COMMA:
      case VK_OEM_MINUS:
      case VK_OEM_PERIOD:
        // but set processed to FALSE, not TRUE to still pass them to the control's default
        // window proc - otherwise built-in keyboard handling won't work
        processed = false;
        break;

      case VK_APPS: {
        // the context menu key: a right click where the pointer is, as wxWindows makes it
        POINT pt;
        ::GetCursorPos(&pt);
        ::ScreenToClient(wxlite::ToHwnd(GetHWND()), &pt);
        WXUINT flags = 0;
        if (wxlite::IsShiftDown()) {
          flags |= MK_SHIFT;
        }
        if (wxlite::IsCtrlDown()) {
          flags |= MK_CONTROL;
        }
        processed = HandleMouseEvent(WM_RBUTTONDOWN, pt.x, pt.y, flags);
      } break;

      default:
        // do generate a CHAR event
        processed = HandleChar(static_cast<WORD>(wParam), lParam);
      }
    }
    if (message == WM_SYSKEYDOWN) { // Let Windows still handle the SYSKEYs
      processed = false;
    }
    break;

  case WM_SYSKEYUP:
  case WM_KEYUP:
    if (wParam == VK_APPS) {
      POINT pt;
      ::GetCursorPos(&pt);
      ::ScreenToClient(wxlite::ToHwnd(GetHWND()), &pt);
      processed = HandleMouseEvent(WM_RBUTTONUP, pt.x, pt.y, 0);
    } else {
      processed = HandleKeyUp(static_cast<WORD>(wParam), lParam);
    }
    break;

  case WM_SYSCHAR:
  case WM_CHAR: // Always an ASCII character
    if (m_lastKeydownProcessed) {
      // The key was handled in the EVT_KEY_DOWN and handling a key in an EVT_KEY_DOWN handler is
      // meant, by design, to prevent EVT_CHARs from happening
      m_lastKeydownProcessed = false;
      processed = true;
    } else {
      processed = HandleChar(wParam, lParam, true);
    }
    break;

  case WM_ERASEBKGND:
    processed = HandleEraseBkgnd(static_cast<WXHDC>(wParam));
    if (processed) {
      // we processed the message, i.e. erased the background
      rc.result = TRUE;
    }
    break;

  case WM_INITDIALOG: {
    wxInitDialogEvent event(GetId());
    event.SetEventObject(this);
    processed = GetEventHandler()->ProcessEvent(event);
    if (processed) {
      // we never set focus from here
      rc.result = FALSE;
    }
  } break;

  case WM_QUERYENDSESSION: {
    wxCloseEvent event(wxEVT_QUERY_END_SESSION, -1);
    event.SetEventObject(wxTheApp);
    event.SetCanVeto(true);
    event.SetLoggingOff(lParam != 0);
    const bool eventProcessed = wxTheApp->ProcessEvent(event);
    rc.allow = !(eventProcessed && event.GetVeto());
    processed = eventProcessed;
  } break;

  case WM_ENDSESSION:
    // only send once
    if (wParam != 0 && this == wxTheApp->GetTopWindow()) {
      wxCloseEvent event(wxEVT_END_SESSION, -1);
      event.SetEventObject(wxTheApp);
      event.SetCanVeto(false);
      event.SetLoggingOff(lParam != 0);
      processed = wxTheApp->ProcessEvent(event);
    }
    break;

  case WM_SETCURSOR:
    processed = HandleSetCursor(static_cast<WXHWND>(wParam), static_cast<short>(LOWORD(lParam)), HIWORD(lParam));
    if (processed) {
      // returning TRUE stops the DefWindowProc() from further processing this message - exactly
      // what we need because we've just set the cursor.
      rc.result = TRUE;
    }
    break;

  default:
    break;
  }

  if (!processed) {
    rc.result = MSWDefWindowProc(message, wParam, lParam);
  }
  return static_cast<long>(rc.result);
}

// --- The message handlers -----------------------------------------------------------------------

bool wxWindow::HandleCreate(void* WXUNUSED(cs), bool* const mayCreate)
{
  // wxWindows 2.4.2 sends wxEVT_CREATE (wxWindowCreateEvent) here, which no window of the engine
  // handles.
  *mayCreate = true;
  return true;
}

bool wxWindow::HandleDestroy()
{
  // and wxEVT_DESTROY here
  return true;
}

bool wxWindow::HandleActivate(const int state, bool WXUNUSED(minimized), WXHWND WXUNUSED(activate))
{
  wxActivateEvent event(wxEVT_ACTIVATE, (state == WA_ACTIVE) || (state == WA_CLICKACTIVE), m_windowId);
  event.SetEventObject(this);
  return GetEventHandler()->ProcessEvent(event);
}

bool wxWindow::HandleSetFocus(const WXHWND hwnd)
{
  wxFocusEvent event(wxEVT_SET_FOCUS, m_windowId);
  event.SetEventObject(this);

  // wxFindWinFromHandle() may return NULL, it is ok
  event.SetWindow(wxFindWinFromHandle(hwnd));
  return GetEventHandler()->ProcessEvent(event);
}

bool wxWindow::HandleKillFocus(const WXHWND hwnd)
{
  // Don't send the event when in the process of being deleted. This can only cause problems if
  // the event handler tries to access the object.
  if (m_isBeingDeleted) {
    return false;
  }

  wxFocusEvent event(wxEVT_KILL_FOCUS, m_windowId);
  event.SetEventObject(this);

  // wxFindWinFromHandle() may return NULL, it is ok
  event.SetWindow(wxFindWinFromHandle(hwnd));
  return GetEventHandler()->ProcessEvent(event);
}

bool wxWindow::HandleShow(const bool show, int WXUNUSED(status))
{
  wxShowEvent event(GetId(), show);
  event.SetEventObject(this);
  return GetEventHandler()->ProcessEvent(event);
}

bool wxWindow::HandleSetCursor(WXHWND WXUNUSED(hWnd), const short nHitTest, int WXUNUSED(mouseMsg))
{
  // the logic is as follows: wxWindows 2.4.2 asks the window for a cursor (wxEVT_SET_CURSOR), then
  // its own one, then the busy or the global one. wx-lite has no cursors: the engine sets its
  // cursor itself (moho::WD3DViewport::MSWWindowProc), and everything else is left to
  // DefWindowProc.
  (void)nHitTest;
  return false;
}

bool wxWindow::HandlePaint()
{
  wxPaintEvent event(m_windowId);
  event.SetEventObject(this);
  return GetEventHandler()->ProcessEvent(event);
}

bool wxWindow::HandleEraseBkgnd(const WXHDC hdc)
{
  // Prevents flicker when dragging
  if (::IsIconic(wxlite::ToHwnd(GetHWND()))) {
    return true;
  }

  // the DC of the message's HDC: the window's client area
  (void)hdc;
  wxClientDC dc(this);

  wxEraseEvent event(m_windowId, &dc);
  event.SetEventObject(this);
  return GetEventHandler()->ProcessEvent(event);
}

bool wxWindow::HandleMinimize()
{
  wxIconizeEvent event(m_windowId);
  event.SetEventObject(this);
  return GetEventHandler()->ProcessEvent(event);
}

bool wxWindow::HandleMaximize()
{
  wxMaximizeEvent event(m_windowId);
  event.SetEventObject(this);
  return GetEventHandler()->ProcessEvent(event);
}

bool wxWindow::HandleMove(const int x, const int y)
{
  wxMoveEvent event(wxPoint(x, y), m_windowId);
  event.SetEventObject(this);
  return GetEventHandler()->ProcessEvent(event);
}

bool wxWindow::HandleSize(int WXUNUSED(w), int WXUNUSED(h), WXUINT WXUNUSED(flag))
{
  wxSizeEvent event(GetSize(), m_windowId);
  event.SetEventObject(this);
  return GetEventHandler()->ProcessEvent(event);
}

bool wxWindow::HandleCommand(const WXWORD id, WXWORD WXUNUSED(cmd), const WXHWND control)
{
  // Native controls notify their parent with WM_COMMAND, and wxWindows 2.4.2 passes it to them
  // (MSWCommand). wx-lite's controls are not native and send their events themselves: what comes
  // here is a command without a control, a menu's or an accelerator's.
  if (control) {
    return false;
  }
  wxCommandEvent event(wxEVT_COMMAND_MENU_SELECTED);
  event.SetEventObject(this);
  event.SetId(id);
  event.SetInt(id);
  return GetEventHandler()->ProcessEvent(event);
}

void wxWindow::InitMouseEvent(wxMouseEvent& event, const int x, const int y, const WXUINT flags)
{
  // our client coords are not quite the same as Windows ones
  const wxPoint pt = GetClientAreaOrigin();
  event.m_x = x - pt.x;
  event.m_y = y - pt.y;

  event.m_shiftDown = (flags & MK_SHIFT) != 0;
  event.m_controlDown = (flags & MK_CONTROL) != 0;
  event.m_leftDown = (flags & MK_LBUTTON) != 0;
  event.m_middleDown = (flags & MK_MBUTTON) != 0;
  event.m_rightDown = (flags & MK_RBUTTON) != 0;
  event.m_altDown = ::GetKeyState(VK_MENU) < 0;

  event.SetTimestamp(::GetMessageTime());

  event.m_eventObject = this;
  event.SetId(GetId());
}

bool wxWindow::HandleMouseEvent(const WXUINT msg, const int x, const int y, const WXUINT flags)
{
  // the mouse events take consecutive IDs from WM_MOUSEFIRST to WM_MOUSELAST
  static const wxEventType* const eventsMouse[] = {
    &wxEVT_MOTION, &wxEVT_LEFT_DOWN, &wxEVT_LEFT_UP, &wxEVT_LEFT_DCLICK, &wxEVT_RIGHT_DOWN,
    &wxEVT_RIGHT_UP, &wxEVT_RIGHT_DCLICK, &wxEVT_MIDDLE_DOWN, &wxEVT_MIDDLE_UP, &wxEVT_MIDDLE_DCLICK
  };

  wxMouseEvent event(*eventsMouse[msg - WM_MOUSEMOVE]);
  InitMouseEvent(event, x, y, flags);
  return GetEventHandler()->ProcessEvent(event);
}

bool wxWindow::HandleMouseMove(const int x, const int y, const WXUINT flags)
{
  if (!m_mouseInWindow) {
    // it would be wrong to assume that just because we get a mouse move event that the mouse is
    // inside the window: although this is usually true, it is not if we had captured the mouse,
    // so we need to check the mouse coordinates here
    if (!HasCapture() || IsMouseInWindow()) {
      // Generate an ENTER event
      m_mouseInWindow = true;

      wxMouseEvent event(wxEVT_ENTER_WINDOW);
      InitMouseEvent(event, x, y, flags);
      (void)GetEventHandler()->ProcessEvent(event);
    }
  }
  return HandleMouseEvent(WM_MOUSEMOVE, x, y, flags);
}

bool wxWindow::HandleMouseWheel(const WXWPARAM wParam, const WXLPARAM lParam)
{
  wxMouseEvent event(wxEVT_MOUSEWHEEL);
  // the position is the screen's, as WM_MOUSEWHEEL has it
  InitMouseEvent(event, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), LOWORD(wParam));
  event.m_wheelRotation = static_cast<short>(HIWORD(wParam));
  event.m_wheelDelta = WHEEL_DELTA;

  // SPI_GETWHEELSCROLLLINES: Windows' default of three lines a notch
  static const int s_linesPerRotation = 3;
  event.m_linesPerAction = s_linesPerRotation;
  return GetEventHandler()->ProcessEvent(event);
}

wxKeyEvent wxWindow::CreateKeyEvent(const wxEventType evType, const int id, const WXLPARAM lParam, const WXWPARAM wParam) const
{
  wxKeyEvent event(evType);
  event.SetId(GetId());
  event.m_shiftDown = wxlite::IsShiftDown();
  event.m_controlDown = wxlite::IsCtrlDown();
  event.m_altDown = (HIWORD(lParam) & 0x2000) == 0x2000; // KF_ALTDOWN

  event.m_eventObject = const_cast<wxWindow*>(this);
  event.m_keyCode = id;
  event.m_rawCode = static_cast<wxUint32>(wParam);
  event.m_rawFlags = static_cast<wxUint32>(lParam);
  event.SetTimestamp(::GetMessageTime());

  // translate the position to client coords
  POINT pt;
  ::GetCursorPos(&pt);
  const RECT rect = WindowRectOf(GetHWND());
  pt.x -= rect.left;
  pt.y -= rect.top;

  event.m_x = pt.x;
  event.m_y = pt.y;
  return event;
}

// isASCII is TRUE only when we're called from WM_CHAR handler and not from WM_KEYDOWN one
bool wxWindow::HandleChar(const WXWPARAM wParam, const WXLPARAM lParam, const bool isASCII)
{
  int id;
  if (isASCII) {
    // If 1 -> 26, translate to either special keycode or just set ctrlDown. IOW, Ctrl-C should
    // result in keycode == 3 and ControlDown() == TRUE.
    id = static_cast<int>(wParam);
    if ((id > 0) && (id < 27)) {
      switch (id) {
      case 13:
        id = WXK_RETURN;
        break;
      case 8:
        id = WXK_BACK;
        break;
      case 9:
        id = WXK_TAB;
        break;
      default:
        break;
      }
    }
  } else { // we're called from WM_KEYDOWN
    id = wxCharCodeMSWToWX(static_cast<int>(wParam));
    if (id == 0) {
      // it's ASCII and will be processed here only when called from WM_CHAR (i.e. when
      // isASCII = TRUE), don't process it now
      return false;
    }
  }

  wxKeyEvent event(CreateKeyEvent(wxEVT_CHAR, id, lParam, wParam));

  // the alphanumeric keys produced by pressing AltGr+something on European keyboards have both
  // Ctrl and Alt modifiers which may confuse the user code - so remove them as AltGr doesn't
  // really mean Ctrl+Alt
  if (event.m_controlDown && event.m_altDown && (id >= 32 && id < 256)) {
    event.m_controlDown = event.m_altDown = false;
  }
  return GetEventHandler()->ProcessEvent(event);
}

bool wxWindow::HandleKeyDown(const WXWPARAM wParam, const WXLPARAM lParam)
{
  int id = wxCharCodeMSWToWX(static_cast<int>(wParam));
  if (!id) {
    // normal ASCII char
    id = static_cast<int>(wParam);
  }

  if (id != -1) { // VZ: does this ever happen (FIXME)?
    wxKeyEvent event(CreateKeyEvent(wxEVT_KEY_DOWN, id, lParam, wParam));
    if (GetEventHandler()->ProcessEvent(event)) {
      return true;
    }
  }
  return false;
}

bool wxWindow::HandleKeyUp(const WXWPARAM wParam, const WXLPARAM lParam)
{
  int id = wxCharCodeMSWToWX(static_cast<int>(wParam));
  if (!id) {
    // normal ASCII char
    id = static_cast<int>(wParam);
  }

  if (id != -1) { // VZ: does this ever happen (FIXME)?
    wxKeyEvent event(CreateKeyEvent(wxEVT_KEY_UP, id, lParam, wParam));
    if (GetEventHandler()->ProcessEvent(event)) {
      return true;
    }
  }
  return false;
}
