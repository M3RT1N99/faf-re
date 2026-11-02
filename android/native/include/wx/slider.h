#pragma once

// wx-lite (android/native/WX_LITE.md): wxWindows 2.4.2's <wx/slider.h> (wxSlider), declared
// only. moho/app/WEmitterWx.h names this class for the emitter editor's members, and the console
// commands include that header; the emitter editor itself is Windows-only (CMakeLists.txt,
// _faf_client_windows_only).

#include "wx/defs.h"

class wxSlider;
