#pragma once

// Android/non-Windows stand-in for <sys/timeb.h>: _ftime64 and struct _timeb (bionic has no sys/timeb.h).
// Everything lives in faf_msvc_crt.h; see there.
#include "../faf_msvc_crt.h"
