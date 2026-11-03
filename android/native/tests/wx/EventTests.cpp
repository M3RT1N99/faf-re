// wx-lite's event system (src/wx/event.cpp) against wxWindows 2.4.2's src/common/event.cpp: which
// handler runs, in which order, what Skip does, and where an event goes when no handler takes it.

#include <string>
#include <vector>

#include "Check.h"
#include "Support.h"
#include "wx/frame.h"
#include "wx/window.h"

namespace
{
  DECLARE_EVENT_TYPE(wxEVT_TEST_ONE, -1)
  DECLARE_EVENT_TYPE(wxEVT_TEST_TWO, -1)
  DEFINE_EVENT_TYPE(wxEVT_TEST_ONE)
  DEFINE_EVENT_TYPE(wxEVT_TEST_TWO)

  using Calls = std::vector<std::string>;

  /// Writes down which of its functions ran; skips the event where told to.
  class Recorder : public wxEvtHandler
  {
  public:
    Calls calls;
    bool skipFirst = false;

    void OnFirst(wxCommandEvent& event)
    {
      calls.push_back("first");
      if (skipFirst) {
        event.Skip();
      }
    }
    void OnAny(wxCommandEvent&)
    {
      calls.push_back("any");
    }
    void OnRange(wxCommandEvent& event)
    {
      calls.push_back("range " + std::to_string(event.GetId()));
    }
    void OnDynamicA(wxCommandEvent& event)
    {
      calls.push_back("dynamic a");
      event.Skip();
    }
    void OnDynamicB(wxCommandEvent& event)
    {
      calls.push_back("dynamic b");
      event.Skip();
    }

    DECLARE_EVENT_TABLE()
  };

  BEGIN_EVENT_TABLE(Recorder, wxEvtHandler)
    EVT_COMMAND(5, wxEVT_TEST_ONE, Recorder::OnFirst)
    EVT_COMMAND(wxID_ANY, wxEVT_TEST_ONE, Recorder::OnAny)
    EVT_COMMAND_RANGE(10, 20, wxEVT_TEST_TWO, Recorder::OnRange)
  END_EVENT_TABLE()

  class DerivedRecorder : public Recorder
  {
  public:
    bool skipDerived = false;

    void OnDerived(wxCommandEvent& event)
    {
      calls.push_back("derived");
      if (skipDerived) {
        event.Skip();
      }
    }

    DECLARE_EVENT_TABLE()
  };

  BEGIN_EVENT_TABLE(DerivedRecorder, Recorder)
    EVT_COMMAND(5, wxEVT_TEST_ONE, DerivedRecorder::OnDerived)
  END_EVENT_TABLE()

  /// An event that is no command event.
  class PlainEvent : public wxEvent
  {
  public:
    PlainEvent(const int id, const wxEventType type)
      : wxEvent(id, type)
    {}
    wxEvent* Clone() const override
    {
      return new PlainEvent(*this);
    }
  };

  /// A frame that writes down the ids of the wxEVT_TEST_ONE events that reach it.
  class EventFrame : public wxFrame
  {
  public:
    EventFrame()
      : wxFrame(nullptr, wxID_ANY, wxT("event test"), wxPoint(0, 0), wxSize(200, 100))
    {}

    std::vector<int> ids;

    void OnTest(wxCommandEvent& event)
    {
      ids.push_back(event.GetId());
    }

    DECLARE_EVENT_TABLE()
  };

  BEGIN_EVENT_TABLE(EventFrame, wxFrame)
    EVT_COMMAND(wxID_ANY, wxEVT_TEST_ONE, EventFrame::OnTest)
  END_EVENT_TABLE()

  bool Process(wxEvtHandler& handler, const wxEventType type, const int id)
  {
    wxCommandEvent event(type, id);
    return handler.ProcessEvent(event);
  }
} // namespace

WXT_TEST(NewEventTypesCountUpFromTheFirst)
{
  WXT_CHECK(wxEVT_TEST_ONE >= wxEVT_FIRST);
  WXT_CHECK_EQ(wxEVT_TEST_TWO, wxEVT_TEST_ONE + 1);
}

WXT_TEST(AnEntryTakesItsTypeAndId)
{
  Recorder recorder;
  WXT_CHECK(Process(recorder, wxEVT_TEST_ONE, 5));
  WXT_CHECK(recorder.calls == Calls{"first"});

  // Of the other type, only the range takes ids, and 5 is not in it.
  recorder.calls.clear();
  WXT_CHECK(!Process(recorder, wxEVT_TEST_TWO, 5));
  WXT_CHECK(recorder.calls.empty());
}

WXT_TEST(AnEntryForAnyIdTakesTheOthers)
{
  Recorder recorder;
  WXT_CHECK(Process(recorder, wxEVT_TEST_ONE, 7));
  WXT_CHECK(recorder.calls == Calls{"any"});
}

WXT_TEST(ARangeTakesItsBounds)
{
  Recorder recorder;
  for (const int id : {9, 10, 15, 20, 21}) {
    (void)Process(recorder, wxEVT_TEST_TWO, id);
  }
  WXT_CHECK(recorder.calls == (Calls{"range 10", "range 15", "range 20"}));
}

WXT_TEST(TheFirstMatchEndsItsTableEvenWhenItSkips)
{
  // wxWindows 2.4.2's SearchEventTable returns after the first entry that matches: Skip lets the
  // base class's table and the next handler see the event, not the entries after it.
  Recorder recorder;
  recorder.skipFirst = true;
  WXT_CHECK(!Process(recorder, wxEVT_TEST_ONE, 5));
  WXT_CHECK(recorder.calls == Calls{"first"});
}

WXT_TEST(TheDerivedTableComesBeforeTheBase)
{
  DerivedRecorder recorder;
  WXT_CHECK(Process(recorder, wxEVT_TEST_ONE, 5));
  WXT_CHECK(recorder.calls == Calls{"derived"});

  recorder.calls.clear();
  recorder.skipDerived = true;
  WXT_CHECK(Process(recorder, wxEVT_TEST_ONE, 5));
  WXT_CHECK(recorder.calls == (Calls{"derived", "first"}));
}

WXT_TEST(DynamicHandlersComeFirstTheNewestFirst)
{
  Recorder recorder;
  recorder.Connect(5, wxEVT_TEST_ONE, (wxObjectEventFunction)(wxEventFunction)(wxCommandEventFunction)&Recorder::OnDynamicA);
  recorder.Connect(5, wxEVT_TEST_ONE, (wxObjectEventFunction)(wxEventFunction)(wxCommandEventFunction)&Recorder::OnDynamicB);
  // Both skip, so each one runs, then the table.
  WXT_CHECK(Process(recorder, wxEVT_TEST_ONE, 5));
  WXT_CHECK(recorder.calls == (Calls{"dynamic b", "dynamic a", "first"}));

  recorder.calls.clear();
  WXT_CHECK(recorder.Disconnect(5, wxEVT_TEST_ONE, (wxObjectEventFunction)(wxEventFunction)(wxCommandEventFunction)&Recorder::OnDynamicB));
  WXT_CHECK(!recorder.Disconnect(5, wxEVT_TEST_ONE, (wxObjectEventFunction)(wxEventFunction)(wxCommandEventFunction)&Recorder::OnDynamicB));
  WXT_CHECK(Process(recorder, wxEVT_TEST_ONE, 5));
  WXT_CHECK(recorder.calls == (Calls{"dynamic a", "first"}));
}

WXT_TEST(TheNextHandlerGetsWhatTheFirstLeaves)
{
  Recorder first;
  Recorder second;
  first.SetNextHandler(&second);

  WXT_CHECK(Process(first, wxEVT_TEST_ONE, 5));
  WXT_CHECK(first.calls == Calls{"first"});
  WXT_CHECK(second.calls.empty());

  // A disabled handler's tables are not searched; the chain goes on.
  first.calls.clear();
  first.SetEvtHandlerEnabled(false);
  WXT_CHECK(Process(first, wxEVT_TEST_ONE, 5));
  WXT_CHECK(first.calls.empty());
  WXT_CHECK(second.calls == Calls{"first"});
  first.SetNextHandler(nullptr);
}

WXT_TEST(WhatNoHandlerTakesGoesToTheApplicationButIdle)
{
  TestApp& app = TestApp::Get();
  app.reached.clear();
  const int idles = app.idles;

  Recorder recorder;
  WXT_CHECK(Process(recorder, wxEVT_TEST_APP, 42));
  WXT_CHECK(app.reached == std::vector<int>{42});

  wxIdleEvent idle;
  WXT_CHECK(!recorder.ProcessEvent(idle));
  WXT_CHECK_EQ(app.idles, idles);
}

WXT_TEST(PendingEventsWaitForTheIdleTime)
{
  // Drained, so that only the wake-up below is in the queue.
  wxtest::Pump();

  Recorder recorder;
  wxCommandEvent five(wxEVT_TEST_ONE, 5);
  wxCommandEvent seven(wxEVT_TEST_ONE, 7);
  recorder.AddPendingEvent(five);
  recorder.AddPendingEvent(seven);
  WXT_CHECK(recorder.calls.empty());

  // wxWakeUpIdle: a message for the loop's thread wakes the loop.
  MSG msg;
  WXT_CHECK(::PeekMessageW(&msg, nullptr, WM_NULL, WM_NULL, PM_REMOVE));
  WXT_CHECK(msg.hwnd == nullptr);

  wxTheApp->ProcessPendingEvents();
  WXT_CHECK(recorder.calls == (Calls{"first", "any"}));
  wxtest::Pump();
}

WXT_TEST(CommandEventsClimbToTheParentWindow)
{
  auto* const frame = new EventFrame;
  auto* const child = new wxWindow(frame, 77, wxPoint(0, 0), wxSize(20, 20));

  wxCommandEvent command(wxEVT_TEST_ONE, 77);
  command.SetEventObject(child);
  WXT_CHECK(child->GetEventHandler()->ProcessEvent(command));
  WXT_CHECK(frame->ids == std::vector<int>{77});

  // Not an event of another kind: a parent would not want a child's size, for one.
  frame->ids.clear();
  PlainEvent plain(77, wxEVT_TEST_ONE);
  plain.SetEventObject(child);
  WXT_CHECK(!child->GetEventHandler()->ProcessEvent(plain));
  WXT_CHECK(frame->ids.empty());

  // And not past a window that blocks them.
  child->SetExtraStyle(wxWS_EX_BLOCK_EVENTS);
  wxCommandEvent blocked(wxEVT_TEST_ONE, 77);
  blocked.SetEventObject(child);
  WXT_CHECK(!child->GetEventHandler()->ProcessEvent(blocked));
  WXT_CHECK(frame->ids.empty());

  delete frame;
}

WXT_TEST(APushedHandlerSeesTheWindowsEventsFirst)
{
  auto* const frame = new EventFrame;
  auto* const pushed = new Recorder;
  frame->PushEventHandler(pushed);
  WXT_CHECK(frame->GetEventHandler() == pushed);

  WXT_CHECK(Process(*frame->GetEventHandler(), wxEVT_TEST_ONE, 5));
  WXT_CHECK(pushed->calls == Calls{"first"});
  WXT_CHECK(frame->ids.empty());

  // Skipped, the window's own table has it.
  pushed->skipFirst = true;
  WXT_CHECK(Process(*frame->GetEventHandler(), wxEVT_TEST_ONE, 5));
  WXT_CHECK(frame->ids == std::vector<int>{5});

  WXT_CHECK(frame->PopEventHandler(true) == nullptr);
  WXT_CHECK(frame->GetEventHandler() == frame);
  delete frame;
}
