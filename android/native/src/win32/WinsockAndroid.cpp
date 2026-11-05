// Winsock's event objects on POSIX sockets, for the game's UDP transport
// (include/win32/winsock2.h). What the Windows kernel does after WSAEventSelect a thread does here:
// it polls the selected sockets that have something left to signal and sets their events, one
// of which a CWaitHandleSet may be waiting for on the engine's main thread. A network event that
// was signalled is not polled for until recvfrom enables it again, so unread data costs no CPU, and
// with nothing to report the thread sleeps in poll.

#include <winsock2.h>

#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sys/eventfd.h>

#include <algorithm>
#include <cerrno>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <vector>

#include "gpg/core/utils/Logging.h"

namespace
{
  struct Selection
  {
    SOCKET socket;
    WSAEVENT event;
    long selected;
    long armed; // of `selected`, what has not been signalled since it was last enabled
  };

  struct Watcher
  {
    std::mutex mutex;
    std::vector<Selection> selections;
    int wake = -1;      // an eventfd: wakes the thread from its poll, for it to look at `selections`
    bool started = false;
    // A closed socket's number can be the next socket's, and the thread's poll keeps the old one
    // open. So closesocket waits until the thread has seen that the socket is gone.
    std::uint64_t removals = 0;
    std::uint64_t removalsSeen = 0;
    std::condition_variable removalSeen;
  };

  // Never destroyed: its thread runs until the process ends.
  Watcher& TheWatcher()
  {
    static Watcher* const sWatcher = new Watcher;
    return *sWatcher;
  }

  void WakeLocked(const Watcher& watcher)
  {
    const std::uint64_t one = 1;
    (void)!::write(watcher.wake, &one, sizeof(one));
  }

  long Signalled(const long armed, const short revents)
  {
    // An error wakes whoever waits for anything: the next call on the socket reports it.
    if ((revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
      return armed;
    }
    return (((revents & POLLIN) != 0 ? FD_READ : 0) | ((revents & POLLOUT) != 0 ? FD_WRITE : 0)) & armed;
  }

  std::vector<Selection>::iterator FindLocked(Watcher& watcher, const SOCKET socket)
  {
    return std::find_if(watcher.selections.begin(), watcher.selections.end(), [socket](const Selection& selection) {
      return selection.socket == socket;
    });
  }

  void* WatchSockets(void* const argument)
  {
    Watcher& watcher = *static_cast<Watcher*>(argument);
    std::vector<pollfd> polled;
    for (;;) {
      polled.clear();
      polled.push_back(pollfd{watcher.wake, POLLIN, 0});
      {
        std::lock_guard<std::mutex> lock(watcher.mutex);
        for (const Selection& selection : watcher.selections) {
          if (selection.armed != 0) {
            const short events = static_cast<short>(((selection.armed & FD_READ) != 0 ? POLLIN : 0) |
                                                    ((selection.armed & FD_WRITE) != 0 ? POLLOUT : 0));
            polled.push_back(pollfd{selection.socket, events, 0});
          }
        }
        watcher.removalsSeen = watcher.removals;
      }
      watcher.removalSeen.notify_all();

      if (::poll(polled.data(), polled.size(), -1) < 0) {
        if (errno != EINTR) {
          gpg::Warnf("WSAEventSelect: poll() failed: errno %d", errno);
          ::Sleep(100); // what poll can run out of comes back, and this is no busy loop meanwhile
        }
        continue;
      }
      if ((polled[0].revents & POLLIN) != 0) {
        std::uint64_t count = 0;
        (void)!::read(watcher.wake, &count, sizeof(count));
      }

      std::lock_guard<std::mutex> lock(watcher.mutex);
      for (std::size_t index = 1; index < polled.size(); ++index) {
        const auto selection = FindLocked(watcher, polled[index].fd);
        if (polled[index].revents != 0 && selection != watcher.selections.end()) {
          const long signalled = Signalled(selection->armed, polled[index].revents);
          if (signalled != 0) {
            selection->armed &= ~signalled;
            (void)::SetEvent(selection->event); // under the lock: closesocket and then WSACloseEvent are safe
          }
        }
      }
    }
  }

  bool StartLocked(Watcher& watcher)
  {
    if (watcher.started) {
      return true;
    }
    watcher.wake = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (watcher.wake < 0) {
      return false;
    }
    pthread_t thread{};
    const int error = ::pthread_create(&thread, nullptr, &WatchSockets, &watcher);
    if (error != 0) {
      (void)::close(watcher.wake);
      watcher.wake = -1;
      errno = error;
      return false;
    }
    (void)::pthread_setname_np(thread, "WSAEventSelect");
    (void)::pthread_detach(thread);
    watcher.started = true;
    return true;
  }
} // namespace

WSAEVENT WSACreateEvent()
{
  return ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
}

BOOL WSACloseEvent(const WSAEVENT event)
{
  return ::CloseHandle(event);
}

BOOL WSASetEvent(const WSAEVENT event)
{
  return ::SetEvent(event);
}

BOOL WSAResetEvent(const WSAEVENT event)
{
  return ::ResetEvent(event);
}

int WSAEventSelect(const SOCKET socket, const WSAEVENT event, const long networkEvents)
{
  if (event == WSA_INVALID_EVENT && networkEvents != 0) {
    errno = EINVAL;
    return SOCKET_ERROR;
  }
  int type = 0;
  socklen_t typeLength = sizeof(type);
  if (::getsockopt(socket, SOL_SOCKET, SO_TYPE, &type, &typeLength) != 0) {
    return SOCKET_ERROR;
  }
  const int flags = ::fcntl(socket, F_GETFL);
  if (flags < 0 || ::fcntl(socket, F_SETFL, flags | O_NONBLOCK) != 0) {
    return SOCKET_ERROR;
  }

  Watcher& watcher = TheWatcher();
  std::lock_guard<std::mutex> lock(watcher.mutex);
  if (!StartLocked(watcher)) {
    gpg::Warnf("WSAEventSelect: the thread cannot start: errno %d", errno);
    return SOCKET_ERROR;
  }
  const auto selection = FindLocked(watcher, socket);
  if (selection != watcher.selections.end()) {
    watcher.selections.erase(selection);
  }
  if (networkEvents != 0) {
    watcher.selections.push_back(Selection{socket, event, networkEvents, networkEvents});
  }
  WakeLocked(watcher);
  return 0;
}

DWORD WSAWaitForMultipleEvents(
  const DWORD count, const WSAEVENT* const events, const BOOL waitAll, const DWORD milliseconds, const BOOL
)
{
  // One event: the POSIX layer's wait for one sleeps, where its wait for several looks every millisecond.
  return count == 1 ? ::WaitForSingleObject(events[0], milliseconds)
                    : ::WaitForMultipleObjects(count, events, waitAll, milliseconds);
}

int recvfrom(
  const SOCKET socket, char* const buffer, const int length, const int flags, sockaddr* const from, int* const fromLength
)
{
  socklen_t size = fromLength != nullptr ? static_cast<socklen_t>(*fromLength) : 0;
  const ssize_t received = ::recvfrom(
    socket, static_cast<void*>(buffer), static_cast<std::size_t>(length), flags, from, fromLength != nullptr ? &size : nullptr
  );
  const int error = errno;
  if (fromLength != nullptr) {
    *fromLength = static_cast<int>(size);
  }

  Watcher& watcher = TheWatcher();
  {
    std::lock_guard<std::mutex> lock(watcher.mutex);
    const auto selection = FindLocked(watcher, socket);
    if (selection != watcher.selections.end() && (selection->selected & ~selection->armed & FD_READ) != 0) {
      selection->armed |= FD_READ;
      WakeLocked(watcher);
    }
  }
  errno = error;
  return static_cast<int>(received);
}

int getsockname(const SOCKET socket, sockaddr* const name, int* const nameLength)
{
  socklen_t size = static_cast<socklen_t>(*nameLength);
  const int result = ::getsockname(socket, name, &size);
  *nameLength = static_cast<int>(size);
  return result;
}

bool Win32Android_ConnectorSocketOpen()
{
  Watcher& watcher = TheWatcher();
  std::lock_guard<std::mutex> lock(watcher.mutex);
  return std::any_of(watcher.selections.begin(), watcher.selections.end(), [](const Selection& selection) {
    return (selection.selected & FD_WRITE) != 0;
  });
}

int Win32Android_CloseSocket(const SOCKET socket)
{
  Watcher& watcher = TheWatcher();
  {
    std::unique_lock<std::mutex> lock(watcher.mutex);
    const auto selection = FindLocked(watcher, socket);
    if (selection != watcher.selections.end()) {
      watcher.selections.erase(selection);
      const std::uint64_t removal = ++watcher.removals;
      WakeLocked(watcher);
      watcher.removalSeen.wait(lock, [&watcher, removal] { return watcher.removalsSeen >= removal; });
    }
  }
  return ::close(socket);
}
