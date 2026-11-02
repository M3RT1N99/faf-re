#pragma once

// <objbase.h> for the Android client build: COM's base (in include/win32/Windows.h here) and its
// initialisation, which the application calls once (moho/app/WinApp.cpp). There is no COM library
// to start on Android, so it succeeds at once; CoCreateInstance finds no class all the same.

#include <Windows.h>

inline HRESULT CoInitialize(void*) noexcept
{
  return S_OK;
}

inline void CoUninitialize() noexcept {}
