#pragma once

// <psapi.h> for the Android client build: the process memory counters the render info reports
// (moho/render/RenderInfo.cpp), with Win32's fields. src/win32/PsapiAndroid.cpp fills them from
// /proc/self/status.

#include <Windows.h>

struct PROCESS_MEMORY_COUNTERS
{
  DWORD cb;
  DWORD PageFaultCount;
  SIZE_T PeakWorkingSetSize;
  SIZE_T WorkingSetSize;
  SIZE_T QuotaPeakPagedPoolUsage;
  SIZE_T QuotaPagedPoolUsage;
  SIZE_T QuotaPeakNonPagedPoolUsage;
  SIZE_T QuotaNonPagedPoolUsage;
  SIZE_T PagefileUsage;
  SIZE_T PeakPagefileUsage;
};

struct PROCESS_MEMORY_COUNTERS_EX : PROCESS_MEMORY_COUNTERS
{
  SIZE_T PrivateUsage;
};

/// The calling process's counters (`process` is GetCurrentProcess()); `size` says whether the
/// caller passed the _EX form. Working set is the resident set (VmRSS, peak VmHWM), private usage
/// the resident anonymous memory and swap (RssAnon + VmSwap), the page file usage the same.
BOOL GetProcessMemoryInfo(HANDLE process, PROCESS_MEMORY_COUNTERS* counters, DWORD size);
