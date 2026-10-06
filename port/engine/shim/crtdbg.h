#pragma once

// Android/non-Windows stand-in for MSVC's <crtdbg.h> as a Release build sees it
// (main.vcxproj Release|x64 defines NDEBUG, not _DEBUG): the debug-heap and
// report macros reduce to their no-op or plain-CRT forms, with the values and
// types MSVC's header gives them in that configuration. There is no debug heap
// off Windows.
//
// Users: gpg/core/utils/Global.cpp (the FAF_SYSHEAP=2 probe heap:
// _malloc_dbg, _free_dbg, _msize_dbg) and moho/app/WinMain.cpp
// (_CrtSetReportHook, _CrtSetDbgFlag).
#include "faf_msvc_crt.h"

#define _CRT_WARN 0
#define _CRT_ERROR 1
#define _CRT_ASSERT 2
#define _CRT_ERRCNT 3

#define _CRTDBG_MODE_FILE 0x1
#define _CRTDBG_MODE_DEBUG 0x2
#define _CRTDBG_MODE_WNDW 0x4
#define _CRTDBG_REPORT_MODE -1

#define _CRTDBG_ALLOC_MEM_DF 0x01
#define _CRTDBG_DELAY_FREE_MEM_DF 0x02
#define _CRTDBG_CHECK_ALWAYS_DF 0x04
#define _CRTDBG_RESERVED_DF 0x08
#define _CRTDBG_CHECK_CRT_DF 0x10
#define _CRTDBG_LEAK_CHECK_DF 0x20
#define _CRTDBG_CHECK_EVERY_16_DF 0x00100000
#define _CRTDBG_CHECK_EVERY_128_DF 0x00800000
#define _CRTDBG_CHECK_EVERY_1024_DF 0x04000000
#define _CRTDBG_CHECK_DEFAULT_DF 0
#define _CRTDBG_REPORT_FLAG -1

#define _FREE_BLOCK 0
#define _NORMAL_BLOCK 1
#define _CRT_BLOCK 2
#define _IGNORE_BLOCK 3
#define _CLIENT_BLOCK 4
#define _MAX_BLOCKS 5

extern "C++" {
typedef void* _HFILE;
typedef int(* _CRT_REPORT_HOOK)(int, char*, int*);
typedef int(* _CRT_REPORT_HOOKW)(int, wchar_t*, int*);
typedef void(* _CRT_DUMP_CLIENT)(void*, size_t);
typedef int(* _CRT_ALLOC_HOOK)(int, void*, size_t, int, long, const unsigned char*, int);
}

#define _ASSERT(expr) ((void)0)
#define _ASSERTE(expr) ((void)0)
#define _ASSERT_EXPR(expr, msg) ((void)0)
#define _RPT0(rptno, msg) ((void)0)
#define _RPT1(rptno, msg, arg1) ((void)0)
#define _RPT2(rptno, msg, arg1, arg2) ((void)0)
#define _RPT3(rptno, msg, arg1, arg2, arg3) ((void)0)
#define _RPT4(rptno, msg, arg1, arg2, arg3, arg4) ((void)0)
#define _RPTF0(rptno, msg) ((void)0)
#define _RPTF1(rptno, msg, arg1) ((void)0)
#define _RPTF2(rptno, msg, arg1, arg2) ((void)0)
#define _RPTF3(rptno, msg, arg1, arg2, arg3) ((void)0)
#define _RPTF4(rptno, msg, arg1, arg2, arg3, arg4) ((void)0)

#define _CrtDbgBreak() ((void)0)
#define _CrtSetReportHook(f) ((_CRT_REPORT_HOOK)0)
#define _CrtGetReportHook() ((_CRT_REPORT_HOOK)0)
#define _CrtSetReportHook2(t, f) ((int)0)
#define _CrtSetReportHookW2(t, f) ((int)0)
#define _CrtSetReportMode(t, f) ((int)0)
#define _CrtSetReportFile(t, f) ((_HFILE)0)
#define _CrtSetDbgFlag(f) ((int)0)
#define _CrtSetBreakAlloc(a) ((long)0)
#define _CrtSetAllocHook(f) ((_CRT_ALLOC_HOOK)0)
#define _CrtGetAllocHook() ((_CRT_ALLOC_HOOK)0)
#define _CrtSetDumpClient(f) ((_CRT_DUMP_CLIENT)0)
#define _CrtGetDumpClient() ((_CRT_DUMP_CLIENT)0)
#define _CrtCheckMemory() ((int)1)
#define _CrtDoForAllClientObjects(f, c) ((void)0)
#define _CrtDumpMemoryLeaks() ((int)0)
#define _CrtIsValidPointer(p, n, r) ((int)1)
#define _CrtIsValidHeapPointer(p) ((int)1)
#define _CrtIsMemoryBlock(p, t, r, f, l) ((int)1)
#define _CrtReportBlockType(p) ((int)-1)
#define _CrtMemCheckpoint(s) ((void)0)
#define _CrtMemDifference(s1, s2, s3) ((int)0)
#define _CrtMemDumpAllObjectsSince(s) ((void)0)
#define _CrtMemDumpStatistics(s) ((void)0)

#define _malloc_dbg(s, t, f, l) malloc(s)
#define _calloc_dbg(c, s, t, f, l) calloc(c, s)
#define _realloc_dbg(p, s, t, f, l) realloc(p, s)
#define _free_dbg(p, t) free(p)
#define _msize_dbg(p, t) _msize(p)
#define _strdup_dbg(s, t, f, l) _strdup(s)
