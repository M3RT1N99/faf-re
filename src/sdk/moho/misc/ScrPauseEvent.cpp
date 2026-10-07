// A wx event for the Lua debugger window (ScrDebugWindow), posted only by ScrDebugHooks while that
// window is open. Android has no wxWidgets, so there the TU is empty: its one static initialiser is
// wxNewEventType(), not a Lua, RType or console registration.
#if defined(_WIN32)
#include "moho/misc/ScrPauseEvent.h"

/**
 * Address: 0x00BC5F40 (FUN_00BC5F40, dynamic initializer)
 */
DEFINE_EVENT_TYPE(moho::EVT_SCR_PAUSE)

/**
 * Address: 0x004B4330 (FUN_004B4330)
 */
moho::ScrPauseEvent::ScrPauseEvent(const msvc8::string& sourceName, const int sourceLine)
  : wxEvent(0, EVT_SCR_PAUSE)
  , mSourceName(sourceName)
  , mSourceLine(sourceLine)
{
}

/**
 * Address: 0x004B43F0 (FUN_004B43F0)
 */
wxEvent* moho::ScrPauseEvent::Clone() const
{
  return new ScrPauseEvent(*this);
}
#endif
