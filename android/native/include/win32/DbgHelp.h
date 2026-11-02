#pragma once

// <DbgHelp.h> for the Android client build: the symbol lookup the frame sampler names its sample
// addresses with (gpg/core/diagnostics/FrameSampler.cpp), and the options the application sets
// (moho/app/WinApp.cpp). src/win32/DbgHelpAndroid.cpp answers from the dynamic symbol tables
// (dladdr) with demangled names: the symbols a library exports or keeps for the dynamic linker.

#include <Windows.h>

#define SYMOPT_UNDNAME 0x00000002u
#define SYMOPT_DEFERRED_LOADS 0x00000004u
#define SYMOPT_LOAD_LINES 0x00000010u
#define SYMOPT_FAIL_CRITICAL_ERRORS 0x00000200u

struct SYMBOL_INFO
{
  ULONG SizeOfStruct;
  ULONG TypeIndex;
  ULONG64 Reserved[2];
  ULONG Index;
  ULONG Size;
  ULONG64 ModBase;
  ULONG Flags;
  ULONG64 Value;
  ULONG64 Address;
  ULONG Register;
  ULONG Scope;
  ULONG Tag;
  ULONG NameLen;
  ULONG MaxNameLen;
  CHAR Name[1];
};

/// The options in force after the call (SymSetOptions keeps them; they change nothing here).
DWORD SymSetOptions(DWORD options);
BOOL SymInitialize(HANDLE process, const char* searchPath, BOOL invadeProcess);
BOOL SymCleanup(HANDLE process);
/// The symbol holding `address`: its start, name (demangled, cut to MaxNameLen) and the offset.
BOOL SymFromAddr(HANDLE process, DWORD64 address, DWORD64* displacement, SYMBOL_INFO* symbol);
