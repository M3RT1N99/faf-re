#pragma once

// wx-lite (android/native/WX_LITE.md): wxWindows 2.4.2's <wx/event.h>, for what the engine uses:
// the event types, the event classes, wxEvtHandler with its static and dynamic event tables, and
// the event table macros. Declarations and macros as in wxWindows 2.4.2 (include/wx/event.h;
// wxWindows Library Licence), which numbers its event types at start-up (DEFINE_EVENT_TYPE:
// wxNewEventType) and keeps references to them in the static tables. src/wx/event.cpp has the
// processing, as src/common/event.cpp does it.

#include <mutex>
#include <vector>

#include "wx/defs.h"
#include "wx/gdicmn.h"
#include "wx/object.h"
#include "wx/string.h"

class wxDC;
class wxWindow;
class wxEvtHandler;

using wxEventType = int;

#define DECLARE_EVENT_TABLE_ENTRY(type, id, idLast, fn, obj) wxEventTableEntry(type, id, idLast, fn, obj)

#define BEGIN_DECLARE_EVENT_TYPES()
#define END_DECLARE_EVENT_TYPES()
#define DECLARE_EVENT_TYPE(name, value) extern const wxEventType name;
#define DECLARE_LOCAL_EVENT_TYPE(name, value) extern const wxEventType name;
#define DECLARE_EXPORTED_LOCAL_EVENT_TYPE(usergoo, name, value) extern const wxEventType usergoo name;
#define DEFINE_EVENT_TYPE(name) const wxEventType name = wxNewEventType();
#define DEFINE_LOCAL_EVENT_TYPE(name) const wxEventType name = wxNewEventType();

/// A new event type: wxEVT_FIRST, then one more each call.
wxEventType wxNewEventType();

BEGIN_DECLARE_EVENT_TYPES()
DECLARE_EVENT_TYPE(wxEVT_NULL, 0)
const wxEventType wxEVT_FIRST = 10000;
const wxEventType wxEVT_USER_FIRST = wxEVT_FIRST + 2000;

DECLARE_EVENT_TYPE(wxEVT_COMMAND_BUTTON_CLICKED, 1)
DECLARE_EVENT_TYPE(wxEVT_COMMAND_CHECKBOX_CLICKED, 2)
DECLARE_EVENT_TYPE(wxEVT_COMMAND_CHOICE_SELECTED, 3)
DECLARE_EVENT_TYPE(wxEVT_COMMAND_LISTBOX_SELECTED, 4)
DECLARE_EVENT_TYPE(wxEVT_COMMAND_TEXT_UPDATED, 7)
DECLARE_EVENT_TYPE(wxEVT_COMMAND_TEXT_ENTER, 8)
DECLARE_EVENT_TYPE(wxEVT_COMMAND_MENU_SELECTED, 9)
DECLARE_EVENT_TYPE(wxEVT_COMMAND_SLIDER_UPDATED, 10)
DECLARE_EVENT_TYPE(wxEVT_COMMAND_COMBOBOX_SELECTED, 15)

DECLARE_EVENT_TYPE(wxEVT_LEFT_DOWN, 100)
DECLARE_EVENT_TYPE(wxEVT_LEFT_UP, 101)
DECLARE_EVENT_TYPE(wxEVT_MIDDLE_DOWN, 102)
DECLARE_EVENT_TYPE(wxEVT_MIDDLE_UP, 103)
DECLARE_EVENT_TYPE(wxEVT_RIGHT_DOWN, 104)
DECLARE_EVENT_TYPE(wxEVT_RIGHT_UP, 105)
DECLARE_EVENT_TYPE(wxEVT_MOTION, 106)
DECLARE_EVENT_TYPE(wxEVT_ENTER_WINDOW, 107)
DECLARE_EVENT_TYPE(wxEVT_LEAVE_WINDOW, 108)
DECLARE_EVENT_TYPE(wxEVT_LEFT_DCLICK, 109)
DECLARE_EVENT_TYPE(wxEVT_MIDDLE_DCLICK, 110)
DECLARE_EVENT_TYPE(wxEVT_RIGHT_DCLICK, 111)
DECLARE_EVENT_TYPE(wxEVT_SET_FOCUS, 112)
DECLARE_EVENT_TYPE(wxEVT_KILL_FOCUS, 113)
DECLARE_EVENT_TYPE(wxEVT_MOUSEWHEEL, 115)

DECLARE_EVENT_TYPE(wxEVT_CHAR, 212)
DECLARE_EVENT_TYPE(wxEVT_CHAR_HOOK, 213)
DECLARE_EVENT_TYPE(wxEVT_KEY_DOWN, 215)
DECLARE_EVENT_TYPE(wxEVT_KEY_UP, 216)

DECLARE_EVENT_TYPE(wxEVT_SCROLL_TOP, 300)
DECLARE_EVENT_TYPE(wxEVT_SCROLL_BOTTOM, 301)
DECLARE_EVENT_TYPE(wxEVT_SCROLL_LINEUP, 302)
DECLARE_EVENT_TYPE(wxEVT_SCROLL_LINEDOWN, 303)
DECLARE_EVENT_TYPE(wxEVT_SCROLL_PAGEUP, 304)
DECLARE_EVENT_TYPE(wxEVT_SCROLL_PAGEDOWN, 305)
DECLARE_EVENT_TYPE(wxEVT_SCROLL_THUMBTRACK, 306)
DECLARE_EVENT_TYPE(wxEVT_SCROLL_THUMBRELEASE, 307)
DECLARE_EVENT_TYPE(wxEVT_SCROLL_ENDSCROLL, 308)

DECLARE_EVENT_TYPE(wxEVT_SIZE, 400)
DECLARE_EVENT_TYPE(wxEVT_MOVE, 401)
DECLARE_EVENT_TYPE(wxEVT_CLOSE_WINDOW, 402)
DECLARE_EVENT_TYPE(wxEVT_END_SESSION, 403)
DECLARE_EVENT_TYPE(wxEVT_QUERY_END_SESSION, 404)
DECLARE_EVENT_TYPE(wxEVT_ACTIVATE_APP, 405)
DECLARE_EVENT_TYPE(wxEVT_ACTIVATE, 409)
DECLARE_EVENT_TYPE(wxEVT_CREATE, 410)
DECLARE_EVENT_TYPE(wxEVT_DESTROY, 411)
DECLARE_EVENT_TYPE(wxEVT_SHOW, 412)
DECLARE_EVENT_TYPE(wxEVT_ICONIZE, 413)
DECLARE_EVENT_TYPE(wxEVT_MAXIMIZE, 414)
DECLARE_EVENT_TYPE(wxEVT_PAINT, 416)
DECLARE_EVENT_TYPE(wxEVT_ERASE_BACKGROUND, 417)
DECLARE_EVENT_TYPE(wxEVT_INIT_DIALOG, 438)
DECLARE_EVENT_TYPE(wxEVT_IDLE, 439)
DECLARE_EVENT_TYPE(wxEVT_UPDATE_UI, 440)
END_DECLARE_EVENT_TYPES()

// --- Events --------------------------------------------------------------------------------------

class wxEvent : public wxObject
{
private:
  wxEvent& operator=(const wxEvent&) = delete;

protected:
  wxEvent(const wxEvent& src); // for implementing Clone()

public:
  wxEvent(int id = 0, wxEventType commandType = wxEVT_NULL);

  void SetEventType(wxEventType typ)
  {
    m_eventType = typ;
  }
  wxEventType GetEventType() const
  {
    return m_eventType;
  }
  wxObject* GetEventObject() const
  {
    return m_eventObject;
  }
  void SetEventObject(wxObject* obj)
  {
    m_eventObject = obj;
  }
  long GetTimestamp() const
  {
    return m_timeStamp;
  }
  void SetTimestamp(long ts = 0)
  {
    m_timeStamp = ts;
  }
  int GetId() const
  {
    return m_id;
  }
  void SetId(int Id)
  {
    m_id = Id;
  }

  /// The handler that called this lets the next one in the chain see the event as well.
  void Skip(bool skip = true)
  {
    m_skipped = skip;
  }
  bool GetSkipped() const
  {
    return m_skipped;
  }
  bool IsCommandEvent() const
  {
    return m_isCommandEvent;
  }

  virtual wxEvent* Clone() const = 0;

public:
  wxObject* m_eventObject;
  wxEventType m_eventType;
  long m_timeStamp;
  int m_id;
  wxObject* m_callbackUserData;
  bool m_skipped;
  bool m_isCommandEvent;
};

class wxCommandEvent : public wxEvent
{
private:
  wxCommandEvent& operator=(const wxCommandEvent& event) = delete;

public:
  wxCommandEvent(wxEventType commandType = wxEVT_NULL, int id = 0);
  wxCommandEvent(const wxCommandEvent& event)
    : wxEvent(event)
    , m_commandString(event.m_commandString)
    , m_commandInt(event.m_commandInt)
    , m_extraLong(event.m_extraLong)
    , m_clientData(event.m_clientData)
  {}

  void SetClientData(void* clientData)
  {
    m_clientData = clientData;
  }
  void* GetClientData() const
  {
    return m_clientData;
  }
  int GetSelection() const
  {
    return m_commandInt;
  }
  void SetString(const wxString& s)
  {
    m_commandString = s;
  }
  wxString GetString() const
  {
    return m_commandString;
  }
  bool IsChecked() const
  {
    return m_commandInt != 0;
  }
  bool IsSelection() const
  {
    return m_extraLong != 0;
  }
  void SetExtraLong(long extraLong)
  {
    m_extraLong = extraLong;
  }
  long GetExtraLong() const
  {
    return m_extraLong;
  }
  void SetInt(int i)
  {
    m_commandInt = i;
  }
  long GetInt() const
  {
    return m_commandInt;
  }

  wxEvent* Clone() const override
  {
    return new wxCommandEvent(*this);
  }

public:
  wxString m_commandString; // String event argument
  int m_commandInt;
  long m_extraLong; // Additional information (e.g. select/deselect)
  void* m_clientData; // Arbitrary client data
};

class wxScrollEvent : public wxCommandEvent
{
public:
  wxScrollEvent(wxEventType commandType = wxEVT_NULL, int id = 0, int pos = 0, int orient = 0);

  int GetOrientation() const
  {
    return static_cast<int>(m_extraLong);
  }
  int GetPosition() const
  {
    return m_commandInt;
  }
  void SetOrientation(int orient)
  {
    m_extraLong = static_cast<long>(orient);
  }
  void SetPosition(int pos)
  {
    m_commandInt = pos;
  }

  wxEvent* Clone() const override
  {
    return new wxScrollEvent(*this);
  }
};

enum
{
  wxMOUSE_BTN_ANY = -1,
  wxMOUSE_BTN_NONE = -1,
  wxMOUSE_BTN_LEFT = 0,
  wxMOUSE_BTN_MIDDLE = 1,
  wxMOUSE_BTN_RIGHT = 2
};

/// ButtonDown, ButtonUp, ButtonDClick, Button, ButtonIsDown and GetButton number the buttons 1
/// (left), 2 (middle) and 3 (right), -1 being any: wxWindows 2.4.2's numbers. (Its wxMOUSE_BTN_
/// enum above counts from 0; only wxMOUSE_BTN_ANY reaches these functions.)
class wxMouseEvent : public wxEvent
{
public:
  wxMouseEvent(wxEventType mouseType = wxEVT_NULL);
  wxMouseEvent(const wxMouseEvent& event)
    : wxEvent(event)
  {
    Assign(event);
  }

  bool IsButton() const
  {
    return Button(wxMOUSE_BTN_ANY);
  }
  bool ButtonDown(int but = wxMOUSE_BTN_ANY) const;
  bool ButtonDClick(int but = wxMOUSE_BTN_ANY) const;
  bool ButtonUp(int but = wxMOUSE_BTN_ANY) const;
  bool Button(int but) const;
  bool ButtonIsDown(int but) const;
  int GetButton() const;

  bool ControlDown() const
  {
    return m_controlDown;
  }
  bool MetaDown() const
  {
    return m_metaDown;
  }
  bool AltDown() const
  {
    return m_altDown;
  }
  bool ShiftDown() const
  {
    return m_shiftDown;
  }

  bool LeftDown() const
  {
    return m_eventType == wxEVT_LEFT_DOWN;
  }
  bool MiddleDown() const
  {
    return m_eventType == wxEVT_MIDDLE_DOWN;
  }
  bool RightDown() const
  {
    return m_eventType == wxEVT_RIGHT_DOWN;
  }
  bool LeftUp() const
  {
    return m_eventType == wxEVT_LEFT_UP;
  }
  bool MiddleUp() const
  {
    return m_eventType == wxEVT_MIDDLE_UP;
  }
  bool RightUp() const
  {
    return m_eventType == wxEVT_RIGHT_UP;
  }
  bool LeftDClick() const
  {
    return m_eventType == wxEVT_LEFT_DCLICK;
  }
  bool MiddleDClick() const
  {
    return m_eventType == wxEVT_MIDDLE_DCLICK;
  }
  bool RightDClick() const
  {
    return m_eventType == wxEVT_RIGHT_DCLICK;
  }
  bool LeftIsDown() const
  {
    return m_leftDown;
  }
  bool MiddleIsDown() const
  {
    return m_middleDown;
  }
  bool RightIsDown() const
  {
    return m_rightDown;
  }
  bool Dragging() const
  {
    return m_eventType == wxEVT_MOTION && (LeftIsDown() || MiddleIsDown() || RightIsDown());
  }
  bool Moving() const
  {
    return m_eventType == wxEVT_MOTION;
  }
  bool Entering() const
  {
    return m_eventType == wxEVT_ENTER_WINDOW;
  }
  bool Leaving() const
  {
    return m_eventType == wxEVT_LEAVE_WINDOW;
  }

  void GetPosition(wxCoord* xpos, wxCoord* ypos) const
  {
    if (xpos) {
      *xpos = m_x;
    }
    if (ypos) {
      *ypos = m_y;
    }
  }
  void GetPosition(long* xpos, long* ypos) const
  {
    if (xpos) {
      *xpos = static_cast<long>(m_x);
    }
    if (ypos) {
      *ypos = static_cast<long>(m_y);
    }
  }
  wxPoint GetPosition() const
  {
    return wxPoint(m_x, m_y);
  }
  wxCoord GetX() const
  {
    return m_x;
  }
  wxCoord GetY() const
  {
    return m_y;
  }

  int GetWheelRotation() const
  {
    return m_wheelRotation;
  }
  int GetWheelDelta() const
  {
    return m_wheelDelta;
  }
  int GetLinesPerAction() const
  {
    return m_linesPerAction;
  }

  wxEvent* Clone() const override
  {
    return new wxMouseEvent(*this);
  }
  wxMouseEvent& operator=(const wxMouseEvent& event)
  {
    Assign(event);
    return *this;
  }

public:
  wxCoord m_x, m_y;
  bool m_leftDown;
  bool m_middleDown;
  bool m_rightDown;
  bool m_controlDown;
  bool m_shiftDown;
  bool m_altDown;
  bool m_metaDown;
  int m_wheelRotation;
  int m_wheelDelta;
  int m_linesPerAction;

protected:
  void Assign(const wxMouseEvent& evt);
};

class wxKeyEvent : public wxEvent
{
public:
  wxKeyEvent(wxEventType keyType = wxEVT_NULL);
  wxKeyEvent(const wxKeyEvent& evt);

  bool ControlDown() const
  {
    return m_controlDown;
  }
  bool MetaDown() const
  {
    return m_metaDown;
  }
  bool AltDown() const
  {
    return m_altDown;
  }
  bool ShiftDown() const
  {
    return m_shiftDown;
  }
  bool HasModifiers() const
  {
    return ControlDown() || AltDown();
  }

  int GetKeyCode() const
  {
    return static_cast<int>(m_keyCode);
  }
  /// The Windows virtual key (WM_KEYDOWN, WM_KEYUP) or the character (WM_CHAR).
  wxUint32 GetRawKeyCode() const
  {
    return m_rawCode;
  }
  /// The message's lParam: repeat count, scan code, the previous-state and transition bits.
  wxUint32 GetRawKeyFlags() const
  {
    return m_rawFlags;
  }

  void GetPosition(wxCoord* xpos, wxCoord* ypos) const
  {
    if (xpos) {
      *xpos = m_x;
    }
    if (ypos) {
      *ypos = m_y;
    }
  }
  void GetPosition(long* xpos, long* ypos) const
  {
    if (xpos) {
      *xpos = static_cast<long>(m_x);
    }
    if (ypos) {
      *ypos = static_cast<long>(m_y);
    }
  }
  wxPoint GetPosition() const
  {
    return wxPoint(m_x, m_y);
  }
  wxCoord GetX() const
  {
    return m_x;
  }
  wxCoord GetY() const
  {
    return m_y;
  }
  long KeyCode() const
  {
    return m_keyCode;
  }

  wxEvent* Clone() const override
  {
    return new wxKeyEvent(*this);
  }
  wxKeyEvent& operator=(const wxKeyEvent& evt)
  {
    m_x = evt.m_x;
    m_y = evt.m_y;
    m_keyCode = evt.m_keyCode;
    m_controlDown = evt.m_controlDown;
    m_shiftDown = evt.m_shiftDown;
    m_altDown = evt.m_altDown;
    m_metaDown = evt.m_metaDown;
    m_scanCode = evt.m_scanCode;
    m_rawCode = evt.m_rawCode;
    m_rawFlags = evt.m_rawFlags;
    return *this;
  }

public:
  wxCoord m_x, m_y;
  long m_keyCode;
  bool m_controlDown;
  bool m_shiftDown;
  bool m_altDown;
  bool m_metaDown;
  bool m_scanCode;
  wxChar m_uniChar;
  wxUint32 m_rawCode;
  wxUint32 m_rawFlags;
};

class wxSizeEvent : public wxEvent
{
public:
  wxSizeEvent()
    : wxEvent(0, wxEVT_SIZE)
  {}
  wxSizeEvent(const wxSize& sz, int id = 0)
    : wxEvent(id, wxEVT_SIZE)
    , m_size(sz)
  {}
  wxSizeEvent(const wxSizeEvent& event)
    : wxEvent(event)
    , m_size(event.m_size)
  {}

  wxSize GetSize() const
  {
    return m_size;
  }

  wxEvent* Clone() const override
  {
    return new wxSizeEvent(*this);
  }

public:
  wxSize m_size;
};

class wxMoveEvent : public wxEvent
{
public:
  wxMoveEvent()
    : wxEvent(0, wxEVT_MOVE)
  {}
  wxMoveEvent(const wxPoint& pos, int id = 0)
    : wxEvent(id, wxEVT_MOVE)
    , m_pos(pos)
  {}
  wxMoveEvent(const wxMoveEvent& event)
    : wxEvent(event)
    , m_pos(event.m_pos)
  {}

  wxPoint GetPosition() const
  {
    return m_pos;
  }

  wxEvent* Clone() const override
  {
    return new wxMoveEvent(*this);
  }

  wxPoint m_pos;
};

class wxPaintEvent : public wxEvent
{
public:
  wxPaintEvent(int Id = 0)
    : wxEvent(Id, wxEVT_PAINT)
  {}

  wxEvent* Clone() const override
  {
    return new wxPaintEvent(*this);
  }
};

class wxEraseEvent : public wxEvent
{
  wxEraseEvent& operator=(const wxEraseEvent& event) = delete;

public:
  wxEraseEvent(int Id = 0, wxDC* dc = nullptr)
    : wxEvent(Id, wxEVT_ERASE_BACKGROUND)
    , m_dc(dc)
  {}
  wxEraseEvent(const wxEraseEvent& event)
    : wxEvent(event)
    , m_dc(event.m_dc)
  {}

  wxDC* GetDC() const
  {
    return m_dc;
  }

  wxEvent* Clone() const override
  {
    return new wxEraseEvent(*this);
  }

  wxDC* m_dc;
};

class wxFocusEvent : public wxEvent
{
  wxFocusEvent& operator=(const wxFocusEvent& event) = delete;

public:
  wxFocusEvent(wxEventType type = wxEVT_NULL, int id = 0)
    : wxEvent(id, type)
  {
    m_win = nullptr;
  }
  wxFocusEvent(const wxFocusEvent& event)
    : wxEvent(event)
  {
    m_win = event.m_win;
  }

  /// The window that loses (wxEVT_SET_FOCUS) or gains (wxEVT_KILL_FOCUS) the focus, if any.
  wxWindow* GetWindow() const
  {
    return m_win;
  }
  void SetWindow(wxWindow* win)
  {
    m_win = win;
  }

  wxEvent* Clone() const override
  {
    return new wxFocusEvent(*this);
  }

  wxWindow* m_win;
};

class wxActivateEvent : public wxEvent
{
public:
  wxActivateEvent(wxEventType type = wxEVT_NULL, bool active = true, int Id = 0)
    : wxEvent(Id, type)
  {
    m_active = active;
  }
  wxActivateEvent(const wxActivateEvent& event)
    : wxEvent(event)
  {
    m_active = event.m_active;
  }

  bool GetActive() const
  {
    return m_active;
  }

  wxEvent* Clone() const override
  {
    return new wxActivateEvent(*this);
  }

  bool m_active;
};

class wxInitDialogEvent : public wxEvent
{
public:
  wxInitDialogEvent(int Id = 0)
    : wxEvent(Id, wxEVT_INIT_DIALOG)
  {}

  wxEvent* Clone() const override
  {
    return new wxInitDialogEvent(*this);
  }
};

class wxCloseEvent : public wxEvent
{
public:
  wxCloseEvent(wxEventType type = wxEVT_NULL, int id = 0)
    : wxEvent(id, type)
    , m_loggingOff(true)
    , m_veto(false)
    , m_canVeto(true)
  {}
  wxCloseEvent(const wxCloseEvent& event)
    : wxEvent(event)
    , m_loggingOff(event.m_loggingOff)
    , m_veto(event.m_veto)
    , m_canVeto(event.m_canVeto)
  {}

  void SetLoggingOff(bool logOff)
  {
    m_loggingOff = logOff;
  }
  bool GetLoggingOff() const
  {
    return m_loggingOff;
  }
  void Veto(bool veto = true)
  {
    wxCHECK_RET(m_canVeto, wxT("call to Veto() ignored (can't veto this event)"));
    m_veto = veto;
  }
  void SetCanVeto(bool canVeto)
  {
    m_canVeto = canVeto;
  }
  bool CanVeto() const
  {
    return m_canVeto;
  }
  bool GetVeto() const
  {
    return m_canVeto && m_veto;
  }

  wxEvent* Clone() const override
  {
    return new wxCloseEvent(*this);
  }

protected:
  bool m_loggingOff;
  bool m_veto, m_canVeto;
};

class wxShowEvent : public wxEvent
{
public:
  wxShowEvent(int id = 0, bool show = false)
    : wxEvent(id, wxEVT_SHOW)
  {
    m_show = show;
  }
  wxShowEvent(const wxShowEvent& event)
    : wxEvent(event)
  {
    m_show = event.m_show;
  }

  void SetShow(bool show)
  {
    m_show = show;
  }
  bool GetShow() const
  {
    return m_show;
  }

  wxEvent* Clone() const override
  {
    return new wxShowEvent(*this);
  }

protected:
  bool m_show;
};

class wxIconizeEvent : public wxEvent
{
public:
  wxIconizeEvent(int id = 0, bool iconized = true)
    : wxEvent(id, wxEVT_ICONIZE)
  {
    m_iconized = iconized;
  }
  wxIconizeEvent(const wxIconizeEvent& event)
    : wxEvent(event)
  {
    m_iconized = event.m_iconized;
  }

  bool Iconized() const
  {
    return m_iconized;
  }

  wxEvent* Clone() const override
  {
    return new wxIconizeEvent(*this);
  }

protected:
  bool m_iconized;
};

class wxMaximizeEvent : public wxEvent
{
public:
  wxMaximizeEvent(int id = 0)
    : wxEvent(id, wxEVT_MAXIMIZE)
  {}

  wxEvent* Clone() const override
  {
    return new wxMaximizeEvent(*this);
  }
};

class wxUpdateUIEvent : public wxCommandEvent
{
public:
  wxUpdateUIEvent(wxWindowID commandId = 0)
    : wxCommandEvent(wxEVT_UPDATE_UI, commandId)
  {
    m_checked = m_enabled = m_setEnabled = m_setText = m_setChecked = false;
  }
  wxUpdateUIEvent(const wxUpdateUIEvent& event)
    : wxCommandEvent(event)
    , m_checked(event.m_checked)
    , m_enabled(event.m_enabled)
    , m_setEnabled(event.m_setEnabled)
    , m_setText(event.m_setText)
    , m_setChecked(event.m_setChecked)
    , m_text(event.m_text)
  {}

  bool GetChecked() const
  {
    return m_checked;
  }
  bool GetEnabled() const
  {
    return m_enabled;
  }
  wxString GetText() const
  {
    return m_text;
  }
  bool GetSetText() const
  {
    return m_setText;
  }
  bool GetSetChecked() const
  {
    return m_setChecked;
  }
  bool GetSetEnabled() const
  {
    return m_setEnabled;
  }

  void Check(bool check)
  {
    m_checked = check;
    m_setChecked = true;
  }
  void Enable(bool enable)
  {
    m_enabled = enable;
    m_setEnabled = true;
  }
  void SetText(const wxString& text)
  {
    m_text = text;
    m_setText = true;
  }

  wxEvent* Clone() const override
  {
    return new wxUpdateUIEvent(*this);
  }

protected:
  bool m_checked;
  bool m_enabled;
  bool m_setEnabled;
  bool m_setText;
  bool m_setChecked;
  wxString m_text;
};

class wxIdleEvent : public wxEvent
{
public:
  wxIdleEvent()
    : wxEvent(0, wxEVT_IDLE)
    , m_requestMore(false)
  {}
  wxIdleEvent(const wxIdleEvent& event)
    : wxEvent(event)
    , m_requestMore(event.m_requestMore)
  {}

  void RequestMore(bool needMore = true)
  {
    m_requestMore = needMore;
  }
  bool MoreRequested() const
  {
    return m_requestMore;
  }

  wxEvent* Clone() const override
  {
    return new wxIdleEvent(*this);
  }

protected:
  bool m_requestMore;
};

// --- Event tables --------------------------------------------------------------------------------

typedef void (wxObject::*wxObjectEventFunction)(wxEvent&);

struct wxEventTableEntryBase
{
private:
  wxEventTableEntryBase& operator=(const wxEventTableEntryBase& event) = delete;

public:
  wxEventTableEntryBase(int id, int idLast, wxObjectEventFunction fn, wxObject* data)
    : m_id(id)
    , m_lastId(idLast)
    , m_fn(fn)
    , m_callbackUserData(data)
  {}
  wxEventTableEntryBase(const wxEventTableEntryBase& event)
    : m_id(event.m_id)
    , m_lastId(event.m_lastId)
    , m_fn(event.m_fn)
    , m_callbackUserData(event.m_callbackUserData)
  {}

  // the range of ids for this entry: if m_lastId == -1, the range consists only of m_id,
  // otherwise it is m_id..m_lastId inclusive
  int m_id, m_lastId;

  // function to call: not wxEventFunction, because of dependency problems
  wxObjectEventFunction m_fn;

  // arbitrary user data associated with the callback
  wxObject* m_callbackUserData;
};

/// A row of a static event table. It refers to its event type, which gets its number at
/// start-up, possibly after the tables of another translation unit were laid down.
struct wxEventTableEntry : public wxEventTableEntryBase
{
  wxEventTableEntry(const int& evType, int id, int idLast, wxObjectEventFunction fn, wxObject* data)
    : wxEventTableEntryBase(id, idLast, fn, data)
    , m_eventType(evType)
  {}

  const int& m_eventType;
};

/// A row of a handler's dynamic event table (Connect).
struct wxDynamicEventTableEntry : public wxEventTableEntryBase
{
  wxDynamicEventTableEntry(int evType, int id, int idLast, wxObjectEventFunction fn, wxObject* data)
    : wxEventTableEntryBase(id, idLast, fn, data)
    , m_eventType(evType)
  {}

  int m_eventType;
};

struct wxEventTable
{
  const wxEventTable* baseTable; // base event table (next in chain)
  const wxEventTableEntry* entries; // bottom of entry array
};

class wxEvtHandler : public wxObject
{
public:
  wxEvtHandler();
  ~wxEvtHandler() override;

  wxEvtHandler* GetNextHandler() const
  {
    return m_nextHandler;
  }
  wxEvtHandler* GetPreviousHandler() const
  {
    return m_previousHandler;
  }
  void SetNextHandler(wxEvtHandler* handler)
  {
    m_nextHandler = handler;
  }
  void SetPreviousHandler(wxEvtHandler* handler)
  {
    m_previousHandler = handler;
  }

  void SetEvtHandlerEnabled(bool enabled)
  {
    m_enabled = enabled;
  }
  bool GetEvtHandlerEnabled() const
  {
    return m_enabled;
  }

  /// Handles `event` now: the application's filter, this handler's dynamic table, its class's
  /// tables up the hierarchy, the next handler, a window's parent for a command event, the
  /// application. True when a handler took it (did not Skip).
  virtual bool ProcessEvent(wxEvent& event);

  /// Queues a copy of `event` for ProcessPendingEvents (the application's idle time) and wakes
  /// the application. Any thread may call this.
  void AddPendingEvent(wxEvent& event);
  void ProcessPendingEvents();

  void Connect(int id, int lastId, int eventType, wxObjectEventFunction func, wxObject* userData = nullptr);
  void Connect(int id, int eventType, wxObjectEventFunction func, wxObject* userData = nullptr)
  {
    Connect(id, wxID_ANY, eventType, func, userData);
  }
  bool Disconnect(
    int id, int lastId, wxEventType eventType, wxObjectEventFunction func = nullptr, wxObject* userData = nullptr
  );
  bool Disconnect(
    int id, wxEventType eventType = wxEVT_NULL, wxObjectEventFunction func = nullptr, wxObject* userData = nullptr
  )
  {
    return Disconnect(id, wxID_ANY, eventType, func, userData);
  }

  void SetClientData(void* data)
  {
    m_clientData = data;
  }
  void* GetClientData() const
  {
    return m_clientData;
  }

  virtual bool SearchEventTable(wxEventTable& table, wxEvent& event);
  bool SearchDynamicEventTable(wxEvent& event);

private:
  static const wxEventTableEntry sm_eventTableEntries[];

protected:
  static const wxEventTable sm_eventTable;

  virtual const wxEventTable* GetEventTable() const;

  wxEvtHandler* m_nextHandler;
  wxEvtHandler* m_previousHandler;
  std::vector<wxDynamicEventTableEntry*>* m_dynamicEvents;
  std::vector<wxEvent*>* m_pendingEvents;
  std::mutex* m_eventsLocker;

  // optimization: instead of using costly IsKindOf() we keep this flag telling whether we are a
  // window or not
  bool m_isWindow;

  // Is event handler enabled?
  bool m_enabled;

  void* m_clientData;

  DECLARE_NO_COPY_CLASS(wxEvtHandler)
};

typedef void (wxEvtHandler::*wxEventFunction)(wxEvent&);
typedef void (wxEvtHandler::*wxCommandEventFunction)(wxCommandEvent&);
typedef void (wxEvtHandler::*wxScrollEventFunction)(wxScrollEvent&);
typedef void (wxEvtHandler::*wxSizeEventFunction)(wxSizeEvent&);
typedef void (wxEvtHandler::*wxMoveEventFunction)(wxMoveEvent&);
typedef void (wxEvtHandler::*wxPaintEventFunction)(wxPaintEvent&);
typedef void (wxEvtHandler::*wxEraseEventFunction)(wxEraseEvent&);
typedef void (wxEvtHandler::*wxMouseEventFunction)(wxMouseEvent&);
typedef void (wxEvtHandler::*wxCharEventFunction)(wxKeyEvent&);
typedef void (wxEvtHandler::*wxFocusEventFunction)(wxFocusEvent&);
typedef void (wxEvtHandler::*wxActivateEventFunction)(wxActivateEvent&);
typedef void (wxEvtHandler::*wxInitDialogEventFunction)(wxInitDialogEvent&);
typedef void (wxEvtHandler::*wxCloseEventFunction)(wxCloseEvent&);
typedef void (wxEvtHandler::*wxShowEventFunction)(wxShowEvent&);
typedef void (wxEvtHandler::*wxIconizeEventFunction)(wxIconizeEvent&);
typedef void (wxEvtHandler::*wxMaximizeEventFunction)(wxMaximizeEvent&);
typedef void (wxEvtHandler::*wxUpdateUIEventFunction)(wxUpdateUIEvent&);
typedef void (wxEvtHandler::*wxIdleEventFunction)(wxIdleEvent&);

#define DECLARE_EVENT_TABLE()                            \
private:                                                 \
  static const wxEventTableEntry sm_eventTableEntries[]; \
                                                         \
protected:                                               \
  static const wxEventTable sm_eventTable;               \
  virtual const wxEventTable* GetEventTable() const;

#define BEGIN_EVENT_TABLE(theClass, baseClass)                                                  \
  const wxEventTable* theClass::GetEventTable() const                                           \
  {                                                                                             \
    return &theClass::sm_eventTable;                                                            \
  }                                                                                             \
  const wxEventTable theClass::sm_eventTable = {&baseClass::sm_eventTable, &theClass::sm_eventTableEntries[0]}; \
  const wxEventTableEntry theClass::sm_eventTableEntries[] = {

#define END_EVENT_TABLE() DECLARE_EVENT_TABLE_ENTRY(wxEVT_NULL, 0, 0, 0, 0) };

// The rows: the handler cast to the generic member function, as wxWindows 2.4.2 writes them.
#define wxLITE_EVENT_ROW(type, id, idLast, fnType, fn) \
  DECLARE_EVENT_TABLE_ENTRY(type, id, idLast, (wxObjectEventFunction)(wxEventFunction)(fnType) & fn, (wxObject*)NULL),

// Generic events
#define EVT_CUSTOM(event, id, func) wxLITE_EVENT_ROW(event, id, wxID_ANY, wxEventFunction, func)
#define EVT_CUSTOM_RANGE(event, id1, id2, func) wxLITE_EVENT_ROW(event, id1, id2, wxEventFunction, func)

// Miscellaneous
#define EVT_SIZE(func) wxLITE_EVENT_ROW(wxEVT_SIZE, wxID_ANY, wxID_ANY, wxSizeEventFunction, func)
#define EVT_MOVE(func) wxLITE_EVENT_ROW(wxEVT_MOVE, wxID_ANY, wxID_ANY, wxMoveEventFunction, func)
#define EVT_CLOSE(func) wxLITE_EVENT_ROW(wxEVT_CLOSE_WINDOW, wxID_ANY, wxID_ANY, wxCloseEventFunction, func)
#define EVT_END_SESSION(func) wxLITE_EVENT_ROW(wxEVT_END_SESSION, wxID_ANY, wxID_ANY, wxCloseEventFunction, func)
#define EVT_QUERY_END_SESSION(func) \
  wxLITE_EVENT_ROW(wxEVT_QUERY_END_SESSION, wxID_ANY, wxID_ANY, wxCloseEventFunction, func)
#define EVT_PAINT(func) wxLITE_EVENT_ROW(wxEVT_PAINT, wxID_ANY, wxID_ANY, wxPaintEventFunction, func)
#define EVT_ERASE_BACKGROUND(func) \
  wxLITE_EVENT_ROW(wxEVT_ERASE_BACKGROUND, wxID_ANY, wxID_ANY, wxEraseEventFunction, func)
#define EVT_CHAR(func) wxLITE_EVENT_ROW(wxEVT_CHAR, wxID_ANY, wxID_ANY, wxCharEventFunction, func)
#define EVT_KEY_DOWN(func) wxLITE_EVENT_ROW(wxEVT_KEY_DOWN, wxID_ANY, wxID_ANY, wxCharEventFunction, func)
#define EVT_KEY_UP(func) wxLITE_EVENT_ROW(wxEVT_KEY_UP, wxID_ANY, wxID_ANY, wxCharEventFunction, func)
#define EVT_CHAR_HOOK(func) wxLITE_EVENT_ROW(wxEVT_CHAR_HOOK, wxID_ANY, wxID_ANY, wxCharEventFunction, func)
#define EVT_SET_FOCUS(func) wxLITE_EVENT_ROW(wxEVT_SET_FOCUS, wxID_ANY, wxID_ANY, wxFocusEventFunction, func)
#define EVT_KILL_FOCUS(func) wxLITE_EVENT_ROW(wxEVT_KILL_FOCUS, wxID_ANY, wxID_ANY, wxFocusEventFunction, func)
#define EVT_ACTIVATE(func) wxLITE_EVENT_ROW(wxEVT_ACTIVATE, wxID_ANY, wxID_ANY, wxActivateEventFunction, func)
#define EVT_ACTIVATE_APP(func) \
  wxLITE_EVENT_ROW(wxEVT_ACTIVATE_APP, wxID_ANY, wxID_ANY, wxActivateEventFunction, func)
#define EVT_IDLE(func) wxLITE_EVENT_ROW(wxEVT_IDLE, wxID_ANY, wxID_ANY, wxIdleEventFunction, func)
#define EVT_SHOW(func) wxLITE_EVENT_ROW(wxEVT_SHOW, wxID_ANY, wxID_ANY, wxShowEventFunction, func)
#define EVT_ICONIZE(func) wxLITE_EVENT_ROW(wxEVT_ICONIZE, wxID_ANY, wxID_ANY, wxIconizeEventFunction, func)
#define EVT_MAXIMIZE(func) wxLITE_EVENT_ROW(wxEVT_MAXIMIZE, wxID_ANY, wxID_ANY, wxMaximizeEventFunction, func)
#define EVT_INIT_DIALOG(func) \
  wxLITE_EVENT_ROW(wxEVT_INIT_DIALOG, wxID_ANY, wxID_ANY, wxInitDialogEventFunction, func)

// Mouse events
#define EVT_LEFT_DOWN(func) wxLITE_EVENT_ROW(wxEVT_LEFT_DOWN, wxID_ANY, wxID_ANY, wxMouseEventFunction, func)
#define EVT_LEFT_UP(func) wxLITE_EVENT_ROW(wxEVT_LEFT_UP, wxID_ANY, wxID_ANY, wxMouseEventFunction, func)
#define EVT_MIDDLE_DOWN(func) wxLITE_EVENT_ROW(wxEVT_MIDDLE_DOWN, wxID_ANY, wxID_ANY, wxMouseEventFunction, func)
#define EVT_MIDDLE_UP(func) wxLITE_EVENT_ROW(wxEVT_MIDDLE_UP, wxID_ANY, wxID_ANY, wxMouseEventFunction, func)
#define EVT_RIGHT_DOWN(func) wxLITE_EVENT_ROW(wxEVT_RIGHT_DOWN, wxID_ANY, wxID_ANY, wxMouseEventFunction, func)
#define EVT_RIGHT_UP(func) wxLITE_EVENT_ROW(wxEVT_RIGHT_UP, wxID_ANY, wxID_ANY, wxMouseEventFunction, func)
#define EVT_MOTION(func) wxLITE_EVENT_ROW(wxEVT_MOTION, wxID_ANY, wxID_ANY, wxMouseEventFunction, func)
#define EVT_LEFT_DCLICK(func) wxLITE_EVENT_ROW(wxEVT_LEFT_DCLICK, wxID_ANY, wxID_ANY, wxMouseEventFunction, func)
#define EVT_MIDDLE_DCLICK(func) \
  wxLITE_EVENT_ROW(wxEVT_MIDDLE_DCLICK, wxID_ANY, wxID_ANY, wxMouseEventFunction, func)
#define EVT_RIGHT_DCLICK(func) wxLITE_EVENT_ROW(wxEVT_RIGHT_DCLICK, wxID_ANY, wxID_ANY, wxMouseEventFunction, func)
#define EVT_LEAVE_WINDOW(func) wxLITE_EVENT_ROW(wxEVT_LEAVE_WINDOW, wxID_ANY, wxID_ANY, wxMouseEventFunction, func)
#define EVT_ENTER_WINDOW(func) wxLITE_EVENT_ROW(wxEVT_ENTER_WINDOW, wxID_ANY, wxID_ANY, wxMouseEventFunction, func)
#define EVT_MOUSEWHEEL(func) wxLITE_EVENT_ROW(wxEVT_MOUSEWHEEL, wxID_ANY, wxID_ANY, wxMouseEventFunction, func)

// All mouse events: thirteen rows to one handler
#define EVT_MOUSE_EVENTS(func) \
  EVT_LEFT_DOWN(func)          \
  EVT_LEFT_UP(func)            \
  EVT_MIDDLE_DOWN(func)        \
  EVT_MIDDLE_UP(func)          \
  EVT_RIGHT_DOWN(func)         \
  EVT_RIGHT_UP(func)           \
  EVT_MOTION(func)             \
  EVT_LEFT_DCLICK(func)        \
  EVT_MIDDLE_DCLICK(func)      \
  EVT_RIGHT_DCLICK(func)       \
  EVT_ENTER_WINDOW(func)       \
  EVT_LEAVE_WINDOW(func)       \
  EVT_MOUSEWHEEL(func)

// Scrolling from wxSlider and wxScrollBar
#define EVT_COMMAND_SCROLL_THUMBTRACK(id, func) \
  wxLITE_EVENT_ROW(wxEVT_SCROLL_THUMBTRACK, id, wxID_ANY, wxScrollEventFunction, func)

// Convenience macros for commonly-used commands
#define EVT_COMMAND(id, event, fn) wxLITE_EVENT_ROW(event, id, wxID_ANY, wxCommandEventFunction, fn)
#define EVT_COMMAND_RANGE(id1, id2, event, fn) wxLITE_EVENT_ROW(event, id1, id2, wxCommandEventFunction, fn)
#define EVT_BUTTON(id, fn) wxLITE_EVENT_ROW(wxEVT_COMMAND_BUTTON_CLICKED, id, wxID_ANY, wxCommandEventFunction, fn)
#define EVT_CHECKBOX(id, fn) \
  wxLITE_EVENT_ROW(wxEVT_COMMAND_CHECKBOX_CLICKED, id, wxID_ANY, wxCommandEventFunction, fn)
#define EVT_CHOICE(id, fn) wxLITE_EVENT_ROW(wxEVT_COMMAND_CHOICE_SELECTED, id, wxID_ANY, wxCommandEventFunction, fn)
#define EVT_LISTBOX(id, fn) \
  wxLITE_EVENT_ROW(wxEVT_COMMAND_LISTBOX_SELECTED, id, wxID_ANY, wxCommandEventFunction, fn)
#define EVT_TEXT(id, fn) wxLITE_EVENT_ROW(wxEVT_COMMAND_TEXT_UPDATED, id, wxID_ANY, wxCommandEventFunction, fn)
#define EVT_TEXT_ENTER(id, fn) wxLITE_EVENT_ROW(wxEVT_COMMAND_TEXT_ENTER, id, wxID_ANY, wxCommandEventFunction, fn)
#define EVT_MENU(id, func) wxLITE_EVENT_ROW(wxEVT_COMMAND_MENU_SELECTED, id, wxID_ANY, wxCommandEventFunction, func)
#define EVT_MENU_RANGE(id1, id2, func) \
  wxLITE_EVENT_ROW(wxEVT_COMMAND_MENU_SELECTED, id1, id2, wxCommandEventFunction, func)
#define EVT_SLIDER(id, fn) wxLITE_EVENT_ROW(wxEVT_COMMAND_SLIDER_UPDATED, id, wxID_ANY, wxCommandEventFunction, fn)
#define EVT_COMBOBOX(id, fn) \
  wxLITE_EVENT_ROW(wxEVT_COMMAND_COMBOBOX_SELECTED, id, wxID_ANY, wxCommandEventFunction, fn)
#define EVT_UPDATE_UI(id, func) wxLITE_EVENT_ROW(wxEVT_UPDATE_UI, id, wxID_ANY, wxUpdateUIEventFunction, func)
