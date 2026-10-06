// <stdlib.h>, followed by the MSVC CRT names that MSVC declares in it
// (_countof, _MAX_PATH, errno_t, _dupenv_s, ...).
// faf_msvc_crt.h says which C headers are interposed like this, and why the
// others are not.
//
// No include guard: every #include reaches the C library's header, which
// guards itself, and faf_msvc_crt.h guards itself too.
#include_next <stdlib.h>
#if defined(__cplusplus)
#include "faf_msvc_crt.h"
#endif
