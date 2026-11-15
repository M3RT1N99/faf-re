// What the app's UI package (android/app/src/main/assets/fafre_ui, android/TOUCH_UI.md) asks of the app through the UI's
// globals, on SDL's activity, until the touch schemes come back to it: on the earlier host src/host/MenuFingers.cpp and
// GriffAndroid.cpp register these, and when they are built on SDL's input this file goes.
//
// - The menus' page, whose the fingers are, what to bring into view and the HUD's factor (FafreSetPage, FafreSetFingers,
//   FafreShowLines, FafreSetHud) go nowhere yet: fingers are SDL's mouse, and FAF's screens keep their size in the picture.
//   The factors the package gives FAF's dialogs and HUD are its own Lua's, and hold.
// - FAF's Menu tab's Keyboard (FafreKeyboard, the hook of ui/game/tabs.lua) brings the screen keyboard, or takes it away.
// - Griff is off (FafreGriffWanted, which FAF's construction panel's and control groups' hooks ask): fafre/griff.lua's Start
//   finds no FafreGriffBox and leaves FAF's UI as it is, as with the mouse scheme on the earlier host. Griff's other globals
//   come with GriffAndroid.cpp on SDL's input.

#include "ScreenKeyboardAndroid.h"
#include "lua/LuaPrimitives.h"
#include "moho/lua/CScrLuaBinder.h"

namespace
{
  int TakesNothing(lua_State*)
  {
    return 0;
  }

  int Keyboard(lua_State*)
  {
    screenkeyboard::Toggle();
    return 0;
  }

  int GriffWanted(lua_State* const lua)
  {
    lua_pushboolean(lua, 0);
    return 1;
  }

  constexpr char kSetPageHelp[] = "FafreSetPage([left, top, right, bottom]) - the picture's part the view fits to the screen's width, or none";
  constexpr char kSetFingersHelp[] = "FafreSetFingers(menu) - whether fingers act as in the menus or as in a game";
  constexpr char kShowLinesHelp[] = "FafreShowLines(top, bottom) - the page goes where the screen shows these lines of the picture";
  constexpr char kSetHudHelp[] = "FafreSetHud(factor) - the UI factor of the session's HUD, by which the app places its buttons";
  constexpr char kKeyboardHelp[] = "FafreKeyboard() - the phone's keyboard comes, or goes where it is up";
  constexpr char kGriffWantedHelp[] = "FafreGriffWanted() - whether the app's touch scheme Griff wants FafreGriffBox: not on SDL's activity yet";

  moho::CScrLuaBinder gSetPageBinder(moho::SCR_UserLuaInitSet(), "FafreSetPage", &TakesNothing, nullptr, "<global>", kSetPageHelp);
  moho::CScrLuaBinder gSetFingersBinder(moho::SCR_UserLuaInitSet(), "FafreSetFingers", &TakesNothing, nullptr, "<global>", kSetFingersHelp);
  moho::CScrLuaBinder gShowLinesBinder(moho::SCR_UserLuaInitSet(), "FafreShowLines", &TakesNothing, nullptr, "<global>", kShowLinesHelp);
  moho::CScrLuaBinder gSetHudBinder(moho::SCR_UserLuaInitSet(), "FafreSetHud", &TakesNothing, nullptr, "<global>", kSetHudHelp);
  moho::CScrLuaBinder gKeyboardBinder(moho::SCR_UserLuaInitSet(), "FafreKeyboard", &Keyboard, nullptr, "<global>", kKeyboardHelp);
  moho::CScrLuaBinder
    gGriffWantedBinder(moho::SCR_UserLuaInitSet(), "FafreGriffWanted", &GriffWanted, nullptr, "<global>", kGriffWantedHelp);
} // namespace
