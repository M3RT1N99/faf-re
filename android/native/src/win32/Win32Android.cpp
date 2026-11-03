// The Android client build's Win32 additions (android/native/include/win32/Windows.h), on top of
// the engine's POSIX Win32 layer (src/platform/posix/Win32Shim.cpp). They use that layer's public
// functions, so paths, case resolution and errors behave as there.

#include <Windows.h>

#include <android/log.h>
#include <dlfcn.h>

#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

#include <Win32AndroidHost.h>

#include "platform/posix/Backtrace.h"

namespace
{
  // ShellExecuteW's answer when the host opened the file: any value above 32 means success.
  constexpr ULONG_PTR kShellExecuteOpened = 33;

  std::atomic<Win32AndroidShellOpen> gShellOpen{nullptr};

  std::wstring Widen(const char* const text)
  {
    const int length = ::MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (length <= 0) {
      return std::wstring();
    }
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    (void)::MultiByteToWideChar(CP_UTF8, 0, text, -1, wide.data(), length);
    wide.resize(static_cast<std::size_t>(length - 1));
    return wide;
  }

  std::string Narrow(const wchar_t* const text)
  {
    const int length = ::WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 0) {
      return std::string();
    }
    std::string narrow(static_cast<std::size_t>(length), '\0');
    (void)::WideCharToMultiByte(CP_UTF8, 0, text, -1, narrow.data(), length, nullptr, nullptr);
    narrow.resize(static_cast<std::size_t>(length - 1));
    return narrow;
  }

  /// Copies `text` into the caller's buffer as GetModuleFileName does: truncated with
  /// ERROR_INSUFFICIENT_BUFFER when it does not fit.
  DWORD CopyOut(const std::string& text, char* const buffer, const DWORD size)
  {
    if (text.size() >= size) {
      std::memcpy(buffer, text.c_str(), size - 1);
      buffer[size - 1] = '\0';
      ::SetLastError(ERROR_INSUFFICIENT_BUFFER);
      return size;
    }
    std::memcpy(buffer, text.c_str(), text.size() + 1);
    ::SetLastError(ERROR_SUCCESS);
    return static_cast<DWORD>(text.size());
  }
} // namespace

const char* GetCommandLineA()
{
  static const std::string sCommandLine = Narrow(::GetCommandLineW());
  return sCommandLine.c_str();
}

DWORD GetModuleFileNameA(const HMODULE module, char* const fileName, const DWORD size)
{
  if (fileName == nullptr || size == 0) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  if (module == nullptr) {
    wchar_t wide[4096]{};
    if (::GetModuleFileNameW(nullptr, wide, 4096) == 0) {
      return 0;
    }
    return CopyOut(Narrow(wide), fileName, size);
  }
  // A module is a shared object's base address (GetModuleHandleExA).
  Dl_info info{};
  if (::dladdr(module, &info) == 0 || info.dli_fname == nullptr) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  return CopyOut(info.dli_fname, fileName, size);
}

BOOL GetModuleHandleExA(const DWORD flags, const char* const moduleName, HMODULE* const module)
{
  if (module == nullptr || (flags & GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS) == 0) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  Dl_info info{};
  if (::dladdr(moduleName, &info) == 0 || info.dli_fbase == nullptr) {
    *module = nullptr;
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  *module = info.dli_fbase;
  return TRUE;
}

BOOL GetModuleHandleExW(const DWORD flags, const wchar_t* const moduleName, HMODULE* const module)
{
  // With GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS the "name" is an address: the same lookup.
  return ::GetModuleHandleExA(flags, reinterpret_cast<const char*>(moduleName), module);
}

HMODULE LoadLibraryW(const wchar_t* const fileName)
{
  if (fileName == nullptr) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return nullptr;
  }
  return ::LoadLibraryA(Narrow(fileName).c_str());
}

USHORT RtlCaptureStackBackTrace(const DWORD framesToSkip, const DWORD framesToCapture, void** const backTrace, DWORD* const backTraceHash)
{
  void* frames[128];
  // Frame 0 of Backtrace is this function: the caller's own frames start at 1.
  const int got = platform::posix::Backtrace(frames, 128);
  const int first = 1 + static_cast<int>(framesToSkip);
  int copied = 0;
  DWORD hash = 0;
  for (int at = first; at < got && copied < static_cast<int>(framesToCapture); ++at, ++copied) {
    backTrace[copied] = frames[at];
    hash += static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(frames[at]));
  }
  if (backTraceHash != nullptr) {
    *backTraceHash = hash;
  }
  return static_cast<USHORT>(copied);
}

BOOL IsDebuggerPresent()
{
  std::ifstream status("/proc/self/status");
  std::string line;
  while (std::getline(status, line)) {
    if (line.rfind("TracerPid:", 0) == 0) {
      return std::strtol(line.c_str() + 10, nullptr, 10) != 0 ? TRUE : FALSE;
    }
  }
  return FALSE;
}

HRESULT SHGetFolderPathA(const HWND owner, const int folder, const HANDLE token, const DWORD flags, char* const path)
{
  if (path == nullptr) {
    return E_INVALIDARG;
  }
  wchar_t wide[MAX_PATH]{};
  const HRESULT result = ::SHGetFolderPathW(owner, folder, token, flags, wide);
  if (result != S_OK) {
    return result;
  }
  const std::string narrow = Narrow(wide);
  if (narrow.size() >= MAX_PATH) {
    return E_FAIL;
  }
  std::memcpy(path, narrow.c_str(), narrow.size() + 1);
  return S_OK;
}

int SHCreateDirectoryExA(HWND, const char* const path, const void*)
{
  if (path == nullptr || path[0] == '\0') {
    return static_cast<int>(ERROR_INVALID_PARAMETER);
  }
  const std::wstring wide = Widen(path);
  const DWORD existing = ::GetFileAttributesW(wide.c_str());
  if (existing != INVALID_FILE_ATTRIBUTES) {
    return static_cast<int>((existing & FILE_ATTRIBUTE_DIRECTORY) != 0 ? ERROR_ALREADY_EXISTS : ERROR_FILE_EXISTS);
  }
  // Each missing level in turn, as `mkdir -p`; the engine's layer resolves separators and case.
  for (std::size_t at = 1; at <= wide.size(); ++at) {
    if (at == wide.size() || wide[at] == L'\\' || wide[at] == L'/') {
      const std::wstring level = wide.substr(0, at);
      if (::CreateDirectoryW(level.c_str(), nullptr) == FALSE && ::GetLastError() != ERROR_ALREADY_EXISTS) {
        return static_cast<int>(::GetLastError());
      }
    }
  }
  return static_cast<int>(ERROR_SUCCESS);
}

BOOL MoveFileExA(const char* const from, const char* const to, const DWORD flags)
{
  if (from == nullptr || to == nullptr) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  return ::MoveFileExW(Widen(from).c_str(), Widen(to).c_str(), flags);
}

int getenv_s(std::size_t* const requiredSize, char* const buffer, const std::size_t bufferSize, const char* const name) noexcept
{
  if (requiredSize == nullptr || name == nullptr || (buffer == nullptr && bufferSize != 0)) {
    return EINVAL;
  }
  const char* const value = std::getenv(name);
  if (value == nullptr) {
    *requiredSize = 0;
    if (buffer != nullptr) {
      buffer[0] = '\0';
    }
    return 0;
  }
  const std::size_t needed = std::strlen(value) + 1;
  *requiredSize = needed;
  if (buffer == nullptr) {
    return 0; // the size query
  }
  if (needed > bufferSize) {
    buffer[0] = '\0';
    return ERANGE;
  }
  std::memcpy(buffer, value, needed);
  return 0;
}

DWORD SuspendThread(HANDLE)
{
  ::SetLastError(ERROR_NOT_SUPPORTED);
  return static_cast<DWORD>(-1);
}

DWORD ResumeThread(HANDLE)
{
  ::SetLastError(ERROR_NOT_SUPPORTED);
  return static_cast<DWORD>(-1);
}

HRESULT CoCreateInstance(REFCLSID, LPUNKNOWN, DWORD, REFIID, void** const object)
{
  if (object != nullptr) {
    *object = nullptr;
  }
  return REGDB_E_CLASSNOTREG;
}

LONG RegOpenKeyExW(HKEY, const wchar_t*, DWORD, DWORD, HKEY* const result)
{
  if (result != nullptr) {
    *result = nullptr;
  }
  return static_cast<LONG>(ERROR_FILE_NOT_FOUND);
}

LONG RegQueryValueExW(HKEY, const wchar_t*, DWORD*, DWORD*, BYTE*, DWORD*)
{
  return static_cast<LONG>(ERROR_FILE_NOT_FOUND);
}

LONG RegCloseKey(HKEY)
{
  return static_cast<LONG>(ERROR_SUCCESS);
}

LONG RegOpenKeyExA(HKEY, const char*, DWORD, DWORD, HKEY* const result)
{
  if (result != nullptr) {
    *result = nullptr;
  }
  return static_cast<LONG>(ERROR_FILE_NOT_FOUND);
}

LONG RegQueryValueExA(HKEY, const char*, DWORD*, DWORD*, BYTE*, DWORD*)
{
  return static_cast<LONG>(ERROR_FILE_NOT_FOUND);
}

LONG RegCreateKeyExA(HKEY, const char*, DWORD, char*, DWORD, DWORD, void*, HKEY* const result, DWORD*)
{
  if (result != nullptr) {
    *result = nullptr;
  }
  return static_cast<LONG>(ERROR_ACCESS_DENIED);
}

LONG RegSetValueExA(HKEY, const char*, DWORD, DWORD, const BYTE*, DWORD)
{
  return static_cast<LONG>(ERROR_INVALID_HANDLE);
}

BOOL CreateProcessA(
  const char*, char*, void*, void*, BOOL, DWORD, void*, const char*, STARTUPINFOA*, PROCESS_INFORMATION* const processInformation
)
{
  if (processInformation != nullptr) {
    *processInformation = PROCESS_INFORMATION{};
  }
  ::SetLastError(ERROR_NOT_SUPPORTED);
  return FALSE;
}

BOOL CreateProcessW(
  const wchar_t*, wchar_t*, void*, void*, BOOL, DWORD, void*, const wchar_t*, STARTUPINFOW*,
  PROCESS_INFORMATION* const processInformation
)
{
  if (processInformation != nullptr) {
    *processInformation = PROCESS_INFORMATION{};
  }
  ::SetLastError(ERROR_NOT_SUPPORTED);
  return FALSE;
}

BOOL ShellExecuteExW(SHELLEXECUTEINFOW* const info)
{
  if (info != nullptr) {
    info->hProcess = nullptr;
    info->hInstApp = reinterpret_cast<HINSTANCE>(static_cast<ULONG_PTR>(SE_ERR_NOASSOC));
  }
  ::SetLastError(ERROR_NOT_SUPPORTED);
  return FALSE;
}

void Win32Android_SetShellOpen(const Win32AndroidShellOpen open)
{
  gShellOpen.store(open);
}

HINSTANCE ShellExecuteW(
  HWND, const wchar_t* const operation, const wchar_t* const file, const wchar_t*, const wchar_t*, int
)
{
  // A null verb is the default one, which is "open" for what the game opens.
  const bool opens = operation == nullptr || ::_wcsicmp(operation, L"open") == 0;
  const Win32AndroidShellOpen open = gShellOpen.load();
  if (opens && file != nullptr && open != nullptr && open(file)) {
    ::SetLastError(ERROR_SUCCESS);
    return reinterpret_cast<HINSTANCE>(static_cast<ULONG_PTR>(kShellExecuteOpened));
  }
  ::SetLastError(ERROR_NO_ASSOCIATION);
  return reinterpret_cast<HINSTANCE>(static_cast<ULONG_PTR>(SE_ERR_NOASSOC));
}

BOOL TerminateProcess(const HANDLE process, const UINT exitCode)
{
  if (process != ::GetCurrentProcess()) {
    ::SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
  }
  __android_log_print(ANDROID_LOG_INFO, "faf", "TerminateProcess: exit code %u", exitCode);
  std::_Exit(static_cast<int>(exitCode));
}

BOOL CopyFileW(const wchar_t* const existingFileName, const wchar_t* const newFileName, const BOOL failIfExists)
{
  if (existingFileName == nullptr || newFileName == nullptr) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  // Through the engine's layer, which resolves the paths' separators and case.
  const HANDLE source = ::CreateFileW(existingFileName, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  if (source == INVALID_HANDLE_VALUE) {
    return FALSE;
  }
  const HANDLE target =
    ::CreateFileW(newFileName, GENERIC_WRITE, 0, nullptr, failIfExists != FALSE ? CREATE_NEW : CREATE_ALWAYS, 0, nullptr);
  if (target == INVALID_HANDLE_VALUE) {
    const DWORD error = ::GetLastError();
    (void)::CloseHandle(source);
    ::SetLastError(error);
    return FALSE;
  }
  char buffer[64 * 1024];
  BOOL ok = TRUE;
  for (;;) {
    DWORD read = 0;
    if (::ReadFile(source, buffer, sizeof(buffer), &read, nullptr) == FALSE) {
      ok = FALSE;
      break;
    }
    if (read == 0) {
      break;
    }
    DWORD written = 0;
    if (::WriteFile(target, buffer, read, &written, nullptr) == FALSE || written != read) {
      ok = FALSE;
      break;
    }
  }
  const DWORD error = ::GetLastError();
  (void)::CloseHandle(target);
  (void)::CloseHandle(source);
  if (ok == FALSE) {
    (void)::DeleteFileW(newFileName);
    ::SetLastError(error);
  }
  return ok;
}
