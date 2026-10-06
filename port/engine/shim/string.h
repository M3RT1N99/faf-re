// <string.h>, followed by the MSVC CRT names that MSVC declares in it
// (_stricmp, _strnicmp, _strdup, strcpy_s, memcpy_s, ...).
// faf_msvc_crt.h says which C headers are interposed like this, and why the
// others are not.
//
// No include guard: every #include reaches the C library's header, which
// guards itself, and faf_msvc_crt.h guards itself too.
#include_next <string.h>
#if defined(__cplusplus)
#include "faf_msvc_crt.h"
#endif
