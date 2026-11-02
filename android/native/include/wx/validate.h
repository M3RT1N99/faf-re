#pragma once

// wx-lite (android/native/WX_LITE.md): wxValidator of wxWindows 2.4.2 (include/wx/validate.h).
// The engine sets no validator: the windows' constructors take the default one, whose Clone is
// null, so a window holds none (wxWindowBase::SetValidator).

#include "wx/event.h"

class wxWindow;
class wxWindowBase;

class wxValidator : public wxEvtHandler
{
public:
  wxValidator();
  ~wxValidator() override;

  /// A copy for a window to own; null for the default validator.
  virtual wxObject* Clone() const
  {
    return nullptr;
  }
  bool Copy(const wxValidator& val)
  {
    m_validatorWindow = val.m_validatorWindow;
    return true;
  }

  virtual bool Validate(wxWindow* WXUNUSED(parent))
  {
    return false;
  }
  virtual bool TransferToWindow()
  {
    return false;
  }
  virtual bool TransferFromWindow()
  {
    return false;
  }

  wxWindow* GetWindow() const;
  void SetWindow(wxWindowBase* win)
  {
    m_validatorWindow = win;
  }

protected:
  wxWindowBase* m_validatorWindow;
};

extern const wxValidator wxDefaultValidator;
