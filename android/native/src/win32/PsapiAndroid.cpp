// GetProcessMemoryInfo (include/win32/psapi.h) from /proc/self/status, for the render info's
// process line (moho/render/RenderInfo.cpp). The status file's values are in kB.

#include <psapi.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

namespace
{
  /// `line`'s value in bytes when it is the `key` line ("VmRSS:   12345 kB"), else -1.
  long long StatusBytes(const std::string& line, const char* const key)
  {
    const std::size_t keyLength = std::strlen(key);
    if (line.compare(0, keyLength, key) != 0 || line.size() <= keyLength || line[keyLength] != ':') {
      return -1;
    }
    return std::strtoll(line.c_str() + keyLength + 1, nullptr, 10) * 1024;
  }
} // namespace

BOOL GetProcessMemoryInfo(const HANDLE process, PROCESS_MEMORY_COUNTERS* const counters, const DWORD size)
{
  if (process != ::GetCurrentProcess() || counters == nullptr || size < sizeof(PROCESS_MEMORY_COUNTERS)) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  std::ifstream status("/proc/self/status");
  if (!status) {
    ::SetLastError(ERROR_FILE_NOT_FOUND);
    return FALSE;
  }
  long long resident = 0;
  long long residentPeak = 0;
  long long anonymous = 0;
  long long swapped = 0;
  std::string line;
  while (std::getline(status, line)) {
    if (const long long value = StatusBytes(line, "VmRSS"); value >= 0) {
      resident = value;
    } else if (const long long peak = StatusBytes(line, "VmHWM"); peak >= 0) {
      residentPeak = peak;
    } else if (const long long anon = StatusBytes(line, "RssAnon"); anon >= 0) {
      anonymous = anon;
    } else if (const long long swap = StatusBytes(line, "VmSwap"); swap >= 0) {
      swapped = swap;
    }
  }
  const DWORD cb = counters->cb;
  *counters = PROCESS_MEMORY_COUNTERS{};
  counters->cb = cb;
  counters->WorkingSetSize = static_cast<SIZE_T>(resident);
  counters->PeakWorkingSetSize = static_cast<SIZE_T>(residentPeak);
  counters->PagefileUsage = static_cast<SIZE_T>(anonymous + swapped);
  counters->PeakPagefileUsage = counters->PagefileUsage;
  if (size >= sizeof(PROCESS_MEMORY_COUNTERS_EX)) {
    static_cast<PROCESS_MEMORY_COUNTERS_EX*>(counters)->PrivateUsage = counters->PagefileUsage;
  }
  return TRUE;
}
