#pragma once

// wx-lite (android/native/WX_LITE.md): wxWindows 2.4.2's wxStaticBoxBase (include/wx/statbox.h)
// and the MSW port's wxStaticBox (include/wx/msw/statbox.h), for what the engine uses.

#include "wx/control.h"

extern const wxChar* wxStaticBoxNameStr;

class wxStaticBoxBase : public wxControl
{
public:
  wxStaticBoxBase() {}
};

class wxStaticBox : public wxStaticBoxBase
{
public:
  wxStaticBox() {}
  wxStaticBox(
    wxWindow* parent,
    wxWindowID id,
    const wxString& label,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = 0,
    const wxString& name = wxStaticBoxNameStr
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
    const wxString& name = wxStaticBoxNameStr
  );

  // a static box doesn't accept focus
  bool AcceptsFocus() const override
  {
    return false;
  }

protected:
  wxSize DoGetBestSize() const override;
};
