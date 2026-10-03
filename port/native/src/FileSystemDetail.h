#pragma once

// Internal helpers shared by FileSystem.cpp, Vfs.cpp and DataPath.cpp, and the
// resolver entry point the unit tests drive directly. Not part of the API.

#include <optional>
#include <string>
#include <string_view>

namespace faf::port::fs::detail {

  /// ASCII-only case mapping, like the engine's STR_ToLower / NTFS for the
  /// names game data uses. Other bytes pass through unchanged.
  [[nodiscard]] char LowerAscii(char c);
  [[nodiscard]] std::string LowerAscii(std::string_view text);
  [[nodiscard]] std::string UpperAscii(std::string_view text);
  [[nodiscard]] bool EqualsNoCase(std::string_view a, std::string_view b);

  /// NTFS directory order: by upper-cased spelling, raw bytes as tie-break so
  /// names that differ only in case still sort deterministically.
  [[nodiscard]] bool NtfsLess(std::string_view a, std::string_view b);

  /// Length of the root of a '/'-separated path: "C:/" -> 3, "/" -> 1,
  /// "C:" (drive-relative) -> 2, relative -> 0.
  [[nodiscard]] std::size_t RootLength(std::string_view path);

  /// Appends `name` to `dir` with exactly one '/' between them. An empty `dir`
  /// gives `name` (relative).
  [[nodiscard]] std::string JoinPath(std::string_view dir, std::string_view name);

  /// Directory part of a normalised path: "a/b" -> "a", "/a" -> "/",
  /// "C:/a" -> "C:/", "a" -> ".".
  [[nodiscard]] std::string ParentPath(std::string_view path);

  /// Last component of a normalised path ("C:/a/b.lua" -> "b.lua").
  [[nodiscard]] std::string_view FileName(std::string_view path);

  enum class Kind
  {
    Missing,
    File,
    Directory,
    Other,
  };

  /// One stat (following symlinks) of a UTF-8 path, verbatim.
  [[nodiscard]] Kind StatKind(const std::string& path);

  /// The resolver behind fs::ResolveCaseInsensitive. With `trustExactSpelling`
  /// a component that exists as written is taken without listing its
  /// directory (one stat per component - the fast path the runtime uses).
  /// Without it every component is looked up in a directory listing, exact
  /// name first, which yields the true on-disk spelling even on a
  /// case-folding file system; the tests use that to exercise the
  /// case-insensitive matching on a Windows host. `kind` receives the type of
  /// the resolved path.
  [[nodiscard]] std::optional<std::string>
  Resolve(std::string_view path, bool trustExactSpelling, Kind* kind = nullptr);

} // namespace faf::port::fs::detail
