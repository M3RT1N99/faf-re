#pragma once

// <commctrl.h> for the Android client build: what the application names of the common controls
// (moho/app/WinApp.cpp registers them for its wx windows). Android has none to register.

#include <Windows.h>

inline void InitCommonControls() noexcept {}
