#pragma once

// wx-lite (android/native/WX_LITE.md): wxIcon of wxWindows 2.4.2 (include/wx/msw/icon.h), for what
// the engine uses. Windows takes icons from the executable's resources (the engine's frame sets
// IDI_WIN_FAICON); Android has no resources and shows no window icon, so an icon loads nothing
// and is not Ok.

#include "wx/gdicmn.h"
#include "wx/object.h"
#include "wx/string.h"

class wxIcon : public wxObject
{
public:
  wxIcon()
    : m_ok(false)
  {}
  wxIcon(const wxIcon&) = default;
  wxIcon& operator=(const wxIcon&) = default;

  wxIcon(const wxString& name, long type = wxBITMAP_TYPE_ICO_RESOURCE, int desiredWidth = -1, int desiredHeight = -1);

  ~wxIcon() override;

  bool LoadFile(const wxString& name, long type = wxBITMAP_TYPE_ICO_RESOURCE, int desiredWidth = -1, int desiredHeight = -1);

  bool Ok() const
  {
    return m_ok;
  }

  bool operator==(const wxIcon& icon) const
  {
    return m_ok == icon.m_ok && m_name == icon.m_name;
  }
  bool operator!=(const wxIcon& icon) const
  {
    return !(*this == icon);
  }

private:
  bool m_ok;
  wxString m_name;
};

extern wxIcon wxNullIcon;
