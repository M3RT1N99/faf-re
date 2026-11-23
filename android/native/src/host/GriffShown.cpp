// What the touch scheme Griff shows that the touch layer and the program know of (GriffShown.h): the numbers each piece had
// last, which FAF's UI reads each frame of the session's UI and draws in FAF's look (fafre/griffmap.lua, FafreGriffShown).

#include "GriffShown.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <mutex>
#include <utility>
#include <vector>

#include "moho/lua/CScrLuaBinder.h"

namespace
{
  std::mutex gShownMutex;
  std::array<std::vector<float>, griffshown::kPieces> gShown;

  constexpr char kShownHelp[] =
    "FafreGriffShown(piece, values) - what Griff shows of the activity's and the program's (Game.SHOWN_): the piece's numbers into "
    "values; how many";

  int Shown(lua_State* const lua)
  {
    const int piece = static_cast<int>(lua_tonumber(lua, 1));
    std::vector<float> values;
    if (piece >= 0 && piece < griffshown::kPieces) {
      const std::lock_guard<std::mutex> lock(gShownMutex);
      values = gShown[static_cast<std::size_t>(piece)];
    }
    if (lua_istable(lua, 2)) {
      for (std::size_t at = 0; at < values.size(); ++at) {
        lua_pushnumber(lua, values[at]);
        lua_rawseti(lua, 2, static_cast<int>(at) + 1);
      }
    }
    lua_pushnumber(lua, static_cast<lua_Number>(values.size()));
    return 1;
  }

  moho::CScrLuaBinder gShownBinder(moho::SCR_UserLuaInitSet(), "FafreGriffShown", &Shown, nullptr, "<global>", kShownHelp);
} // namespace

void griffshown::Set(const Piece piece, std::vector<float> values)
{
  if (piece < 0 || piece >= kPieces) {
    return;
  }
  const std::lock_guard<std::mutex> lock(gShownMutex);
  gShown[static_cast<std::size_t>(piece)] = std::move(values);
}
