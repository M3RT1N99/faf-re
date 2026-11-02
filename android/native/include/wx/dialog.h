#pragma once

// wx-lite (android/native/WX_LITE.md): wxWindows 2.4.2's wxDialogBase (include/wx/dialog.h) and
// the MSW port's wxDialog (include/wx/msw/dialog.h), for what the engine uses. Android shows no
// wx window, so nobody can answer a modal dialog: ShowModal returns wxID_CANCEL at once, as if it
// had been closed (wx/dialog.h's ShowModal says it at the place).

#include "wx/toplevel.h"

extern const wxChar* wxDialogNameStr;

class wxDialogBase : public wxTopLevelWindow
{
public:
  wxDialogBase()
  {
    Init();
  }
  ~wxDialogBase() override {}

  void Init();

  void SetReturnCode(int returnCode)
  {
    m_returnCode = returnCode;
  }
  int GetReturnCode() const
  {
    return m_returnCode;
  }

protected:
  // the return code from modal dialog
  int m_returnCode;
};

class wxDialog : public wxDialogBase
{
public:
  wxDialog()
  {
    Init();
  }
  wxDialog(
    wxWindow* parent,
    wxWindowID id,
    const wxString& title,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = wxDEFAULT_DIALOG_STYLE,
    const wxString& name = wxDialogNameStr
  )
  {
    Init();
    Create(parent, id, title, pos, size, style, name);
  }

  bool Create(
    wxWindow* parent,
    wxWindowID id,
    const wxString& title,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = wxDEFAULT_DIALOG_STYLE,
    const wxString& name = wxDialogNameStr
  );

  ~wxDialog() override;

  void SetModal(bool flag);
  virtual bool IsModal() const;

  /// Shows the dialog modally and returns how it ended. On Android, with no one to answer it,
  /// it ends at once with wxID_CANCEL (EndModal), as a dialog closed by its close box does.
  virtual int ShowModal();
  virtual void EndModal(int retCode);
  bool IsModalShowing() const;

  bool Show(bool show = true) override;

  // event handlers
  void OnCharHook(wxKeyEvent& event);
  void OnCloseWindow(wxCloseEvent& event);
  /// wxID_OK: Validate and TransferDataFromWindow, then EndModal(wxID_OK) or Show(false).
  void OnOK(wxCommandEvent& event);
  void OnApply(wxCommandEvent& event);
  void OnCancel(wxCommandEvent& event);

  long MSWWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam) override;

protected:
  void Init();

private:
  wxWindow* m_oldFocus;

  DECLARE_EVENT_TABLE()
};
