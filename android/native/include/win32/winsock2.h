#pragma once

// The Android client build's <winsock2.h>: the engine's POSIX layer
// (src/platform/posix/win32/winsock2.h, which the simulation build uses as it is), and what the
// game's UDP transport calls beyond it (moho/net/CNetUDP*, CNetDatagramSocketImpl,
// INetDatagramSocket): event objects and WSAEventSelect, in src/win32/WinsockAndroid.cpp, with
// Winsock's meaning. This directory comes ahead of the engine's on the include path; #include_next
// continues the search behind it.

#include_next <winsock2.h>

using WSAData = WSADATA;

#define WSA_INVALID_EVENT (static_cast<WSAEVENT>(nullptr))
#define FD_READ 0x01L
#define FD_WRITE 0x02L

// A manual-reset event, one of the POSIX layer's, so that a CWaitHandleSet can wait for it too.
WSAEVENT WSACreateEvent();
BOOL WSACloseEvent(WSAEVENT event);
BOOL WSASetEvent(WSAEVENT event);
BOOL WSAResetEvent(WSAEVENT event);

// Sets `event` when a network event of `networkEvents` happens on `socket`, which turns non-blocking.
// A socket has one selection: a new one replaces it, and 0 for `networkEvents` ends it, as closesocket
// does. As on Windows, FD_READ is signalled again only after a recvfrom; FD_WRITE is signalled once, when
// the socket can first be written, and not again after a send that would block, which Windows does: the
// transport ignores sendto's result and waits for its own timers.
int WSAEventSelect(SOCKET socket, WSAEVENT event, long networkEvents);

// Not alertable, whatever `alertable` says: no queued call goes to a thread that waits here.
DWORD WSAWaitForMultipleEvents(DWORD count, const WSAEVENT* events, BOOL waitAll, DWORD milliseconds, BOOL alertable);

// Winsock's lengths are ints. This recvfrom enables FD_READ again.
int recvfrom(SOCKET socket, char* buffer, int length, int flags, sockaddr* from, int* fromLength);
int getsockname(SOCKET socket, sockaddr* name, int* nameLength);

// closesocket ends the socket's selection: the engine's inline knows no events.
int Win32Android_CloseSocket(SOCKET socket);
#define closesocket Win32Android_CloseSocket

// Whether a socket is selected for FD_WRITE: of the game's sockets only a UDP connector's is, while a network lobby
// or game has it open. A discovery socket (the LAN screen's, a host's listener) only reads, a skirmish has none.
bool Win32Android_ConnectorSocketOpen();
