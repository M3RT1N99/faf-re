// NET_OpenDatagramSocket for the Android client build: the broadcast socket of the lobby's and
// the discovery service's game discovery. The engine's (moho/net/INetDatagramSocket.cpp) is part
// of its Winsock transport, which is Windows only and which the Linux build leaves out
// (cmake/FafLinux.cmake). Outside Windows NET_Init answers false, and the engine's function then
// returns null on its first line, as this one does. The callers go on without discovery
// (CLobby logs that its listener could not be created).

#include "moho/net/INetDatagramSocket.h"

namespace moho
{
  INetDatagramSocket* NET_OpenDatagramSocket(u_short, INetDatagramHandler*)
  {
    return nullptr;
  }
} // namespace moho
