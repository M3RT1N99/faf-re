#pragma once

// The port's Lua core (stock LuaPlus 1081 C API, patched copy built as
// faf_port_lua) for C++ callers. Always include this instead of lua.h so the
// prefix header comes first: lua.h only honours LUA_NUMBER when it is defined
// before it, and a caller that saw `double` would disagree with the core about
// every number on the stack.

extern "C" {
#include "faf_port_lua_config.h"

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"
}

static_assert(sizeof(lua_Number) == sizeof(float), "faf_port_lua must use the game's float lua_Number");
