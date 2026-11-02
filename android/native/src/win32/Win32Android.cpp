// The Android client build's Win32 additions (android/native/include/win32/Windows.h), on top of
// the engine's POSIX Win32 layer (src/platform/posix/Win32Shim.cpp). They use that layer's public
// functions, so paths, case resolution and errors behave as there.

#include <Windows.h>

#include <android/native_window.h>
#include <dlfcn.h>

#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

#include "platform/posix/Backtrace.h"

namespace
{
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

  std::atomic<HWND> gForegroundWindow{nullptr};
} // namespace

const char* GetCommandLineA()
{
  static const std::string sCommandLine = Narrow(::GetCommandLineW());
  return sCommandLine.c_str();
}

DWORD MsgWaitForMultipleObjectsEx(const DWORD count, const HANDLE* const handles, const DWORD milliseconds, DWORD, const DWORD flags)
{
  return ::WaitForMultipleObjects(count, handles, (flags & MWMO_WAITALL) != 0 ? TRUE : FALSE, milliseconds);
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

BOOL GetClientRect(const HWND window, RECT* const rect)
{
  if (window == nullptr || rect == nullptr) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  auto* const native = static_cast<ANativeWindow*>(window);
  rect->left = 0;
  rect->top = 0;
  rect->right = ANativeWindow_getWidth(native);
  rect->bottom = ANativeWindow_getHeight(native);
  return TRUE;
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

HWND GetForegroundWindow()
{
  return gForegroundWindow.load();
}

BOOL SetForegroundWindow(const HWND window)
{
  gForegroundWindow.store(window);
  return TRUE;
}
