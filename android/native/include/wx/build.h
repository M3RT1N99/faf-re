#pragma once

// wx-lite (android/native/WX_LITE.md): wxBuildOptions of wxWindows 2.4.2 (include/wx/build.h): the
// version and debug flag a program was built with, which IMPLEMENT_APP checks against the
// library's (wxAppBase::CheckBuildOptions).

#include "wx/defs.h"

class wxBuildOptions
{
public:
  wxBuildOptions()
    : m_verMajor(wxMAJOR_VERSION)
    , m_verMinor(wxMINOR_VERSION)
    , m_isDebug(false)
  {}

private:
  // don't use bool and int, we want to keep binary compatibility
  unsigned char m_verMajor, m_verMinor;
  bool m_isDebug;

  friend class wxAppBase;
};
