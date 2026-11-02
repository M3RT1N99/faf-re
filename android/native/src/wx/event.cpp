// wx-lite (android/native/WX_LITE.md): the events and wxEvtHandler of wxWindows 2.4.2
// (src/common/event.cpp): the event types, the event classes, ProcessEvent's order, the static and
// dynamic tables, and the pending events, which any thread may add and the application's idle
// time processes (wxApp::OnIdle).

#include <algorithm>
#include <mutex>

#include "wx/app.h"
#include "wx/event.h"
#include "wx/window.h"

namespace
{
  /// The handlers with pending events (wxPendingEvents), and its lock (wxPendingEventsLocker).
  std::vector<wxEvtHandler*>& PendingHandlers()
  {
    static std::vector<wxEvtHandler*>* const handlers = new std::vector<wxEvtHandler*>;
    return *handlers;
  }

  std::mutex& PendingHandlersLock()
  {
    static std::mutex* const lock = new std::mutex;
    return *lock;
  }
} // namespace

// --- Event types --------------------------------------------------------------------------------

int wxNewEventType()
{
  // Constant-initialized, so the event types of every translation unit may take their numbers
  // in any order at start-up.
  static int s_lastUsedEventType = wxEVT_FIRST;
  return s_lastUsedEventType++;
}

// common event types are defined here, other event types are defined by the components which use
// them (wxWindows 2.4.2's order)

DEFINE_EVENT_TYPE(wxEVT_NULL)
DEFINE_EVENT_TYPE(wxEVT_COMMAND_BUTTON_CLICKED)
DEFINE_EVENT_TYPE(wxEVT_COMMAND_CHECKBOX_CLICKED)
DEFINE_EVENT_TYPE(wxEVT_COMMAND_CHOICE_SELECTED)
DEFINE_EVENT_TYPE(wxEVT_COMMAND_LISTBOX_SELECTED)
DEFINE_EVENT_TYPE(wxEVT_COMMAND_MENU_SELECTED)
DEFINE_EVENT_TYPE(wxEVT_COMMAND_SLIDER_UPDATED)
DEFINE_EVENT_TYPE(wxEVT_COMMAND_COMBOBOX_SELECTED)

// Mouse event types
DEFINE_EVENT_TYPE(wxEVT_LEFT_DOWN)
DEFINE_EVENT_TYPE(wxEVT_LEFT_UP)
DEFINE_EVENT_TYPE(wxEVT_MIDDLE_DOWN)
DEFINE_EVENT_TYPE(wxEVT_MIDDLE_UP)
DEFINE_EVENT_TYPE(wxEVT_RIGHT_DOWN)
DEFINE_EVENT_TYPE(wxEVT_RIGHT_UP)
DEFINE_EVENT_TYPE(wxEVT_MOTION)
DEFINE_EVENT_TYPE(wxEVT_ENTER_WINDOW)
DEFINE_EVENT_TYPE(wxEVT_LEAVE_WINDOW)
DEFINE_EVENT_TYPE(wxEVT_LEFT_DCLICK)
DEFINE_EVENT_TYPE(wxEVT_MIDDLE_DCLICK)
DEFINE_EVENT_TYPE(wxEVT_RIGHT_DCLICK)
DEFINE_EVENT_TYPE(wxEVT_SET_FOCUS)
DEFINE_EVENT_TYPE(wxEVT_KILL_FOCUS)
DEFINE_EVENT_TYPE(wxEVT_MOUSEWHEEL)

// Character input event type
DEFINE_EVENT_TYPE(wxEVT_CHAR)
DEFINE_EVENT_TYPE(wxEVT_CHAR_HOOK)
DEFINE_EVENT_TYPE(wxEVT_KEY_DOWN)
DEFINE_EVENT_TYPE(wxEVT_KEY_UP)

// wxScrollbar and wxSlider event identifiers
DEFINE_EVENT_TYPE(wxEVT_SCROLL_TOP)
DEFINE_EVENT_TYPE(wxEVT_SCROLL_BOTTOM)
DEFINE_EVENT_TYPE(wxEVT_SCROLL_LINEUP)
DEFINE_EVENT_TYPE(wxEVT_SCROLL_LINEDOWN)
DEFINE_EVENT_TYPE(wxEVT_SCROLL_PAGEUP)
DEFINE_EVENT_TYPE(wxEVT_SCROLL_PAGEDOWN)
DEFINE_EVENT_TYPE(wxEVT_SCROLL_THUMBTRACK)
DEFINE_EVENT_TYPE(wxEVT_SCROLL_THUMBRELEASE)
DEFINE_EVENT_TYPE(wxEVT_SCROLL_ENDSCROLL)

// System events
DEFINE_EVENT_TYPE(wxEVT_SIZE)
DEFINE_EVENT_TYPE(wxEVT_MOVE)
DEFINE_EVENT_TYPE(wxEVT_CLOSE_WINDOW)
DEFINE_EVENT_TYPE(wxEVT_END_SESSION)
DEFINE_EVENT_TYPE(wxEVT_QUERY_END_SESSION)
DEFINE_EVENT_TYPE(wxEVT_ACTIVATE_APP)
DEFINE_EVENT_TYPE(wxEVT_ACTIVATE)
DEFINE_EVENT_TYPE(wxEVT_CREATE)
DEFINE_EVENT_TYPE(wxEVT_DESTROY)
DEFINE_EVENT_TYPE(wxEVT_SHOW)
DEFINE_EVENT_TYPE(wxEVT_ICONIZE)
DEFINE_EVENT_TYPE(wxEVT_MAXIMIZE)
DEFINE_EVENT_TYPE(wxEVT_PAINT)
DEFINE_EVENT_TYPE(wxEVT_ERASE_BACKGROUND)
DEFINE_EVENT_TYPE(wxEVT_INIT_DIALOG)
DEFINE_EVENT_TYPE(wxEVT_IDLE)
DEFINE_EVENT_TYPE(wxEVT_UPDATE_UI)

// wxTextCtrl's (src/common/textcmn.cpp)
DEFINE_EVENT_TYPE(wxEVT_COMMAND_TEXT_UPDATED)
DEFINE_EVENT_TYPE(wxEVT_COMMAND_TEXT_ENTER)

// --- Events -------------------------------------------------------------------------------------

wxEvent::wxEvent(const int theId, const wxEventType commandType)
{
  m_eventType = commandType;
  m_eventObject = nullptr;
  m_timeStamp = 0;
  m_id = theId;
  m_skipped = false;
  m_callbackUserData = nullptr;
  m_isCommandEvent = false;
}

wxEvent::wxEvent(const wxEvent& src)
  : wxObject()
  , m_eventObject(src.m_eventObject)
  , m_eventType(src.m_eventType)
  , m_timeStamp(src.m_timeStamp)
  , m_id(src.m_id)
  , m_callbackUserData(src.m_callbackUserData)
  , m_skipped(src.m_skipped)
  , m_isCommandEvent(src.m_isCommandEvent)
{}

wxCommandEvent::wxCommandEvent(const wxEventType commandType, const int theId)
  : wxEvent(theId, commandType)
{
  m_clientData = nullptr;
  m_extraLong = 0;
  m_commandInt = 0;
  m_commandString = wxEmptyString;
  m_isCommandEvent = true;
}

wxScrollEvent::wxScrollEvent(const wxEventType commandType, const int id, const int pos, const int orient)
  : wxCommandEvent(commandType, id)
{
  m_extraLong = orient;
  m_commandInt = pos;
}

wxMouseEvent::wxMouseEvent(const wxEventType commandType)
{
  m_eventType = commandType;
  m_metaDown = false;
  m_altDown = false;
  m_controlDown = false;
  m_shiftDown = false;
  m_leftDown = false;
  m_rightDown = false;
  m_middleDown = false;
  m_x = 0;
  m_y = 0;
  m_wheelRotation = 0;
  m_wheelDelta = 0;
  m_linesPerAction = 0;
}

void wxMouseEvent::Assign(const wxMouseEvent& event)
{
  m_eventType = event.m_eventType;

  m_x = event.m_x;
  m_y = event.m_y;

  m_leftDown = event.m_leftDown;
  m_middleDown = event.m_middleDown;
  m_rightDown = event.m_rightDown;

  m_controlDown = event.m_controlDown;
  m_shiftDown = event.m_shiftDown;
  m_altDown = event.m_altDown;
  m_metaDown = event.m_metaDown;

  m_wheelRotation = event.m_wheelRotation;
  m_wheelDelta = event.m_wheelDelta;
  m_linesPerAction = event.m_linesPerAction;
}

// True if was a button dclick event (1 = left, 2 = middle, 3 = right) or any button dclick event
// (but = -1)
bool wxMouseEvent::ButtonDClick(const int but) const
{
  switch (but) {
  case -1:
    return LeftDClick() || MiddleDClick() || RightDClick();
  case 1:
    return LeftDClick();
  case 2:
    return MiddleDClick();
  case 3:
    return RightDClick();
  default:
    wxFAIL_MSG(wxT("invalid parameter in wxMouseEvent::ButtonDClick"));
  }
  return false;
}

// True if was a button down event (1 = left, 2 = middle, 3 = right) or any button down event
// (but = -1)
bool wxMouseEvent::ButtonDown(const int but) const
{
  switch (but) {
  case -1:
    return LeftDown() || MiddleDown() || RightDown();
  case 1:
    return LeftDown();
  case 2:
    return MiddleDown();
  case 3:
    return RightDown();
  default:
    wxFAIL_MSG(wxT("invalid parameter in wxMouseEvent::ButtonDown"));
  }
  return false;
}

// True if was a button up event (1 = left, 2 = middle, 3 = right) or any button up event
// (but = -1)
bool wxMouseEvent::ButtonUp(const int but) const
{
  switch (but) {
  case -1:
    return LeftUp() || MiddleUp() || RightUp();
  case 1:
    return LeftUp();
  case 2:
    return MiddleUp();
  case 3:
    return RightUp();
  default:
    wxFAIL_MSG(wxT("invalid parameter in wxMouseEvent::ButtonUp"));
  }
  return false;
}

// True if the given button is currently changing state
bool wxMouseEvent::Button(const int but) const
{
  switch (but) {
  case -1:
    return ButtonUp(-1) || ButtonDown(-1) || ButtonDClick(-1);
  case 1:
    return LeftDown() || LeftUp() || LeftDClick();
  case 2:
    return MiddleDown() || MiddleUp() || MiddleDClick();
  case 3:
    return RightDown() || RightUp() || RightDClick();
  default:
    wxFAIL_MSG(wxT("invalid parameter in wxMouseEvent::Button"));
  }
  return false;
}

bool wxMouseEvent::ButtonIsDown(const int but) const
{
  switch (but) {
  case -1:
    return LeftIsDown() || MiddleIsDown() || RightIsDown();
  case 1:
    return LeftIsDown();
  case 2:
    return MiddleIsDown();
  case 3:
    return RightIsDown();
  default:
    wxFAIL_MSG(wxT("invalid parameter in wxMouseEvent::ButtonIsDown"));
  }
  return false;
}

int wxMouseEvent::GetButton() const
{
  for (int i = 1; i <= 3; i++) {
    if (Button(i)) {
      return i;
    }
  }
  return -1;
}

wxKeyEvent::wxKeyEvent(const wxEventType type)
{
  m_eventType = type;
  m_x = 0;
  m_y = 0;
  m_shiftDown = false;
  m_controlDown = false;
  m_metaDown = false;
  m_altDown = false;
  m_keyCode = 0;
  m_scanCode = false;
  m_uniChar = 0;
  m_rawCode = 0;
  m_rawFlags = 0;
}

wxKeyEvent::wxKeyEvent(const wxKeyEvent& evt)
  : wxEvent(evt)
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
  m_uniChar = evt.m_uniChar;
}

// --- wxEvtHandler -------------------------------------------------------------------------------

const wxEventTable* wxEvtHandler::GetEventTable() const
{
  return &wxEvtHandler::sm_eventTable;
}

const wxEventTable wxEvtHandler::sm_eventTable = {nullptr, &wxEvtHandler::sm_eventTableEntries[0]};

const wxEventTableEntry wxEvtHandler::sm_eventTableEntries[] = {
  DECLARE_EVENT_TABLE_ENTRY(wxEVT_NULL, 0, 0, (wxObjectEventFunction) nullptr, nullptr)
};

wxEvtHandler::wxEvtHandler()
{
  m_nextHandler = nullptr;
  m_previousHandler = nullptr;
  m_enabled = true;
  m_dynamicEvents = nullptr;
  m_isWindow = false;
  m_pendingEvents = nullptr;
  m_eventsLocker = new std::mutex;
  // no client data (yet)
  m_clientData = nullptr;
}

wxEvtHandler::~wxEvtHandler()
{
  // Takes itself out of the list of handlers
  if (m_previousHandler) {
    m_previousHandler->m_nextHandler = m_nextHandler;
  }
  if (m_nextHandler) {
    m_nextHandler->m_previousHandler = m_previousHandler;
  }

  if (m_dynamicEvents) {
    for (wxDynamicEventTableEntry* const entry : *m_dynamicEvents) {
      delete entry->m_callbackUserData;
      delete entry;
    }
    delete m_dynamicEvents;
  }

  // The events still queued for this handler go with it, and the application's list stops naming
  // it (wxWindows 2.4.2 leaves both behind).
  {
    const std::lock_guard<std::mutex> lock(PendingHandlersLock());
    std::vector<wxEvtHandler*>& handlers = PendingHandlers();
    handlers.erase(std::remove(handlers.begin(), handlers.end(), this), handlers.end());
  }
  if (m_pendingEvents) {
    for (wxEvent* const event : *m_pendingEvents) {
      delete event;
    }
    delete m_pendingEvents;
  }

  delete m_eventsLocker;
}

void wxEvtHandler::AddPendingEvent(wxEvent& event)
{
  // 1) Add event to list of pending events of this event handler
  wxEvent* const eventCopy = event.Clone();

  // we must be able to copy the events here so the event class must implement Clone() properly
  // instead of just providing a NULL stab for it
  wxCHECK_RET(eventCopy, wxT("events of this type aren't supposed to be posted"));

  {
    const std::lock_guard<std::mutex> lock(*m_eventsLocker);
    if (!m_pendingEvents) {
      m_pendingEvents = new std::vector<wxEvent*>;
    }
    m_pendingEvents->push_back(eventCopy);
  }

  // 2) Add this event handler to list of event handlers that have pending events.
  {
    const std::lock_guard<std::mutex> lock(PendingHandlersLock());
    PendingHandlers().push_back(this);
  }

  // 3) Inform the system that new pending events are somwehere, and that these should be
  //    processed in idle time.
  wxWakeUpIdle();
}

void wxEvtHandler::ProcessPendingEvents()
{
  std::unique_lock<std::mutex> lock(*m_eventsLocker);
  while (m_pendingEvents && !m_pendingEvents->empty()) {
    wxEvent* const event = m_pendingEvents->front();
    m_pendingEvents->erase(m_pendingEvents->begin());

    // In ProcessEvent, new events might get added and we can safely leave the crtical section
    // here.
    lock.unlock();
    ProcessEvent(*event);
    delete event;
    lock.lock();
  }
}

bool wxEvtHandler::ProcessEvent(wxEvent& event)
{
  // allow the application to hook into event processing
  if (wxTheApp) {
    const int rc = wxTheApp->FilterEvent(event);
    if (rc != -1) {
      wxASSERT_MSG(rc == 1 || rc == 0, wxT("unexpected wxApp::FilterEvent return value"));
      return rc != 0;
    }
    // else: proceed normally
  }

  // An event handler can be enabled or disabled
  if (GetEvtHandlerEnabled()) {
    // Handle per-instance dynamic event tables first
    if (m_dynamicEvents && SearchDynamicEventTable(event)) {
      return true;
    }

    // Then static per-class event tables
    const wxEventTable* table = GetEventTable();

    // Try the associated validator first, if this is a window. Problem: if the event handler of
    // the window has been replaced, this wxEvtHandler may no longer be a window. Therefore
    // validators won't be processed if the handler has been replaced with SetEventHandler. THIS
    // CAN BE CURED if PushEventHandler is used instead of SetEventHandler, and then processing
    // will be passed down the chain of event handlers.
    if (m_isWindow) {
      wxWindow* const win = static_cast<wxWindow*>(this);

      // Can only use the validator of the window which is receiving the event
      if (win == event.GetEventObject()) {
        wxValidator* const validator = win->GetValidator();
        if (validator && validator->ProcessEvent(event)) {
          return true;
        }
      }
    }

    // Search upwards through the inheritance hierarchy
    while (table) {
      if (SearchEventTable(const_cast<wxEventTable&>(*table), event)) {
        return true;
      }
      table = table->baseTable;
    }
  }

  // Try going down the event handler chain
  if (GetNextHandler()) {
    if (GetNextHandler()->ProcessEvent(event)) {
      return true;
    }
  }

  // Carry on up the parent-child hierarchy, but only if event is a command event: it wouldn't
  // make sense for a parent to receive a child's size event, for example
  if (m_isWindow && event.IsCommandEvent()) {
    wxWindow* const win = static_cast<wxWindow*>(this);

    // honour the requests to stop propagation at this window: this is used by the dialogs, for
    // example, to prevent processing the events from the dialog controls in the parent frame
    // which rarely, if ever, makes sense
    if (!(win->GetExtraStyle() & wxWS_EX_BLOCK_EVENTS)) {
      wxWindow* const parent = win->GetParent();
      if (parent && !parent->IsBeingDeleted()) {
        return parent->GetEventHandler()->ProcessEvent(event);
      }
    }
  }

  // Last try - application object.
  if (wxTheApp && (this != wxTheApp)) {
    // Special case: don't pass wxEVT_IDLE to wxApp, since it'll always swallow it. wxEVT_IDLE is
    // sent explicitly to wxApp so it will be processed appropriately via SearchEventTable.
    if (event.GetEventType() != wxEVT_IDLE) {
      if (wxTheApp->ProcessEvent(event)) {
        return true;
      }
    }
  }

  return false;
}

bool wxEvtHandler::SearchEventTable(wxEventTable& table, wxEvent& event)
{
  const wxEventType eventType = event.GetEventType();
  const int eventId = event.GetId();

  for (int i = 0; table.entries[i].m_fn != nullptr; i++) {
    const wxEventTableEntry& entry = table.entries[i];

    // match only if the event type is the same and the id is either -1 in the event table
    // (meaning "any") or the event id matches the id specified in the event table either exactly
    // or by falling into range between first and last
    if (eventType == entry.m_eventType) {
      const int tableId1 = entry.m_id;
      const int tableId2 = entry.m_lastId;

      if ((tableId1 == -1) || (tableId2 == -1 && eventId == tableId1)
          || (tableId2 != -1 && (eventId >= tableId1 && eventId <= tableId2))) {
        event.Skip(false);
        event.m_callbackUserData = entry.m_callbackUserData;

        (this->*((wxEventFunction)(entry.m_fn)))(event);

        return !event.GetSkipped();
      }
    }
  }

  return false;
}

void wxEvtHandler::Connect(
  const int id, const int lastId, const int eventType, const wxObjectEventFunction func, wxObject* const userData
)
{
  auto* const entry = new wxDynamicEventTableEntry(eventType, id, lastId, func, userData);

  if (!m_dynamicEvents) {
    m_dynamicEvents = new std::vector<wxDynamicEventTableEntry*>;
  }

  // Insert at the front of the list so most recent additions are found first
  m_dynamicEvents->insert(m_dynamicEvents->begin(), entry);
}

bool wxEvtHandler::Disconnect(
  const int id, const int lastId, const wxEventType eventType, const wxObjectEventFunction func, wxObject* const userData
)
{
  if (!m_dynamicEvents) {
    return false;
  }

  for (auto it = m_dynamicEvents->begin(); it != m_dynamicEvents->end(); ++it) {
    wxDynamicEventTableEntry* const entry = *it;
    if ((entry->m_id == id) && ((entry->m_lastId == lastId) || (lastId == -1))
        && ((entry->m_eventType == eventType) || (eventType == wxEVT_NULL))
        && ((entry->m_fn == func) || (func == nullptr))
        && ((entry->m_callbackUserData == userData) || (userData == nullptr))) {
      delete entry->m_callbackUserData;
      m_dynamicEvents->erase(it);
      delete entry;
      return true;
    }
  }
  return false;
}

bool wxEvtHandler::SearchDynamicEventTable(wxEvent& event)
{
  wxCHECK_MSG(m_dynamicEvents, false, wxT("caller should check that we have dynamic events"));

  const int commandId = event.GetId();

  // A handler may Connect or Disconnect while it runs: the walk is over a copy.
  const std::vector<wxDynamicEventTableEntry*> entries = *m_dynamicEvents;
  for (wxDynamicEventTableEntry* const entry : entries) {
    if (entry->m_fn) {
      // Match, if event spec says any id will do (id == -1)
      if ((event.GetEventType() == entry->m_eventType)
          && (entry->m_id == -1 || (entry->m_lastId == -1 && commandId == entry->m_id)
              || (entry->m_lastId != -1 && (commandId >= entry->m_id && commandId <= entry->m_lastId)))) {
        event.Skip(false);
        event.m_callbackUserData = entry->m_callbackUserData;

        (this->*((wxEventFunction)(entry->m_fn)))(event);

        if (!event.GetSkipped()) {
          return true;
        }
      }
    }
  }
  return false;
}

// --- The application's pending events (src/common/appcmn.cpp) ------------------------------------

void wxAppBase::ProcessPendingEvents()
{
  std::unique_lock<std::mutex> lock(PendingHandlersLock());
  std::vector<wxEvtHandler*>& handlers = PendingHandlers();
  while (!handlers.empty()) {
    wxEvtHandler* const handler = handlers.front();
    handlers.erase(handlers.begin());

    lock.unlock();
    handler->ProcessPendingEvents();
    lock.lock();
  }
}
