#pragma once

// What the launcher hands the runtime: the data root (NativeActivity's
// externalDataPath, = Context.getExternalFilesDir(null)) and the game's
// command line in the Intent extra "argv" (String[]), the same tokens the
// desktop ForgedAlliance.exe receives. See "Launch contract" in
// docs/port/android.md.

#include <android/native_activity.h>

#include <string>
#include <vector>

#include "faf/port/CommandLine.h"

namespace faf::android {

  /// The data root without a trailing '/', or empty when Android did not
  /// provide one (shared storage unavailable).
  [[nodiscard]] std::string DataRoot(const ANativeActivity* activity);

  /// The extras of the launcher's "Menu replay" (release 0.5.0, GalPlay.h's Java contract).
  struct MenuReplayExtras
  {
    std::string trace;          ///< "menuReplay.trace": the .galtrace file
    std::string runDir;         ///< "menuReplay.runDir": the run directory (exists)
    bool fast = false;          ///< "menuReplay.fast": as fast as possible instead of the recorded pace
    bool forceCpuDecode = false; ///< "menuReplay.forceCpuDecode": BC textures decoded on the CPU
    bool noShaderCache = false; ///< "menuReplay.noShaderCache": no shader or pipeline cache
  };

  struct IntentArgs
  {
    bool ok = false;      ///< false: JNI failed, see `error`.
    bool present = false; ///< The extra exists (a launcher started us with arguments).
    std::vector<std::string> argv;
    /// The String extra "mode": "menu-replay" for the menu replay (GalPlay.h); empty for the game.
    std::string mode;
    MenuReplayExtras menuReplay; ///< read when `mode` is "menu-replay"
    std::string error;
  };

  /// The value of the "mode" extra that selects the menu replay (GalPlay.h).
  inline constexpr const char* kModeMenuReplay = "menu-replay";

  /// Reads getIntent().getStringArrayExtra("argv"), the String extra "mode" and,
  /// in mode "menu-replay", the "menuReplay.*" extras through JNI on the calling
  /// thread (attached to the VM for the duration of the call if needed).
  /// Strings are converted from UTF-16 to UTF-8; null elements are dropped.
  [[nodiscard]] IntentArgs ReadIntentArgs(ANativeActivity* activity);

  enum class Backend
  {
    Vulkan,
    Gles,
  };

  [[nodiscard]] const char* ToString(Backend backend);

  /// The options this runtime acts on, resolved against the data root.
  struct LaunchOptions
  {
    std::string initScript; ///< Absolute; default <root>/faf/bin/init_faf.lua.
    std::string gameLog;    ///< Absolute; empty without /log.
    Backend backend = Backend::Vulkan;
    /// Problems with the arguments that are not fatal (logged): an unknown
    /// renderer, an option without its value, ...
    std::vector<std::string> warnings;
    /// Options present on the command line that this build does not act on.
    /// They stay in the command line for the engine (forward compatibility).
    std::vector<std::string> unusedOptions;
  };

  /// Applies the launch contract to `commandLine`:
  /// - /init <path>: the data-path script; relative paths are taken relative to
  ///   <root>/faf/bin, the directory the desktop game runs from,
  /// - /log <file>: the game log, relative paths likewise,
  /// - /renderer vulkan|gles: the graphics backend (port-specific).
  [[nodiscard]] LaunchOptions ResolveLaunchOptions(const faf::port::CommandLine& commandLine, const std::string& root);

  /// The tokens as one line for the log, quoting tokens that contain spaces.
  [[nodiscard]] std::string DescribeArgs(const std::vector<std::string>& argv);

} // namespace faf::android
