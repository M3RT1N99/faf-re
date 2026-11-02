// wx-lite (android/native/WX_LITE.md): wxPanel (src/generic/panelg.cpp) and the controls of
// wxWindows 2.4.2 the engine uses (src/common/ctrlcmn.cpp, textcmn.cpp; src/msw/control.cpp,
// button.cpp, bmpbuttn.cpp, checkbox.cpp, textctrl.cpp, statbox.cpp, stattext.cpp). A control is
// a child window of wx-lite's class whose text is its label; it keeps its value itself and sends
// the events the Windows control's notifications would make wx send. Their best sizes come from
// the default GUI font's metrics (src/wx/Private.h), as wxMSW measures them.

#include <Windows.h>

#include <algorithm>

#include "Private.h"
#include "wx/bmpbuttn.h"
#include "wx/button.h"
#include "wx/checkbox.h"
#include "wx/control.h"
#include "wx/panel.h"
#include "wx/statbox.h"
#include "wx/stattext.h"
#include "wx/textctrl.h"

namespace
{
  // the margins wxMSW gives its controls (src/msw/private.h)
  constexpr int kDefaultItemWidth = 100;
  constexpr int kDefaultButtonMargin = 4;

  int EditHeightFromCharHeight(const int cy)
  {
    return cy + 8;
  }

  int ButtonHeightFromCharHeight(const int cy)
  {
    return (11 * cy) / 10;
  }
} // namespace

// --- wxPanel ------------------------------------------------------------------------------------

BEGIN_EVENT_TABLE(wxPanel, wxWindow)
  EVT_SIZE(wxPanel::OnSize)
END_EVENT_TABLE()

void wxPanel::Init() {}

bool wxPanel::Create(
  wxWindow* const parent, const wxWindowID id, const wxPoint& pos, const wxSize& size, const long style, const wxString& name
)
{
  const bool ret = wxWindow::Create(parent, id, pos, size, style, name);
  if (ret) {
    // the dialog face's colour, as wxSystemSettings gives it on Windows
    SetBackgroundColour(wxColour(0xD4, 0xD0, 0xC8));
  }
  return ret;
}

wxPanel::~wxPanel() {}

void wxPanel::InitDialog()
{
  wxInitDialogEvent event(GetId());
  event.SetEventObject(this);
  GetEventHandler()->ProcessEvent(event);
}

void wxPanel::OnSize(wxSizeEvent& event)
{
  if (GetAutoLayout()) {
    Layout();
  }
  event.Skip();
}

// --- wxControlBase, wxControl -------------------------------------------------------------------

wxControlBase::~wxControlBase() {}

bool wxControlBase::Create(
  wxWindow* const parent,
  const wxWindowID id,
  const wxPoint& pos,
  const wxSize& size,
  const long style,
  const wxValidator& validator,
  const wxString& name
)
{
  const bool ret = wxWindow::Create(parent, id, pos, size, style, name);
  if (ret) {
    SetValidator(validator);
  }
  return ret;
}

bool wxControlBase::CreateControl(
  wxWindowBase* const parent,
  const wxWindowID id,
  const wxPoint& pos,
  const wxSize& size,
  const long style,
  const wxValidator& validator,
  const wxString& name
)
{
  // even if it's possible to create controls without parents in some port, it should surely be
  // discouraged because it doesn't work at all under Windows
  wxCHECK_MSG(parent, false, wxT("all controls must have parents"));

  if (!CreateBase(parent, id, pos, size, style, validator, name)) {
    return false;
  }

  parent->AddChild(this);
  return true;
}

void wxControlBase::Command(wxCommandEvent& event)
{
  (void)GetEventHandler()->ProcessEvent(event);
}

void wxControlBase::InitCommandEvent(wxCommandEvent& event) const
{
  event.SetEventObject(const_cast<wxControlBase*>(this));
  event.SetClientData(GetClientData());
}

void wxControlBase::SetBestSize(const wxSize& size)
{
  if (size.x == -1 || size.y == -1) {
    wxSize sizeBest = DoGetBestSize();
    if (size.x != -1) {
      sizeBest.x = size.x;
    }
    if (size.y != -1) {
      sizeBest.y = size.y;
    }
    SetSize(sizeBest);
  }
}

bool wxControl::Create(
  wxWindow* const parent,
  const wxWindowID id,
  const wxPoint& pos,
  const wxSize& size,
  const long style,
  const wxValidator& validator,
  const wxString& name
)
{
  const bool rval = wxWindow::Create(parent, id, pos, size, style, name);
  if (rval) {
    SetValidator(validator);
  }
  return rval;
}

wxControl::~wxControl()
{
  m_isBeingDeleted = true;
}

bool wxControl::MSWCreateControl(const wxPoint& pos, const wxSize& size, const wxString& label)
{
  // all controls should have these styles (wxWindows' WS_TABSTOP for the controls that take the
  // focus changes nothing without keyboard navigation)
  WXDWORD exstyle;
  const WXDWORD style = MSWGetCreateWindowFlags(&exstyle) | WS_CHILD | WS_VISIBLE;
  if (!MSWCreate(wxlite::kCanvasClassName, label.c_str(), pos, size, style, exstyle)) {
    return false;
  }

  // set the size now if no initial size specified
  SetBestSize(size);
  return true;
}

bool wxControl::ProcessCommand(wxCommandEvent& event)
{
  return GetEventHandler()->ProcessEvent(event);
}

wxSize wxControl::DoGetBestSize() const
{
  return wxSize(kDefaultItemWidth, EditHeightFromCharHeight(wxlite::kCharHeight));
}

// --- wxButton -----------------------------------------------------------------------------------

bool wxButton::Create(
  wxWindow* const parent,
  const wxWindowID id,
  const wxString& label,
  const wxPoint& pos,
  const wxSize& size,
  const long style,
  const wxValidator& validator,
  const wxString& name
)
{
  if (!CreateControl(parent, id, pos, size, style, validator, name)) {
    return false;
  }
  return MSWCreateControl(pos, size, label);
}

wxButton::~wxButton() {}

wxSize wxButton::GetDefaultSize()
{
  // the size of a standard button in the dialog units is 50x14, translate this to pixels (the
  // multipliers come from the Windows convention, the extra +1/+2 from wxWindows 2.4.2)
  return wxSize((50 * (wxlite::kCharWidth + 1)) / 4, ((14 * wxlite::kCharHeight) + 2) / 8);
}

wxSize wxButton::DoGetBestSize() const
{
  int wBtn = wxlite::TextExtent(GetLabel()).x;

  // add a margin -- the button is wider than just its label
  wBtn += 3 * wxlite::kCharWidth;

  // the button height is proportional to the height of the font used
  const int hBtn = ButtonHeightFromCharHeight(wxlite::kCharHeight);

  // all buttons have at least the standard size unless the user explicitly wants them to be of
  // smaller size and used wxBU_EXACTFIT style when creating the button
  if (!HasFlag(wxBU_EXACTFIT)) {
    wxSize sz = GetDefaultSize();
    if (wBtn > sz.x) {
      sz.x = wBtn;
    }
    if (hBtn > sz.y) {
      sz.y = hBtn;
    }
    return sz;
  }
  return wxSize(wBtn, hBtn);
}

void wxButton::SetDefault() {}

bool wxButton::SendClickEvent()
{
  wxCommandEvent event(wxEVT_COMMAND_BUTTON_CLICKED, GetId());
  event.SetEventObject(this);
  return ProcessCommand(event);
}

void wxButton::Command(wxCommandEvent& event)
{
  ProcessCommand(event);
}

// --- wxBitmapButton -----------------------------------------------------------------------------

bool wxBitmapButton::Create(
  wxWindow* const parent,
  const wxWindowID id,
  const wxBitmap& bitmap,
  const wxPoint& pos,
  const wxSize& size,
  const long style,
  const wxValidator& validator,
  const wxString& name
)
{
  m_bmpNormal = bitmap;
  SetName(name);
  SetValidator(validator);

  parent->AddChild(this);

  m_windowStyle = style;
  m_marginX = m_marginY = kDefaultButtonMargin;

  m_windowId = id == -1 ? NewControlId() : id;

  return MSWCreateControl(pos, size);
}

wxSize wxBitmapButton::DoGetBestSize() const
{
  if (m_bmpNormal.Ok()) {
    const int width = m_bmpNormal.GetWidth();
    const int height = m_bmpNormal.GetHeight();
    int marginH = 0;
    int marginV = 0;

    if (HasFlag(wxBU_AUTODRAW)) {
      marginH = m_marginX + 2;
      marginV = m_marginY + 2;
    }
    return wxSize(width + 2 * marginH, height + 2 * marginV);
  }
  return wxBitmapButtonBase::DoGetBestSize();
}

// --- wxCheckBox ---------------------------------------------------------------------------------

bool wxCheckBox::Create(
  wxWindow* const parent,
  const wxWindowID id,
  const wxString& label,
  const wxPoint& pos,
  const wxSize& size,
  const long style,
  const wxValidator& validator,
  const wxString& name
)
{
  if (!CreateControl(parent, id, pos, size, style, validator, name)) {
    return false;
  }
  return MSWCreateControl(pos, size, label);
}

wxSize wxCheckBox::DoGetBestSize() const
{
  const int checkSize = wxlite::kCharHeight;
  const wxString str = GetLabel();

  int wCheckbox, hCheckbox;
  if (!str.IsEmpty()) {
    const wxSize text = wxlite::TextExtent(str);
    wCheckbox = text.x + checkSize + GetCharWidth();
    hCheckbox = std::max(text.y, checkSize);
  } else {
    wCheckbox = checkSize;
    hCheckbox = checkSize;
  }
  return wxSize(wCheckbox, hCheckbox);
}

void wxCheckBox::SetValue(const bool val)
{
  m_isChecked = val;
}

bool wxCheckBox::GetValue() const
{
  return m_isChecked;
}

void wxCheckBox::Command(wxCommandEvent& event)
{
  SetValue(event.GetInt() != 0);
  ProcessCommand(event);
}

// --- wxTextCtrl ---------------------------------------------------------------------------------

bool wxTextCtrlBase::SetDefaultStyle(const wxTextAttr& style)
{
  // the default text colour is the window's, and so on: an unset part of the style stays
  // unchanged
  const wxColour colText = style.HasTextColour() ? style.GetTextColour() : m_defaultStyle.GetTextColour();
  const wxColour colBack = style.HasBackgroundColour() ? style.GetBackgroundColour() : m_defaultStyle.GetBackgroundColour();
  m_defaultStyle = wxTextAttr(colText, colBack);
  return true;
}

void wxTextCtrl::Init()
{
  m_insertionPoint = 0;
  m_modified = false;
}

bool wxTextCtrl::Create(
  wxWindow* const parent,
  const wxWindowID id,
  const wxString& value,
  const wxPoint& pos,
  const wxSize& size,
  const long style,
  const wxValidator& validator,
  const wxString& name
)
{
  // base initialization
  if (!CreateControl(parent, id, pos, size, style, validator, name)) {
    return false;
  }
  if (!MSWCreateControl(pos, size)) {
    return false;
  }
  // the initial text, as WM_SETTEXT from CreateWindow sets it: without an update event
  m_text = value.Wide();
  return true;
}

wxTextCtrl::~wxTextCtrl() {}

wxString wxTextCtrl::GetValue() const
{
  return wxString(m_text);
}

void wxTextCtrl::SetValue(const wxString& value)
{
  // if the text is long enough, it's faster to just set it instead of first comparing it with
  // the old one (chances are that it will be different anyhow, this comparison is there to
  // avoid flicker for small single-line edit controls mostly)
  if ((value.length() > 0x400) || (value.Wide() != m_text)) {
    DoWriteText(value, false /* not selection only */);
  }

  // we should reset the modified flag even if the value didn't really change

  // mark the control as being not dirty - we changed its text, not the user
  DiscardEdits();

  // for compatibility, don't move the cursor when doing SetValue()
  SetInsertionPoint(0);
}

void wxTextCtrl::WriteText(const wxString& value)
{
  DoWriteText(value);
}

void wxTextCtrl::DoWriteText(const wxString& value, const bool selectionOnly)
{
  // Windows' edit controls hold "\r\n" for a line break in a multiline control; the text here is
  // the text as written, which is how GetValue gives it back on Windows too.
  if (selectionOnly) {
    // no selection: the text goes in at the insertion point
    const std::size_t at = std::min<std::size_t>(static_cast<std::size_t>(std::max<long>(m_insertionPoint, 0)), m_text.size());
    m_text.insert(at, value.Wide());
    m_insertionPoint = static_cast<long>(at + value.length());
  } else {
    m_text = value.Wide();
    m_insertionPoint = 0;
  }
  // each change of the text is one EN_CHANGE, which wxMSW turns into wxEVT_COMMAND_TEXT_UPDATED
  SendUpdateEvent();
}

void wxTextCtrl::AppendText(const wxString& text)
{
  SetInsertionPointEnd();
  WriteText(text);
}

void wxTextCtrl::Clear()
{
  m_text.clear();
  m_insertionPoint = 0;
  SendUpdateEvent();
}

int wxTextCtrl::GetNumberOfLines() const
{
  return static_cast<int>(std::count(m_text.begin(), m_text.end(), L'\n')) + 1;
}

bool wxTextCtrl::IsModified() const
{
  return m_modified;
}

bool wxTextCtrl::IsEditable() const
{
  return !HasFlag(wxTE_READONLY);
}

void wxTextCtrl::MarkDirty()
{
  m_modified = true;
}

void wxTextCtrl::DiscardEdits()
{
  m_modified = false;
}

void wxTextCtrl::SetEditable(const bool editable)
{
  SetWindowStyleFlag(editable ? (GetWindowStyleFlag() & ~wxTE_READONLY) : (GetWindowStyleFlag() | wxTE_READONLY));
}

void wxTextCtrl::SetInsertionPoint(const long pos)
{
  m_insertionPoint = std::clamp<long>(pos, 0, static_cast<long>(m_text.size()));
}

void wxTextCtrl::SetInsertionPointEnd()
{
  m_insertionPoint = static_cast<long>(m_text.size());
}

long wxTextCtrl::GetInsertionPoint() const
{
  return m_insertionPoint;
}

long wxTextCtrl::GetLastPosition() const
{
  return static_cast<long>(m_text.size());
}

bool wxTextCtrl::SendUpdateEvent()
{
  wxCommandEvent event(wxEVT_COMMAND_TEXT_UPDATED, GetId());
  InitCommandEvent(event);
  event.SetString(GetValue());
  return ProcessCommand(event);
}

void wxTextCtrl::Command(wxCommandEvent& event)
{
  SetValue(event.GetString());
  ProcessCommand(event);
}

wxSize wxTextCtrl::DoGetBestSize() const
{
  const int wText = kDefaultItemWidth;
  int hText = EditHeightFromCharHeight(wxlite::kCharHeight);
  if (m_windowStyle & wxTE_MULTILINE) {
    hText *= std::max(GetNumberOfLines(), 5);
  }
  //else: for single line control everything is ok
  return wxSize(wText, hText);
}

// --- wxStaticBox, wxStaticText ------------------------------------------------------------------

bool wxStaticBox::Create(
  wxWindow* const parent,
  const wxWindowID id,
  const wxString& label,
  const wxPoint& pos,
  const wxSize& size,
  const long style,
  const wxString& name
)
{
  if (!CreateControl(parent, id, pos, size, style, wxDefaultValidator, name)) {
    return false;
  }
  return MSWCreateControl(pos, size, label);
}

wxSize wxStaticBox::DoGetBestSize() const
{
  const int wBox = wxlite::TextExtent(GetLabel()).x + 3 * wxlite::kCharWidth;
  const int hBox = EditHeightFromCharHeight(wxlite::kCharHeight);
  return wxSize(wBox, hBox);
}

bool wxStaticText::Create(
  wxWindow* const parent,
  const wxWindowID id,
  const wxString& label,
  const wxPoint& pos,
  const wxSize& size,
  const long style,
  const wxString& name
)
{
  if (!CreateControl(parent, id, pos, size, style, wxDefaultValidator, name)) {
    return false;
  }
  return MSWCreateControl(pos, size, label);
}

wxSize wxStaticText::DoGetBestSize() const
{
  // the widest line, and a line's height for each, the empty ones too
  return wxlite::TextExtent(GetLabel());
}

void wxStaticText::SetLabel(const wxString& label)
{
  wxStaticTextBase::SetLabel(label);

  // adjust the size of the window to fit to the label unless autoresizing is disabled
  if (!(GetWindowStyle() & wxST_NO_AUTORESIZE)) {
    DoSetSize(-1, -1, -1, -1, wxSIZE_AUTO_WIDTH | wxSIZE_AUTO_HEIGHT);
  }
}
