// faf_net_tests (android/native/README.md, Tests): the Winsock events of src/win32/WinsockAndroid.cpp on
// real sockets over the loopback, and the engine's own datagram socket and UDP connector on them: the
// patched files as the game runs them, failure paths included. What the protocol puts on the wire, whose
// sizes the x86 game fixes. Linked statically like faf_wx_lite_tests, so it runs on the emulator and on
// Linux hosts (CI's arm64 runner). Exit code 0 when every check passes.

#include <fcntl.h>
#include <poll.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <winsock2.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <new>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "boost/thread/exceptions.hpp"
#include "boost/weak_ptr.h"
#include "gpg/core/algorithms/MD5.h"
#include "moho/containers/SCoordsVec2.h"
#include "moho/net/CClientManagerImpl.h"
#include "moho/net/CMessage.h"
#include "moho/net/CNetClient.h"
#include "moho/net/Common.h"
#include "moho/net/EClientMsg.h"
#include "moho/net/ECmdStreamOp.h"
#include "moho/net/ELobbyMsg.h"
#include "moho/net/IMessageReceiver.h"
#include "moho/net/INetConnection.h"
#include "moho/net/INetConnector.h"
#include "moho/net/INetDatagramHandler.h"
#include "moho/net/INetDatagramSocket.h"
#include "moho/net/SNetPacket.h"
#include "moho/net/SPeer.h"
#include "moho/render/camera/VTransform.h"
#include "moho/sim/SOCellPos.h"
#include "Wm3Quaternion.h"
#include "Wm3Vector3.h"

namespace
{
  constexpr u_long kLoopback = 0x7F000001; // host order, as the engine passes addresses
  constexpr DWORD kArrivalMs = 2000;       // a datagram over the loopback is there long before
  constexpr DWORD kSilenceMs = 150;        // what has to stay silent is watched this long

  int gFailedChecks = 0;

#define CHECK(condition)                                            \
  do {                                                              \
    if (!(condition)) {                                             \
      std::printf("  %s:%d: %s\n", __FILE__, __LINE__, #condition); \
      ++gFailedChecks;                                              \
    }                                                               \
  } while (false)

  bool Signalled(const WSAEVENT event, const DWORD milliseconds)
  {
    return WSAWaitForMultipleEvents(1, &event, FALSE, milliseconds, FALSE) == WAIT_OBJECT_0;
  }

  /// A UDP socket on the loopback, on `port` or one the system picks.
  SOCKET OpenUdp(const u_short port = 0)
  {
    const SOCKET socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in name{};
    name.sin_family = AF_INET;
    name.sin_port = htons(port);
    name.sin_addr.s_addr = htonl(kLoopback);
    CHECK(socket != INVALID_SOCKET);
    CHECK(::bind(socket, reinterpret_cast<const sockaddr*>(&name), sizeof(name)) == 0);
    return socket;
  }

  u_short PortOf(const SOCKET socket)
  {
    sockaddr_in name{};
    int length = sizeof(name);
    CHECK(getsockname(socket, reinterpret_cast<sockaddr*>(&name), &length) == 0);
    CHECK(length == sizeof(name));
    return ntohs(name.sin_port);
  }

  void SendTo(const SOCKET from, const u_short port, const std::string& text)
  {
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(port);
    to.sin_addr.s_addr = htonl(kLoopback);
    const ssize_t sent = ::sendto(from, text.data(), text.size(), 0, reinterpret_cast<const sockaddr*>(&to), sizeof(to));
    CHECK(sent == static_cast<ssize_t>(text.size()));
  }

  int ReadOne(const SOCKET socket)
  {
    char buffer[1500];
    sockaddr_in from{};
    int fromLength = sizeof(from);
    return recvfrom(socket, buffer, sizeof(buffer), 0, reinterpret_cast<sockaddr*>(&from), &fromLength);
  }

  // --- The events ------------------------------------------------------------------------------

  void EventsAreManualReset()
  {
    WSAEVENT events[2] = {WSACreateEvent(), WSACreateEvent()};
    CHECK(events[0] != WSA_INVALID_EVENT);
    CHECK(!Signalled(events[0], 0));
    CHECK(WSASetEvent(events[0]) != FALSE);
    CHECK(Signalled(events[0], 0));
    CHECK(Signalled(events[0], 0)); // it stays set
    CHECK(WSAResetEvent(events[0]) != FALSE);
    CHECK(!Signalled(events[0], 0));

    const auto start = std::chrono::steady_clock::now();
    CHECK(WSAWaitForMultipleEvents(1, &events[0], FALSE, 60, TRUE) == WAIT_TIMEOUT);
    CHECK(std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(50));

    CHECK(WSAWaitForMultipleEvents(2, events, FALSE, 0, FALSE) == WAIT_TIMEOUT);
    CHECK(WSASetEvent(events[1]) != FALSE);
    CHECK(WSAWaitForMultipleEvents(2, events, FALSE, 0, FALSE) == WAIT_OBJECT_0 + 1);
    CHECK(WSAWaitForMultipleEvents(2, events, TRUE, 0, FALSE) == WAIT_TIMEOUT);
    CHECK(WSASetEvent(events[0]) != FALSE);
    CHECK(WSAWaitForMultipleEvents(2, events, TRUE, 0, FALSE) == WAIT_OBJECT_0);

    // The connector's thread waits without an end, -1 cast to a DWORD, and another thread wakes it.
    CHECK(WSAResetEvent(events[0]) != FALSE);
    std::thread waker([&events] {
      std::this_thread::sleep_for(std::chrono::milliseconds(30));
      (void)WSASetEvent(events[0]);
    });
    CHECK(WSAWaitForMultipleEvents(1, &events[0], FALSE, static_cast<DWORD>(-1), TRUE) == WAIT_OBJECT_0);
    waker.join();

    CHECK(WSACloseEvent(events[0]) != FALSE);
    CHECK(WSACloseEvent(events[1]) != FALSE);
  }

  void SelectRefusesWhatIsNoSelection()
  {
    const WSAEVENT event = WSACreateEvent();

    CHECK(WSAEventSelect(987654, event, FD_READ) == SOCKET_ERROR);
    CHECK(WSAGetLastError() == WSAEBADF);

    const int file = ::open("/proc/self/exe", O_RDONLY | O_CLOEXEC);
    CHECK(file >= 0);
    CHECK(WSAEventSelect(file, event, FD_READ) == SOCKET_ERROR);
    CHECK(WSAGetLastError() == WSAENOTSOCK);
    CHECK(::close(file) == 0);

    const SOCKET socket = OpenUdp();
    CHECK(WSAEventSelect(socket, WSA_INVALID_EVENT, FD_READ) == SOCKET_ERROR);
    CHECK(WSAGetLastError() == WSAEINVAL);
    CHECK(closesocket(socket) == 0);

    CHECK(closesocket(987654) == SOCKET_ERROR);
    CHECK(WSAGetLastError() == WSAEBADF);
    CHECK(WSACloseEvent(event) != FALSE);
  }

  void SelectMakesTheSocketNonBlocking()
  {
    const WSAEVENT event = WSACreateEvent();
    const SOCKET socket = OpenUdp();
    CHECK((::fcntl(socket, F_GETFL) & O_NONBLOCK) == 0);
    CHECK(WSAEventSelect(socket, event, FD_READ) == 0);
    CHECK((::fcntl(socket, F_GETFL) & O_NONBLOCK) != 0);
    CHECK(ReadOne(socket) == SOCKET_ERROR);
    CHECK(WSAGetLastError() == WSAEWOULDBLOCK); // what comes after recvfrom's read keeps its errno
    CHECK(closesocket(socket) == 0);
    CHECK(WSACloseEvent(event) != FALSE);
  }

  void ReadIsSignalledAgainOnlyAfterRecvfrom()
  {
    const WSAEVENT event = WSACreateEvent();
    const SOCKET socket = OpenUdp();
    const SOCKET sender = OpenUdp();
    CHECK(WSAEventSelect(socket, event, FD_READ) == 0);
    CHECK(!Signalled(event, kSilenceMs)); // nothing to read

    SendTo(sender, PortOf(socket), "one");
    CHECK(Signalled(event, kArrivalMs));

    // Winsock: once until a recvfrom, whatever arrives and whoever resets the event.
    CHECK(WSAResetEvent(event) != FALSE);
    SendTo(sender, PortOf(socket), "two");
    CHECK(!Signalled(event, kSilenceMs));

    // A recvfrom that leaves a datagram behind has the event set again: the engine's batches stop
    // reading before the socket is empty and count on it.
    CHECK(ReadOne(socket) == 3);
    CHECK(Signalled(event, kArrivalMs));

    // One that takes the last one leaves it alone.
    CHECK(WSAResetEvent(event) != FALSE);
    CHECK(ReadOne(socket) == 3);
    CHECK(ReadOne(socket) == SOCKET_ERROR);
    CHECK(WSAGetLastError() == WSAEWOULDBLOCK);
    std::this_thread::sleep_for(std::chrono::milliseconds(kSilenceMs)); // a signal that was on its way
    CHECK(WSAResetEvent(event) != FALSE);
    CHECK(!Signalled(event, kSilenceMs));

    // And the next datagram is signalled as the first was.
    SendTo(sender, PortOf(socket), "three");
    CHECK(Signalled(event, kArrivalMs));
    CHECK(ReadOne(socket) == 5);

    CHECK(closesocket(sender) == 0);
    CHECK(closesocket(socket) == 0);
    CHECK(WSACloseEvent(event) != FALSE);
  }

  void RecvfromFillsInTheSender()
  {
    const SOCKET socket = OpenUdp();
    const SOCKET sender = OpenUdp();
    SendTo(sender, PortOf(socket), "hello");
    char buffer[16] = {};
    sockaddr_in from{};
    int fromLength = sizeof(from);
    CHECK(recvfrom(socket, buffer, sizeof(buffer), 0, reinterpret_cast<sockaddr*>(&from), &fromLength) == 5);
    CHECK(std::memcmp(buffer, "hello", 5) == 0);
    CHECK(fromLength == sizeof(from));
    CHECK(ntohl(from.sin_addr.s_addr) == kLoopback);
    CHECK(ntohs(from.sin_port) == PortOf(sender));
    CHECK(closesocket(sender) == 0);
    CHECK(closesocket(socket) == 0);
  }

  void WriteIsSignalledOnce()
  {
    const WSAEVENT event = WSACreateEvent();
    const SOCKET socket = OpenUdp();
    CHECK(WSAEventSelect(socket, event, FD_READ | FD_WRITE) == 0);
    CHECK(Signalled(event, kArrivalMs)); // it can be written
    CHECK(WSAResetEvent(event) != FALSE);
    CHECK(!Signalled(event, kSilenceMs));
    CHECK(closesocket(socket) == 0);
    CHECK(WSACloseEvent(event) != FALSE);
  }

  void ANewSelectionReplacesTheOld()
  {
    const WSAEVENT first = WSACreateEvent();
    const WSAEVENT second = WSACreateEvent();
    const SOCKET socket = OpenUdp();
    const SOCKET sender = OpenUdp();
    CHECK(WSAEventSelect(socket, first, FD_READ) == 0);
    CHECK(WSAEventSelect(socket, second, FD_READ) == 0);
    SendTo(sender, PortOf(socket), "x");
    CHECK(Signalled(second, kArrivalMs));
    CHECK(!Signalled(first, 0));

    // None at all: the socket stays non-blocking, nothing is signalled.
    CHECK(ReadOne(socket) == 1);
    CHECK(WSAResetEvent(second) != FALSE);
    CHECK(WSAEventSelect(socket, WSA_INVALID_EVENT, 0) == 0);
    SendTo(sender, PortOf(socket), "y");
    CHECK(!Signalled(second, kSilenceMs));
    CHECK(ReadOne(socket) == 1);
    CHECK(ReadOne(socket) == SOCKET_ERROR);

    CHECK(closesocket(sender) == 0);
    CHECK(closesocket(socket) == 0);
    CHECK(WSACloseEvent(first) != FALSE);
    CHECK(WSACloseEvent(second) != FALSE);
  }

  void CloseEndsTheSelectionAndFreesThePort()
  {
    const SOCKET sender = OpenUdp();
    const WSAEVENT oldEvent = WSACreateEvent();
    SOCKET socket = OpenUdp();
    const u_short port = PortOf(socket);
    // The thread is in its poll of the old socket each time: the port is free when closesocket returns.
    for (int round = 0; round < 100; ++round) {
      CHECK(WSAEventSelect(socket, oldEvent, FD_READ) == 0);
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      CHECK(closesocket(socket) == 0);
      socket = OpenUdp(port);
    }
    // The new socket has the old one's number, and its traffic is nothing to the old selection's event.
    CHECK(WSAResetEvent(oldEvent) != FALSE);
    SendTo(sender, port, "z");
    CHECK(!Signalled(oldEvent, kSilenceMs));
    CHECK(closesocket(socket) == 0);
    CHECK(closesocket(sender) == 0);
    CHECK(WSACloseEvent(oldEvent) != FALSE);
  }

  void WhatWaitsCostsNoCpu()
  {
    const WSAEVENT unread = WSACreateEvent();
    const WSAEVENT idle = WSACreateEvent();
    const SOCKET withData = OpenUdp();
    const SOCKET withoutData = OpenUdp();
    const SOCKET sender = OpenUdp();
    CHECK(WSAEventSelect(withData, unread, FD_READ) == 0);
    CHECK(WSAEventSelect(withoutData, idle, FD_READ) == 0);
    SendTo(sender, PortOf(withData), "left unread");
    CHECK(Signalled(unread, kArrivalMs));

    timespec start{};
    timespec end{};
    ::clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ::clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &end);
    const long long cpuMs = (end.tv_sec - start.tv_sec) * 1000LL + (end.tv_nsec - start.tv_nsec) / 1000000LL;
    std::printf("  %lld ms of CPU in 500 ms with data unread and a socket idle\n", cpuMs);
    CHECK(cpuMs < 50);

    CHECK(closesocket(sender) == 0);
    CHECK(closesocket(withoutData) == 0);
    CHECK(closesocket(withData) == 0);
    CHECK(WSACloseEvent(idle) != FALSE);
    CHECK(WSACloseEvent(unread) != FALSE);
  }

  // --- The engine's datagram socket --------------------------------------------------------------

  struct Datagrams final : moho::INetDatagramHandler
  {
    std::vector<std::string> payloads;
    u_long lastAddress = 0;
    u_short lastPort = 0;
    bool throwOnNext = false;

    void OnDatagram(moho::CMessage* const message, moho::INetDatagramSocket*, const u_long address, const u_short port) override
    {
      lastAddress = address;
      lastPort = port;
      if (throwOnNext) {
        throwOnNext = false;
        throw std::runtime_error("as a short payload does");
      }
      payloads.emplace_back(message->mBuff.start_ + 3, message->mBuff.end_);
    }
  };

  moho::CMessage Request(const std::string& text)
  {
    moho::CMessage message(moho::ELobbyMsg::LOBMSG_DiscoveryRequest);
    message.mBuff.Resize(3 + text.size(), 0);
    std::memcpy(message.mBuff.start_ + 3, text.data(), text.size());
    message.SetSize(3 + text.size());
    return message;
  }

  u_short FreePort()
  {
    const SOCKET probe = OpenUdp();
    const u_short port = PortOf(probe);
    CHECK(closesocket(probe) == 0);
    return port;
  }

  void DatagramSocketSendsToItself()
  {
    Datagrams handler;
    const u_short port = FreePort();
    moho::INetDatagramSocket* const socket = moho::NET_OpenDatagramSocket(port, &handler);
    CHECK(socket != nullptr);
    if (socket == nullptr) {
      return;
    }
    const HANDLE event = socket->CreateEvent();
    CHECK(event != nullptr);
    CHECK(socket->CreateEvent() == event); // lazily, once
    CHECK(!Signalled(event, 0));

    moho::CMessage message = Request("is there a game?");
    socket->Send(&message, kLoopback, port);
    CHECK(Signalled(event, kArrivalMs));
    socket->Pull();
    CHECK(!Signalled(event, 0));
    CHECK(handler.payloads.size() == 1);
    CHECK(!handler.payloads.empty() && handler.payloads[0] == "is there a game?");
    CHECK(handler.lastAddress == kLoopback);
    CHECK(handler.lastPort == port);

    // The port is taken now: a second opening fails, as a second host on one phone does.
    Datagrams other;
    CHECK(moho::NET_OpenDatagramSocket(port, &other) == nullptr);
    CHECK(std::string(moho::NET_GetWinsockErrorString()) == "WSAEADDRINUSE");

    // What is not a whole message goes unread by the handler; a handler's exception stays in Pull.
    const SOCKET sender = OpenUdp();
    SendTo(sender, port, "xy");
    moho::CMessage liar = Request("short");
    liar.SetSize(200);
    socket->Send(&liar, kLoopback, port);
    CHECK(Signalled(event, kArrivalMs));
    socket->Pull();
    CHECK(handler.payloads.size() == 1);
    handler.throwOnNext = true;
    moho::CMessage thrown = Request("thrown");
    socket->Send(&thrown, kLoopback, port);
    CHECK(Signalled(event, kArrivalMs));
    socket->Pull();
    CHECK(handler.payloads.size() == 1);
    CHECK(!handler.throwOnNext);

    // A flood stops after a batch of 64, and the rest wakes the game again.
    handler.payloads.clear();
    moho::CMessage flood = Request("again");
    for (int count = 0; count < 70; ++count) {
      socket->Send(&flood, kLoopback, port);
    }
    CHECK(Signalled(event, kArrivalMs));
    socket->Pull();
    CHECK(handler.payloads.size() == 64);
    CHECK(Signalled(event, kArrivalMs));
    socket->Pull();
    CHECK(handler.payloads.size() == 70);
    CHECK(closesocket(sender) == 0);

    // Gone with its object, event and all: the port is free at once.
    delete socket;
    Datagrams again;
    moho::INetDatagramSocket* const reopened = moho::NET_OpenDatagramSocket(port, &again);
    CHECK(reopened != nullptr);
    delete reopened;
  }

  /// Whether a broadcast comes back to this machine. A system does so on most networks, but owes it to no
  /// one, and without a network there is none to send. A plain socket asks, so that nothing of the engine's
  /// has a say in whether the engine's path below is checked.
  bool BroadcastsComeBack()
  {
    const SOCKET probe = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    const int on = 1;
    CHECK(::setsockopt(probe, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on)) == 0);
    sockaddr_in name{};
    name.sin_family = AF_INET;
    name.sin_port = htons(FreePort());
    name.sin_addr.s_addr = htonl(INADDR_ANY);
    CHECK(::bind(probe, reinterpret_cast<const sockaddr*>(&name), sizeof(name)) == 0);
    name.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    const bool sent = ::sendto(probe, "x", 1, 0, reinterpret_cast<const sockaddr*>(&name), sizeof(name)) == 1;
    pollfd readable{probe, POLLIN, 0};
    const bool back = sent && ::poll(&readable, 1, kArrivalMs) == 1;
    CHECK(closesocket(probe) == 0);
    return back;
  }

  void DatagramSocketBroadcasts()
  {
    if (!BroadcastsComeBack()) {
      std::printf("  no broadcast comes back on this network: not checked\n");
      return;
    }

    // Discovery's way: to everyone on the port, from a socket on any port.
    Datagrams hostHandler;
    Datagrams seekerHandler;
    const u_short port = FreePort();
    moho::INetDatagramSocket* const host = moho::NET_OpenDatagramSocket(port, &hostHandler);
    moho::INetDatagramSocket* const seeker = moho::NET_OpenDatagramSocket(0, &seekerHandler);
    CHECK(host != nullptr && seeker != nullptr);
    if (host != nullptr && seeker != nullptr) {
      const HANDLE hostEvent = host->CreateEvent();
      const HANDLE seekerEvent = seeker->CreateEvent();
      moho::CMessage request = Request("anyone?");
      seeker->SendDefault(&request, port);
      CHECK(Signalled(hostEvent, kArrivalMs));
      host->Pull();
      CHECK(hostHandler.payloads.size() == 1);
      CHECK(!hostHandler.payloads.empty() && hostHandler.payloads[0] == "anyone?");
      // And the answer goes back to the sender's port, as the lobby's does.
      moho::CMessage answer = Request("here");
      host->Send(&answer, hostHandler.lastAddress, hostHandler.lastPort);
      CHECK(Signalled(seekerEvent, kArrivalMs));
      seeker->Pull();
      CHECK(seekerHandler.payloads.size() == 1);
      CHECK(!seekerHandler.payloads.empty() && seekerHandler.payloads[0] == "here");
      if (gFailedChecks == 0) {
        std::printf("  a broadcast came back through the sockets, from %s\n", moho::NET_GetDottedOctetFromUInt32(hostHandler.lastAddress).c_str());
      }
    }
    delete seeker;
    delete host;
  }

  // --- The engine's UDP connector ------------------------------------------------------------------

  // The connections outlive a test by their goodbyes, so their receivers are never deleted.
  struct Messages final : moho::IMessageReceiver
  {
    std::vector<std::string> payloads;
    std::vector<unsigned> types;

    void ReceiveMessage(moho::CMessage* const message, moho::CMessageDispatcher*) override
    {
      types.push_back(message->GetType().raw());
      payloads.emplace_back(message->mBuff.start_ + 3, message->mBuff.end_);
    }
  };

  moho::INetConnector* MakeConnector(const u_short port = 0)
  {
    return moho::NET_MakeConnector(port, moho::ENetProtocolType::kUdp, boost::weak_ptr<moho::INetNATTraversalProvider>{});
  }

  /// Until `done` says so, or `seconds` passed, as the lobby's frame pulls and pushes.
  template <class Done>
  bool Pump(moho::INetConnector* const a, moho::INetConnector* const b, const Done& done, const int seconds = 5)
  {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (!done()) {
      if (std::chrono::steady_clock::now() > end) {
        return false;
      }
      a->Pull();
      a->Push();
      b->Pull();
      b->Push();
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return true;
  }

  /// Bytes that do not deflate, as MaybeSendTestBurst has them.
  std::string RandomText(const std::size_t size, const std::uint32_t seed)
  {
    std::minstd_rand random(seed);
    std::string text(size, '\0');
    for (char& character : text) {
      character = static_cast<char>(random() >> 7);
    }
    return text;
  }

  /// How many packets `connector` has sent so far that could hold no more.
  int FullPacketsSent(moho::INetConnector* const connector)
  {
    const moho::SSendStampWindow window = connector->SnapshotSendStamps(60 * 60 * 1000);
    int count = 0;
    for (const moho::SSendStamp& stamp : window.items) {
      if (stamp.direction == 0 && stamp.payloadSizeBytes == moho::kPacketMaxSize) {
        ++count;
      }
    }
    return count;
  }

  void ConnectorsTalkOverTheLoopback()
  {
    moho::INetConnector* const host = MakeConnector();
    moho::INetConnector* const guest = MakeConnector();
    CHECK(host != nullptr && guest != nullptr);
    if (host == nullptr || guest == nullptr) {
      return;
    }
    CHECK(host->GetProtocol() == moho::ENetProtocolType::kUdp);
    const u_short hostPort = host->GetLocalPort();
    CHECK(hostPort != 0);
    CHECK(guest->GetLocalPort() != hostPort);

    // The port is taken: a second connector fails with the system's word in the log, and the lobby says so.
    CHECK(MakeConnector(hostPort) == nullptr);
    CHECK(std::string(moho::NET_GetWinsockErrorString()) == "WSAEADDRINUSE");

    // The game's event is set from the connector's thread when somebody knocks.
    const HANDLE selected = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    host->SelectEvent(selected);

    Messages* const guestInbox = new Messages;
    moho::INetConnection* const toHost = guest->Connect(kLoopback, hostPort);
    CHECK(toHost != nullptr);
    toHost->PushReceiver(0, 256, guestInbox);
    guest->Push();
    CHECK(::WaitForSingleObject(selected, 3000) == WAIT_OBJECT_0);

    u_long address = 0;
    u_short port = 0;
    CHECK(Pump(host, guest, [&] { return host->FindNextAddress(address, port); }));
    CHECK(address == kLoopback);
    CHECK(port == guest->GetLocalPort());

    Messages* const hostInbox = new Messages;
    moho::INetConnection* const toGuest = host->Accept(address, port);
    CHECK(toGuest != nullptr);
    toGuest->PushReceiver(0, 256, hostInbox);

    // A message of many packets each way, whole and in order, behind the handshake's word. 4000 bytes
    // leave a loaded machine room: net_MaxSendRate lets them go in about a second of Pump's five.
    const std::string fromHost = RandomText(4000, 1);
    const std::string fromGuest = RandomText(4000, 2);
    toGuest->Write(Request(fromHost));
    toHost->Write(Request(fromGuest));
    CHECK(Pump(host, guest, [&] { return guestInbox->payloads.size() >= 2 && hostInbox->payloads.size() >= 2; }));
    const unsigned made = static_cast<unsigned>(moho::ELobbyMsg::LOBMSG_ConnMade);
    CHECK(guestInbox->types.size() >= 2 && guestInbox->types[0] == made);
    CHECK(hostInbox->types.size() >= 2 && hostInbox->types[0] == made);
    CHECK(guestInbox->payloads.size() >= 2 && guestInbox->payloads[1] == fromHost);
    CHECK(hostInbox->payloads.size() >= 2 && hostInbox->payloads[1] == fromGuest);
    const int fullFromHost = FullPacketsSent(host);
    const int fullFromGuest = FullPacketsSent(guest);
    std::printf("  full packets to carry the message: %d from the host, %d from the guest\n", fullFromHost, fullFromGuest);
    CHECK(fullFromHost >= 7);
    CHECK(fullFromGuest >= 7);

    // Garbage on the port is dropped and the connection goes on.
    const SOCKET stranger = OpenUdp();
    SendTo(stranger, hostPort, "too short");
    SendTo(stranger, hostPort, std::string(600, '\x03'));
    toHost->Write(Request("after the garbage"));
    CHECK(Pump(host, guest, [&] { return hostInbox->payloads.size() >= 3; }));

    // A guest that goes away: its goodbye ends the connection on the other side.
    const u_short guestPort = guest->GetLocalPort();
    const unsigned lost = static_cast<unsigned>(moho::ELobbyMsg::LOBMSG_ConnLostEof);
    toHost->ScheduleDestroy();
    guest->Push();
    guest->Destroy();
    CHECK(Pump(host, host, [&] { return !hostInbox->types.empty() && hostInbox->types.back() == lost; }));

    // Destroy closes the socket once the connector's thread has said goodbye: the port is free again.
    moho::INetConnector* again = nullptr;
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (again == nullptr && std::chrono::steady_clock::now() < end) {
      again = MakeConnector(guestPort);
      if (again == nullptr) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
    }
    CHECK(again != nullptr);
    if (again != nullptr) {
      again->Destroy();
    }

    // The game closes its event right after Destroy.
    toGuest->ScheduleDestroy();
    host->Push();
    host->Destroy();
    CHECK(::CloseHandle(selected) != FALSE);
    CHECK(closesocket(stranger) == 0);
  }

  /// Passes datagrams between two UDP sockets until `running` goes false: a network that can go.
  void RelayDatagrams(const SOCKET towardsGuest, const SOCKET towardsHost, const u_short hostPort, const std::atomic<bool>& running)
  {
    u_short guestPort = 0;
    char buffer[1500];
    while (running) {
      sockaddr_in from{};
      int fromLength = sizeof(from);
      int received = recvfrom(towardsGuest, buffer, sizeof(buffer), 0, reinterpret_cast<sockaddr*>(&from), &fromLength);
      if (received > 0) {
        guestPort = ntohs(from.sin_port);
        SendTo(towardsHost, hostPort, std::string(buffer, static_cast<std::size_t>(received)));
      }
      fromLength = sizeof(from);
      received = recvfrom(towardsHost, buffer, sizeof(buffer), 0, reinterpret_cast<sockaddr*>(&from), &fromLength);
      if (received > 0 && guestPort != 0) {
        SendTo(towardsGuest, guestPort, std::string(buffer, static_cast<std::size_t>(received)));
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  }

  void ALostNetworkIsSilenceUntilItComesBack()
  {
    // The Wi-Fi that goes: no message says so. The connections show how long the other side has been
    // quiet, which the lobby's dialog and the game's connection dialog go by, and go on when it returns.
    moho::INetConnector* const host = MakeConnector();
    moho::INetConnector* const guest = MakeConnector();
    CHECK(host != nullptr && guest != nullptr);
    if (host == nullptr || guest == nullptr) {
      return;
    }
    const SOCKET towardsGuest = OpenUdp();
    const SOCKET towardsHost = OpenUdp();
    const WSAEVENT unused = WSACreateEvent();
    CHECK(WSAEventSelect(towardsGuest, unused, FD_READ) == 0); // makes them non-blocking
    CHECK(WSAEventSelect(towardsHost, unused, FD_READ) == 0);
    std::atomic<bool> running{true};
    std::thread relay(&RelayDatagrams, towardsGuest, towardsHost, host->GetLocalPort(), std::cref(running));

    Messages* const guestInbox = new Messages;
    moho::INetConnection* const connection = guest->Connect(kLoopback, PortOf(towardsGuest));
    connection->PushReceiver(0, 256, guestInbox);
    u_long address = 0;
    u_short port = 0;
    CHECK(Pump(host, guest, [&] { return host->FindNextAddress(address, port); }));
    Messages* const hostInbox = new Messages;
    moho::INetConnection* const accepted = host->Accept(address, port);
    accepted->PushReceiver(0, 256, hostInbox);
    CHECK(Pump(host, guest, [&] { return !guestInbox->types.empty() && !hostInbox->types.empty(); }));

    running = false;
    relay.join();
    // Past the ten seconds after which a connection counts as timed out.
    CHECK(Pump(host, guest, [&] { return connection->GetTime() > 11000.0f && accepted->GetTime() > 11000.0f; }, 20));

    running = true;
    relay = std::thread(&RelayDatagrams, towardsGuest, towardsHost, host->GetLocalPort(), std::cref(running));
    connection->Write(Request("after the silence"));
    CHECK(Pump(host, guest, [&] { return !hostInbox->payloads.empty() && hostInbox->payloads.back() == "after the silence"; }, 10));
    CHECK(Pump(host, guest, [&] { return connection->GetTime() < 2000.0f && accepted->GetTime() < 2000.0f; }, 10));
    running = false;
    relay.join();

    connection->ScheduleDestroy();
    accepted->ScheduleDestroy();
    guest->Destroy();
    host->Destroy();
    CHECK(closesocket(towardsHost) == 0);
    CHECK(closesocket(towardsGuest) == 0);
    CHECK(WSACloseEvent(unused) != FALSE);
  }

  /// The next DATA packet with something in it past sequence number `after` that waits at `socket`, as a relay
  /// that stands still leaves them; what comes before it is lost, as a network may lose it. Empty if none comes.
  std::string TakeData(const SOCKET socket, const int after, int& sequence)
  {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    char buffer[1500];
    while (std::chrono::steady_clock::now() < end) {
      sockaddr_in from{};
      int fromLength = sizeof(from);
      const int received = recvfrom(socket, buffer, sizeof(buffer), 0, reinterpret_cast<sockaddr*>(&from), &fromLength);
      if (received >= static_cast<int>(moho::kNetPacketHeaderSize)) {
        moho::SNetPacketHeader header;
        std::memcpy(&header, buffer, sizeof(header));
        if (header.mType == moho::PT_Data && header.mPayloadLength != 0 && header.mSequenceNumber > after) {
          sequence = header.mSequenceNumber;
          return std::string(buffer, static_cast<std::size_t>(received));
        }
      } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
    }
    return {};
  }

  void WhatArrivesAfterDestroySetsNoEvent()
  {
    // The game closes its event right after Destroy, and the connection lives on until its peer has
    // acknowledged the end of its stream, or for ten seconds: data from a live peer reaches it meanwhile.
    // This test is the network from the handshake on, and hands the host one packet at a time.
    moho::INetConnector* const host = MakeConnector();
    moho::INetConnector* const guest = MakeConnector();
    CHECK(host != nullptr && guest != nullptr);
    if (host == nullptr || guest == nullptr) {
      return;
    }
    const SOCKET towardsGuest = OpenUdp();
    const SOCKET towardsHost = OpenUdp();
    const WSAEVENT unused = WSACreateEvent();
    CHECK(WSAEventSelect(towardsGuest, unused, FD_READ) == 0); // makes them non-blocking
    CHECK(WSAEventSelect(towardsHost, unused, FD_READ) == 0);
    const u_short hostPort = host->GetLocalPort();
    std::atomic<bool> running{true};
    std::thread relay(&RelayDatagrams, towardsGuest, towardsHost, hostPort, std::cref(running));

    Messages* const guestInbox = new Messages;
    moho::INetConnection* const connection = guest->Connect(kLoopback, PortOf(towardsGuest));
    connection->PushReceiver(0, 256, guestInbox);
    u_long address = 0;
    u_short port = 0;
    CHECK(Pump(host, guest, [&] { return host->FindNextAddress(address, port); }));
    Messages* const hostInbox = new Messages;
    moho::INetConnection* const accepted = host->Accept(address, port);
    accepted->PushReceiver(0, 256, hostInbox);
    CHECK(Pump(host, guest, [&] { return !guestInbox->types.empty() && !hostInbox->types.empty(); }));
    running = false;
    relay.join();

    const HANDLE selected = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    host->SelectEvent(selected);

    // Before Destroy a packet of the peer's data sets the game's event.
    connection->Write(Request("before"));
    int sequence = -1;
    const std::string before = TakeData(towardsGuest, sequence, sequence);
    CHECK(!before.empty());
    SendTo(towardsHost, hostPort, before);
    CHECK(Signalled(selected, kArrivalMs));

    // After it the same does not, though the connection takes it: its stream's end stays unacknowledged.
    CHECK(::ResetEvent(selected) != FALSE);
    accepted->ScheduleDestroy();
    host->Push();
    host->Destroy();
    connection->Write(Request("after"));
    const std::string after = TakeData(towardsGuest, sequence, sequence);
    CHECK(!after.empty());
    SendTo(towardsHost, hostPort, after);
    CHECK(!Signalled(selected, kSilenceMs));
    CHECK(::CloseHandle(selected) != FALSE);

    connection->ScheduleDestroy();
    guest->Destroy();
    CHECK(closesocket(towardsHost) == 0);
    CHECK(closesocket(towardsGuest) == 0);
    CHECK(WSACloseEvent(unused) != FALSE);
  }

  /// As FAF's lobby is destroyed from a script callback: the connector's Destroy, inside its Pull's dispatch.
  struct DestroysItsConnector final : moho::IMessageReceiver
  {
    moho::INetConnector* connector = nullptr;

    void ReceiveMessage(moho::CMessage* const message, moho::CMessageDispatcher*) override
    {
      if (connector != nullptr && message->GetType().raw() == static_cast<unsigned>(moho::ELobbyMsg::LOBMSG_DiscoveryRequest)) {
        connector->Destroy();
        connector = nullptr;
      }
    }
  };

  void ADestroyInsidePullEndsTheConnector()
  {
    moho::INetConnector* const guest = MakeConnector();
    CHECK(guest != nullptr);
    if (guest == nullptr) {
      return;
    }
    // A few times: when Pull lets go, the connector's thread may delete the connector at once.
    for (int round = 0; round < 5; ++round) {
      moho::INetConnector* const host = MakeConnector();
      CHECK(host != nullptr);
      if (host == nullptr) {
        return;
      }
      const u_short hostPort = host->GetLocalPort();
      moho::INetConnection* const toHost = guest->Connect(kLoopback, hostPort);
      u_long address = 0;
      u_short port = 0;
      CHECK(Pump(host, guest, [&] { return host->FindNextAddress(address, port); }));
      DestroysItsConnector* const receiver = new DestroysItsConnector;
      receiver->connector = host;
      host->Accept(address, port)->PushReceiver(0, 256, receiver);
      toHost->Write(Request("the end"));

      const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
      while (receiver->connector != nullptr && std::chrono::steady_clock::now() < end) {
        host->Pull();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      CHECK(receiver->connector == nullptr);

      // Its connection says goodbye, the thread deletes the connector, and the socket with it.
      moho::INetConnector* again = nullptr;
      const auto portEnd = std::chrono::steady_clock::now() + std::chrono::seconds(15);
      while (again == nullptr && std::chrono::steady_clock::now() < portEnd) {
        again = MakeConnector(hostPort);
        if (again == nullptr) {
          std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
      }
      CHECK(again != nullptr);
      if (again != nullptr) {
        again->Destroy();
      }
      toHost->ScheduleDestroy();
    }
    guest->Destroy();
  }

  void AThreadThatDoesNotStartEndsTheConstructor()
  {
    // The connector's last owner deletes it, but not what is still being constructed: the exception unwinds the
    // constructor as any other, and the new-expression frees the memory. Half of what this process has mapped is
    // all it may have for a moment, so a new thread's stack does not fit, also when the thread of an earlier
    // connector ends meanwhile and gives its own stack back; the heap's regions are mapped already.
    rlimit before{};
    CHECK(::getrlimit(RLIMIT_AS, &before) == 0);
    FILE* const statm = std::fopen("/proc/self/statm", "r");
    unsigned long long pages = 0;
    CHECK(statm != nullptr && std::fscanf(statm, "%llu", &pages) == 1);
    if (pages == 0) {
      return;
    }
    CHECK(std::fclose(statm) == 0);
    rlimit tight = before;
    tight.rlim_cur = pages * static_cast<unsigned long long>(::sysconf(_SC_PAGESIZE)) / 2;
    CHECK(::setrlimit(RLIMIT_AS, &tight) == 0);
    bool thrown = false;
    try {
      (void)MakeConnector();
    } catch (const boost::thread_resource_error&) {
      thrown = true;
    } catch (const std::bad_alloc&) {
      std::printf("  the heap wanted more room before the thread: not checked\n");
      thrown = true;
    }
    CHECK(::setrlimit(RLIMIT_AS, &before) == 0);
    CHECK(thrown);

    moho::INetConnector* const next = MakeConnector();
    CHECK(next != nullptr);
    if (next != nullptr) {
      next->Destroy();
    }
  }

  // --- What goes on the wire ---------------------------------------------------------------------

  /// A connection that keeps what is written to it.
  struct Recorder final : moho::INetConnection
  {
    std::string written;

    u_long GetAddr() override { return kLoopback; }
    u_short GetPort() override { return 1; }
    float GetPing() override { return 0.0f; }
    float GetTime() override { return 0.0f; }
    void Write(moho::NetDataSpan* const data) override { written.append(reinterpret_cast<const char*>(data->start), data->size()); }
    void Close() override {}
    msvc8::string ToString() override { return "recorder"; }
    void ScheduleDestroy() override {}
  };

  void NewPeerIsTheNamePlusTenBytes()
  {
    // The host passes each player on to the others, on Windows as 4 + 2 + 4 bytes behind the name, and the
    // Windows game reads them so. u_long is 8 bytes on Android's 64-bit systems.
    Recorder connection;
    const moho::SPeer peer(msvc8::string("Third"), 0x01020304, 0x0A0B0C0D, 0x1E1F, &connection, moho::ENetworkPlayerState::kEstablished);
    peer.SendInfoTo(&connection);

    const std::string name("Third\0", 6);
    const std::string expected = name + std::string("\x0D\x0C\x0B\x0A", 4) + std::string("\x1F\x1E", 2) + std::string("\x04\x03\x02\x01", 4);
    CHECK(connection.written.size() == 3 + name.size() + 10);
    CHECK(connection.written.size() >= 3 && static_cast<std::uint8_t>(connection.written[0]) == static_cast<std::uint8_t>(moho::ELobbyMsg::LOBMSG_NewPeer));
    CHECK(connection.written.size() >= 3 && static_cast<std::uint8_t>(connection.written[1]) == 3 + expected.size());
    CHECK(connection.written.size() >= 3 && connection.written.substr(3) == expected);
  }

  void WhatIsCopiedRawHasItsSize()
  {
    // Written and read as raw bytes, with no static_assert on them in a 64-bit build (platform/LayoutAsserts.h).
    CHECK(sizeof(moho::SNetPacketHeader) == 15);
    CHECK(sizeof(moho::SPacketBodyConnect) == 45);
    CHECK(sizeof(moho::SPacketBodyAnswer) == 77);
    CHECK(sizeof(gpg::MD5Digest) == 16);
    CHECK(sizeof(moho::SCoordsVec2) == 8);
    CHECK(sizeof(Wm3::Vector3<float>) == 12);
    CHECK(sizeof(Wm3::Quaternion<float>) == 16);
    CHECK(sizeof(moho::VTransform) == 28);
    CHECK(sizeof(moho::SOCellPos) == 4);
  }

  // --- Leaving a game ------------------------------------------------------------------------------

  void EndingTheGameLeavesTheBeatsToThisClient()
  {
    // A local and a net client, whose connection has no peer: nobody sends again, as after End Game.
    moho::INetConnector* const connector = MakeConnector();
    CHECK(connector != nullptr);
    if (connector == nullptr) {
      return;
    }
    moho::INetConnection* const connection = connector->Connect(kLoopback, FreePort());
    moho::CClientManagerImpl* const manager = moho::CLIENT_CreateClientManager(2, connector, 0, false);
    Messages stream;
    manager->PushReceiver(0u, 50u, &stream);
    manager->CreateLocalClient("this one", 0, 1, 0);
    auto* const other = static_cast<moho::CNetClient*>(manager->CreateNetClient("other", 1, 2, 1, connection));
    moho::CMarshaller marshaller(manager);
    manager->Cleanup(); // this client has loaded; the other says nothing, as one still loading

    // It held the game to a tenth of the speed when it was last heard.
    moho::CMessage rate{moho::EClientMsg::CLIMSG_IntParam};
    const std::int32_t slow = -10;
    rate.Append(reinterpret_cast<const char*>(&slow), sizeof(slow));
    other->ReceiveMessage(&rate, nullptr);
    CHECK(manager->GetSimRate() == -10);

    // This client's five beats wait for the other.
    int partial = 0;
    int available = 0;
    marshaller.AdvanceBeat(5);
    manager->DoBeat();
    manager->GetPartiallyQueuedBeat(partial);
    manager->GetAvailableBeat(available);
    CHECK(partial == 5);
    CHECK(available == 0);
    CHECK(!manager->IsEveryoneReady());

    // End Game: no more waiting for it, and no beat from nowhere. The issue thread takes the partially queued
    // beat as what it may issue while the sim is over: a client that counted as having queued them all ran it
    // at one beat each pass (CSimDriver::ThreadRun).
    manager->Disconnect();
    for (int pass = 0; pass < 10; ++pass) {
      manager->DoBeat();
    }
    manager->GetPartiallyQueuedBeat(partial);
    manager->GetAvailableBeat(available);
    CHECK(partial == 5);
    CHECK(available == 5);
    CHECK(manager->IsEveryoneReady());
    CHECK(manager->GetSimRate() == 0);

    // The next beat this client issues carries the end into the stream, and nothing says the other's army is
    // abandoned: the player who left was this one.
    marshaller.AdvanceBeat(1);
    manager->DoBeat();
    manager->GetPartiallyQueuedBeat(partial);
    manager->GetAvailableBeat(available);
    CHECK(partial == 6);
    CHECK(available == 6);
    for (int beat = 1; beat <= available; ++beat) {
      manager->UpdateStates(beat);
    }
    const auto seen = [&](const moho::ECmdStreamOp op) {
      return std::find(stream.types.begin(), stream.types.end(), static_cast<unsigned>(op)) != stream.types.end();
    };
    CHECK(seen(moho::ECmdStreamOp::CMDST_EndGame));
    CHECK(!seen(moho::ECmdStreamOp::CMDST_CommandSourceTerminated));

    delete manager;
  }

  struct Test
  {
    const char* name;
    void (*run)();
  };

  const Test kTests[] = {
    {"EventsAreManualReset", &EventsAreManualReset},
    {"SelectRefusesWhatIsNoSelection", &SelectRefusesWhatIsNoSelection},
    {"SelectMakesTheSocketNonBlocking", &SelectMakesTheSocketNonBlocking},
    {"ReadIsSignalledAgainOnlyAfterRecvfrom", &ReadIsSignalledAgainOnlyAfterRecvfrom},
    {"RecvfromFillsInTheSender", &RecvfromFillsInTheSender},
    {"WriteIsSignalledOnce", &WriteIsSignalledOnce},
    {"ANewSelectionReplacesTheOld", &ANewSelectionReplacesTheOld},
    {"CloseEndsTheSelectionAndFreesThePort", &CloseEndsTheSelectionAndFreesThePort},
    {"WhatWaitsCostsNoCpu", &WhatWaitsCostsNoCpu},
    {"DatagramSocketSendsToItself", &DatagramSocketSendsToItself},
    {"DatagramSocketBroadcasts", &DatagramSocketBroadcasts},
    {"ConnectorsTalkOverTheLoopback", &ConnectorsTalkOverTheLoopback},
    {"ALostNetworkIsSilenceUntilItComesBack", &ALostNetworkIsSilenceUntilItComesBack},
    {"WhatArrivesAfterDestroySetsNoEvent", &WhatArrivesAfterDestroySetsNoEvent},
    {"ADestroyInsidePullEndsTheConnector", &ADestroyInsidePullEndsTheConnector},
    {"AThreadThatDoesNotStartEndsTheConstructor", &AThreadThatDoesNotStartEndsTheConstructor},
    {"NewPeerIsTheNamePlusTenBytes", &NewPeerIsTheNamePlusTenBytes},
    {"WhatIsCopiedRawHasItsSize", &WhatIsCopiedRawHasItsSize},
    {"EndingTheGameLeavesTheBeatsToThisClient", &EndingTheGameLeavesTheBeatsToThisClient},
  };
} // namespace

int main()
{
  // Whatever a crash or a hang leaves of this output on a pipe: bionic's stdout is fully buffered there.
  std::setvbuf(stdout, nullptr, _IOLBF, 0);

  int failedTests = 0;
  for (const Test& test : kTests) {
    gFailedChecks = 0;
    std::printf("%s\n", test.name);
    test.run();
    if (gFailedChecks != 0) {
      ++failedTests;
      std::printf("  FAIL\n");
    }
  }
  std::printf("%zu tests, %d failed\n", std::size(kTests), failedTests);
  // Without the engine's teardown: the connectors' threads may still be saying goodbye.
  std::fflush(stdout);
  std::_Exit(failedTests == 0 ? 0 : 1);
}
