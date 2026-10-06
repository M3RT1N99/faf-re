#pragma once

// Force-included by main.vcxproj's x64 configurations and by the arm64 engine
// sweep (scripts/port/engine_sweep.py, port/engine/README.md). The Win32
// configurations do not include it.
//
// The recovered tree pins the Win32 layout of the 2007 binary with thousands of
// `static_assert(sizeof(T) == 0xNN)` / `static_assert(offsetof(T, m) == 0xNN)`.
// Those numbers are facts about the x86 image: on x64 and arm64 every pointer,
// vtable pointer and size_t is 8 bytes, so every assert on a pointer-bearing
// type fails, and there is no 64-bit original to hold the layout to. The Win32
// build keeps all of them. Here they are switched off wholesale on every target
// that is not 32-bit x86, rather than gated one by one across ~1,100 files: a
// static_assert has no effect on code generation, so this changes diagnostics
// only. The condition used to be `_M_X64`, which left the asserts on for
// aarch64; on the x64 MSVC build the old and the new condition are both true.
//
// Code that must be checked off x86 as well (wire formats, file headers) cannot
// rely on static_assert in this configuration; the x86 build still checks it.
// A layout without pointers is the same on x64 (LLP64), but not on arm64
// Android (LP64: `long` is 8 bytes, `wchar_t` is 4), so formats that use those
// types need an always-on check of their own (docs/port/android-roadmap.md, W1).
#if defined(__cplusplus) && !defined(_M_IX86) && !defined(__i386__)
#define _ALLOW_KEYWORD_MACROS
#define static_assert(...) static_assert(true, "x86 layout assert, not checked on this target")
#endif
