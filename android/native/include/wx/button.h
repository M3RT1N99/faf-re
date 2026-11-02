#pragma once

// wx-lite (android/native/WX_LITE.md): wxWindows 2.4.2's wxButtonBase (include/wx/button.h) and
// the MSW port's wxButton (include/wx/msw/button.h), for what the engine uses.

#include "wx/control.h"

#define wxBU_NOAUTODRAW 0x0000
#define wxBU_AUTODRAW 0x0004
#define wxBU_LEFT 0x0040
#define wxBU_TOP 0x0080
#define wxBU_RIGHT 0x0100
#define wxBU_BOTTOM 0x0200
#define wxBU_ALIGN_MASK (wxBU_LEFT | wxBU_TOP | wxBU_RIGHT | wxBU_BOTTOM)
#define wxBU_EXACTFIT 0x0001

extern const wxChar* wxButtonNameStr;

class wxButtonBase : public wxControl
{
public:
  wxButtonBase() {}

  /// Makes this the default button, the one Enter presses in a dialog on Windows. wx-lite does no
  /// keyboard navigation (wxWindow::MSWProcessMessage): there is nothing to mark.
  virtual void SetDefault() = 0;
};

class wxButton : public wxButtonBase
{
public:
  wxButton() {}
  wxButton(
    wxWindow* parent,
    wxWindowID id,
    const wxString& label,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = 0,
    const wxValidator& validator = wxDefaultValidator,
    const wxString& name = wxButtonNameStr
  )
  {
    Create(parent, id, label, pos, size, style, validator, name);
  }

  bool Create(
    wxWindow* parent,
    wxWindowID id,
    const wxString& label,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = 0,
    const wxValidator& validator = wxDefaultValidator,
    const wxString& name = wxButtonNameStr
  );

  ~wxButton() override;

  void SetDefault() override;

  /// Sends the button's wxEVT_COMMAND_BUTTON_CLICKED.
  void Command(wxCommandEvent& event) override;
  bool SendClickEvent();

  static wxSize GetDefaultSize();

protected:
  wxSize DoGetBestSize() const override;
};
