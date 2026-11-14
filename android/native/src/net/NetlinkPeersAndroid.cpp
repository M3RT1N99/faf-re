// The in-engine peer links (moho/net/NetlinkPeers.h) for the Android build, in place of NetlinkPeers.cpp,
// which runs them on netlink (src/netlink: libjuice and libdatachannel, with usrsctp, plog and Mbed TLS).
// This build does not make those libraries yet. The game talks to an ICE adapter as the shipped game does,
// which is what these functions do with net_InEngineLinks and net_NativeNet off; asking for either says in
// the log that the build has neither, and the network panel shows no links. The game's own lobby needs none
// of this: its UDP connector works on the sockets themselves.

#include "moho/net/NetlinkPeers.h"

#include "gpg/core/utils/Logging.h"
#include "moho/misc/StartupHelpers.h"
#include "moho/net/NetConVars.h"

namespace moho
{
  SNetlinkSnapshot NETLINK_GetSnapshot()
  {
    return SNetlinkSnapshot{};
  }

  void NETLINK_KeepLogLines() {}

  bool NETLINK_ReconnectPeer(std::int64_t, const faf::netlink::PathChoice*)
  {
    return false;
  }

  void NETLINK_SayHello(CGpgNetInterface&)
  {
    if (net_InEngineLinks != 0 || CFG_GetArgOption("/inenginelinks", 0u, nullptr)) {
      gpg::Warnf("NETLINK: this Android build has no in-engine links");
    }
  }

  bool NETLINK_HandleCommand(const fa::string&, const fa::vector<SNetCommandArg>&)
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
      gpg::Warnf("NATIVENET: this Android build has no native networking");
    }
  }

  bool NATIVENET_HandleCommand(const fa::string&, fa::vector<SNetCommandArg>&)
  {
    return false;
  }

  void NATIVENET_OnGameMessage(const char*) {}
} // namespace moho
