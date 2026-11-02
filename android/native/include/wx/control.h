#pragma once

// wx-lite (android/native/WX_LITE.md): wxWindows 2.4.2's wxControlBase (include/wx/control.h) and
// the MSW port's wxControl (include/wx/msw/control.h), for what the engine uses. A control is a
// child window of wx-lite's class that keeps its state (value, check) itself, its label being the
// window's text: the message layer has no standard controls, and Android shows none.

#include "wx/window.h"

extern const wxChar* wxControlNameStr;

class wxControlBase : public wxWindow
{
public:
  wxControlBase() {}
  ~wxControlBase() override;

  bool Create(
    wxWindow* parent,
    wxWindowID id,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = 0,
    const wxValidator& validator = wxDefaultValidator,
    const wxString& name = wxControlNameStr
  );

  /// Sends `event` to the control's handlers (Command, as wxControlBase).
  virtual void Command(wxCommandEvent& event);

protected:
  bool CreateControl(
    wxWindowBase* parent,
    wxWindowID id,
    const wxPoint& pos,
    const wxSize& size,
    long style,
    const wxValidator& validator,
    const wxString& name
  );

  /// Gives the control its best size where `size` has -1.
  void SetBestSize(const wxSize& size);

  /// The source of a command event: this control (InitCommandEvent).
  void InitCommandEvent(wxCommandEvent& event) const;
};

class wxControl : public wxControlBase
{
public:
  wxControl() {}
  wxControl(
    wxWindow* parent,
    wxWindowID id,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = 0,
    const wxValidator& validator = wxDefaultValidator,
    const wxString& name = wxControlNameStr
  )
  {
    Create(parent, id, pos, size, style, validator, name);
  }

  bool Create(
    wxWindow* parent,
    wxWindowID id,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = 0,
    const wxValidator& validator = wxDefaultValidator,
    const wxString& name = wxControlNameStr
  );

  ~wxControl() override;

  /// Hands `event` to the control's handlers and returns whether one took it.
  bool ProcessCommand(wxCommandEvent& event);

protected:
  /// Creates the control's window (wxWindow::MSWCreate of wx-lite's control class).
  bool MSWCreateControl(const wxPoint& pos, const wxSize& size, const wxString& label = wxEmptyString);

  wxSize DoGetBestSize() const override;
};
