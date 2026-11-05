// The in-engine peer links (moho/net/NetlinkPeers.h) for the Android client build, in place of
// NetlinkPeers.cpp, which runs them on netlink (src/netlink: libjuice and libdatachannel). Those
// are not built for Android yet: online play is step C5 (android/CLIENT_PLAN.md). The game talks to
// an ICE adapter as the shipped game does, which is what these functions do with net_InEngineLinks
// and net_NativeNet off; asking for either says in the log that the build has neither. The game's
// own lobby needs none of this: its UDP connector works here on the sockets themselves.

#include "moho/net/NetlinkPeers.h"

#include "gpg/core/utils/Logging.h"
#include "moho/misc/StartupHelpers.h"
#include "moho/net/NetConVars.h"

namespace moho
{
  void NETLINK_SayHello(CGpgNetInterface&)
  {
    if (net_InEngineLinks != 0 || CFG_GetArgOption("/inenginelinks", 0u, nullptr)) {
      gpg::Warnf("NETLINK: no in-engine links on Android yet (android/CLIENT_PLAN.md, C5)");
    }
  }

  bool NETLINK_HandleCommand(const msvc8::string&, const msvc8::vector<SNetCommandArg>&)
  {
    return false;
  }

  void NETLINK_Shutdown() {}

  bool NATIVENET_Enabled()
  {
    return false;
  }

  void NATIVENET_Start()
  {
    if (net_NativeNet != 0 || CFG_GetArgOption("/nativenet", 0u, nullptr)) {
      gpg::Warnf("NATIVENET: no native networking on Android yet (android/CLIENT_PLAN.md, C5)");
    }
  }

  bool NATIVENET_HandleCommand(const msvc8::string&, msvc8::vector<SNetCommandArg>&)
  {
    return false;
  }

  void NATIVENET_OnGameMessage(const char*) {}
} // namespace moho
