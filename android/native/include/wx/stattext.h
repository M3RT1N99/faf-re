#pragma once

// wx-lite (android/native/WX_LITE.md): wxWindows 2.4.2's wxStaticTextBase (include/wx/stattext.h)
// and the MSW port's wxStaticText (include/wx/msw/stattext.h), for what the engine uses.

#include "wx/control.h"

#define wxST_NO_AUTORESIZE 0x0001

extern const wxChar* wxStaticTextNameStr;

class wxStaticTextBase : public wxControl
{
public:
  wxStaticTextBase() {}

  // overriden base class virtuals
  bool AcceptsFocus() const override
  {
    return false;
  }
};

class wxStaticText : public wxStaticTextBase
{
public:
  wxStaticText() {}
  wxStaticText(
    wxWindow* parent,
    wxWindowID id,
    const wxString& label,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = 0,
    const wxString& name = wxStaticTextNameStr
  )
  {
    Create(parent, id, label, pos, size, style, name);
  }

  bool Create(
    wxWindow* parent,
    wxWindowID id,
    const wxString& label,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = 0,
    const wxString& name = wxStaticTextNameStr
  );

  // override some methods to resize the window properly
  void SetLabel(const wxString& label) override;

protected:
  wxSize DoGetBestSize() const override;
};
