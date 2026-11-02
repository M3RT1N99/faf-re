#pragma once

// wx-lite (android/native/WX_LITE.md): wxObject, the root of wxWindows' classes. The engine
// needs it as the class the event tables' member function pointers are cast to; the run-time
// class information of wxWindows 2.4.2 (wxClassInfo) has no user here.

#include "wx/defs.h"

class wxObject
{
public:
  wxObject() = default;
  wxObject(const wxObject&) = default;
  wxObject& operator=(const wxObject&) = default;
  virtual ~wxObject();
};
