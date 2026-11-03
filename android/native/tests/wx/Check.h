#pragma once

// The checks of the wx-lite tests (android/native/tests/wx, faf_wx_lite_tests): a test is a
// function registered under its name; a check that fails records where and what, and the test
// goes on, so one run shows every difference.

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

namespace wxtest
{
  using TestFunction = void (*)();

  struct Test
  {
    const char* name;
    TestFunction run;
  };

  /// The tests in the order their files registered them.
  std::vector<Test>& Tests();

  struct Registration
  {
    Registration(const char* name, const TestFunction run)
    {
      Tests().push_back(Test{name, run});
    }
  };

  /// A failed check of the test that runs.
  void Fail(const char* file, int line, const std::string& what);

  std::string Hex(unsigned long long value);
  std::string Describe(const std::wstring& value);
  std::string Describe(const std::string& value);
  std::string Describe(const char* value);

  template <class T>
  std::string Describe(const T& value)
  {
    if constexpr (std::is_enum_v<T>) {
      return Describe(static_cast<std::underlying_type_t<T>>(value));
    } else if constexpr (std::is_pointer_v<T>) {
      return Describe(static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(value)));
    } else if constexpr (std::is_same_v<T, bool>) {
      return value ? "true" : "false";
    } else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) {
      const long long number = value;
      return std::to_string(number) + (number < 0 || number > 9 ? " (0x" + Hex(static_cast<unsigned long long>(number)) + ")" : "");
    } else if constexpr (std::is_integral_v<T>) {
      const unsigned long long number = value;
      return std::to_string(number) + (number > 9 ? " (0x" + Hex(number) + ")" : "");
    } else {
      return "(a value)";
    }
  }

  template <class A, class B>
  void CheckEqual(
    const A& actual, const B& expected, const char* actualText, const char* expectedText, const char* file, const int line
  )
  {
    if (!(actual == expected)) {
      Fail(file, line, std::string(actualText) + " == " + expectedText + ": " + Describe(actual) + ", expected " + Describe(expected));
    }
  }
} // namespace wxtest

#define WXT_TEST(name)                                                          \
  static void name();                                                           \
  static const ::wxtest::Registration name##Registration(#name, &name);       \
  static void name()

#define WXT_CHECK(condition) ((condition) ? (void)0 : ::wxtest::Fail(__FILE__, __LINE__, #condition))

#define WXT_CHECK_EQ(actual, expected) ::wxtest::CheckEqual((actual), (expected), #actual, #expected, __FILE__, __LINE__)
