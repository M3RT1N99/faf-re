// faf_griff_tests: the touch scheme Griff's rules (include/Griff*.h: the draft, the pins, the handle's
// place, the tap's reach, the undo) and the choice of what a finger meant (include/TapTarget.h,
// include/ListDrag.h), all of them headers that need nothing of the engine. Linked statically, so it
// runs on the emulator and on Linux hosts alike. Exit code 0 when every check passes, 1 otherwise.

#include <cstdio>
#include <cstring>
#include <string>

#include "Check.h"

namespace
{
  int gFailedChecks = 0; // in the test that runs

  const char* BaseName(const char* path)
  {
    const char* slash = std::strrchr(path, '/');
    const char* backslash = std::strrchr(path, '\\');
    const char* last = slash > backslash ? slash : backslash;
    return last != nullptr ? last + 1 : path;
  }
} // namespace

std::vector<wxtest::Test>& wxtest::Tests()
{
  static std::vector<Test> tests;
  return tests;
}

void wxtest::Fail(const char* const file, const int line, const std::string& what)
{
  ++gFailedChecks;
  std::printf("  %s:%d: %s\n", BaseName(file), line, what.c_str());
}

std::string wxtest::Hex(const unsigned long long value)
{
  char text[24];
  (void)std::snprintf(text, sizeof(text), "%llX", value);
  return text;
}

std::string wxtest::Describe(const std::wstring& value)
{
  std::string out = "\"";
  for (const wchar_t c : value) {
    out += (c >= 32 && c < 127) ? static_cast<char>(c) : '?';
  }
  return out + "\"";
}

std::string wxtest::Describe(const std::string& value)
{
  return "\"" + value + "\"";
}

std::string wxtest::Describe(const char* const value)
{
  return value != nullptr ? Describe(std::string(value)) : std::string("null");
}

int main()
{
  int failedTests = 0;
  for (const wxtest::Test& test : wxtest::Tests()) {
    gFailedChecks = 0;
    std::printf("%s\n", test.name);
    test.run();
    if (gFailedChecks != 0) {
      ++failedTests;
      std::printf("  FAIL\n");
    }
  }
  std::printf("%zu tests, %d failed\n", wxtest::Tests().size(), failedTests);
  std::fflush(stdout);
  return failedTests == 0 ? 0 : 1;
}
