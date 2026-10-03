#pragma once

// <root>/launch/status.json: how far the last start got. The launcher clears
// it before every start and shows it in its "Last run" panel when it comes
// back to the foreground, so a run that dies early still explains itself.
// Contract (docs/port/android.md):
//
//   {"schema":1,"state":"loading|running|error|exited",
//    "stage":"args|datapath|mount|splash|renderer|frame",
//    "message":"...","renderer":"vulkan|gles|none",
//    "mounts":785,"archives":20,"archiveEntries":38237,"scriptMs":812.5,"mountMs":190.2,
//    "splash":"/textures/...","splashFormat":"DXT5","width":1024,"height":768,
//    "timestamp":"2026-10-03T12:00:00Z","versionName":"0.3.0"}
//
// Every change rewrites the whole file through a temporary file and rename(),
// so the launcher never reads half a file. The first terminal state (error or
// exited) sticks: later updates, for example from a worker thread that is
// still finishing, are ignored, so "error" is never overwritten by progress.

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace faf::android {

  enum class RunState
  {
    Loading,
    Running,
    Error,
    Exited,
  };

  enum class Stage
  {
    Args,
    DataPath,
    Mount,
    Splash,
    Renderer,
    Frame,
  };

  [[nodiscard]] std::string_view ToString(RunState state);
  [[nodiscard]] std::string_view ToString(Stage stage);

  struct RunStatusData
  {
    RunState state = RunState::Loading;
    Stage stage = Stage::Args;
    std::string message;
    std::string renderer = "none";
    std::uint64_t mounts = 0;
    std::uint64_t archives = 0;
    std::uint64_t archiveEntries = 0;
    double scriptMs = 0.0;
    double mountMs = 0.0;
    std::string splash;
    std::string splashFormat;
    int width = 0;
    int height = 0;
  };

  /// Serialises `data` in the contract's format (one line, UTF-8; invalid
  /// UTF-8 in strings becomes U+FFFD so the launcher's JSON parser accepts it).
  [[nodiscard]] std::string ToJson(const RunStatusData& data, std::string_view timestamp, std::string_view versionName);

  class RunStatus
  {
  public:
    /// `path` is <root>/launch/status.json; its directory must exist.
    RunStatus(std::string path, std::string versionName);

    RunStatus(const RunStatus&) = delete;
    RunStatus& operator=(const RunStatus&) = delete;

    /// Applies `edit` to the current data and writes the file, unless a
    /// terminal state was written already. `edit` must not change `state` to
    /// a terminal one; use Finish for that.
    template <class Edit>
    void Update(Edit&& edit)
    {
      const std::lock_guard lock(mMutex);
      if (mTerminal) {
        return;
      }
      edit(mData);
      WriteLocked();
    }

    /// Writes the terminal `state` (Error or Exited) with `message`; `stage`
    /// replaces the current stage when given. Returns false when a terminal
    /// state had been written before (nothing changes then).
    bool Finish(RunState state, std::optional<Stage> stage, std::string message);

    [[nodiscard]] bool IsTerminal() const;
    [[nodiscard]] RunStatusData Snapshot() const;

  private:
    void WriteLocked();

    const std::string mPath;
    const std::string mVersionName;
    mutable std::mutex mMutex;
    RunStatusData mData;
    bool mTerminal = false;
    bool mReportedWriteError = false;
  };

} // namespace faf::android
