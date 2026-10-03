#pragma once

// A minimal test runner (no third-party framework is vendored for the port).
//
//   FAF_TEST(NormalizeKeepsDrive) { FAF_CHECK_EQ(fs::NormalizePath("C:\\a"), "C:/a"); }
//
// Each test executable links TestMain.cpp, runs every registered test (or the
// ones named on the command line) and exits non-zero when a check failed.
// FAF_REQUIRE stops the current test; FAF_CHECK records and continues.
// A test calls faf::test::Skip("why") to report that it could not run; an
// executable whose tests all skipped exits with 77, CTest's SKIP_RETURN_CODE.

#include <cstdio>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace faf::test {

  using TestFunction = void (*)();

  struct TestCase
  {
    const char* name;
    TestFunction function;
  };

  std::vector<TestCase>& Registry();
  bool Register(const char* name, TestFunction function);

  /// Records a failed check for the running test.
  void Fail(const char* file, int line, const std::string& message);

  /// Marks the running test as skipped and stops it.
  [[noreturn]] void Skip(const std::string& reason);

  struct RequireFailed
  {};
  struct Skipped
  {};

  template <typename T>
  std::string Describe(const T& value)
  {
    if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, std::string_view> ||
                  std::is_convertible_v<T, const char*>) {
      return "\"" + std::string(value) + "\"";
    } else if constexpr (std::is_same_v<T, bool>) {
      return value ? "true" : "false";
    } else if constexpr (requires(std::ostream& os, const T& v) { os << v; }) {
      std::ostringstream stream;
      stream << value;
      return stream.str();
    } else {
      return "<value>";
    }
  }

} // namespace faf::test

#define FAF_TEST(name)                                                                                                 \
  static void name();                                                                                                  \
  [[maybe_unused]] static const bool name##Registered = ::faf::test::Register(#name, &name);                           \
  static void name()

#define FAF_CHECK(condition)                                                                                           \
  do {                                                                                                                 \
    if (!(condition)) {                                                                                                \
      ::faf::test::Fail(__FILE__, __LINE__, "CHECK(" #condition ")");                                                  \
    }                                                                                                                  \
  } while (false)

#define FAF_REQUIRE(condition)                                                                                         \
  do {                                                                                                                 \
    if (!(condition)) {                                                                                                \
      ::faf::test::Fail(__FILE__, __LINE__, "REQUIRE(" #condition ")");                                                \
      throw ::faf::test::RequireFailed{};                                                                              \
    }                                                                                                                  \
  } while (false)

#define FAF_CHECK_EQ(actual, expected)                                                                                 \
  do {                                                                                                                 \
    /* Copies: `actual` may refer into a temporary such as *optional. */                                               \
    const auto fafActual = (actual);                                                                                   \
    const auto fafExpected = (expected);                                                                               \
    if (!(fafActual == fafExpected)) {                                                                                 \
      ::faf::test::Fail(                                                                                               \
        __FILE__,                                                                                                      \
        __LINE__,                                                                                                      \
        "CHECK_EQ(" #actual ", " #expected "): " + ::faf::test::Describe(fafActual) +                                  \
          " != " + ::faf::test::Describe(fafExpected)                                                                  \
      );                                                                                                               \
    }                                                                                                                  \
  } while (false)
