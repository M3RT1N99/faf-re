// The DbgHelp symbol lookup of include/win32/DbgHelp.h, from the dynamic symbol tables: dladdr
// finds the symbol a library exports or keeps for the dynamic linker, and the C++ ABI's demangler
// gives its name as SYMOPT_UNDNAME would. Symbols that only a debug file has are not found, as
// DbgHelp does not find them without the PDB.

#include <DbgHelp.h>

#include <cxxabi.h>
#include <dlfcn.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>

namespace
{
  std::atomic<DWORD> gOptions{0};
} // namespace

DWORD SymSetOptions(const DWORD options)
{
  gOptions.store(options);
  return options;
}

BOOL SymInitialize(HANDLE, const char*, BOOL)
{
  return TRUE;
}

BOOL SymCleanup(HANDLE)
{
  return TRUE;
}

BOOL SymFromAddr(HANDLE, const DWORD64 address, DWORD64* const displacement, SYMBOL_INFO* const symbol)
{
  if (symbol == nullptr || symbol->MaxNameLen == 0) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  Dl_info info{};
  if (::dladdr(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(address)), &info) == 0 || info.dli_sname == nullptr ||
      info.dli_saddr == nullptr) {
    ::SetLastError(ERROR_NOT_FOUND);
    return FALSE;
  }
  int status = 0;
  char* const demangled = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status);
  const char* const name = (status == 0 && demangled != nullptr) ? demangled : info.dli_sname;
  const std::size_t length = std::min<std::size_t>(std::strlen(name), symbol->MaxNameLen - 1);
  std::memcpy(symbol->Name, name, length);
  symbol->Name[length] = '\0';
  std::free(demangled);
  symbol->NameLen = static_cast<ULONG>(length);
  symbol->Address = reinterpret_cast<std::uintptr_t>(info.dli_saddr);
  symbol->ModBase = reinterpret_cast<std::uintptr_t>(info.dli_fbase);
  symbol->Size = 0;
  if (displacement != nullptr) {
    *displacement = address - symbol->Address;
  }
  return TRUE;
}
