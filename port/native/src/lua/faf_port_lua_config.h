/*
 * Prefix header of the port's Lua core (faf_port_lua).
 *
 * Force-included into every LuaPlus C file of faf_port_lua and included first
 * by src/LuaCore.h, so the core and its C++ callers agree on lua_Number.
 *
 * 1. The game's number type. faf_lua_config.h (copied from
 *    dependencies/LuaPlus_Build1081 into the staged tree) makes lua_Number a
 *    float, as in Forged Alliance; scripts see the same rounding and the same
 *    tostring() output ("%.9g") as in the game.
 *
 * 2. Wide-char shims for non-Windows C libraries. LuaPlus 1081 was written for
 *    MSVC, whose headers declare the wide-char functions implicitly and offer
 *    _snwprintf. bionic and glibc need <wchar.h>/<wctype.h>, and C99
 *    swprintf(buf, count, fmt, ...) has _snwprintf's argument order. Only
 *    LuaPlusAddons.c (lua_number2wstr) and lobject.c/lbaselib.c/liolib.c touch
 *    these, and only for LuaPlus wide strings, which the port never creates.
 */
#ifndef FAF_PORT_LUA_CONFIG_H
#define FAF_PORT_LUA_CONFIG_H

#include "faf_lua_config.h"

#if !defined(_WIN32)
#include <wchar.h>
#include <wctype.h>
#ifndef _snwprintf
#define _snwprintf swprintf
#endif
#endif

#endif /* FAF_PORT_LUA_CONFIG_H */
