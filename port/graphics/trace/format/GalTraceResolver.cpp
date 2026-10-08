// Payload resolvers and the reference checks (GalTraceResolver.h).

#include "GalTraceResolver.h"

#include "GalTraceIO.h"

#include <cstdio>
#include <map>
#include <set>

namespace galtrace
{
  namespace
  {
    char LowerAscii(const char c)
    {
      return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }

    std::string Bytes(const std::uint64_t bytes)
    {
      if (bytes >= 10u * 1024u * 1024u) {
        return std::to_string(bytes / (1024u * 1024u)) + " MB";
      }
      if (bytes >= 10u * 1024u) {
        return std::to_string(bytes / 1024u) + " KB";
      }
      return std::to_string(bytes) + " bytes";
    }
  } // namespace

  // ---- DirectoryResolver --------------------------------------------------------------------------

  DirectoryResolver::DirectoryResolver(std::string root) : root_(std::move(root)) {}

  std::string DirectoryResolver::MapPath(const std::string& root, const std::string& vfsPath)
  {
    std::string relative;
    relative.reserve(vfsPath.size());
    for (const char c : vfsPath) {
      relative += (c == '\\') ? '/' : LowerAscii(c);
    }
    while (!relative.empty() && relative[0] == '/') {
      relative.erase(0, 1);
    }
    // No ".." component: the tree is the only place a reference may read from.
    std::size_t at = 0;
    while (at <= relative.size()) {
      std::size_t end = relative.find('/', at);
      if (end == std::string::npos) {
        end = relative.size();
      }
      const std::string component = relative.substr(at, end - at);
      if (component == "..") {
        return {};
      }
      at = end + 1;
    }
    if (relative.empty()) {
      return {};
    }
    std::string path = root;
    if (!path.empty() && path.back() != '/' && path.back() != '\\') {
      path += '/';
    }
    return path + relative;
  }

  bool DirectoryResolver::ReadGameFile(const std::string& vfsPath, std::vector<std::uint8_t>* const out, std::string* const error)
  {
    out->clear();
    const std::string path = MapPath(root_, vfsPath);
    std::FILE* const file = path.empty() ? nullptr : std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
      if (error != nullptr) {
        *error = vfsPath + ": not in " + root_;
      }
      return false;
    }
    bool ok = std::fseek(file, 0, SEEK_END) == 0;
    const long length = ok ? std::ftell(file) : -1;
    ok = ok && length >= 0 && std::fseek(file, 0, SEEK_SET) == 0;
    if (ok) {
      out->resize(static_cast<std::size_t>(length));
      ok = length == 0 || std::fread(out->data(), 1, out->size(), file) == out->size();
    }
    std::fclose(file);
    if (!ok) {
      out->clear();
    }
    if (!ok && error != nullptr) {
      *error = vfsPath + ": read error in " + path;
    }
    return ok;
  }

  // ---- scanning and verifying references ----------------------------------------------------------

  bool ScanPayloadRefs(const std::string& tracePath, std::vector<PayloadRefInfo>* const refs, std::string* const error)
  {
    refs->clear();
    Reader reader;
    if (!reader.Open(tracePath, error)) {
      return false;
    }
    Record record;
    while (reader.Next(&record, false)) {
      // Payload definitions are indexed as they pass; nothing to do per record.
    }
    if (reader.Failed()) {
      if (error != nullptr) {
        *error = reader.Error();
      }
      return false;
    }
    // One entry per game file (path and content), at its first use: a payload made of parts lists each
    // part with its own size and hashes.
    std::set<std::string> seen;
    auto add = [&](const std::uint32_t id, const std::string& path, const std::string& archive, const BlobKey& key) {
      std::string name;
      for (const char c : path) {
        name += LowerAscii(c);
      }
      if (seen.insert(name + "|" + Hex64(key.a) + Hex64(key.b) + std::to_string(key.size)).second) {
        refs->push_back(PayloadRefInfo{id, path, archive, key});
      }
    };
    for (std::uint32_t id = 1; id <= reader.PayloadCount(); ++id) {
      const PayloadEntry* const entry = reader.GetPayload(id);
      if (entry != nullptr && entry->kind == PayloadKind::Reference) {
        if (entry->parts.empty()) {
          add(id, entry->path, entry->archive, entry->key);
        }
        for (const PayloadRefPart& part : entry->parts) {
          add(id, part.path, part.archive, part.key);
        }
      }
    }
    return true;
  }

  bool VerifyPayloadRefs(const std::vector<PayloadRefInfo>& refs, PayloadResolver& resolver, std::vector<PayloadRefProblem>* const problems)
  {
    if (problems != nullptr) {
      problems->clear();
    }
    bool ok = true;
    std::vector<std::uint8_t> bytes;
    for (const PayloadRefInfo& ref : refs) {
      std::string error;
      PayloadRefProblem problem;
      problem.ref = ref;
      if (!resolver.ReadGameFile(ref.path, &bytes, &error)) {
        problem.missing = true;
        problem.detail = error;
      } else if (bytes.size() != ref.key.size) {
        problem.foundSize = bytes.size();
        problem.detail = "has " + Bytes(bytes.size()) + ", the trace was recorded with " + Bytes(ref.key.size);
      } else if (!(HashBlob(bytes.data(), bytes.size()) == ref.key)) {
        problem.foundSize = bytes.size();
        problem.detail = "same size, different content";
      } else {
        continue;
      }
      ok = false;
      if (problems != nullptr) {
        problems->push_back(std::move(problem));
      }
    }
    return ok;
  }

  std::string DescribeRefProblems(const std::vector<PayloadRefProblem>& problems, const std::size_t maxLines)
  {
    if (problems.empty()) {
      return {};
    }
    // Missing files grouped by archive first (the usual cause: an archive not imported), then
    // different files (another FAF or SCFA version).
    std::map<std::string, std::vector<const PayloadRefProblem*>> missing;
    std::vector<const PayloadRefProblem*> different;
    for (const PayloadRefProblem& problem : problems) {
      if (problem.missing) {
        missing[problem.ref.archive.empty() ? std::string("?") : problem.ref.archive].push_back(&problem);
      } else {
        different.push_back(&problem);
      }
    }
    std::string text;
    std::size_t lines = 0;
    std::size_t omitted = 0;
    auto line = [&](const std::string& value) {
      if (lines < maxLines) {
        text += value + "\n";
        ++lines;
      } else {
        ++omitted;
      }
    };
    for (const auto& [archive, list] : missing) {
      line("Missing from your game data: " + std::to_string(list.size()) + " file(s) the trace takes from " + archive +
           (archive == "?" ? std::string() : " (import " + archive + ")") + ":");
      for (const PayloadRefProblem* problem : list) {
        line("  " + problem->ref.path);
      }
    }
    if (!different.empty()) {
      line("Different from the files the trace was recorded with (another game or FAF version?):");
      for (const PayloadRefProblem* problem : different) {
        line("  " + problem->ref.path + (problem->ref.archive.empty() ? std::string() : " (" + problem->ref.archive + ")") + ": " +
             problem->detail);
      }
    }
    if (omitted != 0) {
      text += "... and " + std::to_string(omitted) + " more line(s)\n";
    }
    return text;
  }

  std::vector<std::string> RefArchives(const std::vector<PayloadRefInfo>& refs)
  {
    std::vector<std::string> archives;
    std::set<std::string> seen;
    for (const PayloadRefInfo& ref : refs) {
      if (seen.insert(ref.archive).second) {
        archives.push_back(ref.archive);
      }
    }
    return archives;
  }
} // namespace galtrace
