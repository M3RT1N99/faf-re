// Live networking (moho/net/INetDatagramSocket.cpp, INetTCPSocket.cpp, CHostManager.cpp,
// CGpgNetInterface.cpp) for the Android headless runner; see HeadlessStubs.h.
//
// The runner state: a replay plays from a file through CReplayClient and CLocalClient; nothing opens
// a socket. The closure's callers are the GPGNet connection of SessionStartup.cpp (only with
// /gpgnet), LAN game discovery (CDiscoveryService.cpp, started only by the lobby's Lua) and
// CWldUiInterface (IClientMgrUIInterface.cpp), which the runner never installs.
//
// moho/net/Common.cpp is not stubbed here: CSimDriver measures its own speed with Common.cpp's
// NetSpeeds (SimDriver.cpp), which must be the real code, so Common.cpp is ported and linked (M3b
// integration). Its connector factories leave the connectors out off Windows (NET_Init() is false
// there); its host-name lookup (NET_GetHostName) keeps CHostManager, whose stand-ins below are
// traps. If the socket TUs below are linked for real as well, delete their stand-ins here.

#include "HeadlessStubs.h"

#include "moho/net/CGpgNetInterface.h"
#include "moho/net/CHostManager.h"
#include "moho/net/Common.h"
#include "moho/net/INetDatagramSocket.h"
#include "moho/net/INetTCPSocket.h"

namespace moho
{
  // CHostManager.cpp:73, :78, :189 (the reverse-lookup cache behind NET_GetHostName, Common.cpp:955;
  // the instance is a function-local static of NET_GetHostManager, built on the first lookup). The lookups come only from the
  // /gpgnet connection (SessionStartup.cpp) and LAN game discovery (CDiscoveryService.cpp), which the
  // runner never starts; a stand-in could not answer a DNS reverse lookup anyway, so these are traps.
  CHostManager::CHostManager()
  {
    FAF_RUNNER_TRAP("CHostManager::CHostManager");
  }

  CHostManager::~CHostManager()
  {
    FAF_RUNNER_TRAP("CHostManager::~CHostManager");
  }

  msvc8::string CHostManager::GetHostName(const std::uint32_t host)
  {
    (void)host;
    FAF_RUNNER_TRAP("CHostManager::GetHostName");
  }

  // INetDatagramSocket.cpp:49: opens a UDP socket (caller: CDiscoveryService.cpp).
  INetDatagramSocket* NET_OpenDatagramSocket(const u_short port, INetDatagramHandler* const handler)
  {
    (void)port;
    (void)handler;
    FAF_RUNNER_STUB("NET_OpenDatagramSocket");
    return nullptr;
  }

  // INetTCPSocket.cpp:35: connects a TCP socket (caller: SessionStartup.cpp's /gpgnet connection).
  INetTCPSocket* NET_TCPConnect(const u_long address, const u_short port)
  {
    (void)address;
    (void)port;
    FAF_RUNNER_STUB("NET_TCPConnect");
    return nullptr;
  }

  // CHostManager.cpp:119: resolves a host name (caller: SessionStartup.cpp's /gpgnet connection).
  bool NET_GetAddrInfo(const char* const str, const u_short defaultPort, const bool isTcp, u_long& address, u_short& port)
  {
    (void)str;
    (void)defaultPort;
    (void)isTcp;
    (void)address;
    (void)port;
    FAF_RUNNER_STUB("NET_GetAddrInfo");
    return false;
  }

  // CGpgNetInterface.cpp:380 and :445: both return at once without a GPGNet connection, which only
  // /gpgnet makes (the first also without WLD_GetDriver(), null in the runner).
  void GPGNET_ReportBottleneck(const SClientBottleneckInfo& info)
  {
    (void)info;
    FAF_RUNNER_STUB("GPGNET_ReportBottleneck");
  }

  void GPGNET_ReportBottleneckCleared()
  {
    FAF_RUNNER_STUB("GPGNET_ReportBottleneckCleared");
  }
} // namespace moho
