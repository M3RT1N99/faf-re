// The console command SC_CreateEntityDialog for the Android client build. The engine's
// (moho/app/WRefEditDialog.cpp, not built here) opens the reflection editor on the first selected
// unit, a wxWindows tool window, which wx-lite leaves out (WX_LITE.md). The command keeps the
// engine's gate, cheats on in the active session, and says in the log that there is no window.

#include "gpg/core/utils/Logging.h"
#include "moho/sim/CWldSession.h"
#include "moho/sim/Sim.h"

void moho::funcl_SC_CreateEntityDialog()
{
  // patch_SC_CreateEntityDialog's gate.
  const CWldSession* const session = WLD_GetActiveSession();
  if (session != nullptr && session->IsCheatsEnabled) {
    gpg::Warnf("SC_CreateEntityDialog: the Android build has no entity dialog");
  }
}
