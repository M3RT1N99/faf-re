#pragma once

// wx-lite (android/native/WX_LITE.md): wxWindows 2.4.2's wxTextAttr and wxTextCtrlBase
// (include/wx/textctrl.h) and the MSW port's wxTextCtrl (include/wx/msw/textctrl.h), for what the
// engine uses. The control holds its text. Each change of it sends one
// wxEVT_COMMAND_TEXT_UPDATED, as the Windows edit controls' EN_CHANGE does under wxMSW (the
// engine's log window counts on it: WWinLogWindow::mIsInitializingControls).

#include "wx/colour.h"
#include "wx/control.h"

#define wxTE_NO_VSCROLL 0x0002
#define wxTE_AUTO_SCROLL 0x0008
#define wxTE_READONLY 0x0010
#define wxTE_MULTILINE 0x0020
#define wxTE_PROCESS_TAB 0x0040
#define wxTE_LEFT 0x0000
#define wxTE_CENTER wxALIGN_CENTER_HORIZONTAL
#define wxTE_RIGHT wxALIGN_RIGHT
#define wxTE_CENTRE wxTE_CENTER
#define wxTE_RICH 0x0080
#define wxTE_PROCESS_ENTER 0x0400
#define wxTE_PASSWORD 0x0800
#define wxTE_AUTO_URL 0x1000
#define wxTE_NOHIDESEL 0x2000
#define wxTE_DONTWRAP wxHSCROLL
#define wxTE_LINEWRAP 0x4000
#define wxTE_WORDWRAP 0x0000
#define wxTE_RICH2 0x8000

extern const wxChar* wxTextCtrlNameStr;

/// The style of a text range: its colours (wxWindows 2.4.2's also has a font, which wx-lite
/// does not have).
class wxTextAttr
{
public:
  wxTextAttr() {}
  wxTextAttr(const wxColour& colText, const wxColour& colBack = wxNullColour)
    : m_colText(colText)
    , m_colBack(colBack)
  {}

  void SetTextColour(const wxColour& colText)
  {
    m_colText = colText;
  }
  void SetBackgroundColour(const wxColour& colBack)
  {
    m_colBack = colBack;
  }

  bool HasTextColour() const
  {
    return m_colText.Ok();
  }
  bool HasBackgroundColour() const
  {
    return m_colBack.Ok();
  }
  bool IsDefault() const
  {
    return !HasTextColour() && !HasBackgroundColour();
  }

  const wxColour& GetTextColour() const
  {
    return m_colText;
  }
  const wxColour& GetBackgroundColour() const
  {
    return m_colBack;
  }

private:
  wxColour m_colText, m_colBack;
};

class wxTextCtrlBase : public wxControl
{
public:
  wxTextCtrlBase() {}
  ~wxTextCtrlBase() override {}

  // accessors
  virtual wxString GetValue() const = 0;
  virtual void SetValue(const wxString& value) = 0;
  virtual int GetNumberOfLines() const = 0;
  virtual bool IsModified() const = 0;
  virtual bool IsEditable() const = 0;
  bool IsSingleLine() const
  {
    return !(GetWindowStyle() & wxTE_MULTILINE);
  }
  bool IsMultiLine() const
  {
    return !IsSingleLine();
  }

  // operations
  virtual void Clear() override = 0;
  virtual void WriteText(const wxString& text) = 0;
  virtual void AppendText(const wxString& text) = 0;
  virtual void MarkDirty() = 0;
  virtual void DiscardEdits() = 0;

  // the style of the text written from here on
  virtual bool SetDefaultStyle(const wxTextAttr& style);
  const wxTextAttr& GetDefaultStyle() const
  {
    return m_defaultStyle;
  }

  virtual void SetEditable(bool editable) = 0;

  virtual void SetInsertionPoint(long pos) = 0;
  virtual void SetInsertionPointEnd() = 0;
  virtual long GetInsertionPoint() const = 0;
  virtual long GetLastPosition() const = 0;

protected:
  wxTextAttr m_defaultStyle;
};

class wxTextCtrl : public wxTextCtrlBase
{
public:
  wxTextCtrl()
  {
    Init();
  }
  wxTextCtrl(
    wxWindow* parent,
    wxWindowID id,
    const wxString& value = wxEmptyString,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = 0,
    const wxValidator& validator = wxDefaultValidator,
    const wxString& name = wxTextCtrlNameStr
  )
  {
    Init();
    Create(parent, id, value, pos, size, style, validator, name);
  }
  ~wxTextCtrl() override;

  bool Create(
    wxWindow* parent,
    wxWindowID id,
    const wxString& value = wxEmptyString,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = 0,
    const wxValidator& validator = wxDefaultValidator,
    const wxString& name = wxTextCtrlNameStr
  );

  // implement base class pure virtuals
  wxString GetValue() const override;
  /// Writes `value` if it differs (always above 1024 characters, as wxMSW), then marks the
  /// control unmodified with the insertion point at 0.
  void SetValue(const wxString& value) override;
  int GetNumberOfLines() const override;
  bool IsModified() const override;
  bool IsEditable() const override;

  void Clear() override;
  void WriteText(const wxString& text) override;
  void AppendText(const wxString& text) override;
  void MarkDirty() override;
  void DiscardEdits() override;

  void SetEditable(bool editable) override;

  void SetInsertionPoint(long pos) override;
  void SetInsertionPointEnd() override;
  long GetInsertionPoint() const override;
  long GetLastPosition() const override;

  void Command(wxCommandEvent& event) override;

protected:
  void Init();

  /// Replaces the selection (`selectionOnly`) or the whole text with `value`, and reports it.
  void DoWriteText(const wxString& value, bool selectionOnly = true);
  /// wxEVT_COMMAND_TEXT_UPDATED with the text, to the control's handlers.
  bool SendUpdateEvent();

  wxSize DoGetBestSize() const override;

private:
  std::wstring m_text;
  long m_insertionPoint;
  bool m_modified;
};
