#include "faf/port/DataPath.h"

#include <chrono>
#include <cstring>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <utility>

#include "faf/port/FileSystem.h"

#include "FileSystemDetail.h"
#include "LuaCore.h"
#include "NativeFile.h"

namespace faf::port {

  namespace {

    // How the bindings stay safe. The Lua core is C and reports errors with
    // longjmp, which skips C++ destructors, and C++ exceptions must never
    // unwind through its frames. So every binding
    //   1. checks its Lua arguments first (those raise before any C++ object exists),
    //   2. does its C++ work inside a try block that owns all C++ objects and
    //      leaves only plain values or Lua stack entries behind, and
    //   3. raises Lua errors only after that block has closed.
    // The one exception is Lua running out of memory while a block pushes its
    // results; that can only leak the block's temporaries.
    //
    // All per-run state lives in a RunContext on RunDataPathScript's stack. The
    // bindings reach it through upvalue 1 (a light userdata), never through a
    // global, so two runs - in sequence or on two threads - share nothing.

    struct RunContext
    {
      const DataPathOptions* options = nullptr;
      std::string localAppData; ///< With a trailing '/', or empty when not configured.
      std::string appData;
      std::string personal;
      std::string launchDir;
      std::string initFileDir;
      std::size_t logLines = 0;

      void Log(const std::string_view line) const noexcept
      {
        if (!options->log) {
          return;
        }
        try {
          options->log(line);
        } catch (...) {
          // A failing log sink must not take the script run down with it.
        }
      }

      void Trace(const std::string_view line) const noexcept
      {
        if (!options->trace) {
          return;
        }
        try {
          options->trace(line);
        } catch (...) {
        }
      }

      void Trace(const char* const prefix, const char* const text) const noexcept
      {
        try {
          Trace(std::string(prefix) + text);
        } catch (...) {
        }
      }

      void LogSkippedWrite(const char* const what, const char* const path) const noexcept
      {
        try {
          Log(std::string("datapath: ") + what + "(" + path + ") skipped: writes are disabled for this run");
        } catch (...) {
        }
      }
    };

    [[nodiscard]] RunContext& Context(lua_State* const L)
    {
      return *static_cast<RunContext*>(lua_touserdata(L, lua_upvalueindex(1)));
    }

    [[nodiscard]] bool IsWriteMode(const char* const mode)
    {
      return std::strpbrk(mode, "wa+") != nullptr;
    }

    /// The SHGetFolderPath ids the engine accepts (kUnsafePaths in
    /// src/sdk/moho/misc/StartupHelpers.cpp). The ones without an Android
    /// counterpart map to LOCAL_APPDATA; anything else is an error, as there.
    constexpr const char* kEngineFolderIds[] = {
      "DESKTOP",           "CSIDL_FAVORITES",     "CSIDL_STARTUP",      "CSIDL_RECENT",
      "SENDTO",            "BITBUCKET",           "STARTMENU",          "MYMUSIC",
      "MYVIDEO",           "DESKTOPDIRECTORY",    "FONTS",              "TEMPLATES",
      "COMMON_STARTMENU",  "COMMON_PROGRAMS",     "COMMON_STARTUP",     "COMMON_DESKTOPDIRECTORY",
      "COMMON_FAVORITES",  "COMMON_APPDATA",      "PROGRAM_FILES",      "MYPICTURES",
      "PROFILE",           "SYSTEMX86",           "PROGRAM_FILESX86",   "PROGRAM_FILES_COMMON",
      "PROGRAM_FILES_COMMONX86", "COMMON_TEMPLATES", "COMMON_DOCUMENTS", "COMMON_MUSIC",
      "COMMON_PICTURES",   "COMMON_VIDEO",
    };

    [[nodiscard]] const std::string* FolderFor(const RunContext& context, const std::string_view id)
    {
      if (id == "LOCAL_APPDATA") {
        return &context.localAppData;
      }
      if (id == "APPDATA") {
        return &context.appData;
      }
      if (id == "PERSONAL" || id == "MYDOCUMENTS") {
        return &context.personal;
      }
      for (const char* const known : kEngineFolderIds) {
        if (id == known) {
          return &context.localAppData;
        }
      }
      return nullptr;
    }

    /// Pushes the path a script's file access should use: the case-insensitive
    /// resolution of `path` (for writes: of its directory), or `path` itself
    /// when nothing matches so the caller's own error names what was asked
    /// for. False on a C++ failure (nothing pushed). Not noexcept on purpose:
    /// a Lua out-of-memory longjmp may pass through (MSVC unwinds longjmp like
    /// an exception and would call terminate in a noexcept frame).
    [[nodiscard]] bool PushResolvedPath(lua_State* const L, const char* const path, const bool forWriting)
    {
      try {
        std::string resolved;
        if (!forWriting) {
          if (std::optional<std::string> match = fs::ResolveCaseInsensitive(path)) {
            resolved = std::move(*match);
          }
        } else {
          const std::string normalized = fs::NormalizePath(path);
          if (!normalized.empty()) {
            const std::optional<std::string> parent =
              fs::ResolveCaseInsensitive(fs::detail::ParentPath(normalized));
            if (parent) {
              resolved = fs::detail::JoinPath(*parent, fs::detail::FileName(normalized));
            }
          }
        }
        if (resolved.empty()) {
          lua_pushstring(L, path);
        } else {
          lua_pushlstring(L, resolved.data(), resolved.size());
        }
        return true;
      } catch (...) {
        return false;
      }
    }

    /// Calls upvalue 2 (the stock function a binding wraps) with every
    /// argument on the stack and returns all of its results.
    int CallWrapped(lua_State* const L)
    {
      const int argumentCount = lua_gettop(L);
      lua_pushvalue(L, lua_upvalueindex(2));
      lua_insert(L, 1);
      lua_call(L, argumentCount, LUA_MULTRET);
      return lua_gettop(L);
    }

    /// Replaces argument `index` with its resolved form, then calls the wrapped function.
    int CallWrappedWithResolvedArgument(lua_State* const L, const int index, const bool forWriting)
    {
      const char* const path = luaL_checkstring(L, index);
      if (!PushResolvedPath(L, path, forWriting)) {
        return luaL_error(L, "out of memory");
      }
      lua_replace(L, index);
      return CallWrapped(L);
    }

    // ------------------------------------------------------------- bindings

    /// LOG / _ALERT / print: tostring() of every argument joined by tabs, like
    /// the engine's LS_LOG (src/sdk/lua/LuaObject.cpp).
    int LuaLog(lua_State* const L)
    {
      RunContext& context = Context(L);
      const int argumentCount = lua_gettop(L);
      lua_pushliteral(L, "tostring");
      lua_gettable(L, LUA_GLOBALSINDEX);
      const int tostringIndex = argumentCount + 1;
      lua_pushliteral(L, "");
      for (int i = 1; i <= argumentCount; ++i) {
        if (i > 1) {
          lua_pushliteral(L, "\t");
          lua_concat(L, 2);
        }
        lua_pushvalue(L, tostringIndex);
        lua_pushvalue(L, i);
        lua_call(L, 1, 1);
        if (!lua_isstring(L, -1)) {
          return luaL_error(L, "`tostring' must return a string to `LOG'");
        }
        lua_concat(L, 2);
      }
      ++context.logLines;
      context.Log(std::string_view(lua_tostring(L, -1), lua_strlen(L, -1)));
      return 0;
    }

    /// SHGetFolderPath(id): the configured directory with a trailing '/'
    /// (cfunc_SHGetFolderPathL appends '\\'; '/' works on both systems).
    int LuaSHGetFolderPath(lua_State* const L)
    {
      RunContext& context = Context(L);
      if (lua_gettop(L) != 1) {
        return luaL_error(L, "SHGetFolderPath: expected 1 args, but got %d", lua_gettop(L));
      }
      const char* const id = luaL_checkstring(L, 1);
      const std::string* const folder = FolderFor(context, id);
      if (folder == nullptr) {
        return luaL_error(L, "SHGetFolderPath: unknown folder id '%s'", id);
      }
      if (folder->empty()) {
        return luaL_error(L, "SHGetFolderPath('%s'): no directory configured", id);
      }
      if (context.options->allowWrites) {
        // Windows' known folders always exist; scripts assume they can list them.
        try {
          (void)fs::CreateDirectories(*folder);
        } catch (...) {
        }
      }
      lua_pushlstring(L, folder->data(), folder->size());
      return 1;
    }

    /// io.dir(spec): _findfirst emulation (lua::io_dir, src/sdk/lua/LuaObject.cpp).
    int LuaIoDir(lua_State* const L)
    {
      const char* const spec = luaL_checkstring(L, 1);
      lua_newtable(L);
      bool failed = false;
      try {
        const std::vector<std::string> names = fs::FindFiles(spec);
        int index = 1;
        for (const std::string& name : names) {
          lua_pushlstring(L, name.data(), name.size());
          lua_rawseti(L, -2, index++);
        }
      } catch (...) {
        failed = true;
      }
      if (failed) {
        return luaL_error(L, "io.dir: out of memory");
      }
      return 1;
    }

    int LuaIoOpen(lua_State* const L)
    {
      const RunContext& context = Context(L);
      const char* const path = luaL_checkstring(L, 1);
      const char* const mode = luaL_optstring(L, 2, "r");
      const bool writes = IsWriteMode(mode);
      if (writes && !context.options->allowWrites) {
        context.LogSkippedWrite("io.open", path);
        lua_pushnil(L);
        lua_pushfstring(L, "%s: writes are disabled for this run", path);
        return 2;
      }
      return CallWrappedWithResolvedArgument(L, 1, writes);
    }

    /// io.lines(path) / io.input(path): resolve a file name; pass anything else through.
    int LuaIoReadFileArgument(lua_State* const L)
    {
      if (lua_type(L, 1) != LUA_TSTRING) {
        return CallWrapped(L);
      }
      return CallWrappedWithResolvedArgument(L, 1, false);
    }

    int LuaIoOutput(lua_State* const L)
    {
      if (lua_type(L, 1) != LUA_TSTRING) {
        return CallWrapped(L);
      }
      if (!Context(L).options->allowWrites) {
        return luaL_error(L, "io.output(%s): writes are disabled for this run", lua_tostring(L, 1));
      }
      return CallWrappedWithResolvedArgument(L, 1, true);
    }

    int LuaOsRemove(lua_State* const L)
    {
      const RunContext& context = Context(L);
      const char* const path = luaL_checkstring(L, 1);
      if (!context.options->allowWrites) {
        // Report success: the script's shader-cache cleanup must carry on
        // exactly as it would have.
        context.LogSkippedWrite("os.remove", path);
        lua_pushboolean(L, 1);
        return 1;
      }
      return CallWrappedWithResolvedArgument(L, 1, false);
    }

    int LuaOsRename(lua_State* const L)
    {
      const RunContext& context = Context(L);
      const char* const from = luaL_checkstring(L, 1);
      (void)luaL_checkstring(L, 2);
      if (!context.options->allowWrites) {
        context.LogSkippedWrite("os.rename", from);
        lua_pushboolean(L, 1);
        return 1;
      }
      const char* const to = lua_tostring(L, 2);
      if (!PushResolvedPath(L, to, true)) {
        return luaL_error(L, "out of memory");
      }
      lua_replace(L, 2);
      return CallWrappedWithResolvedArgument(L, 1, false);
    }

    /// os.setlocale changes the whole process's C locale (every thread's
    /// ctype calls), so a data-path script may only query it.
    int LuaOsSetLocale(lua_State* const L)
    {
      if (lua_isnoneornil(L, 1)) {
        return CallWrapped(L);
      }
      lua_pushnil(L);
      return 1;
    }

    int LuaUnavailable(lua_State* const L)
    {
      return luaL_error(L, "%s is not available to the data-path script", lua_tostring(L, lua_upvalueindex(2)));
    }

    /// Loads a script file resolved case-insensitively. Pushes the chunk (status
    /// 0) or an error message, like luaL_loadfile.
    int LoadScriptFile(lua_State* const L, const char* const path)
    {
      int status = LUA_ERRMEM;
      bool pushed = false;
      try {
        std::vector<unsigned char> bytes;
        std::string error;
        const std::optional<std::string> resolved = fs::ResolveCaseInsensitive(path);
        if (resolved && fs::ReadWholeFile(*resolved, bytes, &error)) {
          const std::string chunkName = "@" + *resolved;
          // luaL_loadbuffer is protected: it returns a status, it does not longjmp.
          status = luaL_loadbuffer(L, reinterpret_cast<const char*>(bytes.data()), bytes.size(), chunkName.c_str());
        } else {
          status = LUA_ERRFILE;
          lua_pushfstring(L, "cannot read %s", path);
        }
        pushed = true;
      } catch (...) {
      }
      if (!pushed) {
        lua_pushliteral(L, "out of memory");
      }
      return status;
    }

    int LuaDoFile(lua_State* const L)
    {
      const char* const path = luaL_checkstring(L, 1);
      Context(L).Trace("datapath: dofile ", path);
      const int base = lua_gettop(L);
      if (LoadScriptFile(L, path) != 0) {
        return lua_error(L);
      }
      lua_call(L, 0, LUA_MULTRET);
      return lua_gettop(L) - base;
    }

    int LuaLoadFile(lua_State* const L)
    {
      const char* const path = luaL_checkstring(L, 1);
      if (LoadScriptFile(L, path) == 0) {
        return 1;
      }
      lua_pushnil(L);
      lua_insert(L, -2);
      return 2;
    }

    /// Message handler for the script's pcall: the error plus a traceback
    /// (the port opens no debug library, so debug.traceback is not available).
    int Traceback(lua_State* const L)
    {
      constexpr int kMaxLevels = 24;
      if (lua_type(L, 1) != LUA_TSTRING) {
        return 1;
      }
      lua_settop(L, 1);
      lua_pushliteral(L, "\nstack traceback:");
      lua_concat(L, 2);
      lua_Debug frame{};
      for (int level = 1; level <= kMaxLevels && lua_getstack(L, level, &frame) != 0; ++level) {
        lua_getinfo(L, "Snl", &frame);
        if (frame.currentline > 0) {
          lua_pushfstring(L, "\n\t%s:%d:", frame.short_src, frame.currentline);
        } else {
          lua_pushfstring(L, "\n\t%s:", frame.short_src);
        }
        if (frame.name != nullptr) {
          lua_pushfstring(L, " in function `%s'", frame.name);
        } else if (frame.what != nullptr && *frame.what == 'm') {
          lua_pushliteral(L, " in main chunk");
        } else if (frame.what != nullptr && *frame.what == 'C') {
          lua_pushliteral(L, " in C function");
        } else {
          lua_pushfstring(L, " in function <%s:%d>", frame.short_src, frame.linedefined);
        }
        lua_concat(L, 3);
      }
      return 1;
    }

    // ------------------------------------------------------------- setup

    void SetGlobalFunction(
      lua_State* const L,
      RunContext* const context,
      const char* const name,
      lua_CFunction function
    )
    {
      lua_pushstring(L, name);
      lua_pushlightuserdata(L, context);
      lua_pushcclosure(L, function, 1);
      lua_settable(L, LUA_GLOBALSINDEX);
    }

    void SetGlobalString(lua_State* const L, const char* const name, const std::string& value)
    {
      lua_pushstring(L, name);
      lua_pushlstring(L, value.data(), value.size());
      lua_settable(L, LUA_GLOBALSINDEX);
    }

    /// table[name] = closure(function, context, <previous table[name]>) - the
    /// previous value becomes upvalue 2, so wrappers can call the stock function.
    void WrapField(
      lua_State* const L,
      RunContext* const context,
      const char* const table,
      const char* const name,
      lua_CFunction function
    )
    {
      lua_pushstring(L, table);
      lua_gettable(L, LUA_GLOBALSINDEX);
      if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return;
      }
      lua_pushstring(L, name);
      lua_pushlightuserdata(L, context);
      lua_pushstring(L, name);
      lua_rawget(L, -4);
      lua_pushcclosure(L, function, 2);
      lua_rawset(L, -3);
      lua_pop(L, 1);
    }

    /// Like WrapField, but upvalue 2 is the qualified name for the error message.
    void DisableField(lua_State* const L, RunContext* const context, const char* const table, const char* const name)
    {
      lua_pushstring(L, table);
      lua_gettable(L, LUA_GLOBALSINDEX);
      if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return;
      }
      lua_pushstring(L, name);
      lua_pushlightuserdata(L, context);
      lua_pushfstring(L, "%s.%s", table, name);
      lua_pushcclosure(L, &LuaUnavailable, 2);
      lua_rawset(L, -3);
      lua_pop(L, 1);
    }

    /// `("x"):upper()` and `version:find(...)`. LuaPlus indexes a non-table
    /// value through the per-type default metatable itself (luaT_gettmbyobj),
    /// so the string functions are copied into the string type's table.
    void ExposeStringMethods(lua_State* const L)
    {
      const int top = lua_gettop(L);
      lua_pushliteral(L, "");
      lua_getmetatable(L, -1);
      lua_pushliteral(L, "string");
      lua_gettable(L, LUA_GLOBALSINDEX);
      if (lua_istable(L, -1) && lua_istable(L, -2)) {
        lua_pushnil(L);
        while (lua_next(L, -2) != 0) {
          lua_pushvalue(L, -2);
          lua_insert(L, -2);
          lua_rawset(L, -5);
        }
      }
      lua_settop(L, top);
    }

    /// Runs under lua_cpcall: library loading and every registration can raise
    /// (out of memory) and must not reach Lua's panic handler, which exits.
    int SetupState(lua_State* const L)
    {
      auto* const context = static_cast<RunContext*>(lua_touserdata(L, 1));
      lua_settop(L, 0);

      luaopen_base(L);
      lua_settop(L, 0);
      luaopen_table(L);
      lua_settop(L, 0);
      luaopen_io(L); // Also opens `os` in Lua 5.0.
      lua_settop(L, 0);
      luaopen_string(L);
      lua_settop(L, 0);
      luaopen_math(L);
      lua_settop(L, 0);
      ExposeStringMethods(L);

      // LuaPlus's towstring() is the one way left to create a wide string. The
      // engine's VM has no wide strings, no FAF script uses them, and LuaPlus
      // assumes a 2-byte lua_WChar in them (and passes an int to "%s" for
      // towstring(table)), which breaks with Android's 4-byte wchar_t.
      lua_pushliteral(L, "towstring");
      lua_pushnil(L);
      lua_settable(L, LUA_GLOBALSINDEX);

      SetGlobalFunction(L, context, "LOG", &LuaLog);
      SetGlobalFunction(L, context, "_ALERT", &LuaLog);
      SetGlobalFunction(L, context, "print", &LuaLog);
      SetGlobalFunction(L, context, "SHGetFolderPath", &LuaSHGetFolderPath);
      SetGlobalFunction(L, context, "dofile", &LuaDoFile);
      SetGlobalFunction(L, context, "loadfile", &LuaLoadFile);

      WrapField(L, context, "io", "dir", &LuaIoDir);
      WrapField(L, context, "io", "open", &LuaIoOpen);
      WrapField(L, context, "io", "lines", &LuaIoReadFileArgument);
      WrapField(L, context, "io", "input", &LuaIoReadFileArgument);
      WrapField(L, context, "io", "output", &LuaIoOutput);
      WrapField(L, context, "os", "remove", &LuaOsRemove);
      WrapField(L, context, "os", "rename", &LuaOsRename);
      WrapField(L, context, "os", "setlocale", &LuaOsSetLocale);
      DisableField(L, context, "os", "execute");
      DisableField(L, context, "os", "exit");

      SetGlobalString(L, "LaunchDir", context->launchDir);
      SetGlobalString(L, "InitFileDir", context->initFileDir);
      return 0;
    }

    struct ScriptChunk
    {
      const unsigned char* data = nullptr;
      std::size_t size = 0;
      const char* chunkName = nullptr;
    };

    /// Runs under lua_cpcall: load and run the script against the globals,
    /// with Traceback as the message handler.
    int RunScript(lua_State* const L)
    {
      const auto* const chunk = static_cast<const ScriptChunk*>(lua_touserdata(L, 1));
      lua_settop(L, 0);
      lua_pushcfunction(L, &Traceback);
      if (luaL_loadbuffer(L, reinterpret_cast<const char*>(chunk->data), chunk->size, chunk->chunkName) != 0) {
        return lua_error(L);
      }
      if (lua_pcall(L, 0, 0, 1) != 0) {
        return lua_error(L);
      }
      return 0;
    }

    struct Collection
    {
      DataPathResult* result = nullptr;
      bool outOfMemory = false;
    };

    [[nodiscard]] std::string_view StringAt(lua_State* const L, const int index)
    {
      if (lua_type(L, index) != LUA_TSTRING) {
        return {};
      }
      return {lua_tostring(L, index), lua_strlen(L, index)};
    }

    /// Appends the string values of global table `name`, in table order.
    void CollectStrings(lua_State* const L, const char* const name, std::vector<std::string>& out, bool& outOfMemory)
    {
      lua_pushstring(L, name);
      lua_gettable(L, LUA_GLOBALSINDEX);
      const int table = lua_gettop(L);
      if (lua_istable(L, table)) {
        lua_pushnil(L);
        while (lua_next(L, table) != 0) {
          const std::string_view value = StringAt(L, -1);
          if (!value.empty()) {
            try {
              out.emplace_back(value);
            } catch (...) {
              outOfMemory = true;
            }
          }
          lua_pop(L, 1);
        }
      }
      lua_settop(L, table - 1);
    }

    /// Runs under lua_cpcall: read back `path`, `hook` and `protocols` in
    /// table (lua_next) order, which is how the engine's LuaTableIterator walks
    /// them (CollectMountPointsFromLua, StartupHelpers.cpp). Entries without a
    /// string `dir` are skipped; a missing mountpoint means "/".
    int CollectTables(lua_State* const L)
    {
      auto* const collection = static_cast<Collection*>(lua_touserdata(L, 1));
      DataPathResult& result = *collection->result;
      lua_settop(L, 0);

      lua_pushliteral(L, "path");
      lua_gettable(L, LUA_GLOBALSINDEX);
      if (lua_istable(L, 1)) {
        lua_pushnil(L);
        while (lua_next(L, 1) != 0) {
          if (lua_istable(L, 3)) {
            lua_pushliteral(L, "dir");
            lua_gettable(L, 3);
            lua_pushliteral(L, "mountpoint");
            lua_gettable(L, 3);
            const std::string_view dir = StringAt(L, 4);
            const std::string_view mountpoint = StringAt(L, 5);
            if (!dir.empty()) {
              try {
                result.mounts.push_back(
                  MountSpec{std::string(dir), mountpoint.empty() ? std::string("/") : std::string(mountpoint)}
                );
              } catch (...) {
                collection->outOfMemory = true;
              }
            }
          }
          lua_settop(L, 2);
        }
      }
      lua_settop(L, 0);

      CollectStrings(L, "hook", result.hooks, collection->outOfMemory);
      CollectStrings(L, "protocols", result.protocols, collection->outOfMemory);
      return 0;
    }

    [[nodiscard]] std::string TopMessage(lua_State* const L)
    {
      if (lua_type(L, -1) == LUA_TSTRING) {
        return std::string(lua_tostring(L, -1), lua_strlen(L, -1));
      }
      return "(error object is not a string)";
    }

    struct LuaStateCloser
    {
      void operator()(lua_State* const L) const
      {
        lua_close(L);
      }
    };

    [[nodiscard]] std::string FolderWithSlash(const std::string& folder)
    {
      if (folder.empty()) {
        return {};
      }
      std::string out = fs::NormalizePath(folder);
      if (out.back() != '/') {
        out.push_back('/');
      }
      return out;
    }

    void Run(const DataPathOptions& options, DataPathResult& result)
    {
      RunContext context;
      context.options = &options;
      const auto fail = [&](std::string message) {
        result.ok = false;
        result.error = std::move(message);
        context.Log("datapath: " + result.error);
      };

      fs::detail::Kind kind = fs::detail::Kind::Missing;
      std::optional<std::string> script = fs::detail::Resolve(options.scriptPath, true, &kind);
      if (!script || kind != fs::detail::Kind::File) {
        fail("data-path script not found: " + options.scriptPath);
        return;
      }
      if (!detail::PathFromUtf8(*script).is_absolute()) {
        std::error_code ec;
        const std::filesystem::path absolute = std::filesystem::absolute(detail::PathFromUtf8(*script), ec);
        if (!ec) {
          script = fs::NormalizePath(detail::Utf8FromPath(absolute));
        }
      }

      // DISK_SetupDataAndSearchPaths publishes the launch directory and the
      // directory holding the script; on Android both are the script's.
      context.initFileDir = fs::detail::ParentPath(*script);
      context.launchDir = options.launchDir.empty() ? context.initFileDir : fs::NormalizePath(options.launchDir);
      context.localAppData = FolderWithSlash(options.folders.localAppData);
      context.appData =
        options.folders.appData.empty() ? context.localAppData : FolderWithSlash(options.folders.appData);
      context.personal =
        options.folders.personal.empty() ? context.localAppData : FolderWithSlash(options.folders.personal);
      result.initFileDir = context.initFileDir;
      result.launchDir = context.launchDir;

      std::vector<unsigned char> source;
      std::string readError;
      if (!fs::ReadWholeFile(*script, source, &readError)) {
        fail("cannot read the data-path script: " + readError);
        return;
      }

      context.Trace("datapath: read " + std::to_string(source.size()) + " bytes, creating the Lua state");
      const std::unique_ptr<lua_State, LuaStateCloser> state(lua_open());
      if (state == nullptr) {
        fail("cannot create a Lua state");
        return;
      }
      lua_State* const L = state.get();

      if (lua_cpcall(L, &SetupState, &context) != 0) {
        fail("Lua setup failed: " + TopMessage(L));
        return;
      }
      lua_settop(L, 0);
      context.Trace("datapath: Lua state ready, running the script");

      const std::string chunkName = "@" + *script;
      ScriptChunk chunk;
      chunk.data = source.data();
      chunk.size = source.size();
      chunk.chunkName = chunkName.c_str();
      const auto start = std::chrono::steady_clock::now();
      const int status = lua_cpcall(L, &RunScript, &chunk);
      result.scriptMilliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
      result.logLines = context.logLines;
      if (status != 0) {
        fail(TopMessage(L));
        return;
      }
      lua_settop(L, 0);

      Collection collection;
      collection.result = &result;
      if (lua_cpcall(L, &CollectTables, &collection) != 0) {
        fail("reading the path tables failed: " + TopMessage(L));
        return;
      }
      if (collection.outOfMemory) {
        fail("out of memory while reading the path tables");
        return;
      }
      result.ok = true;
    }

  } // namespace

  DataPathResult RunDataPathScript(const DataPathOptions& options)
  {
    DataPathResult result;
    try {
      Run(options, result);
    } catch (const std::exception& e) {
      result.ok = false;
      result.error = std::string("data-path runner: ") + e.what();
    } catch (...) {
      result.ok = false;
      result.error = "data-path runner: unknown exception";
    }
    return result;
  }

  bool CheckLuaSyntax(
    const void* const source,
    const std::size_t size,
    const std::string_view chunkName,
    std::string* error
  )
  {
    try {
      std::string name(chunkName);
      if (name.empty() || (name.front() != '@' && name.front() != '=')) {
        name.insert(name.begin(), '@');
      }
      const std::unique_ptr<lua_State, LuaStateCloser> state(lua_open());
      if (state == nullptr) {
        if (error != nullptr) {
          *error = "cannot create a Lua state";
        }
        return false;
      }
      // Parsing only: no libraries, nothing runs. luaL_loadbuffer is protected.
      if (luaL_loadbuffer(state.get(), static_cast<const char*>(source), size, name.c_str()) == 0) {
        return true;
      }
      if (error != nullptr) {
        *error = TopMessage(state.get());
      }
      return false;
    } catch (const std::exception& e) {
      if (error != nullptr) {
        *error = e.what();
      }
      return false;
    }
  }

} // namespace faf::port
