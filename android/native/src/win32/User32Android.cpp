// The message layer of the Android build (include/win32/WinUser.h, include/win32/Win32AndroidHost.h):
// windows, the threads' message queues, painting, focus, capture and activation, the host's input,
// the low-level keyboard hooks, the clipboard and global memory, with the behaviour of Win32's USER
// that the engine and wx-lite rely on.
//
// The layer's state is under one mutex. A window procedure, a hook or a task of the layer never runs
// with it held: each may call the layer again.

#include <Windows.h>
#include <Win32AndroidHost.h>

#include <android/log.h>
#include <android/native_window.h>
#include <unistd.h>

#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <cwctype>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
  struct ThreadQueue;

  struct WindowClass
  {
    std::wstring name;
    ATOM atom = 0;
    UINT style = 0;
    WNDPROC procedure = nullptr;
    int windowExtra = 0;
    HINSTANCE instance = nullptr;
    HICON icon = nullptr;
    HCURSOR cursor = nullptr;
    HBRUSH background = nullptr;
  };

  struct Window
  {
    WindowClass* windowClass = nullptr;
    WNDPROC procedure = nullptr;
    DWORD style = 0; // WS_VISIBLE and WS_DISABLED are the window's visibility and enabled state
    DWORD exStyle = 0;
    LONG_PTR id = 0; // a child's id, a top-level window's menu
    LONG_PTR userData = 0;
    HINSTANCE instance = nullptr;
    std::vector<unsigned char> extra;
    Window* parent = nullptr; // a child's parent
    Window* owner = nullptr; // a top-level window's owner
    std::vector<Window*> children; // the top of the z-order first
    RECT rect{}; // in the parent's client coordinates; a top-level window's on the screen
    RECT restored{}; // the rectangle before it was minimized or maximized
    std::wstring text;
    bool minimized = false;
    bool maximized = false;
    bool updatePending = false;
    bool erasePending = false;
    RECT update{}; // client coordinates
    bool destroying = false;
    ThreadQueue* queue = nullptr;
  };

  struct Posted
  {
    MSG msg{};
    wchar_t character = 0; // what a key press types, for TranslateMessage
  };

  /// A message sent from another thread, or a task of the layer for this thread.
  struct Sent
  {
    HWND window = nullptr;
    UINT message = 0;
    WPARAM wParam = 0;
    LPARAM lParam = 0;
    std::function<void()> task;
    bool done = false;
    LRESULT result = 0;
  };

  struct ThreadQueue
  {
    DWORD threadId = 0;
    std::deque<Posted> posted;
    std::deque<std::shared_ptr<Sent>> sent;
    bool quit = false;
    int quitCode = 0;
    HANDLE wake = nullptr; // manual reset: something arrived since the thread last looked
    int inSend = 0;
    LONG messageTime = 0;
    DWORD messagePos = 0;
    MSG lastKeyDown{}; // the last key press taken from the queue, and what it types
    wchar_t lastKeyCharacter = 0;
  };

  struct DeviceContext
  {
    Window* window = nullptr;
  };

  struct GlobalBlock
  {
    std::unique_ptr<unsigned char[]> data;
    SIZE_T size = 0;
    UINT flags = 0;
    int locks = 0;
  };

  struct Hook
  {
    HOOKPROC procedure = nullptr;
  };

  struct Click
  {
    UINT message = 0;
    DWORD time = 0;
    POINT point{};
    Window* window = nullptr;
  };

  struct State
  {
    std::mutex mutex;
    std::condition_variable sentDone;
    std::vector<std::unique_ptr<WindowClass>> classes;
    std::unordered_set<Window*> windows;
    std::vector<Window*> topLevel; // the top of the z-order first
    std::unordered_map<DWORD, std::unique_ptr<ThreadQueue>> queues;
    std::unordered_set<DeviceContext*> deviceContexts;
    std::unordered_map<HGLOBAL, std::unique_ptr<GlobalBlock>> globals;
    std::vector<std::unique_ptr<Hook>> keyboardHooks; // the last installed first
    Window* focus = nullptr;
    Window* active = nullptr;
    Window* lastActive = nullptr; // the active window when the activity went to the back
    Window* capture = nullptr;
    bool foreground = false; // the activity is in front (the host)
    bool appActive = false; // the windows had WM_ACTIVATEAPP(TRUE) last
    POINT cursor{};
    bool clipped = false;
    RECT clip{};
    HCURSOR cursorShape = nullptr;
    int cursorCount = 0;
    BYTE keys[256]{}; // GetKeyState: bit 7 down, bit 0 toggled
    BYTE asyncKeys[256]{}; // GetAsyncKeyState
    int screenWidth = 0;
    int screenHeight = 0;
    ANativeWindow* screenSurface = nullptr; // the activity's, from the host; one reference held
    Click lastClick;
    DWORD clipboardThread = 0;
    bool clipboardOpen = false;
    std::unordered_map<UINT, HGLOBAL> clipboard;
  };

  /// The layer's state, alive until the process ends: static destructors may still reach it.
  State& Layer()
  {
    static State* const state = new State;
    return *state;
  }

  using Lock = std::unique_lock<std::mutex>;

  constexpr int kDoubleClickDistance = 4; // SM_CXDOUBLECLK, SM_CYDOUBLECLK
  constexpr UINT kDoubleClickTime = 500;
  char kMonitor; // the one monitor's identity

  std::string Utf8(const wchar_t* text)
  {
    std::string out;
    if (text == nullptr) {
      return out;
    }
    for (; *text != 0; ++text) {
      const std::uint32_t c = static_cast<std::uint32_t>(*text);
      if (c < 0x80) {
        out += static_cast<char>(c);
      } else if (c < 0x800) {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
      } else if (c < 0x10000) {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
      } else {
        out += static_cast<char>(0xF0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
      }
    }
    return out;
  }

  std::wstring Wide(const char* text)
  {
    std::wstring out;
    if (text == nullptr) {
      return out;
    }
    const auto* p = reinterpret_cast<const unsigned char*>(text);
    while (*p != 0) {
      std::uint32_t c = *p++;
      int more = 0;
      if (c >= 0xF0) {
        c &= 0x07;
        more = 3;
      } else if (c >= 0xE0) {
        c &= 0x0F;
        more = 2;
      } else if (c >= 0xC0) {
        c &= 0x1F;
        more = 1;
      }
      for (; more > 0 && (*p & 0xC0) == 0x80; --more) {
        c = (c << 6) | (*p++ & 0x3F);
      }
      out += static_cast<wchar_t>(c);
    }
    return out;
  }

  bool SameName(const std::wstring& a, const wchar_t* b)
  {
    std::size_t i = 0;
    for (; i < a.size() && b[i] != 0; ++i) {
      if (std::towlower(static_cast<wint_t>(a[i])) != std::towlower(static_cast<wint_t>(b[i]))) {
        return false;
      }
    }
    return i == a.size() && b[i] == 0;
  }

  int Width(const RECT& r)
  {
    return r.right - r.left;
  }
  int Height(const RECT& r)
  {
    return r.bottom - r.top;
  }

  bool Contains(const RECT& r, const POINT p)
  {
    return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
  }

  RECT Union(const RECT& a, const RECT& b)
  {
    if (a.right <= a.left || a.bottom <= a.top) {
      return b;
    }
    return RECT{std::min(a.left, b.left), std::min(a.top, b.top), std::max(a.right, b.right), std::max(a.bottom, b.bottom)};
  }

  HWND Handle(Window* window)
  {
    return static_cast<HWND>(window);
  }

  // --- Locked helpers: the caller holds the layer's mutex ---------------------------------------

  Window* WindowLocked(const HWND handle)
  {
    auto* const window = static_cast<Window*>(handle);
    return Layer().windows.count(window) != 0 ? window : nullptr;
  }

  ThreadQueue& QueueLocked(const DWORD threadId)
  {
    State& layer = Layer();
    std::unique_ptr<ThreadQueue>& queue = layer.queues[threadId];
    if (!queue) {
      queue = std::make_unique<ThreadQueue>();
      queue->threadId = threadId;
      queue->wake = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    }
    return *queue;
  }

  ThreadQueue& CurrentQueueLocked()
  {
    return QueueLocked(::GetCurrentThreadId());
  }

  bool OnThisThread(const Window* window)
  {
    return window->queue->threadId == ::GetCurrentThreadId();
  }

  Window* RootLocked(Window* window)
  {
    while (window != nullptr && window->parent != nullptr) {
      window = window->parent;
    }
    return window;
  }

  bool IsDescendantLocked(const Window* ancestor, const Window* window)
  {
    for (const Window* w = window; w != nullptr; w = w->parent) {
      if (w == ancestor) {
        return true;
      }
    }
    return false;
  }

  bool VisibleLocked(const Window* window)
  {
    for (const Window* w = window; w != nullptr; w = w->parent) {
      if ((w->style & WS_VISIBLE) == 0 || w->minimized) {
        return false;
      }
    }
    return true;
  }

  /// The client area's origin on the screen.
  POINT ScreenOriginLocked(const Window* window)
  {
    POINT origin{0, 0};
    for (const Window* w = window; w != nullptr; w = w->parent) {
      origin.x += w->rect.left;
      origin.y += w->rect.top;
    }
    return origin;
  }

  RECT ScreenLocked()
  {
    return RECT{0, 0, Layer().screenWidth, Layer().screenHeight};
  }

  /// The rectangle a maximized window takes: the screen's, or the parent's client area.
  RECT MaximizedRectLocked(const Window* window)
  {
    if (window->parent != nullptr) {
      return RECT{0, 0, Width(window->parent->rect), Height(window->parent->rect)};
    }
    return ScreenLocked();
  }

  std::vector<Window*>& SiblingsLocked(const Window* window)
  {
    return window->parent != nullptr ? window->parent->children : Layer().topLevel;
  }

  void Wake(ThreadQueue& queue)
  {
    (void)::SetEvent(queue.wake);
    Layer().sentDone.notify_all();
  }

  void PostLocked(ThreadQueue& queue, const HWND window, const UINT message, const WPARAM wParam, const LPARAM lParam)
  {
    Posted posted;
    posted.msg = MSG{window, message, wParam, lParam, ::GetTickCount(), Layer().cursor};
    queue.posted.push_back(posted);
    Wake(queue);
  }

  /// Input from the host. A mouse move replaces one still waiting for the same window, as
  /// Windows coalesces them.
  void PostInputLocked(
    ThreadQueue& queue, Window* window, const UINT message, const WPARAM wParam, const LPARAM lParam, const wchar_t character
  )
  {
    Posted posted;
    posted.msg = MSG{Handle(window), message, wParam, lParam, ::GetTickCount(), Layer().cursor};
    posted.character = character;
    if (message == WM_MOUSEMOVE && !queue.posted.empty() && queue.posted.back().msg.message == WM_MOUSEMOVE
        && queue.posted.back().msg.hwnd == posted.msg.hwnd) {
      queue.posted.back() = posted;
    } else {
      queue.posted.push_back(posted);
    }
    Wake(queue);
  }

  /// Runs `task` on the thread of `queue`, as a sent message that nobody waits for.
  void PostTaskLocked(ThreadQueue& queue, std::function<void()> task)
  {
    auto sent = std::make_shared<Sent>();
    sent->task = std::move(task);
    queue.sent.push_back(std::move(sent));
    Wake(queue);
  }

  /// Visible windows of the thread, the parent before its children: WM_PAINT goes to the first
  /// with an update region.
  Window* PaintWindowLocked(const std::vector<Window*>& windows, const ThreadQueue& queue, const Window* filter)
  {
    for (Window* window : windows) {
      if (window->queue != &queue || !VisibleLocked(window)) {
        continue;
      }
      if (window->updatePending && (filter == nullptr || IsDescendantLocked(filter, window))) {
        return window;
      }
      if (Window* const child = PaintWindowLocked(window->children, queue, filter)) {
        return child;
      }
    }
    return nullptr;
  }

  bool InRange(const UINT message, const UINT filterMin, const UINT filterMax)
  {
    return (filterMin == 0 && filterMax == 0) || (message >= filterMin && message <= filterMax);
  }

  /// The PeekMessage window filter: null any, -1 the thread's own messages, else the window
  /// and its children.
  bool ForWindowLocked(const MSG& msg, const HWND filter)
  {
    if (filter == nullptr) {
      return true;
    }
    if (filter == reinterpret_cast<HWND>(static_cast<LONG_PTR>(-1))) {
      return msg.hwnd == nullptr;
    }
    Window* const window = WindowLocked(msg.hwnd);
    return window != nullptr && IsDescendantLocked(WindowLocked(filter), window);
  }

  UINT WakeBit(const UINT message)
  {
    if (message >= WM_KEYFIRST && message <= WM_KEYLAST) {
      return QS_KEY;
    }
    if (message == WM_MOUSEMOVE) {
      return QS_MOUSEMOVE;
    }
    if (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) {
      return QS_MOUSEBUTTON;
    }
    return QS_POSTMESSAGE;
  }

  /// Whether the thread's queue holds something PeekMessage (with these filters) or a wait for
  /// `wakeMask` would take.
  bool HasMessageLocked(
    const ThreadQueue& queue, const HWND filter, const UINT filterMin, const UINT filterMax, const UINT wakeMask
  )
  {
    if ((wakeMask & QS_SENDMESSAGE) != 0 && !queue.sent.empty()) {
      return true;
    }
    for (const Posted& posted : queue.posted) {
      if ((wakeMask & WakeBit(posted.msg.message)) != 0 && InRange(posted.msg.message, filterMin, filterMax)
          && ForWindowLocked(posted.msg, filter)) {
        return true;
      }
    }
    if ((wakeMask & QS_POSTMESSAGE) != 0 && queue.quit && InRange(WM_QUIT, filterMin, filterMax)) {
      return true;
    }
    return (wakeMask & QS_PAINT) != 0 && InRange(WM_PAINT, filterMin, filterMax)
        && PaintWindowLocked(Layer().topLevel, queue, WindowLocked(filter)) != nullptr;
  }

  /// The key state as of an input message taken from the queue (GetKeyState).
  void TakeInputLocked(ThreadQueue& queue, const Posted& posted)
  {
    State& layer = Layer();
    const MSG& msg = posted.msg;
    auto press = [&layer](const int key, const bool down) {
      if (down) {
        if ((layer.keys[key] & 0x80) == 0) {
          layer.keys[key] ^= 0x01;
        }
        layer.keys[key] |= 0x80;
      } else {
        layer.keys[key] &= static_cast<BYTE>(~0x80);
      }
    };
    switch (msg.message) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP: {
      const bool down = msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN;
      const int key = static_cast<int>(msg.wParam & 0xFF);
      press(key, down);
      const bool extended = (msg.lParam & (1 << 24)) != 0;
      if (key == VK_SHIFT) {
        press(((msg.lParam >> 16) & 0xFF) == 0x36 ? VK_RSHIFT : VK_LSHIFT, down);
      } else if (key == VK_CONTROL) {
        press(extended ? VK_RCONTROL : VK_LCONTROL, down);
      } else if (key == VK_MENU) {
        press(extended ? VK_RMENU : VK_LMENU, down);
      }
      if (down) {
        queue.lastKeyDown = msg;
        queue.lastKeyCharacter = posted.character;
      }
      break;
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
      press(VK_LBUTTON, true);
      break;
    case WM_LBUTTONUP:
      press(VK_LBUTTON, false);
      break;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONDBLCLK:
      press(VK_RBUTTON, true);
      break;
    case WM_RBUTTONUP:
      press(VK_RBUTTON, false);
      break;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONDBLCLK:
      press(VK_MBUTTON, true);
      break;
    case WM_MBUTTONUP:
      press(VK_MBUTTON, false);
      break;
    case WM_XBUTTONDOWN:
    case WM_XBUTTONDBLCLK:
    case WM_XBUTTONUP:
      press(HIWORD(msg.wParam) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2, msg.message != WM_XBUTTONUP);
      break;
    default:
      break;
    }
  }

  WPARAM MouseKeysLocked()
  {
    const BYTE* const keys = Layer().asyncKeys;
    WPARAM state = 0;
    state |= (keys[VK_LBUTTON] & 0x80) != 0 ? MK_LBUTTON : 0;
    state |= (keys[VK_RBUTTON] & 0x80) != 0 ? MK_RBUTTON : 0;
    state |= (keys[VK_MBUTTON] & 0x80) != 0 ? MK_MBUTTON : 0;
    state |= (keys[VK_XBUTTON1] & 0x80) != 0 ? MK_XBUTTON1 : 0;
    state |= (keys[VK_XBUTTON2] & 0x80) != 0 ? MK_XBUTTON2 : 0;
    state |= (keys[VK_SHIFT] & 0x80) != 0 ? MK_SHIFT : 0;
    state |= (keys[VK_CONTROL] & 0x80) != 0 ? MK_CONTROL : 0;
    return state;
  }

  Window* WindowAtLocked(const std::vector<Window*>& windows, const POINT point, const POINT origin)
  {
    for (Window* window : windows) {
      if ((window->style & WS_VISIBLE) == 0 || window->minimized) {
        continue;
      }
      const RECT rect{
        origin.x + window->rect.left, origin.y + window->rect.top, origin.x + window->rect.right,
        origin.y + window->rect.bottom
      };
      if (!Contains(rect, point)) {
        continue;
      }
      if ((window->style & WS_DISABLED) != 0 && window->parent != nullptr) {
        return nullptr; // a disabled child: its parent takes the point
      }
      Window* const child = WindowAtLocked(window->children, point, POINT{rect.left, rect.top});
      return child != nullptr ? child : window;
    }
    return nullptr;
  }

  POINT ClampToClipLocked(POINT point)
  {
    const State& layer = Layer();
    if (layer.clipped) {
      point.x = std::clamp(point.x, layer.clip.left, std::max(layer.clip.left, layer.clip.right - 1));
      point.y = std::clamp(point.y, layer.clip.top, std::max(layer.clip.top, layer.clip.bottom - 1));
    }
    return point;
  }

  /// A mouse message for the capture window or the one under the pointer, in its client
  /// coordinates.
  void PostMouseLocked(UINT message, const WORD xButton)
  {
    State& layer = Layer();
    Window* const target = layer.capture != nullptr ? layer.capture : WindowAtLocked(layer.topLevel, layer.cursor, POINT{0, 0});
    if (target == nullptr) {
      return;
    }
    const POINT origin = ScreenOriginLocked(target);
    const POINT client{layer.cursor.x - origin.x, layer.cursor.y - origin.y};

    const bool press = message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == WM_MBUTTONDOWN
                    || message == WM_XBUTTONDOWN;
    if (press && target->windowClass != nullptr && (target->windowClass->style & CS_DBLCLKS) != 0) {
      const Click& last = layer.lastClick;
      const DWORD now = ::GetTickCount();
      if (last.window == target && last.message == message && now - last.time <= kDoubleClickTime
          && std::abs(last.point.x - layer.cursor.x) <= kDoubleClickDistance / 2
          && std::abs(last.point.y - layer.cursor.y) <= kDoubleClickDistance / 2) {
        message += WM_LBUTTONDBLCLK - WM_LBUTTONDOWN; // the DBLCLK message follows each DOWN
        layer.lastClick = Click{};
      } else {
        layer.lastClick = Click{message, now, layer.cursor, target};
      }
    }

    const WPARAM wParam = MouseKeysLocked() | (static_cast<WPARAM>(xButton) << 16);
    PostInputLocked(*target->queue, target, message, wParam, MAKELPARAM(client.x, client.y), 0);
  }

  // --- Unlocked helpers ---------------------------------------------------------------------------

  /// Processes the messages and tasks other threads sent to this one.
  void ProcessSent()
  {
    State& layer = Layer();
    for (;;) {
      Lock lock(layer.mutex);
      ThreadQueue& queue = CurrentQueueLocked();
      if (queue.sent.empty()) {
        return;
      }
      const std::shared_ptr<Sent> sent = queue.sent.front();
      queue.sent.pop_front();
      if (sent->task) {
        lock.unlock();
        sent->task();
        lock.lock();
      } else {
        Window* const window = WindowLocked(sent->window);
        const WNDPROC procedure = window != nullptr ? window->procedure : nullptr;
        ++queue.inSend;
        lock.unlock();
        const LRESULT result = procedure != nullptr ? procedure(sent->window, sent->message, sent->wParam, sent->lParam) : 0;
        lock.lock();
        --queue.inSend;
        sent->result = result;
      }
      sent->done = true;
      layer.sentDone.notify_all();
    }
  }

  BOOL PeekCore(MSG* const out, const HWND filter, const UINT filterMin, const UINT filterMax, const UINT remove)
  {
    State& layer = Layer();
    ProcessSent();

    Lock lock(layer.mutex);
    ThreadQueue& queue = CurrentQueueLocked();
    // The thread has looked: only what arrives from now on wakes a wait without MWMO_INPUTAVAILABLE.
    (void)::ResetEvent(queue.wake);

    for (auto it = queue.posted.begin(); it != queue.posted.end(); ++it) {
      if (!InRange(it->msg.message, filterMin, filterMax) || !ForWindowLocked(it->msg, filter)) {
        continue;
      }
      const Posted posted = *it;
      *out = posted.msg;
      queue.messageTime = static_cast<LONG>(posted.msg.time);
      queue.messagePos = static_cast<DWORD>(MAKELONG(posted.msg.pt.x, posted.msg.pt.y));
      if ((remove & PM_REMOVE) == 0) {
        return TRUE;
      }
      queue.posted.erase(it);
      TakeInputLocked(queue, posted);

      // Windows sends WM_SETCURSOR as the pointer moves over a window or presses on it, while no
      // window has the capture.
      const UINT message = posted.msg.message;
      const bool mouse = message == WM_MOUSEMOVE || message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN
                      || message == WM_MBUTTONDOWN || message == WM_XBUTTONDOWN || message == WM_LBUTTONDBLCLK
                      || message == WM_RBUTTONDBLCLK || message == WM_MBUTTONDBLCLK;
      if (mouse && layer.capture == nullptr && WindowLocked(posted.msg.hwnd) != nullptr) {
        lock.unlock();
        (void)::SendMessageW(
          posted.msg.hwnd, WM_SETCURSOR, reinterpret_cast<WPARAM>(posted.msg.hwnd), MAKELPARAM(HTCLIENT, message)
        );
      }
      return TRUE;
    }

    if (queue.quit && InRange(WM_QUIT, filterMin, filterMax)) {
      *out = MSG{nullptr, WM_QUIT, static_cast<WPARAM>(queue.quitCode), 0, ::GetTickCount(), layer.cursor};
      if ((remove & PM_REMOVE) != 0) {
        queue.quit = false;
      }
      return TRUE;
    }

    if (InRange(WM_PAINT, filterMin, filterMax)) {
      if (Window* const window = PaintWindowLocked(layer.topLevel, queue, WindowLocked(filter))) {
        // WM_PAINT is not removed: it comes again until the update region is validated.
        *out = MSG{Handle(window), WM_PAINT, 0, 0, ::GetTickCount(), layer.cursor};
        return TRUE;
      }
    }
    return FALSE;
  }

  /// WM_ACTIVATEAPP to the thread's top-level windows: the application becomes the active one
  /// (its window is activated while the activity is in front) or stops being it.
  void ActivateApplication(const bool active)
  {
    State& layer = Layer();
    std::vector<HWND> topLevel;
    {
      Lock lock(layer.mutex);
      if (layer.appActive == active) {
        return;
      }
      layer.appActive = active;
      for (Window* top : layer.topLevel) {
        if (OnThisThread(top)) {
          topLevel.push_back(Handle(top));
        }
      }
    }
    for (const HWND top : topLevel) {
      (void)::SendMessageW(top, WM_ACTIVATEAPP, active ? TRUE : FALSE, static_cast<LPARAM>(::GetCurrentThreadId()));
    }
  }

  /// WM_ACTIVATE to the window that stops being active and the one that becomes it.
  void Activate(Window* const next)
  {
    State& layer = Layer();
    Lock lock(layer.mutex);
    Window* const previous = layer.active;
    if (previous == next) {
      return;
    }
    layer.active = next;
    const bool previousAlive = previous != nullptr && WindowLocked(Handle(previous)) != nullptr;
    const WORD previousMinimized = previousAlive && previous->minimized ? 1 : 0;
    const WORD nextMinimized = next != nullptr && next->minimized ? 1 : 0;
    const bool activatesApplication = next != nullptr && layer.foreground && !layer.appActive;
    lock.unlock();
    if (previousAlive) {
      (void)::SendMessageW(Handle(previous), WM_ACTIVATE, MAKEWPARAM(WA_INACTIVE, previousMinimized), reinterpret_cast<LPARAM>(Handle(next)));
    }
    if (activatesApplication) {
      ActivateApplication(true);
    }
    if (next != nullptr) {
      (void)::SendMessageW(
        Handle(next), WM_ACTIVATE, MAKEWPARAM(WA_ACTIVE, nextMinimized), reinterpret_cast<LPARAM>(previousAlive ? Handle(previous) : nullptr)
      );
    }
  }

  /// The host's foreground change, on the thread of the windows.
  void ApplyForeground(const HWND window, const BOOL foreground)
  {
    State& layer = Layer();
    {
      Lock lock(layer.mutex);
      layer.foreground = foreground != FALSE;
    }
    if (foreground != FALSE) {
      bool alreadyActive = false;
      {
        Lock lock(layer.mutex);
        Window* const target = WindowLocked(window);
        alreadyActive = target != nullptr && layer.active == RootLocked(target);
      }
      if (alreadyActive) {
        ActivateApplication(true);
      } else {
        (void)::SetActiveWindow(window);
      }
      return;
    }
    // To the back: the application and its active window go inactive, and the focus goes; they
    // come back with the window's activation (DefWindowProc and wx set the focus on WM_ACTIVATE).
    {
      Lock lock(layer.mutex);
      layer.lastActive = layer.active;
    }
    ActivateApplication(false);
    Activate(nullptr);
    (void)::SetFocus(nullptr);
  }

  void DestroyTree(Window* window);

  /// Removes `window` from the layer and frees it (its children are gone already).
  void FreeWindowLocked(Window* const window)
  {
    State& layer = Layer();
    std::vector<Window*>& siblings = SiblingsLocked(window);
    siblings.erase(std::remove(siblings.begin(), siblings.end(), window), siblings.end());
    for (auto& entry : layer.queues) {
      std::deque<Posted>& posted = entry.second->posted;
      posted.erase(
        std::remove_if(posted.begin(), posted.end(), [window](const Posted& p) { return p.msg.hwnd == Handle(window); }),
        posted.end()
      );
    }
    for (Window* other : layer.windows) {
      if (other->owner == window) {
        other->owner = nullptr;
      }
    }
    if (layer.lastClick.window == window) {
      layer.lastClick = Click{};
    }
    // DestroyWindow moved these on already; a window procedure may have given them back to the
    // window while it was being destroyed.
    if (layer.active == window) {
      layer.active = nullptr;
    }
    if (layer.lastActive == window) {
      layer.lastActive = nullptr;
    }
    if (layer.focus == window) {
      layer.focus = nullptr;
    }
    if (layer.capture == window) {
      layer.capture = nullptr;
    }
    for (DeviceContext* context : layer.deviceContexts) {
      if (context->window == window) {
        context->window = nullptr;
      }
    }
    layer.windows.erase(window);
    delete window;
  }

  /// WM_DESTROY to the window, then its children are destroyed, then WM_NCDESTROY, as
  /// DestroyWindow does it.
  void DestroyTree(Window* const window)
  {
    State& layer = Layer();
    const HWND handle = Handle(window);
    (void)::SendMessageW(handle, WM_DESTROY, 0, 0);
    for (;;) {
      Window* child = nullptr;
      {
        Lock lock(layer.mutex);
        if (WindowLocked(handle) == nullptr || window->children.empty()) {
          break;
        }
        child = window->children.front();
        child->destroying = true;
      }
      DestroyTree(child);
    }
    (void)::SendMessageW(handle, WM_NCDESTROY, 0, 0);
    Lock lock(layer.mutex);
    if (WindowLocked(handle) != nullptr) {
      FreeWindowLocked(window);
    }
  }

  void InvalidateTreeLocked(Window* const window)
  {
    window->updatePending = true;
    window->erasePending = true;
    window->update = RECT{0, 0, Width(window->rect), Height(window->rect)};
    for (Window* child : window->children) {
      InvalidateTreeLocked(child);
    }
    Wake(*window->queue);
  }

  /// WM_WINDOWPOSCHANGED for a change of the rectangle or the visibility; DefWindowProc turns it
  /// into WM_SIZE and WM_MOVE.
  void NotifyPosition(const HWND handle, const WINDOWPOS& position)
  {
    WINDOWPOS copy = position;
    (void)::SendMessageW(handle, WM_WINDOWPOSCHANGED, 0, reinterpret_cast<LPARAM>(&copy));
  }

  LRESULT DefaultSetCursor(Window* window, const HWND handle, const WPARAM wParam, const LPARAM lParam)
  {
    HWND parent = nullptr;
    HCURSOR cursor = nullptr;
    {
      Lock lock(Layer().mutex);
      if (WindowLocked(handle) == nullptr) {
        return FALSE;
      }
      parent = window->parent != nullptr ? Handle(window->parent) : nullptr;
      cursor = window->windowClass != nullptr ? window->windowClass->cursor : nullptr;
    }
    if (parent != nullptr && ::SendMessageW(parent, WM_SETCURSOR, wParam, lParam) != FALSE) {
      return TRUE;
    }
    if (static_cast<short>(LOWORD(lParam)) == HTCLIENT && cursor != nullptr) {
      (void)::SetCursor(cursor);
      return TRUE;
    }
    return FALSE;
  }

  void LogBox(const wchar_t* const caption, const wchar_t* const text)
  {
    __android_log_print(ANDROID_LOG_WARN, "faf", "MessageBox: %s: %s", Utf8(caption).c_str(), Utf8(text).c_str());
  }
} // namespace

// --- Classes ----------------------------------------------------------------------------------------

ATOM RegisterClassW(const WNDCLASSW* const windowClass)
{
  if (windowClass == nullptr || windowClass->lpszClassName == nullptr || IS_INTRESOURCE(windowClass->lpszClassName)) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  State& layer = Layer();
  Lock lock(layer.mutex);
  for (const auto& existing : layer.classes) {
    if (SameName(existing->name, windowClass->lpszClassName)) {
      ::SetLastError(1410u); // ERROR_CLASS_ALREADY_EXISTS
      return 0;
    }
  }
  auto entry = std::make_unique<WindowClass>();
  entry->name = windowClass->lpszClassName;
  entry->atom = static_cast<ATOM>(0xC000u + layer.classes.size());
  entry->style = windowClass->style;
  entry->procedure = windowClass->lpfnWndProc;
  entry->windowExtra = std::max(0, windowClass->cbWndExtra);
  entry->instance = windowClass->hInstance;
  entry->icon = windowClass->hIcon;
  entry->cursor = windowClass->hCursor;
  entry->background = windowClass->hbrBackground;
  const ATOM atom = entry->atom;
  layer.classes.push_back(std::move(entry));
  return atom;
}

namespace
{
  WindowClass* ClassLocked(const wchar_t* const name)
  {
    for (const auto& entry : Layer().classes) {
      if (IS_INTRESOURCE(name) ? entry->atom == static_cast<ATOM>(reinterpret_cast<ULONG_PTR>(name)) : SameName(entry->name, name)) {
        return entry.get();
      }
    }
    return nullptr;
  }
} // namespace

BOOL UnregisterClassW(const wchar_t* const className, HINSTANCE)
{
  State& layer = Layer();
  Lock lock(layer.mutex);
  WindowClass* const entry = ClassLocked(className);
  if (entry == nullptr) {
    ::SetLastError(1411u); // ERROR_CLASS_DOES_NOT_EXIST
    return FALSE;
  }
  for (const Window* window : layer.windows) {
    if (window->windowClass == entry) {
      ::SetLastError(1412u); // ERROR_CLASS_HAS_WINDOWS
      return FALSE;
    }
  }
  // The entry stays (its atom with it), nameless: a window class's atom is not given out again.
  entry->name.clear();
  return TRUE;
}

BOOL GetClassInfoW(HINSTANCE, const wchar_t* const className, WNDCLASSW* const windowClass)
{
  Lock lock(Layer().mutex);
  const WindowClass* const entry = ClassLocked(className);
  if (entry == nullptr || windowClass == nullptr) {
    ::SetLastError(1411u);
    return FALSE;
  }
  *windowClass = WNDCLASSW{
    entry->style, entry->procedure, 0, entry->windowExtra, entry->instance, entry->icon, entry->cursor, entry->background,
    nullptr, entry->name.c_str()
  };
  return TRUE;
}

// --- Windows ----------------------------------------------------------------------------------------

HWND CreateWindowExW(
  const DWORD exStyle,
  const wchar_t* const className,
  const wchar_t* const windowName,
  const DWORD style,
  int x,
  int y,
  int width,
  int height,
  const HWND parentHandle,
  const HMENU menu,
  const HINSTANCE instance,
  const LPVOID param
)
{
  State& layer = Layer();
  auto* const window = new Window;
  CREATESTRUCTW create{};
  {
    Lock lock(layer.mutex);
    WindowClass* const windowClass = ClassLocked(className);
    if (windowClass == nullptr) {
      delete window;
      ::SetLastError(1407u); // ERROR_CANNOT_FIND_WND_CLASS
      return nullptr;
    }
    Window* const parent = parentHandle != nullptr ? WindowLocked(parentHandle) : nullptr;
    if (parentHandle != nullptr && parent == nullptr) {
      delete window;
      ::SetLastError(ERROR_INVALID_WINDOW_HANDLE);
      return nullptr;
    }
    const bool child = (style & WS_CHILD) != 0;
    if (child && parent == nullptr) {
      delete window;
      ::SetLastError(1406u); // ERROR_TLW_WITH_WSCHILD
      return nullptr;
    }
    if (x == CW_USEDEFAULT) {
      x = 0;
      y = 0;
    }
    if (width == CW_USEDEFAULT) {
      width = child ? 0 : layer.screenWidth;
      height = child ? 0 : layer.screenHeight;
    }

    window->windowClass = windowClass;
    window->procedure = windowClass->procedure;
    window->style = style & ~static_cast<DWORD>(WS_VISIBLE); // ShowWindow below makes it visible
    window->exStyle = exStyle;
    window->id = reinterpret_cast<LONG_PTR>(menu);
    window->instance = instance;
    window->extra.assign(static_cast<std::size_t>(windowClass->windowExtra), 0);
    window->parent = child ? parent : nullptr;
    window->owner = child ? nullptr : (parent != nullptr ? RootLocked(parent) : nullptr);
    window->rect = RECT{x, y, x + std::max(width, 0), y + std::max(height, 0)};
    window->restored = window->rect;
    window->queue = &CurrentQueueLocked();
    layer.windows.insert(window);
    SiblingsLocked(window).insert(SiblingsLocked(window).begin(), window);

    create = CREATESTRUCTW{
      param, instance, menu, parentHandle, height, width, y, x, static_cast<LONG>(style), windowName, className, exStyle
    };
  }

  const HWND handle = Handle(window);
  if (::SendMessageW(handle, WM_NCCREATE, 0, reinterpret_cast<LPARAM>(&create)) == FALSE
      || ::SendMessageW(handle, WM_CREATE, 0, reinterpret_cast<LPARAM>(&create)) == -1) {
    (void)::DestroyWindow(handle);
    ::SetLastError(1400u);
    return nullptr;
  }
  {
    Lock lock(layer.mutex);
    if (WindowLocked(handle) == nullptr) {
      return nullptr;
    }
  }
  // As CreateWindowEx: the window learns its size and place after WM_CREATE.
  (void)::SendMessageW(handle, WM_SIZE, SIZE_RESTORED, MAKELPARAM(std::max(width, 0), std::max(height, 0)));
  (void)::SendMessageW(handle, WM_MOVE, 0, MAKELPARAM(x, y));
  if ((style & WS_MAXIMIZE) != 0) {
    (void)::ShowWindow(handle, (style & WS_VISIBLE) != 0 ? SW_SHOWMAXIMIZED : SW_MAXIMIZE);
  } else if ((style & WS_VISIBLE) != 0) {
    (void)::ShowWindow(handle, SW_SHOW);
  }
  return handle;
}

BOOL DestroyWindow(const HWND handle)
{
  State& layer = Layer();
  Window* window = nullptr;
  bool hadFocus = false;
  bool hadCapture = false;
  bool wasActive = false;
  {
    Lock lock(layer.mutex);
    window = WindowLocked(handle);
    if (window == nullptr) {
      ::SetLastError(ERROR_INVALID_WINDOW_HANDLE);
      return FALSE;
    }
    if (!OnThisThread(window)) {
      ::SetLastError(ERROR_ACCESS_DENIED);
      return FALSE;
    }
    window->destroying = true;
    hadFocus = layer.focus != nullptr && IsDescendantLocked(window, layer.focus);
    hadCapture = layer.capture != nullptr && IsDescendantLocked(window, layer.capture);
    wasActive = layer.active == window;
  }

  // The windows it owns go first.
  for (;;) {
    HWND owned = nullptr;
    {
      Lock lock(layer.mutex);
      for (Window* other : layer.topLevel) {
        if (other->owner == window && !other->destroying) {
          owned = Handle(other);
          break;
        }
      }
    }
    if (owned == nullptr) {
      break;
    }
    (void)::DestroyWindow(owned);
  }

  if (hadCapture) {
    (void)::ReleaseCapture();
  }
  if (hadFocus) {
    (void)::SetFocus(nullptr);
  }
  if (wasActive) {
    // The next visible top-level window of the thread becomes active.
    Window* next = nullptr;
    {
      Lock lock(layer.mutex);
      for (Window* other : layer.topLevel) {
        if (other != window && !other->destroying && OnThisThread(other) && VisibleLocked(other)) {
          next = other;
          break;
        }
      }
    }
    Activate(next);
  }

  DestroyTree(window);
  return TRUE;
}

BOOL IsWindow(const HWND handle)
{
  Lock lock(Layer().mutex);
  return WindowLocked(handle) != nullptr ? TRUE : FALSE;
}

BOOL IsChild(const HWND parentHandle, const HWND handle)
{
  Lock lock(Layer().mutex);
  Window* const parent = WindowLocked(parentHandle);
  Window* const window = WindowLocked(handle);
  return parent != nullptr && window != nullptr && window != parent && IsDescendantLocked(parent, window) ? TRUE : FALSE;
}

HWND GetParent(const HWND handle)
{
  Lock lock(Layer().mutex);
  Window* const window = WindowLocked(handle);
  if (window == nullptr) {
    ::SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    return nullptr;
  }
  if (window->parent != nullptr) {
    return Handle(window->parent);
  }
  // A top-level popup's parent is its owner.
  return (window->style & WS_POPUP) != 0 && window->owner != nullptr ? Handle(window->owner) : nullptr;
}

HWND SetParent(const HWND childHandle, const HWND parentHandle)
{
  Lock lock(Layer().mutex);
  Window* const window = WindowLocked(childHandle);
  Window* const parent = parentHandle != nullptr ? WindowLocked(parentHandle) : nullptr;
  if (window == nullptr || (parentHandle != nullptr && parent == nullptr) || (parent != nullptr && IsDescendantLocked(window, parent))) {
    ::SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    return nullptr;
  }
  Window* const previous = window->parent;
  std::vector<Window*>& before = SiblingsLocked(window);
  before.erase(std::remove(before.begin(), before.end(), window), before.end());
  window->parent = parent;
  std::vector<Window*>& after = SiblingsLocked(window);
  after.insert(after.begin(), window);
  return previous != nullptr ? Handle(previous) : nullptr;
}

HWND GetAncestor(const HWND handle, const UINT flags)
{
  Lock lock(Layer().mutex);
  Window* window = WindowLocked(handle);
  if (window == nullptr) {
    return nullptr;
  }
  switch (flags) {
  case GA_PARENT:
    return window->parent != nullptr ? Handle(window->parent) : nullptr;
  case GA_ROOT:
    return Handle(RootLocked(window));
  case GA_ROOTOWNER:
    window = RootLocked(window);
    while (window->owner != nullptr) {
      window = RootLocked(window->owner);
    }
    return Handle(window);
  default:
    return nullptr;
  }
}

DWORD GetWindowThreadProcessId(const HWND handle, DWORD* const processId)
{
  Lock lock(Layer().mutex);
  Window* const window = WindowLocked(handle);
  if (window == nullptr) {
    return 0;
  }
  if (processId != nullptr) {
    *processId = static_cast<DWORD>(::getpid());
  }
  return window->queue->threadId;
}

LONG_PTR GetWindowLongPtrW(const HWND handle, const int index)
{
  Lock lock(Layer().mutex);
  Window* const window = WindowLocked(handle);
  if (window == nullptr) {
    ::SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    return 0;
  }
  switch (index) {
  case GWLP_WNDPROC:
    return reinterpret_cast<LONG_PTR>(window->procedure);
  case GWLP_HINSTANCE:
    return reinterpret_cast<LONG_PTR>(window->instance);
  case GWLP_HWNDPARENT:
    return reinterpret_cast<LONG_PTR>(window->parent != nullptr ? window->parent : window->owner);
  case GWLP_ID:
    return window->id;
  case GWL_STYLE:
    return static_cast<LONG_PTR>(
      window->style | (window->minimized ? WS_MINIMIZE : 0) | (window->maximized ? WS_MAXIMIZE : 0)
    );
  case GWL_EXSTYLE:
    return static_cast<LONG_PTR>(window->exStyle);
  case GWLP_USERDATA:
    return window->userData;
  default:
    if (index >= 0 && static_cast<std::size_t>(index) + sizeof(LONG_PTR) <= window->extra.size()) {
      LONG_PTR value = 0;
      std::memcpy(&value, window->extra.data() + index, sizeof(value));
      return value;
    }
    ::SetLastError(1413u); // ERROR_INVALID_INDEX
    return 0;
  }
}

LONG_PTR SetWindowLongPtrW(const HWND handle, const int index, const LONG_PTR value)
{
  Lock lock(Layer().mutex);
  Window* const window = WindowLocked(handle);
  if (window == nullptr) {
    ::SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    return 0;
  }
  LONG_PTR previous = 0;
  switch (index) {
  case GWLP_WNDPROC:
    previous = reinterpret_cast<LONG_PTR>(window->procedure);
    window->procedure = reinterpret_cast<WNDPROC>(value);
    return previous;
  case GWLP_HINSTANCE:
    previous = reinterpret_cast<LONG_PTR>(window->instance);
    window->instance = reinterpret_cast<HINSTANCE>(value);
    return previous;
  case GWLP_ID:
    previous = window->id;
    window->id = value;
    return previous;
  case GWL_STYLE: {
    previous = static_cast<LONG_PTR>(window->style);
    const DWORD style = static_cast<DWORD>(value);
    window->minimized = (style & WS_MINIMIZE) != 0 ? window->minimized : false;
    window->maximized = (style & WS_MAXIMIZE) != 0 ? window->maximized : false;
    window->style = style & ~static_cast<DWORD>(WS_MINIMIZE | WS_MAXIMIZE);
    return previous;
  }
  case GWL_EXSTYLE:
    previous = static_cast<LONG_PTR>(window->exStyle);
    window->exStyle = static_cast<DWORD>(value);
    return previous;
  case GWLP_USERDATA:
    previous = window->userData;
    window->userData = value;
    return previous;
  default:
    if (index >= 0 && static_cast<std::size_t>(index) + sizeof(LONG_PTR) <= window->extra.size()) {
      std::memcpy(&previous, window->extra.data() + index, sizeof(previous));
      std::memcpy(window->extra.data() + index, &value, sizeof(value));
      return previous;
    }
    ::SetLastError(1413u);
    return 0;
  }
}

LONG GetWindowLongW(const HWND handle, const int index)
{
  return static_cast<LONG>(::GetWindowLongPtrW(handle, index));
}

LONG SetWindowLongW(const HWND handle, const int index, const LONG value)
{
  return static_cast<LONG>(::SetWindowLongPtrW(handle, index, value));
}

BOOL IsWindowVisible(const HWND handle)
{
  Lock lock(Layer().mutex);
  Window* const window = WindowLocked(handle);
  if (window == nullptr) {
    return FALSE;
  }
  for (const Window* w = window; w != nullptr; w = w->parent) {
    if ((w->style & WS_VISIBLE) == 0) {
      return FALSE;
    }
  }
  return TRUE;
}

BOOL IsWindowEnabled(const HWND handle)
{
  Lock lock(Layer().mutex);
  Window* const window = WindowLocked(handle);
  return window != nullptr && (window->style & WS_DISABLED) == 0 ? TRUE : FALSE;
}

BOOL EnableWindow(const HWND handle, const BOOL enable)
{
  State& layer = Layer();
  bool wasDisabled = false;
  bool loseFocus = false;
  {
    Lock lock(layer.mutex);
    Window* const window = WindowLocked(handle);
    if (window == nullptr) {
      ::SetLastError(ERROR_INVALID_WINDOW_HANDLE);
      return FALSE;
    }
    wasDisabled = (window->style & WS_DISABLED) != 0;
    if (wasDisabled == (enable == FALSE)) {
      return wasDisabled ? TRUE : FALSE;
    }
    if (enable != FALSE) {
      window->style &= ~static_cast<DWORD>(WS_DISABLED);
    } else {
      window->style |= WS_DISABLED;
      loseFocus = layer.focus != nullptr && IsDescendantLocked(window, layer.focus);
    }
  }
  if (loseFocus) {
    (void)::SetFocus(nullptr);
  }
  (void)::SendMessageW(handle, WM_ENABLE, enable != FALSE ? TRUE : FALSE, 0);
  return wasDisabled ? TRUE : FALSE;
}

BOOL ShowWindow(const HWND handle, const int command)
{
  State& layer = Layer();
  bool wasVisible = false;
  bool nowVisible = false;
  bool stateChanged = false;
  bool activate = false;
  bool deactivate = false;
  bool topLevel = false;
  WINDOWPOS position{};
  {
    Lock lock(layer.mutex);
    Window* const window = WindowLocked(handle);
    if (window == nullptr) {
      ::SetLastError(ERROR_INVALID_WINDOW_HANDLE);
      return FALSE;
    }
    wasVisible = (window->style & WS_VISIBLE) != 0;
    topLevel = window->parent == nullptr;
    const RECT before = window->rect;
    const bool wasMinimized = window->minimized;
    const bool wasMaximized = window->maximized;

    switch (command) {
    case SW_HIDE:
      window->style &= ~static_cast<DWORD>(WS_VISIBLE);
      deactivate = layer.active == window;
      break;
    case SW_SHOWMINIMIZED:
    case SW_MINIMIZE:
    case SW_SHOWMINNOACTIVE:
      if (!window->minimized) {
        if (!window->maximized) {
          window->restored = window->rect;
        }
        window->minimized = true;
        // An iconic window has no client area.
        window->rect = RECT{-32000, -32000, -32000, -32000};
      }
      window->style |= WS_VISIBLE;
      deactivate = layer.active == window;
      break;
    case SW_SHOWMAXIMIZED: // SW_MAXIMIZE
      if (!window->maximized || window->minimized) {
        if (!window->minimized && !window->maximized) {
          window->restored = window->rect;
        }
        window->minimized = false;
        window->maximized = true;
        window->rect = MaximizedRectLocked(window);
      }
      window->style |= WS_VISIBLE;
      activate = topLevel;
      break;
    case SW_SHOWNORMAL: // SW_NORMAL
    case SW_RESTORE:
      if (window->minimized || window->maximized) {
        const bool fromMinimized = window->minimized;
        window->minimized = false;
        if (fromMinimized && window->maximized && command == SW_RESTORE) {
          window->rect = MaximizedRectLocked(window);
        } else {
          window->maximized = false;
          window->rect = window->restored;
        }
      }
      window->style |= WS_VISIBLE;
      activate = topLevel;
      break;
    case SW_SHOWNOACTIVATE:
    case SW_SHOWNA:
      window->style |= WS_VISIBLE;
      break;
    case SW_SHOW:
    case SW_SHOWDEFAULT:
    default:
      window->style |= WS_VISIBLE;
      activate = topLevel;
      break;
    }

    nowVisible = (window->style & WS_VISIBLE) != 0;
    stateChanged = wasMinimized != window->minimized || wasMaximized != window->maximized;
    const bool moved = before.left != window->rect.left || before.top != window->rect.top;
    const bool sized = Width(before) != Width(window->rect) || Height(before) != Height(window->rect);
    UINT flags = SWP_NOZORDER | SWP_NOACTIVATE;
    flags |= moved ? 0 : SWP_NOMOVE;
    flags |= (sized || stateChanged) ? 0 : SWP_NOSIZE;
    flags |= nowVisible != wasVisible ? (nowVisible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW) : 0;
    position = WINDOWPOS{
      handle, nullptr, window->rect.left, window->rect.top, Width(window->rect), Height(window->rect), flags
    };
    if (nowVisible && (!wasVisible || sized || stateChanged)) {
      InvalidateTreeLocked(window);
    }
  }

  if (nowVisible != wasVisible) {
    (void)::SendMessageW(handle, WM_SHOWWINDOW, nowVisible ? TRUE : FALSE, 0);
  }
  if ((position.flags & (SWP_NOMOVE | SWP_NOSIZE)) != (SWP_NOMOVE | SWP_NOSIZE) || nowVisible != wasVisible) {
    NotifyPosition(handle, position);
  }
  if (deactivate) {
    Activate(nullptr);
  } else if (activate && nowVisible) {
    (void)::SetActiveWindow(handle);
  }
  return wasVisible ? TRUE : FALSE;
}

BOOL IsIconic(const HWND handle)
{
  Lock lock(Layer().mutex);
  Window* const window = WindowLocked(handle);
  return window != nullptr && window->minimized ? TRUE : FALSE;
}

BOOL IsZoomed(const HWND handle)
{
  Lock lock(Layer().mutex);
  Window* const window = WindowLocked(handle);
  return window != nullptr && window->maximized && !window->minimized ? TRUE : FALSE;
}

BOOL SetWindowPos(const HWND handle, const HWND insertAfter, const int x, const int y, const int width, const int height, const UINT flags)
{
  State& layer = Layer();
  WINDOWPOS position{};
  bool visibilityChanged = false;
  bool nowVisible = false;
  bool activate = false;
  {
    Lock lock(layer.mutex);
    Window* const window = WindowLocked(handle);
    if (window == nullptr) {
      ::SetLastError(ERROR_INVALID_WINDOW_HANDLE);
      return FALSE;
    }
    const RECT before = window->rect;
    RECT rect = before;
    if ((flags & SWP_NOMOVE) == 0) {
      rect = RECT{x, y, x + Width(before), y + Height(before)};
    }
    if ((flags & SWP_NOSIZE) == 0) {
      rect.right = rect.left + std::max(width, 0);
      rect.bottom = rect.top + std::max(height, 0);
    }
    if (!window->minimized) {
      window->rect = rect;
      if (!window->maximized) {
        window->restored = rect;
      }
    }

    if ((flags & SWP_NOZORDER) == 0) {
      std::vector<Window*>& siblings = SiblingsLocked(window);
      siblings.erase(std::remove(siblings.begin(), siblings.end(), window), siblings.end());
      auto at = siblings.begin();
      if (insertAfter == HWND_BOTTOM) {
        at = siblings.end();
      } else if (insertAfter == HWND_TOPMOST) {
        window->exStyle |= WS_EX_TOPMOST;
      } else if (insertAfter == HWND_NOTOPMOST) {
        window->exStyle &= ~static_cast<DWORD>(WS_EX_TOPMOST);
      } else if (insertAfter != HWND_TOP) {
        Window* const after = WindowLocked(insertAfter);
        const auto found = std::find(siblings.begin(), siblings.end(), after);
        at = found != siblings.end() ? found + 1 : siblings.begin();
      }
      siblings.insert(at, window);
    }

    const bool wasVisible = (window->style & WS_VISIBLE) != 0;
    if ((flags & SWP_SHOWWINDOW) != 0) {
      window->style |= WS_VISIBLE;
    }
    if ((flags & SWP_HIDEWINDOW) != 0) {
      window->style &= ~static_cast<DWORD>(WS_VISIBLE);
    }
    nowVisible = (window->style & WS_VISIBLE) != 0;
    visibilityChanged = wasVisible != nowVisible;
    activate = (flags & SWP_NOACTIVATE) == 0 && window->parent == nullptr && nowVisible;

    const bool moved = before.left != window->rect.left || before.top != window->rect.top;
    const bool sized = Width(before) != Width(window->rect) || Height(before) != Height(window->rect);
    UINT notified = flags & ~static_cast<UINT>(SWP_NOMOVE | SWP_NOSIZE);
    notified |= moved ? 0 : SWP_NOMOVE;
    notified |= sized ? 0 : SWP_NOSIZE;
    position = WINDOWPOS{
      handle, insertAfter, window->rect.left, window->rect.top, Width(window->rect), Height(window->rect), notified
    };
    if (nowVisible && (sized || visibilityChanged) && (flags & SWP_NOREDRAW) == 0) {
      InvalidateTreeLocked(window);
    }
  }
  if ((position.flags & (SWP_NOMOVE | SWP_NOSIZE)) != (SWP_NOMOVE | SWP_NOSIZE) || visibilityChanged
      || (flags & SWP_FRAMECHANGED) != 0) {
    NotifyPosition(handle, position);
  }
  if (activate) {
    (void)::SetActiveWindow(handle);
  }
  return TRUE;
}

BOOL MoveWindow(const HWND handle, const int x, const int y, const int width, const int height, const BOOL repaint)
{
  return ::SetWindowPos(handle, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE | (repaint != FALSE ? 0 : SWP_NOREDRAW));
}

BOOL BringWindowToTop(const HWND handle)
{
  return ::SetWindowPos(handle, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
}

BOOL GetWindowRect(const HWND handle, RECT* const rect)
{
  Lock lock(Layer().mutex);
  Window* const window = WindowLocked(handle);
  if (window == nullptr || rect == nullptr) {
    ::SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    return FALSE;
  }
  const POINT origin = ScreenOriginLocked(window);
  *rect = RECT{origin.x, origin.y, origin.x + Width(window->rect), origin.y + Height(window->rect)};
  return TRUE;
}

BOOL GetClientRect(const HWND handle, RECT* const rect)
{
  Lock lock(Layer().mutex);
  Window* const window = WindowLocked(handle);
  if (window == nullptr || rect == nullptr) {
    ::SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    return FALSE;
  }
  *rect = RECT{0, 0, Width(window->rect), Height(window->rect)};
  return TRUE;
}

BOOL AdjustWindowRectEx(RECT* const rect, DWORD, BOOL, DWORD)
{
  return rect != nullptr ? TRUE : FALSE;
}

BOOL ClientToScreen(const HWND handle, POINT* const point)
{
  Lock lock(Layer().mutex);
  Window* const window = WindowLocked(handle);
  if (window == nullptr || point == nullptr) {
    return FALSE;
  }
  const POINT origin = ScreenOriginLocked(window);
  point->x += origin.x;
  point->y += origin.y;
  return TRUE;
}

BOOL ScreenToClient(const HWND handle, POINT* const point)
{
  Lock lock(Layer().mutex);
  Window* const window = WindowLocked(handle);
  if (window == nullptr || point == nullptr) {
    return FALSE;
  }
  const POINT origin = ScreenOriginLocked(window);
  point->x -= origin.x;
  point->y -= origin.y;
  return TRUE;
}

int MapWindowPoints(const HWND from, const HWND to, POINT* const points, const UINT count)
{
  Lock lock(Layer().mutex);
  Window* const fromWindow = from != nullptr ? WindowLocked(from) : nullptr;
  Window* const toWindow = to != nullptr ? WindowLocked(to) : nullptr;
  if ((from != nullptr && fromWindow == nullptr) || (to != nullptr && toWindow == nullptr)) {
    ::SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    return 0;
  }
  const POINT fromOrigin = fromWindow != nullptr ? ScreenOriginLocked(fromWindow) : POINT{0, 0};
  const POINT toOrigin = toWindow != nullptr ? ScreenOriginLocked(toWindow) : POINT{0, 0};
  const int dx = fromOrigin.x - toOrigin.x;
  const int dy = fromOrigin.y - toOrigin.y;
  for (UINT i = 0; points != nullptr && i < count; ++i) {
    points[i].x += dx;
    points[i].y += dy;
  }
  ::SetLastError(ERROR_SUCCESS);
  return MAKELONG(static_cast<WORD>(dx), static_cast<WORD>(dy));
}

HWND WindowFromPoint(const POINT point)
{
  Lock lock(Layer().mutex);
  return Handle(WindowAtLocked(Layer().topLevel, point, POINT{0, 0}));
}

BOOL SetWindowTextW(const HWND handle, const wchar_t* const text)
{
  if (!::IsWindow(handle)) {
    ::SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    return FALSE;
  }
  return static_cast<BOOL>(::SendMessageW(handle, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(text != nullptr ? text : L"")));
}

BOOL SetWindowTextA(const HWND handle, const char* const text)
{
  const std::wstring wide = Wide(text);
  return ::SetWindowTextW(handle, wide.c_str());
}

int GetWindowTextW(const HWND handle, wchar_t* const text, const int maxCount)
{
  if (text == nullptr || maxCount <= 0) {
    return 0;
  }
  text[0] = 0;
  return static_cast<int>(::SendMessageW(handle, WM_GETTEXT, static_cast<WPARAM>(maxCount), reinterpret_cast<LPARAM>(text)));
}

int GetWindowTextLengthW(const HWND handle)
{
  return static_cast<int>(::SendMessageW(handle, WM_GETTEXTLENGTH, 0, 0));
}

// --- Painting ---------------------------------------------------------------------------------------

BOOL InvalidateRect(const HWND handle, const RECT* const rect, const BOOL erase)
{
  State& layer = Layer();
  Lock lock(layer.mutex);
  if (handle == nullptr) {
    for (Window* top : layer.topLevel) {
      InvalidateTreeLocked(top);
    }
    return TRUE;
  }
  Window* const window = WindowLocked(handle);
  if (window == nullptr) {
    ::SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    return FALSE;
  }
  const RECT client{0, 0, Width(window->rect), Height(window->rect)};
  window->update = window->updatePending ? Union(window->update, rect != nullptr ? *rect : client) : (rect != nullptr ? *rect : client);
  window->updatePending = true;
  window->erasePending = window->erasePending || erase != FALSE;
  Wake(*window->queue);
  return TRUE;
}

BOOL ValidateRect(const HWND handle, const RECT* const rect)
{
  Lock lock(Layer().mutex);
  Window* const window = WindowLocked(handle);
  if (window == nullptr) {
    return FALSE;
  }
  const bool covers = rect == nullptr
                   || (rect->left <= window->update.left && rect->top <= window->update.top
                       && rect->right >= window->update.right && rect->bottom >= window->update.bottom);
  if (covers) {
    window->updatePending = false;
    window->erasePending = false;
    window->update = RECT{};
  }
  return TRUE;
}

BOOL GetUpdateRect(const HWND handle, RECT* const rect, BOOL)
{
  Lock lock(Layer().mutex);
  Window* const window = WindowLocked(handle);
  if (window == nullptr || !window->updatePending) {
    if (rect != nullptr) {
      *rect = RECT{};
    }
    return FALSE;
  }
  if (rect != nullptr) {
    *rect = window->update;
  }
  return TRUE;
}

BOOL UpdateWindow(const HWND handle)
{
  bool paint = false;
  {
    Lock lock(Layer().mutex);
    Window* const window = WindowLocked(handle);
    if (window == nullptr) {
      return FALSE;
    }
    paint = window->updatePending && VisibleLocked(window);
  }
  if (paint) {
    (void)::SendMessageW(handle, WM_PAINT, 0, 0);
  }
  return TRUE;
}

BOOL RedrawWindow(const HWND handle, const RECT* const rect, void*, const UINT flags)
{
  if ((flags & RDW_INVALIDATE) != 0) {
    if ((flags & RDW_ALLCHILDREN) != 0) {
      Lock lock(Layer().mutex);
      if (Window* const window = WindowLocked(handle)) {
        InvalidateTreeLocked(window);
      }
    } else {
      (void)::InvalidateRect(handle, rect, (flags & RDW_ERASE) != 0 ? TRUE : FALSE);
    }
  }
  if ((flags & RDW_UPDATENOW) != 0) {
    (void)::UpdateWindow(handle);
  }
  return TRUE;
}

HDC BeginPaint(const HWND handle, PAINTSTRUCT* const paint)
{
  State& layer = Layer();
  HDC dc = nullptr;
  bool erase = false;
  {
    Lock lock(layer.mutex);
    Window* const window = WindowLocked(handle);
    if (window == nullptr || paint == nullptr) {
      ::SetLastError(ERROR_INVALID_WINDOW_HANDLE);
      return nullptr;
    }
    auto* const context = new DeviceContext{window};
    layer.deviceContexts.insert(context);
    dc = reinterpret_cast<HDC>(context);
    *paint = PAINTSTRUCT{};
    paint->hdc = dc;
    paint->rcPaint = window->updatePending ? window->update : RECT{};
    erase = window->erasePending;
    window->updatePending = false;
    window->erasePending = false;
    window->update = RECT{};
  }
  if (erase) {
    // fErase: the background is still to be erased (nobody did it on WM_ERASEBKGND).
    paint->fErase = ::SendMessageW(handle, WM_ERASEBKGND, reinterpret_cast<WPARAM>(dc), 0) == 0 ? TRUE : FALSE;
  }
  return dc;
}

BOOL EndPaint(const HWND, const PAINTSTRUCT* const paint)
{
  if (paint == nullptr) {
    return FALSE;
  }
  (void)::ReleaseDC(nullptr, paint->hdc);
  return TRUE;
}

HDC GetDC(const HWND handle)
{
  State& layer = Layer();
  Lock lock(layer.mutex);
  Window* const window = handle != nullptr ? WindowLocked(handle) : nullptr;
  if (handle != nullptr && window == nullptr) {
    ::SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    return nullptr;
  }
  auto* const context = new DeviceContext{window};
  layer.deviceContexts.insert(context);
  return reinterpret_cast<HDC>(context);
}

int ReleaseDC(const HWND, const HDC dc)
{
  State& layer = Layer();
  Lock lock(layer.mutex);
  auto* const context = reinterpret_cast<DeviceContext*>(dc);
  if (layer.deviceContexts.erase(context) == 0) {
    return 0;
  }
  delete context;
  return 1;
}

HWND WindowFromDC(const HDC dc)
{
  State& layer = Layer();
  Lock lock(layer.mutex);
  auto* const context = reinterpret_cast<DeviceContext*>(dc);
  if (layer.deviceContexts.count(context) == 0 || context->window == nullptr || WindowLocked(Handle(context->window)) == nullptr) {
    return nullptr;
  }
  return Handle(context->window);
}

// --- Messages ---------------------------------------------------------------------------------------

BOOL PeekMessageW(MSG* const message, const HWND window, const UINT filterMin, const UINT filterMax, const UINT remove)
{
  if (message == nullptr) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  return PeekCore(message, window, filterMin, filterMax, remove);
}

BOOL GetMessageW(MSG* const message, const HWND window, const UINT filterMin, const UINT filterMax)
{
  if (message == nullptr) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return -1;
  }
  State& layer = Layer();
  for (;;) {
    if (PeekCore(message, window, filterMin, filterMax, PM_REMOVE)) {
      return message->message == WM_QUIT ? FALSE : TRUE;
    }
    HANDLE wake = nullptr;
    {
      Lock lock(layer.mutex);
      ThreadQueue& queue = CurrentQueueLocked();
      if (HasMessageLocked(queue, window, filterMin, filterMax, QS_ALLINPUT)) {
        continue;
      }
      wake = queue.wake;
    }
    (void)::WaitForSingleObject(wake, INFINITE);
  }
}

DWORD MsgWaitForMultipleObjectsEx(
  const DWORD count, const HANDLE* const handles, const DWORD milliseconds, const DWORD wakeMask, const DWORD flags
)
{
  if ((flags & MWMO_WAITALL) != 0) {
    return ::WaitForMultipleObjects(count, handles, TRUE, milliseconds);
  }
  State& layer = Layer();
  std::vector<HANDLE> all(handles, handles + count);
  {
    Lock lock(layer.mutex);
    ThreadQueue& queue = CurrentQueueLocked();
    if ((flags & MWMO_INPUTAVAILABLE) != 0 && HasMessageLocked(queue, nullptr, 0, 0, wakeMask)) {
      return WAIT_OBJECT_0 + count;
    }
    all.push_back(queue.wake);
  }
  const DWORD start = ::GetTickCount();
  for (;;) {
    DWORD remaining = INFINITE;
    if (milliseconds != INFINITE) {
      const DWORD elapsed = ::GetTickCount() - start;
      remaining = elapsed >= milliseconds ? 0 : milliseconds - elapsed;
    }
    const DWORD result = ::WaitForMultipleObjects(count + 1, all.data(), FALSE, remaining);
    if (result != WAIT_OBJECT_0 + count) {
      return result;
    }
    // Something arrived; a kind outside `wakeMask` waits on.
    Lock lock(layer.mutex);
    ThreadQueue& queue = CurrentQueueLocked();
    if (HasMessageLocked(queue, nullptr, 0, 0, wakeMask)) {
      return WAIT_OBJECT_0 + count;
    }
    (void)::ResetEvent(queue.wake);
    if (remaining == 0) {
      return WAIT_TIMEOUT;
    }
  }
}

BOOL PostMessageW(const HWND handle, const UINT message, const WPARAM wParam, const LPARAM lParam)
{
  State& layer = Layer();
  Lock lock(layer.mutex);
  if (handle == nullptr) {
    PostLocked(CurrentQueueLocked(), nullptr, message, wParam, lParam);
    return TRUE;
  }
  Window* const window = WindowLocked(handle);
  if (window == nullptr) {
    ::SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    return FALSE;
  }
  PostLocked(*window->queue, handle, message, wParam, lParam);
  return TRUE;
}

BOOL PostThreadMessageW(const DWORD threadId, const UINT message, const WPARAM wParam, const LPARAM lParam)
{
  State& layer = Layer();
  Lock lock(layer.mutex);
  const auto found = layer.queues.find(threadId);
  if (found == layer.queues.end()) {
    ::SetLastError(1444u); // ERROR_INVALID_THREAD_ID
    return FALSE;
  }
  PostLocked(*found->second, nullptr, message, wParam, lParam);
  return TRUE;
}

LRESULT SendMessageW(const HWND handle, const UINT message, const WPARAM wParam, const LPARAM lParam)
{
  State& layer = Layer();
  Lock lock(layer.mutex);
  Window* const window = WindowLocked(handle);
  if (window == nullptr) {
    ::SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    return 0;
  }
  if (OnThisThread(window)) {
    const WNDPROC procedure = window->procedure;
    lock.unlock();
    return procedure != nullptr ? procedure(handle, message, wParam, lParam) : 0;
  }

  auto sent = std::make_shared<Sent>();
  sent->window = handle;
  sent->message = message;
  sent->wParam = wParam;
  sent->lParam = lParam;
  window->queue->sent.push_back(sent);
  Wake(*window->queue);
  // While it waits, the thread takes what is sent to it, so two threads that send to each other
  // go on, as on Windows.
  ThreadQueue& own = CurrentQueueLocked();
  while (!sent->done) {
    if (!own.sent.empty()) {
      lock.unlock();
      ProcessSent();
      lock.lock();
      continue;
    }
    layer.sentDone.wait(lock);
  }
  return sent->result;
}

BOOL SendNotifyMessageW(const HWND handle, const UINT message, const WPARAM wParam, const LPARAM lParam)
{
  State& layer = Layer();
  Lock lock(layer.mutex);
  Window* const window = WindowLocked(handle);
  if (window == nullptr) {
    ::SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    return FALSE;
  }
  if (OnThisThread(window)) {
    lock.unlock();
    (void)::SendMessageW(handle, message, wParam, lParam);
    return TRUE;
  }
  auto sent = std::make_shared<Sent>();
  sent->window = handle;
  sent->message = message;
  sent->wParam = wParam;
  sent->lParam = lParam;
  window->queue->sent.push_back(std::move(sent));
  Wake(*window->queue);
  return TRUE;
}

BOOL TranslateMessage(const MSG* const message)
{
  if (message == nullptr || (message->message != WM_KEYDOWN && message->message != WM_SYSKEYDOWN)) {
    return FALSE;
  }
  State& layer = Layer();
  Lock lock(layer.mutex);
  ThreadQueue& queue = CurrentQueueLocked();
  const MSG& last = queue.lastKeyDown;
  if (queue.lastKeyCharacter == 0 || last.hwnd != message->hwnd || last.message != message->message
      || last.wParam != message->wParam || last.lParam != message->lParam || last.time != message->time) {
    return FALSE;
  }
  // The character comes next, ahead of later input, as Windows queues it.
  Posted character;
  character.msg = MSG{
    message->hwnd, message->message == WM_KEYDOWN ? WM_CHAR : WM_SYSCHAR, static_cast<WPARAM>(queue.lastKeyCharacter),
    message->lParam, message->time, message->pt
  };
  queue.posted.push_front(character);
  queue.lastKeyCharacter = 0;
  Wake(queue);
  return TRUE;
}

LRESULT DispatchMessageW(const MSG* const message)
{
  if (message == nullptr || message->hwnd == nullptr) {
    return 0;
  }
  WNDPROC procedure = nullptr;
  {
    Lock lock(Layer().mutex);
    Window* const window = WindowLocked(message->hwnd);
    if (window == nullptr) {
      ::SetLastError(ERROR_INVALID_WINDOW_HANDLE);
      return 0;
    }
    procedure = window->procedure;
  }
  return procedure != nullptr ? procedure(message->hwnd, message->message, message->wParam, message->lParam) : 0;
}

void PostQuitMessage(const int exitCode)
{
  State& layer = Layer();
  Lock lock(layer.mutex);
  ThreadQueue& queue = CurrentQueueLocked();
  queue.quit = true;
  queue.quitCode = exitCode;
  Wake(queue);
}

LRESULT CallWindowProcW(const WNDPROC procedure, const HWND handle, const UINT message, const WPARAM wParam, const LPARAM lParam)
{
  return procedure != nullptr ? procedure(handle, message, wParam, lParam) : 0;
}

LONG GetMessageTime()
{
  Lock lock(Layer().mutex);
  return CurrentQueueLocked().messageTime;
}

DWORD GetMessagePos()
{
  Lock lock(Layer().mutex);
  return CurrentQueueLocked().messagePos;
}

BOOL InSendMessage()
{
  Lock lock(Layer().mutex);
  return CurrentQueueLocked().inSend > 0 ? TRUE : FALSE;
}

LRESULT DefWindowProcW(const HWND handle, const UINT message, const WPARAM wParam, const LPARAM lParam)
{
  State& layer = Layer();
  Window* window = nullptr;
  {
    Lock lock(layer.mutex);
    window = WindowLocked(handle);
  }
  if (window == nullptr) {
    return 0;
  }

  switch (message) {
  case WM_NCCREATE: {
    const auto* const create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
    if (create != nullptr && create->lpszName != nullptr && !IS_INTRESOURCE(create->lpszName)) {
      Lock lock(layer.mutex);
      window->text = create->lpszName;
    }
    return TRUE;
  }
  case WM_CLOSE:
    (void)::DestroyWindow(handle);
    return 0;
  case WM_PAINT: {
    PAINTSTRUCT paint;
    (void)::BeginPaint(handle, &paint);
    (void)::EndPaint(handle, &paint);
    return 0;
  }
  case WM_ERASEBKGND: {
    // A class brush erases (there is nothing to draw it on): the background counts as erased.
    Lock lock(layer.mutex);
    return window->windowClass != nullptr && window->windowClass->background != nullptr ? 1 : 0;
  }
  case WM_SETCURSOR:
    return DefaultSetCursor(window, handle, wParam, lParam);
  case WM_SETTEXT: {
    Lock lock(layer.mutex);
    window->text = lParam != 0 ? reinterpret_cast<const wchar_t*>(lParam) : L"";
    return TRUE;
  }
  case WM_GETTEXT: {
    Lock lock(layer.mutex);
    auto* const out = reinterpret_cast<wchar_t*>(lParam);
    if (out == nullptr || wParam == 0) {
      return 0;
    }
    const std::size_t count = std::min<std::size_t>(window->text.size(), static_cast<std::size_t>(wParam) - 1);
    std::wmemcpy(out, window->text.c_str(), count);
    out[count] = 0;
    return static_cast<LRESULT>(count);
  }
  case WM_GETTEXTLENGTH: {
    Lock lock(layer.mutex);
    return static_cast<LRESULT>(window->text.size());
  }
  case WM_WINDOWPOSCHANGED: {
    const auto* const position = reinterpret_cast<const WINDOWPOS*>(lParam);
    if (position == nullptr) {
      return 0;
    }
    WPARAM sizeType = SIZE_RESTORED;
    {
      Lock lock(layer.mutex);
      sizeType = window->minimized ? SIZE_MINIMIZED : (window->maximized ? SIZE_MAXIMIZED : SIZE_RESTORED);
    }
    if ((position->flags & SWP_NOMOVE) == 0) {
      (void)::SendMessageW(handle, WM_MOVE, 0, MAKELPARAM(position->x, position->y));
    }
    if ((position->flags & SWP_NOSIZE) == 0) {
      const int width = sizeType == SIZE_MINIMIZED ? 0 : position->cx;
      const int height = sizeType == SIZE_MINIMIZED ? 0 : position->cy;
      (void)::SendMessageW(handle, WM_SIZE, sizeType, MAKELPARAM(width, height));
    }
    return 0;
  }
  case WM_SYSCOMMAND:
    switch (wParam & 0xFFF0u) {
    case SC_CLOSE:
      (void)::SendMessageW(handle, WM_CLOSE, 0, 0);
      return 0;
    case SC_MINIMIZE:
      (void)::ShowWindow(handle, SW_MINIMIZE);
      return 0;
    case SC_MAXIMIZE:
      (void)::ShowWindow(handle, SW_MAXIMIZE);
      return 0;
    case SC_RESTORE:
      (void)::ShowWindow(handle, SW_RESTORE);
      return 0;
    default:
      // SC_KEYMENU, SC_SIZE, SC_MOVE: there is no menu and no interactive sizing here.
      return 0;
    }
  case WM_SYSKEYDOWN:
    // Alt+F4 closes the top-level window.
    if (wParam == VK_F4 && (lParam & (1 << 29)) != 0) {
      (void)::PostMessageW(::GetAncestor(handle, GA_ROOT), WM_SYSCOMMAND, SC_CLOSE, 0);
    }
    return 0;
  case WM_SYSKEYUP:
    // Alt alone, or F10, asks for the menu (SC_KEYMENU).
    if (wParam == VK_MENU || wParam == VK_F10) {
      (void)::SendMessageW(::GetAncestor(handle, GA_ROOT), WM_SYSCOMMAND, SC_KEYMENU, 0);
    }
    return 0;
  case WM_SYSCHAR:
    if (wParam != VK_SPACE) {
      (void)::SendMessageW(::GetAncestor(handle, GA_ROOT), WM_SYSCOMMAND, SC_KEYMENU, static_cast<LPARAM>(wParam));
    }
    return 0;
  case WM_MOUSEWHEEL: {
    // The wheel goes up to the parent while nobody takes it.
    HWND parent = nullptr;
    {
      Lock lock(layer.mutex);
      parent = window->parent != nullptr ? Handle(window->parent) : nullptr;
    }
    return parent != nullptr ? ::SendMessageW(parent, WM_MOUSEWHEEL, wParam, lParam) : 0;
  }
  case WM_ACTIVATE:
    if (LOWORD(wParam) != WA_INACTIVE && HIWORD(wParam) == 0) {
      (void)::SetFocus(handle);
    }
    return 0;
  case WM_MOUSEACTIVATE:
    return 1; // MA_ACTIVATE
  case WM_NCHITTEST: {
    RECT rect{};
    (void)::GetWindowRect(handle, &rect);
    return Contains(rect, POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}) ? HTCLIENT : HTNOWHERE;
  }
  case WM_QUERYENDSESSION:
    return TRUE;
  default:
    return 0;
  }
}

// --- Keyboard and mouse -----------------------------------------------------------------------------

HWND SetFocus(const HWND handle)
{
  State& layer = Layer();
  Window* previous = nullptr;
  Window* next = nullptr;
  HWND activateRoot = nullptr;
  {
    Lock lock(layer.mutex);
    previous = layer.focus;
    if (handle != nullptr) {
      next = WindowLocked(handle);
      if (next == nullptr) {
        ::SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return nullptr;
      }
      if (!OnThisThread(next)) {
        ::SetLastError(ERROR_ACCESS_DENIED);
        return nullptr;
      }
      if ((next->style & WS_DISABLED) != 0) {
        return nullptr;
      }
      Window* const root = RootLocked(next);
      if (layer.active != root) {
        activateRoot = Handle(root);
      }
    }
    if (previous == next) {
      return Handle(previous);
    }
  }
  if (activateRoot != nullptr) {
    // Activating the window sets the focus to it (DefWindowProc, wx); the focus then moves on.
    (void)::SetActiveWindow(activateRoot);
  }
  {
    Lock lock(layer.mutex);
    previous = layer.focus;
    if (previous == next) {
      return Handle(previous);
    }
    layer.focus = next;
  }
  if (previous != nullptr && ::IsWindow(Handle(previous))) {
    (void)::SendMessageW(Handle(previous), WM_KILLFOCUS, reinterpret_cast<WPARAM>(Handle(next)), 0);
  }
  if (next != nullptr) {
    (void)::SendMessageW(Handle(next), WM_SETFOCUS, reinterpret_cast<WPARAM>(Handle(previous)), 0);
  }
  return Handle(previous);
}

HWND GetFocus()
{
  Lock lock(Layer().mutex);
  return Handle(Layer().focus);
}

HWND SetActiveWindow(const HWND handle)
{
  State& layer = Layer();
  Window* root = nullptr;
  Window* previous = nullptr;
  {
    Lock lock(layer.mutex);
    previous = layer.active;
    if (handle != nullptr) {
      Window* const window = WindowLocked(handle);
      if (window == nullptr) {
        ::SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return nullptr;
      }
      root = RootLocked(window);
    }
  }
  Activate(root);
  return Handle(previous);
}

HWND GetActiveWindow()
{
  Lock lock(Layer().mutex);
  return Handle(Layer().active);
}

HWND GetForegroundWindow()
{
  Lock lock(Layer().mutex);
  return Layer().foreground ? Handle(Layer().active) : nullptr;
}

BOOL SetForegroundWindow(const HWND handle)
{
  {
    Lock lock(Layer().mutex);
    if (!Layer().foreground || WindowLocked(handle) == nullptr) {
      return FALSE;
    }
  }
  (void)::SetActiveWindow(handle);
  return TRUE;
}

SHORT GetKeyState(const int virtualKey)
{
  Lock lock(Layer().mutex);
  const BYTE state = Layer().keys[virtualKey & 0xFF];
  return static_cast<SHORT>(((state & 0x80) != 0 ? 0x8000 : 0) | (state & 0x01));
}

SHORT GetAsyncKeyState(const int virtualKey)
{
  Lock lock(Layer().mutex);
  return (Layer().asyncKeys[virtualKey & 0xFF] & 0x80) != 0 ? static_cast<SHORT>(0x8000) : 0;
}

HWND SetCapture(const HWND handle)
{
  State& layer = Layer();
  Window* previous = nullptr;
  {
    Lock lock(layer.mutex);
    Window* const window = WindowLocked(handle);
    if (window == nullptr) {
      return nullptr;
    }
    previous = layer.capture;
    layer.capture = window;
  }
  if (previous != nullptr && previous != static_cast<Window*>(handle) && ::IsWindow(Handle(previous))) {
    (void)::SendMessageW(Handle(previous), WM_CAPTURECHANGED, 0, reinterpret_cast<LPARAM>(handle));
  }
  return Handle(previous);
}

BOOL ReleaseCapture()
{
  State& layer = Layer();
  Window* previous = nullptr;
  {
    Lock lock(layer.mutex);
    previous = layer.capture;
    layer.capture = nullptr;
  }
  if (previous != nullptr && ::IsWindow(Handle(previous))) {
    (void)::SendMessageW(Handle(previous), WM_CAPTURECHANGED, 0, 0);
  }
  return TRUE;
}

HWND GetCapture()
{
  Lock lock(Layer().mutex);
  return Handle(Layer().capture);
}

BOOL GetCursorPos(POINT* const point)
{
  if (point == nullptr) {
    return FALSE;
  }
  Lock lock(Layer().mutex);
  *point = Layer().cursor;
  return TRUE;
}

BOOL SetCursorPos(const int x, const int y)
{
  State& layer = Layer();
  Lock lock(layer.mutex);
  layer.cursor = ClampToClipLocked(POINT{x, y});
  PostMouseLocked(WM_MOUSEMOVE, 0);
  return TRUE;
}

HCURSOR SetCursor(const HCURSOR cursor)
{
  Lock lock(Layer().mutex);
  const HCURSOR previous = Layer().cursorShape;
  Layer().cursorShape = cursor;
  return previous;
}

HCURSOR GetCursor()
{
  Lock lock(Layer().mutex);
  return Layer().cursorShape;
}

int ShowCursor(const BOOL show)
{
  Lock lock(Layer().mutex);
  Layer().cursorCount += show != FALSE ? 1 : -1;
  return Layer().cursorCount;
}

BOOL ClipCursor(const RECT* const rect)
{
  State& layer = Layer();
  Lock lock(layer.mutex);
  layer.clipped = rect != nullptr;
  if (rect != nullptr) {
    layer.clip = *rect;
    layer.cursor = ClampToClipLocked(layer.cursor);
  }
  return TRUE;
}

BOOL GetClipCursor(RECT* const rect)
{
  if (rect == nullptr) {
    return FALSE;
  }
  Lock lock(Layer().mutex);
  *rect = Layer().clipped ? Layer().clip : ScreenLocked();
  return TRUE;
}

UINT GetDoubleClickTime()
{
  return kDoubleClickTime;
}

// --- Hooks ------------------------------------------------------------------------------------------

HHOOK SetWindowsHookExW(const int hookId, const HOOKPROC procedure, HINSTANCE, DWORD)
{
  if (hookId != WH_KEYBOARD_LL || procedure == nullptr) {
    ::SetLastError(ERROR_NOT_SUPPORTED);
    return nullptr;
  }
  State& layer = Layer();
  Lock lock(layer.mutex);
  auto hook = std::make_unique<Hook>();
  hook->procedure = procedure;
  const HHOOK handle = reinterpret_cast<HHOOK>(hook.get());
  layer.keyboardHooks.insert(layer.keyboardHooks.begin(), std::move(hook));
  return handle;
}

BOOL UnhookWindowsHookEx(const HHOOK handle)
{
  State& layer = Layer();
  Lock lock(layer.mutex);
  auto& hooks = layer.keyboardHooks;
  const auto found = std::find_if(hooks.begin(), hooks.end(), [handle](const auto& h) { return reinterpret_cast<HHOOK>(h.get()) == handle; });
  if (found == hooks.end()) {
    ::SetLastError(1404u); // ERROR_INVALID_HOOK_HANDLE
    return FALSE;
  }
  hooks.erase(found);
  return TRUE;
}

LRESULT CallNextHookEx(const HHOOK handle, const int code, const WPARAM wParam, const LPARAM lParam)
{
  HOOKPROC next = nullptr;
  {
    Lock lock(Layer().mutex);
    auto& hooks = Layer().keyboardHooks;
    const auto found = std::find_if(hooks.begin(), hooks.end(), [handle](const auto& h) { return reinterpret_cast<HHOOK>(h.get()) == handle; });
    if (found != hooks.end() && found + 1 != hooks.end()) {
      next = (*(found + 1))->procedure;
    }
  }
  return next != nullptr ? next(code, wParam, lParam) : 0;
}

// --- The screen, menus, system settings -------------------------------------------------------------

HMONITOR MonitorFromPoint(const POINT point, const DWORD flags)
{
  Lock lock(Layer().mutex);
  if (flags == MONITOR_DEFAULTTONULL && !Contains(ScreenLocked(), point)) {
    return nullptr;
  }
  return reinterpret_cast<HMONITOR>(&kMonitor);
}

HMONITOR MonitorFromWindow(const HWND handle, const DWORD flags)
{
  RECT rect{};
  if (::GetWindowRect(handle, &rect) == FALSE) {
    return flags == MONITOR_DEFAULTTONULL ? nullptr : reinterpret_cast<HMONITOR>(&kMonitor);
  }
  return ::MonitorFromPoint(POINT{(rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2}, flags);
}

BOOL GetMonitorInfoW(const HMONITOR monitor, MONITORINFO* const info)
{
  if (monitor != reinterpret_cast<HMONITOR>(&kMonitor) || info == nullptr || info->cbSize < sizeof(MONITORINFO)) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  Lock lock(Layer().mutex);
  info->rcMonitor = ScreenLocked();
  info->rcWork = ScreenLocked();
  info->dwFlags = MONITORINFOF_PRIMARY;
  return TRUE;
}

int GetSystemMetrics(const int index)
{
  Lock lock(Layer().mutex);
  switch (index) {
  case SM_CXSCREEN:
    return Layer().screenWidth;
  case SM_CYSCREEN:
    return Layer().screenHeight;
  case SM_CXDOUBLECLK:
  case SM_CYDOUBLECLK:
    return kDoubleClickDistance;
  case SM_CMONITORS:
    return 1;
  default:
    return 0;
  }
}

BOOL SystemParametersInfoW(const UINT action, UINT, void* const pvParam, UINT)
{
  switch (action) {
  case SPI_GETSCREENSAVEACTIVE:
    if (pvParam == nullptr) {
      ::SetLastError(ERROR_INVALID_PARAMETER);
      return FALSE;
    }
    *static_cast<BOOL*>(pvParam) = FALSE;
    return TRUE;
  case SPI_SETSCREENSAVEACTIVE:
    return TRUE;
  default:
    ::SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
  }
}

HMENU GetSystemMenu(HWND, BOOL)
{
  return nullptr;
}

BOOL AppendMenuW(const HMENU menu, UINT, UINT_PTR, const wchar_t*)
{
  if (menu == nullptr) {
    ::SetLastError(ERROR_INVALID_MENU_HANDLE);
  }
  return FALSE;
}

// --- Message boxes and dialogs ----------------------------------------------------------------------

int MessageBoxW(HWND, const wchar_t* const text, const wchar_t* const caption, UINT)
{
  LogBox(caption, text);
  ::SetLastError(ERROR_NOT_SUPPORTED);
  return 0;
}

int MessageBoxA(const HWND owner, const char* const text, const char* const caption, const UINT type)
{
  return ::MessageBoxW(owner, Wide(text).c_str(), Wide(caption).c_str(), type);
}

INT_PTR DialogBoxParamW(HINSTANCE, const wchar_t*, HWND, DLGPROC, LPARAM)
{
  ::SetLastError(ERROR_RESOURCE_NAME_NOT_FOUND);
  return -1;
}

BOOL EndDialog(HWND, INT_PTR)
{
  // No dialog exists: DialogBoxParamW makes none.
  ::SetLastError(ERROR_INVALID_WINDOW_HANDLE);
  return FALSE;
}

HWND GetDlgItem(const HWND dialog, const int id)
{
  Lock lock(Layer().mutex);
  Window* const window = WindowLocked(dialog);
  if (window == nullptr) {
    ::SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    return nullptr;
  }
  for (Window* child : window->children) {
    if (child->id == id) {
      return Handle(child);
    }
  }
  ::SetLastError(1421u); // ERROR_CONTROL_ID_NOT_FOUND
  return nullptr;
}

BOOL SetDlgItemTextW(const HWND dialog, const int id, const wchar_t* const text)
{
  const HWND item = ::GetDlgItem(dialog, id);
  return item != nullptr ? ::SetWindowTextW(item, text) : FALSE;
}

// --- Global memory and the clipboard ----------------------------------------------------------------

HGLOBAL GlobalAlloc(const UINT flags, const SIZE_T bytes)
{
  auto block = std::make_unique<GlobalBlock>();
  block->data.reset(new (std::nothrow) unsigned char[std::max<SIZE_T>(bytes, 1)]);
  if (!block->data) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return nullptr;
  }
  if ((flags & GMEM_ZEROINIT) != 0) {
    std::memset(block->data.get(), 0, std::max<SIZE_T>(bytes, 1));
  }
  block->size = bytes;
  block->flags = flags;
  // A fixed block's handle is its memory, a moveable one's is the block, as on Windows.
  const HGLOBAL handle = (flags & GMEM_MOVEABLE) != 0 ? static_cast<HGLOBAL>(block.get()) : static_cast<HGLOBAL>(block->data.get());
  Lock lock(Layer().mutex);
  Layer().globals.emplace(handle, std::move(block));
  return handle;
}

LPVOID GlobalLock(const HGLOBAL memory)
{
  Lock lock(Layer().mutex);
  const auto found = Layer().globals.find(memory);
  if (found == Layer().globals.end()) {
    ::SetLastError(ERROR_INVALID_HANDLE);
    return nullptr;
  }
  ++found->second->locks;
  return found->second->data.get();
}

BOOL GlobalUnlock(const HGLOBAL memory)
{
  Lock lock(Layer().mutex);
  const auto found = Layer().globals.find(memory);
  if (found == Layer().globals.end()) {
    ::SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  if (found->second->locks > 0) {
    --found->second->locks;
  }
  ::SetLastError(NO_ERROR);
  return found->second->locks > 0 ? TRUE : FALSE;
}

HGLOBAL GlobalFree(const HGLOBAL memory)
{
  Lock lock(Layer().mutex);
  if (Layer().globals.erase(memory) == 0) {
    ::SetLastError(ERROR_INVALID_HANDLE);
    return memory;
  }
  return nullptr;
}

SIZE_T GlobalSize(const HGLOBAL memory)
{
  Lock lock(Layer().mutex);
  const auto found = Layer().globals.find(memory);
  return found != Layer().globals.end() ? found->second->size : 0;
}

BOOL OpenClipboard(HWND)
{
  State& layer = Layer();
  Lock lock(layer.mutex);
  if (layer.clipboardOpen && layer.clipboardThread != ::GetCurrentThreadId()) {
    ::SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
  }
  layer.clipboardOpen = true;
  layer.clipboardThread = ::GetCurrentThreadId();
  return TRUE;
}

namespace
{
  bool ClipboardOpenHereLocked()
  {
    if (!Layer().clipboardOpen || Layer().clipboardThread != ::GetCurrentThreadId()) {
      ::SetLastError(1418u); // ERROR_CLIPBOARD_NOT_OPEN
      return false;
    }
    return true;
  }
} // namespace

BOOL CloseClipboard()
{
  Lock lock(Layer().mutex);
  if (!ClipboardOpenHereLocked()) {
    return FALSE;
  }
  Layer().clipboardOpen = false;
  return TRUE;
}

BOOL EmptyClipboard()
{
  State& layer = Layer();
  Lock lock(layer.mutex);
  if (!ClipboardOpenHereLocked()) {
    return FALSE;
  }
  for (const auto& entry : layer.clipboard) {
    layer.globals.erase(entry.second);
  }
  layer.clipboard.clear();
  return TRUE;
}

HANDLE SetClipboardData(const UINT format, const HANDLE data)
{
  State& layer = Layer();
  Lock lock(layer.mutex);
  if (!ClipboardOpenHereLocked()) {
    return nullptr;
  }
  if (layer.globals.count(data) == 0) {
    ::SetLastError(ERROR_INVALID_HANDLE);
    return nullptr;
  }
  const auto previous = layer.clipboard.find(format);
  if (previous != layer.clipboard.end() && previous->second != data) {
    layer.globals.erase(previous->second);
  }
  layer.clipboard[format] = data;
  return data;
}

HANDLE GetClipboardData(const UINT format)
{
  State& layer = Layer();
  Lock lock(layer.mutex);
  if (!ClipboardOpenHereLocked()) {
    return nullptr;
  }
  const auto found = layer.clipboard.find(format);
  return found != layer.clipboard.end() ? found->second : nullptr;
}

BOOL IsClipboardFormatAvailable(const UINT format)
{
  Lock lock(Layer().mutex);
  return Layer().clipboard.count(format) != 0 ? TRUE : FALSE;
}

// --- The host ---------------------------------------------------------------------------------------

void Win32Android_SetScreenSize(const int width, const int height)
{
  State& layer = Layer();
  Lock lock(layer.mutex);
  layer.screenWidth = width;
  layer.screenHeight = height;
  // A maximized window takes the new size, on its own thread.
  for (Window* top : layer.topLevel) {
    if (top->maximized && !top->minimized) {
      const HWND handle = Handle(top);
      PostTaskLocked(*top->queue, [handle, width, height] {
        Window* window = nullptr;
        {
          Lock inner(Layer().mutex);
          window = WindowLocked(handle);
          if (window == nullptr || !window->maximized || window->minimized) {
            return;
          }
        }
        (void)::SetWindowPos(handle, nullptr, 0, 0, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
      });
    }
  }
}

void Win32Android_SetScreenSurface(ANativeWindow* const surface)
{
  State& layer = Layer();
  Lock lock(layer.mutex);
  if (surface != nullptr) {
    ANativeWindow_acquire(surface);
  }
  if (layer.screenSurface != nullptr) {
    ANativeWindow_release(layer.screenSurface);
  }
  layer.screenSurface = surface;
}

ANativeWindow* Win32Android_GetWindowSurface(const HWND handle)
{
  State& layer = Layer();
  Lock lock(layer.mutex);
  return WindowLocked(handle) != nullptr ? layer.screenSurface : nullptr;
}

void Win32Android_SetForeground(const BOOL foreground)
{
  State& layer = Layer();
  Lock lock(layer.mutex);
  Window* window = layer.active;
  if (foreground != FALSE && window == nullptr) {
    // What Windows activates when an application comes back: the window that was active, else
    // the top visible one.
    window = layer.lastActive;
    for (auto top = layer.topLevel.begin(); window == nullptr && top != layer.topLevel.end(); ++top) {
      if (((*top)->style & WS_VISIBLE) != 0 && !(*top)->minimized) {
        window = *top;
      }
    }
  }
  if (window == nullptr) {
    // No window to activate: the first one shown is activated (ShowWindow).
    layer.foreground = foreground != FALSE;
    return;
  }
  const HWND root = Handle(RootLocked(window));
  PostTaskLocked(*window->queue, [root, foreground] { ApplyForeground(root, foreground); });
}

void Win32Android_MouseMove(const int x, const int y)
{
  State& layer = Layer();
  Lock lock(layer.mutex);
  layer.cursor = ClampToClipLocked(POINT{x, y});
  PostMouseLocked(WM_MOUSEMOVE, 0);
}

void Win32Android_MouseButton(const int button, const BOOL down, const int x, const int y)
{
  static const UINT kDown[] = {WM_LBUTTONDOWN, WM_RBUTTONDOWN, WM_MBUTTONDOWN, WM_XBUTTONDOWN, WM_XBUTTONDOWN};
  static const UINT kUp[] = {WM_LBUTTONUP, WM_RBUTTONUP, WM_MBUTTONUP, WM_XBUTTONUP, WM_XBUTTONUP};
  static const int kKey[] = {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2};
  if (button < WIN32ANDROID_BUTTON_LEFT || button > WIN32ANDROID_BUTTON_X2) {
    return;
  }
  State& layer = Layer();
  Lock lock(layer.mutex);
  layer.cursor = ClampToClipLocked(POINT{x, y});
  BYTE& key = layer.asyncKeys[kKey[button]];
  key = down != FALSE ? static_cast<BYTE>(key | 0x80) : static_cast<BYTE>(key & ~0x80);
  const WORD xButton = button == WIN32ANDROID_BUTTON_X1 ? XBUTTON1 : (button == WIN32ANDROID_BUTTON_X2 ? XBUTTON2 : 0);
  PostMouseLocked(down != FALSE ? kDown[button] : kUp[button], xButton);
}

void Win32Android_MouseWheel(const int delta, const int x, const int y)
{
  State& layer = Layer();
  Lock lock(layer.mutex);
  layer.cursor = ClampToClipLocked(POINT{x, y});
  Window* const target = layer.focus != nullptr ? layer.focus : layer.active;
  if (target == nullptr) {
    return;
  }
  PostInputLocked(
    *target->queue, target, WM_MOUSEWHEEL, MAKEWPARAM(static_cast<WORD>(MouseKeysLocked()), static_cast<WORD>(static_cast<short>(delta))),
    MAKELPARAM(layer.cursor.x, layer.cursor.y), 0
  );
}

void Win32Android_Key(const UINT virtualKey, const UINT scanCode, const BOOL down, const BOOL extended, const wchar_t character)
{
  State& layer = Layer();
  const int key = static_cast<int>(virtualKey & 0xFF);

  // The low-level hooks see the key before any queue does, as on Windows (here on this thread).
  HOOKPROC first = nullptr;
  bool altDown = false;
  {
    Lock lock(layer.mutex);
    first = layer.keyboardHooks.empty() ? nullptr : layer.keyboardHooks.front()->procedure;
    altDown = (layer.asyncKeys[VK_MENU] & 0x80) != 0 || (key == VK_MENU && down != FALSE);
  }
  const UINT message = (altDown || key == VK_F10) ? (down != FALSE ? WM_SYSKEYDOWN : WM_SYSKEYUP)
                                                  : (down != FALSE ? WM_KEYDOWN : WM_KEYUP);
  if (first != nullptr) {
    KBDLLHOOKSTRUCT info{};
    info.vkCode = virtualKey;
    info.scanCode = scanCode;
    info.flags = (extended != FALSE ? LLKHF_EXTENDED : 0) | (altDown ? LLKHF_ALTDOWN : 0) | (down != FALSE ? 0 : LLKHF_UP);
    info.time = ::GetTickCount();
    if (first(HC_ACTION, message, reinterpret_cast<LPARAM>(&info)) != 0) {
      return;
    }
  }

  Lock lock(layer.mutex);
  const bool wasDown = (layer.asyncKeys[key] & 0x80) != 0;
  layer.asyncKeys[key] = down != FALSE ? static_cast<BYTE>(layer.asyncKeys[key] | 0x80) : static_cast<BYTE>(layer.asyncKeys[key] & ~0x80);
  LPARAM lParam = 1 | (static_cast<LPARAM>(scanCode & 0xFF) << 16);
  lParam |= extended != FALSE ? (LPARAM{1} << 24) : 0;
  lParam |= altDown && message != WM_KEYDOWN && message != WM_KEYUP ? (LPARAM{1} << 29) : 0;
  lParam |= (wasDown || down == FALSE) ? (LPARAM{1} << 30) : 0;
  lParam |= down == FALSE ? (LPARAM{1} << 31) : 0;
  lParam &= 0xFFFFFFFF; // the 32 bits Windows fills
  // Without a focus window the keys go to the active window, as system keys.
  Window* target = layer.focus;
  UINT targetMessage = message;
  if (target == nullptr) {
    target = layer.active;
    targetMessage = down != FALSE ? WM_SYSKEYDOWN : WM_SYSKEYUP;
  }
  if (target == nullptr) {
    return;
  }
  PostInputLocked(*target->queue, target, targetMessage, virtualKey, lParam, down != FALSE ? character : 0);
}

void Win32Android_Text(const wchar_t* const text)
{
  if (text == nullptr) {
    return;
  }
  State& layer = Layer();
  Lock lock(layer.mutex);
  Window* const target = layer.focus;
  if (target == nullptr) {
    return;
  }
  for (const wchar_t* c = text; *c != 0; ++c) {
    PostInputLocked(*target->queue, target, WM_CHAR, static_cast<WPARAM>(*c), 1, 0);
  }
}
