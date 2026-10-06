// <ctype.h>, followed by the MSVC CRT names that MSVC declares in it
// (_tolower_l, _toupper_l, _towlower_l, ...).
// faf_msvc_crt.h says which C headers are interposed like this, and why the
// others are not.
//
// No include guard: every #include reaches the C library's header, which
// guards itself, and faf_msvc_crt.h guards itself too.
#include_next <ctype.h>
#if defined(__cplusplus)
#include "faf_msvc_crt.h"
#endif
