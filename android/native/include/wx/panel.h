#pragma once

// wx-lite (android/native/WX_LITE.md): wxPanel of wxWindows 2.4.2 (include/wx/generic/panelg.h),
// for what the engine uses.

#include "wx/window.h"

class wxPanel : public wxWindow
{
public:
  wxPanel()
  {
    Init();
  }
  wxPanel(
    wxWindow* parent,
    wxWindowID id = -1,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = wxTAB_TRAVERSAL | wxNO_BORDER,
    const wxString& name = wxPanelNameStr
  )
  {
    Init();
    Create(parent, id, pos, size, style, name);
  }

  bool Create(
    wxWindow* parent,
    wxWindowID id,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = wxTAB_TRAVERSAL | wxNO_BORDER,
    const wxString& name = wxPanelNameStr
  );

  ~wxPanel() override;

  void InitDialog() override;

  /// Lays out by the sizer with SetAutoLayout.
  void OnSize(wxSizeEvent& event);

protected:
  void Init();

  DECLARE_EVENT_TABLE()
};
