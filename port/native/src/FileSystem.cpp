#include "faf/port/FileSystem.h"

#include <algorithm>
#include <filesystem>
#include <new>
#include <system_error>

#include "FileSystemDetail.h"
#include "NativeFile.h"

namespace faf::port::fs {

  namespace stdfs = std::filesystem;
  namespace native = faf::port::detail;
  using detail::Kind;

  namespace {
    [[nodiscard]] bool IsAsciiAlpha(const char c)
    {
      return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    }

    /// Visits the names in `dir` (no "." / ".."), stopping when `visit` returns
    /// false. Unreadable directories visit nothing.
    template <typename Visitor>
    void ForEachName(const std::string& dir, Visitor&& visit)
    {
      std::error_code ec;
      stdfs::directory_iterator it(native::PathFromUtf8(dir.empty() ? std::string(".") : dir), ec);
      const stdfs::directory_iterator end;
      for (; !ec && it != end; it.increment(ec)) {
        if (!visit(native::Utf8FromPath(it->path().filename()))) {
          return;
        }
      }
    }

    /// The name in `dir` that matches `name`: the exact spelling if present,
    /// otherwise the first case-insensitive match in NTFS order.
    [[nodiscard]] std::optional<std::string> FindNameInDirectory(const std::string& dir, const std::string_view name)
    {
      std::optional<std::string> best;
      ForEachName(dir, [&](std::string&& candidate) {
        if (candidate == name) {
          best = std::move(candidate);
          return false;
        }
        if (detail::EqualsNoCase(candidate, name) && (!best || detail::NtfsLess(candidate, *best))) {
          best = std::move(candidate);
        }
        return true;
      });
      return best;
    }
  } // namespace

  namespace detail {

    char LowerAscii(const char c)
    {
      return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }

    std::string LowerAscii(const std::string_view text)
    {
      std::string out(text);
      for (char& c : out) {
        c = LowerAscii(c);
      }
      return out;
    }

    std::string UpperAscii(const std::string_view text)
    {
      std::string out(text);
      for (char& c : out) {
        if (c >= 'a' && c <= 'z') {
          c = static_cast<char>(c - 'a' + 'A');
        }
      }
      return out;
    }

    bool EqualsNoCase(const std::string_view a, const std::string_view b)
    {
      if (a.size() != b.size()) {
        return false;
      }
      for (std::size_t i = 0; i < a.size(); ++i) {
        if (LowerAscii(a[i]) != LowerAscii(b[i])) {
          return false;
        }
      }
      return true;
    }

    bool NtfsLess(const std::string_view a, const std::string_view b)
    {
      const std::size_t common = std::min(a.size(), b.size());
      for (std::size_t i = 0; i < common; ++i) {
        // Compare as unsigned bytes so UTF-8 sequences sort by code point.
        auto upper = [](const char c) {
          const auto byte = static_cast<unsigned char>(c);
          return (byte >= 'a' && byte <= 'z') ? static_cast<unsigned char>(byte - 'a' + 'A') : byte;
        };
        const unsigned char ua = upper(a[i]);
        const unsigned char ub = upper(b[i]);
        if (ua != ub) {
          return ua < ub;
        }
      }
      if (a.size() != b.size()) {
        return a.size() < b.size();
      }
      return a < b;
    }

    std::size_t RootLength(const std::string_view path)
    {
      std::size_t length = 0;
      if (path.size() >= 2 && IsAsciiAlpha(path[0]) && path[1] == ':') {
        length = 2;
      }
      if (length < path.size() && path[length] == '/') {
        ++length;
      }
      return length;
    }

    std::string JoinPath(const std::string_view dir, const std::string_view name)
    {
      if (dir.empty()) {
        return std::string(name);
      }
      std::string out(dir);
      if (out.back() != '/') {
        out.push_back('/');
      }
      out.append(name);
      return out;
    }

    std::string ParentPath(const std::string_view path)
    {
      const std::size_t root = RootLength(path);
      const std::size_t cut = path.find_last_of('/');
      if (cut == std::string_view::npos || cut < root) {
        return root > 0 ? std::string(path.substr(0, root)) : std::string(".");
      }
      return std::string(path.substr(0, cut));
    }

    std::string_view FileName(const std::string_view path)
    {
      const std::size_t cut = path.find_last_of('/');
      return cut == std::string_view::npos ? path.substr(RootLength(path)) : path.substr(cut + 1);
    }

    Kind StatKind(const std::string& path)
    {
      std::error_code ec;
      const stdfs::file_status status = stdfs::status(native::PathFromUtf8(path), ec);
      if (ec || !stdfs::exists(status)) {
        return Kind::Missing;
      }
      if (stdfs::is_directory(status)) {
        return Kind::Directory;
      }
      if (stdfs::is_regular_file(status)) {
        return Kind::File;
      }
      return Kind::Other;
    }

    std::optional<std::string> Resolve(const std::string_view path, const bool trustExactSpelling, Kind* const kind)
    {
      const std::string normalized = NormalizePath(path);
      if (normalized.empty()) {
        return std::nullopt;
      }

      if (trustExactSpelling) {
        const Kind whole = StatKind(normalized);
        if (whole != Kind::Missing) {
          if (kind != nullptr) {
            *kind = whole;
          }
          return normalized;
        }
      }

      const std::size_t rootLength = RootLength(normalized);
      std::string current = normalized.substr(0, rootLength);
      Kind currentKind = current.empty() ? Kind::Directory : StatKind(current);

      std::size_t pos = rootLength;
      while (pos < normalized.size()) {
        std::size_t end = normalized.find('/', pos);
        if (end == std::string::npos) {
          end = normalized.size();
        }
        const std::string_view component(normalized.data() + pos, end - pos);
        pos = end + 1;
        if (component.empty() || component == ".") {
          continue;
        }
        if (currentKind != Kind::Directory) {
          return std::nullopt;
        }

        std::string candidate = JoinPath(current, component);
        // NormalizePath leaves ".." only at the start of a relative path; it
        // names the parent, never a case-insensitive match.
        if (component == "..") {
          currentKind = StatKind(candidate);
          if (currentKind == Kind::Missing) {
            return std::nullopt;
          }
          current = std::move(candidate);
          continue;
        }

        if (trustExactSpelling) {
          const Kind candidateKind = StatKind(candidate);
          if (candidateKind != Kind::Missing) {
            current = std::move(candidate);
            currentKind = candidateKind;
            continue;
          }
        }

        const std::optional<std::string> match = FindNameInDirectory(current, component);
        if (!match) {
          return std::nullopt;
        }
        current = JoinPath(current, *match);
        currentKind = StatKind(current);
        if (currentKind == Kind::Missing) {
          return std::nullopt;
        }
      }

      if (current.empty()) {
        current = ".";
      }
      if (kind != nullptr) {
        *kind = currentKind;
      }
      return current;
    }

  } // namespace detail

  std::string NormalizePath(const std::string_view path)
  {
    if (path.empty()) {
      return {};
    }

    std::string text(path);
    std::replace(text.begin(), text.end(), '\\', '/');

    std::size_t pos = 0;
    std::string out;
    if (text.size() >= 2 && IsAsciiAlpha(text[0]) && text[1] == ':') {
      out = text.substr(0, 2);
      pos = 2;
    }
    const bool rooted = pos < text.size() && text[pos] == '/';
    if (rooted) {
      out.push_back('/');
    }

    std::vector<std::string_view> parts;
    while (pos < text.size()) {
      std::size_t end = text.find('/', pos);
      if (end == std::string::npos) {
        end = text.size();
      }
      const std::string_view part(text.data() + pos, end - pos);
      pos = end + 1;
      if (part.empty() || part == ".") {
        continue;
      }
      if (part == "..") {
        if (!parts.empty() && parts.back() != "..") {
          parts.pop_back();
        } else if (!rooted) {
          parts.push_back(part);
        }
        // ".." above a root stays at the root, as on Windows and POSIX.
        continue;
      }
      parts.push_back(part);
    }

    for (std::size_t i = 0; i < parts.size(); ++i) {
      if (i > 0) {
        out.push_back('/');
      }
      out.append(parts[i]);
    }
    if (out.empty()) {
      out = ".";
    }
    return out;
  }

  bool WildcardMatch(const std::string_view pattern, const std::string_view name)
  {
    // Iterative glob with single-star backtracking: linear in practice and
    // never recursive, so hostile patterns cannot blow the stack.
    std::size_t p = 0;
    std::size_t n = 0;
    std::size_t starPattern = std::string_view::npos;
    std::size_t starName = 0;
    while (n < name.size()) {
      if (p < pattern.size() && pattern[p] == '*') {
        starPattern = p++;
        starName = n;
      } else if (p < pattern.size() &&
                 (pattern[p] == '?' || detail::LowerAscii(pattern[p]) == detail::LowerAscii(name[n]))) {
        ++p;
        ++n;
      } else if (starPattern != std::string_view::npos) {
        p = starPattern + 1;
        n = ++starName;
      } else {
        return false;
      }
    }
    while (p < pattern.size() && pattern[p] == '*') {
      ++p;
    }
    return p == pattern.size();
  }

  std::optional<std::string> ResolveCaseInsensitive(const std::string_view path)
  {
    return detail::Resolve(path, true);
  }

  std::vector<std::string> FindFiles(const std::string_view spec)
  {
    std::string text(spec);
    std::replace(text.begin(), text.end(), '\\', '/');

    std::string directory;
    std::string pattern;
    const std::size_t cut = text.find_last_of('/');
    if (cut == std::string::npos) {
      directory = ".";
      pattern = text;
    } else {
      directory = text.substr(0, cut);
      pattern = text.substr(cut + 1);
      if (directory.empty()) {
        directory = "/";
      } else if (directory.size() == 2 && directory[1] == ':') {
        directory.push_back('/');
      }
    }
    if (pattern.empty()) {
      return {};
    }
    if (pattern == "*.*") {
      // _findfirst's "*.*" also matches names without a dot.
      pattern = "*";
    }

    Kind kind = Kind::Missing;
    const std::optional<std::string> resolved = detail::Resolve(directory, true, &kind);
    if (!resolved || kind != Kind::Directory) {
      return {};
    }

    std::vector<std::string> names;
    ForEachName(*resolved, [&](std::string&& name) {
      if (WildcardMatch(pattern, name)) {
        names.push_back(std::move(name));
      }
      return true;
    });
    std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
      return detail::NtfsLess(a, b);
    });
    if (WildcardMatch(pattern, ".")) {
      // NTFS synthesises these first; init_faf.lua skips them by name.
      names.insert(names.begin(), {".", ".."});
    }
    return names;
  }

  bool IsDirectory(const std::string_view path)
  {
    Kind kind = Kind::Missing;
    return detail::Resolve(path, true, &kind).has_value() && kind == Kind::Directory;
  }

  bool IsRegularFile(const std::string_view path)
  {
    Kind kind = Kind::Missing;
    return detail::Resolve(path, true, &kind).has_value() && kind == Kind::File;
  }

  bool CreateDirectories(const std::string_view path)
  {
    const std::string normalized = NormalizePath(path);
    if (normalized.empty()) {
      return false;
    }
    if (IsDirectory(normalized)) {
      return true;
    }

    const std::size_t rootLength = detail::RootLength(normalized);
    std::string current = normalized.substr(0, rootLength);
    std::size_t pos = rootLength;
    while (pos < normalized.size()) {
      std::size_t end = normalized.find('/', pos);
      if (end == std::string::npos) {
        end = normalized.size();
      }
      const std::string_view component(normalized.data() + pos, end - pos);
      pos = end + 1;

      std::string candidate = detail::JoinPath(current, component);
      Kind kind = detail::StatKind(candidate);
      if (kind == Kind::Missing && component != "..") {
        // Reuse "Gas Powered Games" when the script asks for "gas powered games".
        if (const std::optional<std::string> match = FindNameInDirectory(current, component)) {
          std::string existing = detail::JoinPath(current, *match);
          if (detail::StatKind(existing) == Kind::Directory) {
            candidate = std::move(existing);
            kind = Kind::Directory;
          }
        }
      }
      if (kind == Kind::Missing) {
        std::error_code ec;
        stdfs::create_directory(native::PathFromUtf8(candidate), ec);
        kind = detail::StatKind(candidate); // Another thread may have created it.
      }
      if (kind != Kind::Directory) {
        return false;
      }
      current = std::move(candidate);
    }
    return true;
  }

  bool ReadWholeFile(const std::string_view path, std::vector<unsigned char>& out, std::string* const error)
  {
    out.clear();
    Kind kind = Kind::Missing;
    const std::optional<std::string> resolved = detail::Resolve(path, true, &kind);
    if (!resolved || kind != Kind::File) {
      if (error != nullptr) {
        *error = std::string(path) + ": no such file";
      }
      return false;
    }

    const std::unique_ptr<native::ReadOnlyFile> file = native::ReadOnlyFile::Open(*resolved, error);
    if (file == nullptr) {
      return false;
    }
    if (file->Size() > out.max_size()) {
      if (error != nullptr) {
        *error = *resolved + ": too large to read into memory";
      }
      return false;
    }
    try {
      out.resize(static_cast<std::size_t>(file->Size()));
    } catch (const std::bad_alloc&) {
      if (error != nullptr) {
        *error = *resolved + ": out of memory";
      }
      return false;
    }
    if (!file->ReadAt(0, out.data(), out.size())) {
      out.clear();
      if (error != nullptr) {
        *error = *resolved + ": read error";
      }
      return false;
    }
    return true;
  }

} // namespace faf::port::fs
