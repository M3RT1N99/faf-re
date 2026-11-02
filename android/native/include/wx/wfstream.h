#pragma once

// wx-lite (android/native/WX_LITE.md): wxWindows 2.4.2's <wx/wfstream.h> (the file streams).
// moho/app/WxRuntimeTypes.h includes it, and no client file uses its classes: the wx tool windows
// that do (the emitter editor, the script debugger) are Windows-only (CMakeLists.txt,
// _faf_client_windows_only).

#include "wx/defs.h"
