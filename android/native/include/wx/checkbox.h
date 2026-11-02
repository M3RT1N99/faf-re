#pragma once

// wx-lite (android/native/WX_LITE.md): wxWindows 2.4.2's wxCheckBoxBase (include/wx/checkbox.h)
// and the MSW port's wxCheckBox (include/wx/msw/checkbox.h), for what the engine uses.

#include "wx/control.h"

extern const wxChar* wxCheckBoxNameStr;

class wxCheckBoxBase : public wxControl
{
public:
  // set/get the checked status of the listbox
  virtual void SetValue(bool value) = 0;
  virtual bool GetValue() const = 0;

  bool IsChecked() const
  {
    return GetValue();
  }
};

class wxCheckBox : public wxCheckBoxBase
{
public:
  wxCheckBox()
    : m_isChecked(false)
  {}
  wxCheckBox(
    wxWindow* parent,
    wxWindowID id,
    const wxString& label,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = 0,
    const wxValidator& validator = wxDefaultValidator,
    const wxString& name = wxCheckBoxNameStr
  )
    : m_isChecked(false)
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
    const wxString& name = wxCheckBoxNameStr
  );

  /// Sets the check, as BM_SETCHECK does: without a wxEVT_COMMAND_CHECKBOX_CLICKED.
  void SetValue(bool value) override;
  bool GetValue() const override;

  void Command(wxCommandEvent& event) override;

protected:
  wxSize DoGetBestSize() const override;

private:
  bool m_isChecked;
};
