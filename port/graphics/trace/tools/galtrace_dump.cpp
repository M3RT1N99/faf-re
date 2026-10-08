// galtrace-dump: decodes and validates a galtrace file without the engine or any graphics API.
// Portable C++17 (format library only); built for Windows (x86 and x64) by CMakeLists.txt and for
// Android x86_64 and arm64 by android/build_reader.py, so the same trace is shown to decode on the
// pointer width and ABI the phone will play it on.
//
//   galtrace-dump <trace> [--records [N]] [--json <file>] [--quiet] [--blob <id> <file>] [--data <dir>]
//
// It reads every record, checks the stream rules (GalTraceIO.h Validator: schema, blob hashes, live
// ids of the right type, End totals) and prints the metadata, the op counts and a content digest:
// FNV-1a 64 over every record's header and payload, the header's metadata excluded, so two traces
// of the same run have the same digest when their call streams are identical byte for byte.
// --blob writes one payload (#id as --records prints it) to a file, hash-verified; for a version 2
// reference it needs --data. --data <dir>: the game files a version 2 trace refers to, as a tree that
// mirrors the VFS (galtrace-refs extract); every reference is then read and checked against its hashes.
// Exit code 0 when the trace is valid, 1 when it is not, 2 for usage errors.

#include "../format/GalTraceIO.h"
#include "../format/GalTraceResolver.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{
  std::string JsonEscape(const std::string& text)
  {
    std::string out;
    for (const char raw : text) {
      const auto c = static_cast<unsigned char>(raw);
      switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
          if (c < 0x20) {
            char escaped[8];
            std::snprintf(escaped, sizeof(escaped), "\\u%04x", c);
            out += escaped;
          } else {
            out += static_cast<char>(c);
          }
      }
    }
    return out;
  }

  const char* Abi()
  {
#if defined(__aarch64__)
    return "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#else
    return "unknown";
#endif
  }

  const char* Platform()
  {
#if defined(__ANDROID__)
    return "android";
#elif defined(_WIN32)
    return "windows";
#elif defined(__linux__)
    return "linux";
#else
    return "other";
#endif
  }
} // namespace

int main(int argc, char** argv)
{
  if (argc < 2) {
    std::fprintf(stderr, "usage: galtrace-dump <trace> [--records [N]] [--json <file>] [--quiet] [--blob <id> <file>] [--data <dir>]\n");
    return 2;
  }
  const std::string path = argv[1];
  long long recordsToPrint = 0;
  std::string jsonPath;
  bool quiet = false;
  std::uint32_t blobId = 0;
  std::string blobPath;
  std::string dataDir;
  for (int index = 2; index < argc; ++index) {
    if (std::strcmp(argv[index], "--records") == 0) {
      recordsToPrint = -1;
      if (index + 1 < argc && argv[index + 1][0] != '-') {
        recordsToPrint = std::atoll(argv[++index]);
      }
    } else if (std::strcmp(argv[index], "--json") == 0 && index + 1 < argc) {
      jsonPath = argv[++index];
    } else if (std::strcmp(argv[index], "--blob") == 0 && index + 2 < argc) {
      blobId = static_cast<std::uint32_t>(std::strtoul(argv[index + 1], nullptr, 10));
      blobPath = argv[index + 2];
      index += 2;
    } else if (std::strcmp(argv[index], "--data") == 0 && index + 1 < argc) {
      dataDir = argv[++index];
    } else if (std::strcmp(argv[index], "--quiet") == 0) {
      quiet = true;
    } else {
      std::fprintf(stderr, "unknown option %s\n", argv[index]);
      return 2;
    }
  }

  galtrace::Reader reader;
  std::string error;
  if (!reader.Open(path, &error)) {
    std::fprintf(stderr, "galtrace-dump: %s\n", error.c_str());
    return 1;
  }

  galtrace::DirectoryResolver resolver(dataDir);
  galtrace::Validator validator;
  if (!dataDir.empty()) {
    reader.SetResolver(&resolver);
    validator.SetResolveReferences(true);
  }
  galtrace::Record record;
  std::uint64_t digest = 0xCBF29CE484222325ULL;
  std::uint64_t payloadBytes = 0;
  std::uint64_t printed = 0;
  std::uint64_t offThread = 0;
  std::uint64_t threw = 0;
  bool valid = true;
  while (reader.Next(&record, true)) {
    std::uint8_t header[4] = {
      static_cast<std::uint8_t>(record.op), static_cast<std::uint8_t>(record.op >> 8), static_cast<std::uint8_t>(record.flags),
      static_cast<std::uint8_t>(record.flags >> 8)};
    digest = galtrace::Fnv1a64(header, sizeof(header), digest);
    digest = galtrace::Fnv1a64(record.payload.data(), record.payload.size(), digest);
    payloadBytes += record.payload.size();
    offThread += (record.flags & galtrace::kFlagOffThread) ? 1 : 0;
    threw += (record.flags & galtrace::kFlagThrew) ? 1 : 0;
    if (valid && !validator.Check(record, reader)) {
      valid = false;
    }
    if (recordsToPrint != 0 && (recordsToPrint < 0 || static_cast<long long>(printed) < recordsToPrint)) {
      std::string decodeError;
      const std::string line = galtrace::DecodeRecord(record, &decodeError);
      std::printf("%8llu %s%s\n", static_cast<unsigned long long>(record.index), line.c_str(),
                  decodeError.empty() ? "" : ("  !! " + decodeError).c_str());
      ++printed;
    }
  }
  if (valid) {
    valid = validator.Finish(reader);
  }
  const std::string verdict = valid ? "VALID" : "INVALID: " + validator.Error();
  if (!blobPath.empty()) {
    std::vector<std::uint8_t> bytes;
    galtrace::PayloadStatus status;
    const bool read = reader.ReadPayload(blobId, &bytes, &status) && status.exact;
    std::FILE* const out = read ? std::fopen(blobPath.c_str(), "wb") : nullptr;
    if (out == nullptr) {
      std::fprintf(stderr, "galtrace-dump: payload #%u not written%s%s\n", blobId, status.error.empty() ? "" : ": ", status.error.c_str());
      return 1;
    }
    std::fwrite(bytes.data(), 1, bytes.size(), out);
    std::fclose(out);
    std::fprintf(stderr, "blob #%u: %zu bytes -> %s\n", blobId, bytes.size(), blobPath.c_str());
  }

  const std::vector<const galtrace::OpDesc*>& ops = galtrace::AllOps();
  const std::vector<std::uint64_t>& counts = validator.OpCounts();
  if (!quiet) {
    std::printf("galtrace-dump (%s %s, %u-bit pointers): %s\n", Platform(), Abi(), static_cast<unsigned>(sizeof(void*) * 8), path.c_str());
    std::printf("format version %u\n", reader.Version());
    for (const auto& [key, value] : reader.GetMetadata()) {
      std::printf("meta %s = %s\n", key.c_str(), value.c_str());
    }
    for (std::size_t index = 0; index < counts.size() && index < ops.size(); ++index) {
      if (counts[index] != 0) {
        std::printf("op %-26s %10llu\n", ops[index]->name, static_cast<unsigned long long>(counts[index]));
      }
    }
    std::printf(
      "records %llu, blobs %u, presents %u, objects defined %u, live at end %u, off-thread records %llu, calls that threw %llu\n",
      static_cast<unsigned long long>(validator.Records()), reader.BlobCount(), validator.Presents(), validator.ObjectsDefined(),
      validator.LiveObjects(), static_cast<unsigned long long>(offThread), static_cast<unsigned long long>(threw)
    );
    const std::string resolved = dataDir.empty() ? std::string() : ", " + std::to_string(validator.ReferencesResolved()) + " read from --data and equal";
    std::printf("payloads %u: embedded %u (%llu bytes), references %u (%llu bytes%s), digests %u (%llu bytes), compositions %u (%llu bytes, "
                "%llu literal bytes)\n",
                reader.PayloadCount(), validator.Embedded(), static_cast<unsigned long long>(validator.EmbeddedBytes()), validator.References(),
                static_cast<unsigned long long>(validator.ReferenceBytes()), resolved.c_str(), validator.Digests(),
                static_cast<unsigned long long>(validator.DigestBytes()), validator.Compositions(),
                static_cast<unsigned long long>(validator.CompositionBytes()), static_cast<unsigned long long>(validator.LiteralBytes()));
    std::printf("content digest %s\n", galtrace::Hex64(digest).c_str());
    std::printf("%s\n", verdict.c_str());
  } else {
    std::printf("%s digest %s records %llu presents %u (%s %s)\n", verdict.c_str(), galtrace::Hex64(digest).c_str(),
                static_cast<unsigned long long>(validator.Records()), validator.Presents(), Platform(), Abi());
  }

  if (!jsonPath.empty()) {
    if (std::FILE* json = std::fopen(jsonPath.c_str(), "wb")) {
      std::fprintf(json, "{\n  \"trace\": \"%s\",\n  \"reader\": {\"platform\": \"%s\", \"abi\": \"%s\", \"pointer_bits\": %u},\n",
                   JsonEscape(path).c_str(), Platform(), Abi(), static_cast<unsigned>(sizeof(void*) * 8));
      std::fprintf(json, "  \"valid\": %s,\n  \"verdict\": \"%s\",\n  \"version\": %u,\n", valid ? "true" : "false",
                   JsonEscape(verdict).c_str(), reader.Version());
      std::fprintf(json, "  \"metadata\": {");
      bool first = true;
      for (const auto& [key, value] : reader.GetMetadata()) {
        std::fprintf(json, "%s\"%s\": \"%s\"", first ? "" : ", ", JsonEscape(key).c_str(), JsonEscape(value).c_str());
        first = false;
      }
      std::fprintf(json, "},\n  \"ops\": {");
      first = true;
      for (std::size_t index = 0; index < counts.size() && index < ops.size(); ++index) {
        if (counts[index] != 0) {
          std::fprintf(json, "%s\"%s\": %llu", first ? "" : ", ", ops[index]->name, static_cast<unsigned long long>(counts[index]));
          first = false;
        }
      }
      std::fprintf(json,
                   "},\n  \"records\": %llu,\n  \"blobs\": %u,\n  \"payload_bytes\": %llu,\n  \"presents\": %u,\n"
                   "  \"objects_defined\": %u,\n  \"live_at_end\": %u,\n  \"off_thread_records\": %llu,\n  \"threw\": %llu,\n"
                   "  \"content_digest\": \"%s\",\n",
                   static_cast<unsigned long long>(validator.Records()), reader.BlobCount(),
                   static_cast<unsigned long long>(payloadBytes), validator.Presents(), validator.ObjectsDefined(),
                   validator.LiveObjects(), static_cast<unsigned long long>(offThread), static_cast<unsigned long long>(threw),
                   galtrace::Hex64(digest).c_str());
      std::fprintf(json,
                   "  \"payloads\": {\"embedded\": %u, \"embedded_bytes\": %llu, \"references\": %u, \"reference_bytes\": %llu, "
                   "\"references_resolved\": %u, \"digests\": %u, \"digest_bytes\": %llu, \"compositions\": %u, \"composition_bytes\": %llu, "
                   "\"literal_bytes\": %llu}\n}\n",
                   validator.Embedded(), static_cast<unsigned long long>(validator.EmbeddedBytes()), validator.References(),
                   static_cast<unsigned long long>(validator.ReferenceBytes()), validator.ReferencesResolved(), validator.Digests(),
                   static_cast<unsigned long long>(validator.DigestBytes()), validator.Compositions(),
                   static_cast<unsigned long long>(validator.CompositionBytes()), static_cast<unsigned long long>(validator.LiteralBytes()));
      std::fclose(json);
    }
  }
  return valid ? 0 : 1;
}
