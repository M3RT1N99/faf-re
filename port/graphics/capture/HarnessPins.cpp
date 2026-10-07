// The frame harness's determinism pins (M6a step 0): the clock the UI's Lua reads and the
// global random stream. Port-only (FAF_PORT_GRAPHICS), active only under `/galharness`.
//
// Why these two:
//   - Time. `/framerate N` already fixes the frame delta that CScApp::Main hands to the UI,
//     the world and the renderer (CScApp.cpp:1039-1059), so OnFrame animations are frame
//     exact. But the user Lua functions CurrentTime, GetSystemTimeSeconds and GetSystemTime
//     read the wall clock (gpg::time::GetSystemTimer, Sim.cpp:14387, 18411, 18495), and FAF's
//     WaitSeconds is built on CurrentTime (lua.nx2 lua/userInit.lua:52-67: it waits frames until
//     CurrentTime() - start >= n). The main menu starts its first animation with
//     `ForkThread(function() WaitSeconds(.2) MenuAnimation(true) end)` (lua/ui/menus/main.lua:
//     678-680), so on the wall clock the frame it starts on depends on how fast the machine
//     ran the frames before. Under the harness the three functions read a virtual clock instead:
//     the sum of the frame deltas CScApp::Main has handed out (HarnessFrame).
//   - Random. moho::math_GlobalRandomStream is seeded from time(0) ^ GetTickCount()
//     (MathReflection.cpp:259), and it backs the user Lua `Random` (Sim.cpp:14794-14838) - e.g. a
//     "random" UI skin (lua.nx2 lua/ui/uiutil.lua SetCurrentSkin). Under the harness it gets a
//     fixed seed (`/galseed`, default 0x6D366100).
//
// The clock is swapped at the binder, not in Sim.cpp: every CScrLuaBinder whose function is one
// of the three wall-clock functions gets the harness function instead, during static
// initialisation, before any Lua state exists - so every state, from the first, registers the
// harness version (CScrLuaBinder::Run, CScrLuaBinder.cpp:44-54). Wrong argument counts still go
// to the engine function, which raises the engine's own error.

#include "port/graphics/capture/HarnessInternal.h"

#include <cstdio>
#include <string>

#include "lua/LuaObject.h"
#include "moho/lua/CScrLuaBinder.h"
#include "moho/lua/CScrLuaInitForm.h"
#include "moho/math/MathReflection.h"
#include "moho/sim/CRandomStream.h"
#include "moho/sim/Sim.h"
#include "moho/ui/UiRuntimeTypes.h"

namespace port::graphics::capture::detail
{
  namespace
  {
    using LuaFunction = moho::CScrLuaBinder::LuaFunction;

    double gClockSeconds = 0.0;

    LuaFunction gEngineCurrentTime = nullptr;
    LuaFunction gEngineGetSystemTimeSeconds = nullptr;
    LuaFunction gEngineGetSystemTime = nullptr;

    std::vector<std::string> gSwapped; // "<set> <name>" per rewritten binder
    unsigned gClockReads = 0;          // Lua calls answered from the virtual clock (UI thread)

    // The engine functions push a float (Timer::ElapsedSeconds) as a Lua number; so do these.
    int __cdecl HarnessCurrentTime(lua_State* const state)
    {
      if (lua_gettop(state) != 0) {
        return gEngineCurrentTime(state);
      }
      ++gClockReads;
      lua_pushnumber(state, static_cast<float>(gClockSeconds));
      return 1;
    }

    int __cdecl HarnessGetSystemTimeSeconds(lua_State* const state)
    {
      if (lua_gettop(state) != 0) {
        return gEngineGetSystemTimeSeconds(state);
      }
      ++gClockReads;
      lua_pushnumber(state, static_cast<float>(gClockSeconds));
      return 1;
    }

    // Same "HH:MM:SS" text as cfunc_GetSystemTimeL (Sim.cpp:18399-18431); the clock is never
    // negative here.
    int __cdecl HarnessGetSystemTime(lua_State* const state)
    {
      if (lua_gettop(state) != 0) {
        return gEngineGetSystemTime(state);
      }
      ++gClockReads;
      const auto totalSeconds = static_cast<unsigned long long>(static_cast<float>(gClockSeconds));
      char formatted[16]{};
      (void)std::snprintf(
        formatted, sizeof(formatted), "%02d:%02d:%02d", static_cast<int>((totalSeconds / 3600ULL) % 24ULL),
        static_cast<int>((totalSeconds / 60ULL) % 60ULL), static_cast<int>(totalSeconds % 60ULL)
      );
      lua_pushstring(state, formatted);
      return 1;
    }

    void SeedRandomStream()
    {
      boost::mutex::scoped_lock lock(moho::math_GlobalRandomMutex);
      moho::math_GlobalRandomStream.Seed(Config().seed);
    }
  } // namespace

  void AdvanceClock(const double seconds) noexcept
  {
    gClockSeconds += seconds;
  }

  double ClockSeconds() noexcept
  {
    return gClockSeconds;
  }

  bool InstallPins(std::string* const error)
  {
    for (moho::CScrLuaInitFormSet* set = moho::CScrLuaInitFormSet::GetFirst(); set != nullptr; set = set->GetNext()) {
      for (moho::CScrLuaInitForm* form = set->mForms; form != nullptr; form = form->mNextInSet) {
        auto* const binder = dynamic_cast<moho::CScrLuaBinder*>(form);
        if (binder == nullptr) {
          continue;
        }
        LuaFunction replacement = nullptr;
        if (binder->mFunction == &moho::cfunc_CurrentTime) {
          gEngineCurrentTime = binder->mFunction;
          replacement = &HarnessCurrentTime;
        } else if (binder->mFunction == &moho::cfunc_GetSystemTimeSeconds) {
          gEngineGetSystemTimeSeconds = binder->mFunction;
          replacement = &HarnessGetSystemTimeSeconds;
        } else if (binder->mFunction == &moho::cfunc_GetSystemTime) {
          gEngineGetSystemTime = binder->mFunction;
          replacement = &HarnessGetSystemTime;
        }
        if (replacement != nullptr) {
          binder->mFunction = replacement;
          gSwapped.push_back(
            std::string(set->mSetName != nullptr ? set->mSetName : "?") + " " + (form->mName != nullptr ? form->mName : "?")
          );
        }
      }
    }

    if (gEngineCurrentTime == nullptr || gEngineGetSystemTimeSeconds == nullptr || gEngineGetSystemTime == nullptr) {
      *error =
        "the Lua wall-clock binders (CurrentTime, GetSystemTimeSeconds, GetSystemTime) were not all registered "
        "when the harness initialised; the virtual clock cannot be installed";
      return false;
    }

    std::string list;
    for (const std::string& entry : gSwapped) {
      list += entry + "; ";
    }
    Log("virtual clock installed in %u binders: %s", static_cast<unsigned>(gSwapped.size()), list.c_str());
    SeedRandomStream();
    Log("global random stream seeded with 0x%08X", Config().seed);
    return true;
  }

  // The binders are swapped before any Lua state exists, and CScrLuaBinder::Run registers
  // whatever mFunction holds when a state is created, so every state has the harness clock. What
  // is left to check on the first frame is that the engine is the one this was built against:
  // the user state exists. The summary also counts the clock reads (`clock_reads`), which shows
  // the menu's WaitSeconds really went through it.
  bool VerifyPinsOnFirstFrame(std::string* const error)
  {
    LuaPlus::LuaState* const state = moho::USER_GetLuaState();
    if (state == nullptr || state->m_state == nullptr) {
      *error = "no user Lua state on the first frame";
      return false;
    }
    return true;
  }

  // Again on the first frame: whatever drew from the stream before (static initialisers, Init)
  // drew a fixed number of values, but restarting here also holds if an engine initialiser
  // ever runs after the harness's and reseeds from the time.
  void ReseedRandomStream() noexcept
  {
    SeedRandomStream();
  }

  std::string PinsReportJson()
  {
    std::string json = std::string("{\"pinned\": ") + (Config().noPins ? "false" : "true") + ", \"virtual_clock_binders\": [";
    for (std::size_t index = 0; index < gSwapped.size(); ++index) {
      json += (index != 0 ? ", " : "") + JsonString(gSwapped[index]);
    }
    char seed[16];
    (void)std::snprintf(seed, sizeof(seed), "0x%08X", Config().seed);
    json += "], \"clock_seconds\": " + std::to_string(gClockSeconds) + ", \"clock_reads\": " + std::to_string(gClockReads) +
            ", \"random_seed\": " + JsonString(seed) + "}";
    return json;
  }
} // namespace port::graphics::capture::detail
