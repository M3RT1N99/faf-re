#pragma once

// wx-lite (android/native/WX_LITE.md): wxBrush of wxWindows 2.4.2 (include/wx/msw/brush.h), for
// what the engine uses. A brush is its colour and style: wx-lite's device contexts draw nothing
// (wx/dc.h).

#include "wx/colour.h"
#include "wx/object.h"

// The fill styles (wx/defs.h).
enum
{
  wxSOLID = 100,
  wxDOT,
  wxLONG_DASH,
  wxSHORT_DASH,
  wxDOT_DASH,
  wxUSER_DASH,
  wxTRANSPARENT,
  wxSTIPPLE_MASK_OPAQUE,
  wxSTIPPLE_MASK,
  wxSTIPPLE = 110,
  wxBDIAGONAL_HATCH,
  wxCROSSDIAG_HATCH,
  wxFDIAGONAL_HATCH,
  wxCROSS_HATCH,
  wxHORIZONTAL_HATCH,
  wxVERTICAL_HATCH
};

class wxBrush : public wxObject
{
public:
  /// The null brush: not Ok.
  wxBrush()
    : m_ok(false)
    , m_style(wxSOLID)
  {}
  wxBrush(const wxColour& colour, int style)
    : m_ok(true)
    , m_colour(colour)
    , m_style(style)
  {}
  wxBrush(const wxBrush&) = default;
  wxBrush& operator=(const wxBrush&) = default;

  void SetColour(const wxColour& colour)
  {
    m_colour = colour;
    m_ok = true;
  }
  void SetColour(unsigned char red, unsigned char green, unsigned char blue)
  {
    SetColour(wxColour(red, green, blue));
  }
  void SetStyle(int style)
  {
    m_style = style;
    m_ok = true;
  }

  bool operator==(const wxBrush& brush) const
  {
    return m_ok == brush.m_ok && (!m_ok || (m_colour == brush.m_colour && m_style == brush.m_style));
  }
  bool operator!=(const wxBrush& brush) const
  {
    return !(*this == brush);
  }

  wxColour GetColour() const
  {
    return m_colour;
  }
  int GetStyle() const
  {
    return m_style;
  }
  bool Ok() const
  {
    return m_ok;
  }

private:
  bool m_ok;
  wxColour m_colour;
  int m_style;
};
