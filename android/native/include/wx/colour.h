#pragma once

// wx-lite (android/native/WX_LITE.md): wxColour of wxWindows 2.4.2 (include/wx/msw/colour.h), for
// what the engine uses.

#include "wx/object.h"

class wxColour : public wxObject
{
public:
  wxColour()
    : m_isInit(false)
    , m_red(0)
    , m_green(0)
    , m_blue(0)
  {}
  wxColour(unsigned char red, unsigned char green, unsigned char blue)
    : m_isInit(true)
    , m_red(red)
    , m_green(green)
    , m_blue(blue)
  {}
  wxColour(const wxColour&) = default;
  wxColour& operator=(const wxColour&) = default;

  void Set(unsigned char red, unsigned char green, unsigned char blue)
  {
    m_red = red;
    m_green = green;
    m_blue = blue;
    m_isInit = true;
  }

  bool Ok() const
  {
    return m_isInit;
  }
  unsigned char Red() const
  {
    return m_red;
  }
  unsigned char Green() const
  {
    return m_green;
  }
  unsigned char Blue() const
  {
    return m_blue;
  }

  bool operator==(const wxColour& colour) const
  {
    return m_isInit == colour.m_isInit && m_red == colour.m_red && m_green == colour.m_green
        && m_blue == colour.m_blue;
  }
  bool operator!=(const wxColour& colour) const
  {
    return !(*this == colour);
  }

private:
  bool m_isInit;
  unsigned char m_red;
  unsigned char m_green;
  unsigned char m_blue;
};

extern wxColour wxNullColour;
