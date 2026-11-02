#pragma once

// <TlHelp32.h> for the Android client build: the thread snapshot the crash report walks
// (moho/app/WinApp.cpp), with Win32's entry. Android has no Toolhelp: there is no snapshot, and
// the report finds no other thread to suspend (which it could not suspend either).

#include <Windows.h>

#define TH32CS_SNAPTHREAD 0x00000004u

struct THREADENTRY32
{
  DWORD dwSize;
  DWORD cntUsage;
  DWORD th32ThreadID;
  DWORD th32OwnerProcessID;
  LONG tpBasePri;
  LONG tpDeltaPri;
  DWORD dwFlags;
};

inline HANDLE CreateToolhelp32Snapshot(DWORD, DWORD) noexcept
{
  ::SetLastError(ERROR_NOT_SUPPORTED);
  return INVALID_HANDLE_VALUE;
}

inline BOOL Thread32First(HANDLE, THREADENTRY32*) noexcept
{
  ::SetLastError(ERROR_NO_MORE_FILES);
  return FALSE;
}

inline BOOL Thread32Next(HANDLE, THREADENTRY32*) noexcept
{
  ::SetLastError(ERROR_NO_MORE_FILES);
  return FALSE;
}
