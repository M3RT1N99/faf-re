#pragma once

// Disk access with the Windows semantics the game data and its scripts assume,
// on a POSIX file system. Nothing here is recovered from the binary.
//
// The engine and FAF's init_faf.lua were written against NTFS: names compare
// case-insensitively, '\\' and '/' are both separators, and _findfirst hands
// back "." and ".." plus names in upper-case collation order. Android's app
// storage is case-sensitive ext4/f2fs (or FUSE with unknown case folding), so
// every lookup goes through these helpers instead of opening paths verbatim.
//
// Paths in and out are UTF-8 with '/' separators. Case folding is ASCII-only,
// like the engine's STR_ToLower; other bytes must match exactly.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace faf::port::fs {

  /// '\\' -> '/', runs of '/' collapsed to one, "." components dropped and ".."
  /// folded lexically. A leading '/' and a Windows drive prefix ("C:/") survive.
  /// A relative path that folds away entirely becomes "."; "" stays "".
  [[nodiscard]] std::string NormalizePath(std::string_view path);

  /// Case-insensitive wildcard match with '*' and '?', like _findfirst.
  /// '?' matches one byte.
  [[nodiscard]] bool WildcardMatch(std::string_view pattern, std::string_view name);

  /// Resolves `path` against the disk component by component, matching each
  /// component case-insensitively when the exact name does not exist. Returns
  /// the normalised existing path, or nullopt. Components that exist as
  /// written are kept as written, so on a case-sensitive file system the result
  /// is the on-disk spelling; on a case-folding one (NTFS, some FUSE mounts) it
  /// is a spelling that opens the same file. When several names differ only in
  /// case, the first in NTFS order wins.
  [[nodiscard]] std::optional<std::string> ResolveCaseInsensitive(std::string_view path);

  /// io.dir / _findfirst emulation for a spec such as "C:/x/gamedata/*.scd":
  /// - the directory part is resolved case-insensitively,
  /// - "*.*" matches every name, and a pattern that matches "." also yields "." and "..",
  /// - names come back sorted by their upper-cased spelling (NTFS order), which
  ///   decides the mount order of the *.nx2 / *.scd archives.
  /// Returns bare names (no directory), files and directories alike. An
  /// unreadable directory or a spec ending in a separator gives an empty list.
  [[nodiscard]] std::vector<std::string> FindFiles(std::string_view spec);

  /// Type checks after case-insensitive resolution.
  [[nodiscard]] bool IsDirectory(std::string_view path);
  [[nodiscard]] bool IsRegularFile(std::string_view path);

  /// Creates `path` and its parents. Existing components are reused even when
  /// their case differs. Returns false only if it does not exist afterwards.
  bool CreateDirectories(std::string_view path);

  /// Reads a whole file (resolved case-insensitively).
  bool ReadWholeFile(std::string_view path, std::vector<unsigned char>& out, std::string* error = nullptr);

} // namespace faf::port::fs
