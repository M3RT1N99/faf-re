#include <cstring>
#include <exception>

#include "TestHarness.h"

namespace faf::test {

  namespace {
    struct RunState
    {
      const char* current = nullptr;
      int failures = 0;
      bool currentFailed = false;
      std::string skipReason;
    };

    RunState& State()
    {
      static RunState state;
      return state;
    }
  } // namespace

  std::vector<TestCase>& Registry()
  {
    static std::vector<TestCase> registry;
    return registry;
  }

  bool Register(const char* const name, const TestFunction function)
  {
    Registry().push_back({name, function});
    return true;
  }

  void Fail(const char* const file, const int line, const std::string& message)
  {
    RunState& state = State();
    ++state.failures;
    state.currentFailed = true;
    std::fprintf(stderr, "%s(%d): [%s] %s\n", file, line, state.current ? state.current : "?", message.c_str());
  }

  void Skip(const std::string& reason)
  {
    State().skipReason = reason;
    throw Skipped{};
  }

} // namespace faf::test

int main(int argc, char** argv)
{
  using namespace faf::test;
  RunState& state = State();
  int ran = 0;
  int skipped = 0;
  for (const TestCase& test : Registry()) {
    if (argc > 1) {
      bool selected = false;
      for (int i = 1; i < argc; ++i) {
        selected = selected || std::strcmp(argv[i], test.name) == 0;
      }
      if (!selected) {
        continue;
      }
    }
    state.current = test.name;
    state.currentFailed = false;
    state.skipReason.clear();
    try {
      test.function();
    } catch (const RequireFailed&) {
    } catch (const Skipped&) {
      ++skipped;
      std::printf("[ SKIP ] %s: %s\n", test.name, state.skipReason.c_str());
      continue;
    } catch (const std::exception& e) {
      Fail(__FILE__, __LINE__, std::string("unexpected exception: ") + e.what());
    } catch (...) {
      Fail(__FILE__, __LINE__, "unexpected exception");
    }
    ++ran;
    std::printf("[ %s ] %s\n", state.currentFailed ? "FAIL" : " OK ", test.name);
  }
  std::printf("%d test(s) ran, %d skipped, %d failed check(s)\n", ran, skipped, state.failures);
  if (state.failures > 0) {
    return 1;
  }
  if (ran == 0 && skipped > 0) {
    return 77;
  }
  return ran > 0 ? 0 : 1;
}
