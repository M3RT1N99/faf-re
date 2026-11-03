// faf_wx_lite_tests (android/native/WX_LITE.md, Checks): wx-lite's event system, the Win32
// message layer, and the translation of the layer's messages into wx events. It is linked
// statically, so it runs on the emulator and on Linux hosts as well (CI's arm64 runner); the two
// Android libraries the layer calls are stood in for (NdkDoubles.cpp). Exit code 0 when every
// check passes, 1 otherwise.

#include <Windows.h>
#include <Win32AndroidHost.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "Check.h"
#include "Support.h"

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

  std::vector<wxtest::Message> gMessages;

  LRESULT CALLBACK RecordingProcedure(const HWND hwnd, const UINT message, const WPARAM wParam, const LPARAM lParam)
  {
    gMessages.push_back(wxtest::Message{hwnd, message, wParam, lParam, ::GetCurrentThreadId(), ::InSendMessage()});
    if (message == WM_USER + 7) {
      return static_cast<LRESULT>(wParam + static_cast<WPARAM>(lParam));
    }
    return ::DefWindowProcW(hwnd, message, wParam, lParam);
  }

  const wchar_t* RecordingClass(const bool doubleClicks)
  {
    static const bool sRegistered = [] {
      WNDCLASSW windowClass{};
      windowClass.lpfnWndProc = &RecordingProcedure;
      windowClass.style = CS_DBLCLKS;
      windowClass.lpszClassName = L"WxLiteTestDoubleClicks";
      bool registered = ::RegisterClassW(&windowClass) != 0;
      windowClass.style = 0;
      windowClass.lpszClassName = L"WxLiteTest";
      registered = ::RegisterClassW(&windowClass) != 0 && registered;
      return registered;
    }();
    return sRegistered ? (doubleClicks ? L"WxLiteTestDoubleClicks" : L"WxLiteTest") : nullptr;
  }
} // namespace

// --- Checks ------------------------------------------------------------------------------------

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

// --- Support -----------------------------------------------------------------------------------

void wxtest::Pump()
{
  // A window that never validates its update region would get WM_PAINT for ever: a bound.
  MSG msg;
  for (int taken = 0; taken < 1000 && ::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE); ++taken) {
    if (msg.message == WM_QUIT) {
      break;
    }
    (void)::TranslateMessage(&msg);
    (void)::DispatchMessageW(&msg);
  }
}

std::vector<wxtest::Message>& wxtest::Messages()
{
  return gMessages;
}

std::vector<wxtest::Message> wxtest::Received(const UINT message)
{
  std::vector<Message> out;
  for (const Message& received : gMessages) {
    if (received.message == message) {
      out.push_back(received);
    }
  }
  return out;
}

std::vector<UINT> wxtest::Sequence(const std::vector<UINT>& kinds)
{
  std::vector<UINT> out;
  for (const Message& received : gMessages) {
    for (const UINT kind : kinds) {
      if (received.message == kind) {
        out.push_back(kind);
        break;
      }
    }
  }
  return out;
}

wxtest::RecordingWindow::RecordingWindow(
  const HWND parent, const int x, const int y, const int width, const int height, const DWORD style, const bool doubleClicks
)
  : mWindow(::CreateWindowExW(0, RecordingClass(doubleClicks), L"", style, x, y, width, height, parent, nullptr, nullptr, nullptr))
{
  if (mWindow == nullptr) {
    Fail(__FILE__, __LINE__, "CreateWindowExW failed, error " + std::to_string(::GetLastError()));
  }
}

wxtest::RecordingWindow::~RecordingWindow()
{
  if (mWindow != nullptr && ::IsWindow(mWindow)) {
    (void)::DestroyWindow(mWindow);
  }
}

DEFINE_EVENT_TYPE(wxEVT_TEST_APP)

BEGIN_EVENT_TABLE(TestApp, wxApp)
  EVT_COMMAND(wxID_ANY, wxEVT_TEST_APP, TestApp::OnTestEvent)
  EVT_IDLE(TestApp::OnIdleEvent)
END_EVENT_TABLE()

void TestApp::OnTestEvent(wxCommandEvent& event)
{
  reached.push_back(event.GetId());
}

void TestApp::OnIdleEvent(wxIdleEvent& event)
{
  ++idles;
  event.Skip();
}

// --- The run -----------------------------------------------------------------------------------

int main()
{
  // As the engine starts (wxEntry): the application object, then wxApp::Initialize, which makes
  // the thread's message queue and registers wx's window classes.
  (void)new TestApp;
  if (!wxApp::Initialize()) {
    std::printf("FAIL: wxApp::Initialize\n");
    return 1;
  }
  Win32Android_SetScreenSize(wxtest::kScreenWidth, wxtest::kScreenHeight);

  int failedTests = 0;
  for (const wxtest::Test& test : wxtest::Tests()) {
    gFailedChecks = 0;
    gMessages.clear();
    std::printf("%s\n", test.name);
    test.run();
    // What a test leaves goes before the next one starts.
    (void)::ReleaseCapture();
    (void)::SetFocus(nullptr);
    wxtest::Pump();
    if (gFailedChecks != 0) {
      ++failedTests;
      std::printf("  FAIL\n");
    }
  }
  std::printf("%zu tests, %d failed\n", wxtest::Tests().size(), failedTests);
  std::fflush(stdout);
  return failedTests == 0 ? 0 : 1;
}
