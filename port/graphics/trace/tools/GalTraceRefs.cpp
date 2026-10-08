// galtrace-refs' core (GalTraceRefs.h): recording -> version 2 without game data, and its checks.

#include "GalTraceRefs.h"

#include "../format/GalTraceResolver.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace galtrace::refs
{
  namespace
  {
    // ---- a flat view of one record's fields ---------------------------------------------------------

    struct FlatField
    {
      std::string path; // "context.source", "heads[0].name"
      FieldType type = FieldType::U8;
      std::size_t offset = 0; // of the field's encoding in the payload
      std::size_t length = 0;
      std::uint32_t u32 = 0;  // U8/Bool/U32/I32/Blob/IdRef/IdDef/List count
      std::string text;       // Str
    };

    std::uint32_t Load32(const std::uint8_t* const p)
    {
      return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) | (static_cast<std::uint32_t>(p[2]) << 16) |
             (static_cast<std::uint32_t>(p[3]) << 24);
    }

    std::uint64_t Load64(const std::uint8_t* const p)
    {
      return static_cast<std::uint64_t>(Load32(p)) | (static_cast<std::uint64_t>(Load32(p + 4)) << 32);
    }

    void Put32(std::vector<std::uint8_t>& out, const std::uint32_t value)
    {
      for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<std::uint8_t>(value >> shift));
      }
    }

    bool Flatten(
      const FieldDesc* const fields, const std::uint8_t count, const std::vector<std::uint8_t>& payload, std::size_t* const at,
      const std::string& prefix, std::vector<FlatField>* const out
    )
    {
      auto need = [&](const std::size_t size) { return payload.size() - *at >= size; };
      for (std::uint8_t index = 0; index < count; ++index) {
        const FieldDesc& field = fields[index];
        FlatField flat;
        flat.path = prefix + field.name;
        flat.type = field.type;
        flat.offset = *at;
        switch (field.type) {
          case FieldType::U8:
          case FieldType::Bool:
            if (!need(1)) return false;
            flat.u32 = payload[*at];
            *at += 1;
            break;
          case FieldType::U32:
          case FieldType::I32:
          case FieldType::F32:
          case FieldType::Blob:
          case FieldType::IdRef:
          case FieldType::IdDef:
            if (!need(4)) return false;
            flat.u32 = Load32(payload.data() + *at);
            *at += 4;
            break;
          case FieldType::U64:
            if (!need(8)) return false;
            *at += 8;
            break;
          case FieldType::Str:
          case FieldType::Bytes: {
            if (!need(4)) return false;
            const std::uint32_t length = Load32(payload.data() + *at);
            *at += 4;
            if (!need(length)) return false;
            if (field.type == FieldType::Str) {
              flat.text.assign(reinterpret_cast<const char*>(payload.data() + *at), length);
            }
            *at += length;
            break;
          }
          case FieldType::Rect:
            if (!need(16)) return false;
            *at += 16;
            break;
          case FieldType::OptRect:
          case FieldType::OptMatrix: {
            if (!need(1)) return false;
            const bool present = payload[*at] != 0;
            *at += 1;
            const std::size_t size = field.type == FieldType::OptRect ? 16 : 64;
            if (present) {
              if (!need(size)) return false;
              *at += size;
            }
            break;
          }
          case FieldType::Matrix:
            if (!need(64)) return false;
            *at += 64;
            break;
          case FieldType::Vec4:
            if (!need(16)) return false;
            *at += 16;
            break;
          case FieldType::F32Array:
          case FieldType::U32Array: {
            if (!need(4)) return false;
            const std::uint32_t values = Load32(payload.data() + *at);
            *at += 4;
            if ((payload.size() - *at) / 4 < values) return false;
            *at += 4u * values;
            break;
          }
          case FieldType::Struct:
            if (!Flatten(field.sub, field.subCount, payload, at, flat.path + ".", out)) return false;
            continue; // no entry for the struct itself
          case FieldType::List: {
            if (!need(4)) return false;
            flat.u32 = Load32(payload.data() + *at);
            *at += 4;
            flat.length = 4;
            out->push_back(flat);
            for (std::uint32_t element = 0; element < flat.u32; ++element) {
              if (!Flatten(field.sub, field.subCount, payload, at, flat.path + "[" + std::to_string(element) + "].", out)) return false;
            }
            continue;
          }
        }
        flat.length = *at - flat.offset;
        out->push_back(std::move(flat));
      }
      return true;
    }

    bool FlattenRecord(const Record& record, std::vector<FlatField>* const out)
    {
      out->clear();
      if (record.desc == nullptr) {
        return false;
      }
      std::size_t at = 0;
      return Flatten(record.desc->fields, record.desc->fieldCount, record.payload, &at, "", out) && at == record.payload.size();
    }

    const FlatField* Field(const std::vector<FlatField>& fields, const std::string& path)
    {
      for (const FlatField& field : fields) {
        if (field.path == path) {
          return &field;
        }
      }
      return nullptr;
    }

    std::uint32_t FieldU32(const std::vector<FlatField>& fields, const std::string& path)
    {
      const FlatField* const field = Field(fields, path);
      return field != nullptr ? field->u32 : 0;
    }

    std::string FieldText(const std::vector<FlatField>& fields, const std::string& path)
    {
      const FlatField* const field = Field(fields, path);
      return field != nullptr ? field->text : std::string();
    }

    /** The payload with every Str field whose text is a key of `replace` replaced. */
    std::vector<std::uint8_t> ReplaceStrings(
      const std::vector<std::uint8_t>& payload, const std::vector<FlatField>& fields, const std::map<std::string, std::string>& replace,
      std::uint32_t* const replaced
    )
    {
      std::vector<std::uint8_t> out;
      std::size_t copied = 0;
      for (const FlatField& field : fields) {
        if (field.type != FieldType::Str) {
          continue;
        }
        const auto found = replace.find(field.text);
        if (found == replace.end()) {
          continue;
        }
        out.insert(out.end(), payload.begin() + static_cast<std::ptrdiff_t>(copied), payload.begin() + static_cast<std::ptrdiff_t>(field.offset));
        Put32(out, static_cast<std::uint32_t>(found->second.size()));
        out.insert(out.end(), found->second.begin(), found->second.end());
        copied = field.offset + field.length;
        ++*replaced;
      }
      out.insert(out.end(), payload.begin() + static_cast<std::ptrdiff_t>(copied), payload.end());
      return out;
    }

    std::string Lower(std::string text)
    {
      for (char& c : text) {
        if (c >= 'A' && c <= 'Z') {
          c = static_cast<char>(c - 'A' + 'a');
        }
      }
      return text;
    }

    std::string SizeText(const std::uint64_t bytes)
    {
      char text[48];
      std::snprintf(text, sizeof(text), "%llu bytes", static_cast<unsigned long long>(bytes));
      return text;
    }

    std::uint64_t ContentDigestStep(std::uint64_t digest, const Record& record)
    {
      const std::uint8_t header[4] = {static_cast<std::uint8_t>(record.op), static_cast<std::uint8_t>(record.op >> 8),
                                      static_cast<std::uint8_t>(record.flags), static_cast<std::uint8_t>(record.flags >> 8)};
      digest = Fnv1a64(header, sizeof(header), digest);
      return Fnv1a64(record.payload.data(), record.payload.size(), digest);
    }

    /** "command_line" with every token that looks like a path of the recording machine replaced. */
    std::string SanitizeCommandLine(const std::string& line)
    {
      std::string out;
      std::size_t at = 0;
      while (at < line.size()) {
        std::size_t end = line.find(' ', at);
        if (end == std::string::npos) {
          end = line.size();
        }
        const std::string token = line.substr(at, end - at);
        const bool path = token.find('\\') != std::string::npos || token.find(':') != std::string::npos || token.rfind("..", 0) == 0 ||
                          (token.size() > 1 && token[0] == '/' && token.find('/', 1) != std::string::npos);
        if (!out.empty()) {
          out += ' ';
        }
        out += path ? "<path>" : token;
        at = end + 1;
      }
      return out;
    }

    std::string CommandLineValue(const std::string& line, const std::string& option)
    {
      const std::string needle = option + " ";
      const std::size_t found = line.find(needle);
      if (found == std::string::npos) {
        return {};
      }
      const std::size_t start = found + needle.size();
      const std::size_t end = line.find(' ', start);
      return line.substr(start, end == std::string::npos ? std::string::npos : end - start);
    }

    // ---- what the calls do with each recorded payload -------------------------------------------------

    enum class Use
    {
      GameFile,       // a file image the engine read from its VFS
      BackendOutput,  // GetTexture2D's output
      Readback,       // a read-only lock's content
      SavedOutput,    // SaveTexture / SaveToBuffer output
      TextureWrite,   // what the engine wrote into a locked texture
      BufferData,     // vertex / index buffer contents
      EffectCache,    // a compiled effect read from the cache directory
      Other,
    };

    const char* UseName(const Use use)
    {
      switch (use) {
        case Use::GameFile: return "game file";
        case Use::BackendOutput: return "GetTexture2D output";
        case Use::Readback: return "readback";
        case Use::SavedOutput: return "saved output";
        case Use::TextureWrite: return "texture write";
        case Use::BufferData: return "vertex/index data";
        case Use::EffectCache: return "effect cache file";
        case Use::Other: return "other";
      }
      return "?";
    }

    struct BlobUse
    {
      Use use = Use::Other;
      std::uint64_t record = 0;
      std::string hint;            // the record's path string (GameFile)
      std::uint32_t width = 0;     // BackendOutput
      std::uint32_t height = 0;
      std::uint32_t rowBytes = 0;  // TextureWrite: the locked region
      std::uint32_t rows = 0;
      std::uint32_t format = 0;
    };

    struct Plan
    {
      PayloadKind kind = PayloadKind::Embedded;
      DigestOrigin origin = DigestOrigin::Other;
      std::string path;
      std::string archive;
      std::vector<PayloadRefPart> parts; // Reference made of several files
      std::vector<PayloadCopy> copies;
      std::vector<PayloadLiteral> literals;
      std::string use; // the use it was planned for (report)
    };

    struct Shape
    {
      std::uint32_t rowBytes = 0;
      std::uint32_t rows = 0;
    };

    /** The block-row layout of a GetTexture2D output from its size and dimensions. */
    bool OutputShape(const std::uint32_t size, const std::uint32_t width, const std::uint32_t height, Shape* const shape)
    {
      if (width == 0 || height == 0) {
        return false;
      }
      const std::uint64_t blocksWide = (width + 3u) / 4u;
      const std::uint64_t blocksHigh = (height + 3u) / 4u;
      for (const std::uint32_t bytesPerBlock : {16u, 8u}) {
        if (blocksWide * blocksHigh * bytesPerBlock == size) {
          shape->rowBytes = static_cast<std::uint32_t>(blocksWide * bytesPerBlock);
          shape->rows = static_cast<std::uint32_t>(blocksHigh);
          return true;
        }
      }
      if (static_cast<std::uint64_t>(width) * height * 4u == size) {
        shape->rowBytes = width * 4u;
        shape->rows = height;
        return true;
      }
      return false;
    }

    struct TextureState
    {
      std::uint32_t format = 0;
      std::uint32_t rowBytes = 0; // the current lock's region
      std::uint32_t rows = 0;
    };

    /**
     * The placements of `sources` in `target` (rows of `targetRowBytes`): every place where all of a
     * source's rows appear, row under row, at the same column.
     */
    std::vector<PayloadCopy> FindPlacements(
      const std::vector<std::uint8_t>& target, const std::uint32_t targetRowBytes,
      const std::vector<std::pair<std::uint32_t, const std::vector<std::uint8_t>*>>& sources, const std::map<std::uint32_t, Shape>& shapes
    )
    {
      std::vector<PayloadCopy> placements;
      std::set<std::pair<std::uint32_t, std::uint64_t>> taken; // (offset, rowBytes * rows)
      const std::uint64_t targetRows = targetRowBytes != 0 ? target.size() / targetRowBytes : 0;
      for (const auto& [id, bytes] : sources) {
        const Shape& shape = shapes.at(id);
        if (shape.rowBytes == 0 || shape.rowBytes > targetRowBytes || shape.rows > targetRows) {
          continue;
        }
        // The first row that is not all zeros anchors the search (zeros match everywhere).
        std::uint32_t anchor = shape.rows;
        for (std::uint32_t row = 0; row < shape.rows; ++row) {
          const std::uint8_t* const begin = bytes->data() + static_cast<std::size_t>(row) * shape.rowBytes;
          if (std::any_of(begin, begin + shape.rowBytes, [](const std::uint8_t value) { return value != 0; })) {
            anchor = row;
            break;
          }
        }
        if (anchor == shape.rows) {
          continue;
        }
        const std::uint8_t* const needle = bytes->data() + static_cast<std::size_t>(anchor) * shape.rowBytes;
        const std::boyer_moore_horspool_searcher searcher(needle, needle + shape.rowBytes);
        auto from = target.begin();
        while (true) {
          const auto hit = std::search(from, target.end(), searcher);
          if (hit == target.end()) {
            break;
          }
          const std::uint64_t position = static_cast<std::uint64_t>(hit - target.begin());
          from = hit + 1;
          const std::uint64_t column = position % targetRowBytes;
          const std::uint64_t row = position / targetRowBytes;
          if (column + shape.rowBytes > targetRowBytes || row < anchor || row - anchor + shape.rows > targetRows) {
            continue;
          }
          const std::uint64_t top = row - anchor;
          bool all = true;
          for (std::uint32_t r = 0; all && r < shape.rows; ++r) {
            all = std::memcmp(target.data() + (top + r) * targetRowBytes + column, bytes->data() + static_cast<std::size_t>(r) * shape.rowBytes,
                              shape.rowBytes) == 0;
          }
          if (!all) {
            continue;
          }
          const auto offset = static_cast<std::uint32_t>(top * targetRowBytes + column);
          if (!taken.emplace(offset, static_cast<std::uint64_t>(shape.rowBytes) * shape.rows).second) {
            continue; // another source with the same bytes already placed here
          }
          PayloadCopy copy;
          copy.source = id;
          copy.sourceOffset = 0;
          copy.sourcePitch = shape.rowBytes;
          copy.offset = offset;
          copy.pitch = targetRowBytes;
          copy.rowBytes = shape.rowBytes;
          copy.rows = shape.rows;
          placements.push_back(copy);
        }
      }
      return placements;
    }

    /** Literal runs for the bytes of `target` that `built` does not have (runs closer than 8 bytes merge). */
    std::vector<PayloadLiteral> DiffLiterals(const std::vector<std::uint8_t>& built, const std::vector<std::uint8_t>& target)
    {
      std::vector<PayloadLiteral> literals;
      std::size_t at = 0;
      const std::size_t size = target.size();
      while (at < size) {
        if (built[at] == target[at]) {
          ++at;
          continue;
        }
        // `end` is one past the last differing byte seen; the run ends after 8 equal bytes or at the end.
        std::size_t end = at + 1;
        for (std::size_t probe = end; probe < size && probe - end < 8; ++probe) {
          if (built[probe] != target[probe]) {
            end = probe + 1;
          }
        }
        PayloadLiteral literal;
        literal.offset = static_cast<std::uint32_t>(at);
        literal.bytes.assign(target.begin() + static_cast<std::ptrdiff_t>(at), target.begin() + static_cast<std::ptrdiff_t>(end));
        literals.push_back(std::move(literal));
        at = end;
      }
      return literals;
    }

    // ---- 64-byte windows of game-derived bytes (the content scan) --------------------------------------

    constexpr std::size_t kWindow = 64;
    constexpr std::uint64_t kRollBase = 0x100000001B3ULL;

    std::uint64_t RollPower()
    {
      std::uint64_t power = 1;
      for (std::size_t index = 0; index < kWindow; ++index) {
        power *= kRollBase;
      }
      return power;
    }

    std::uint64_t WindowHash(const std::uint8_t* const data)
    {
      std::uint64_t hash = 0;
      for (std::size_t index = 0; index < kWindow; ++index) {
        hash = hash * kRollBase + data[index];
      }
      return hash;
    }

    bool Trivial(const std::uint8_t* const data, const std::size_t size)
    {
      // One repeated byte, or one repeated 4-byte word: matches anywhere, says nothing.
      bool sameByte = true;
      bool sameWord = true;
      for (std::size_t index = 1; index < size && (sameByte || sameWord); ++index) {
        sameByte = sameByte && data[index] == data[0];
        sameWord = sameWord && data[index] == data[index % 4];
      }
      return sameByte || sameWord;
    }

    struct WindowSource
    {
      std::string name;
      std::vector<std::uint8_t> bytes;
    };

    class WindowIndex
    {
    public:
      void Add(std::string name, std::vector<std::uint8_t> bytes)
      {
        sources_.push_back(WindowSource{std::move(name), std::move(bytes)});
        const auto source = static_cast<std::uint32_t>(sources_.size() - 1);
        const std::vector<std::uint8_t>& data = sources_.back().bytes;
        for (std::size_t offset = 0; offset + kWindow <= data.size(); offset += 16) {
          if (!Trivial(data.data() + offset, kWindow)) {
            windows_.emplace(WindowHash(data.data() + offset), std::make_pair(source, static_cast<std::uint32_t>(offset)));
            ++count_;
          }
        }
      }

      [[nodiscard]] std::uint64_t Count() const { return count_; }

      /** Bytes of `data` covered by windows of the index (any alignment), and the first source hit. */
      std::uint64_t Scan(const std::uint8_t* const data, const std::size_t size, std::string* const firstSource) const
      {
        if (size < kWindow || windows_.empty()) {
          return 0;
        }
        static const std::uint64_t power = RollPower();
        std::uint64_t hash = WindowHash(data);
        std::uint64_t covered = 0;
        std::size_t coveredUntil = 0;
        for (std::size_t offset = 0;; ++offset) {
          const auto range = windows_.equal_range(hash);
          for (auto it = range.first; it != range.second; ++it) {
            const WindowSource& source = sources_[it->second.first];
            if (std::memcmp(source.bytes.data() + it->second.second, data + offset, kWindow) == 0 && !Trivial(data + offset, kWindow)) {
              const std::size_t start = std::max(offset, coveredUntil);
              covered += offset + kWindow - start;
              coveredUntil = offset + kWindow;
              if (firstSource != nullptr && firstSource->empty()) {
                *firstSource = source.name + " at " + std::to_string(it->second.second);
              }
              break;
            }
          }
          if (offset + kWindow >= size) {
            break;
          }
          hash = hash * kRollBase - power * data[offset] + data[offset + kWindow];
        }
        return covered;
      }

    private:
      std::vector<WindowSource> sources_;
      std::unordered_multimap<std::uint64_t, std::pair<std::uint32_t, std::uint32_t>> windows_;
      std::uint64_t count_ = 0;
    };

    /** A resolver over the index, for the reader and the validator. */
    CallbackResolver IndexResolver(GameFileIndex& index)
    {
      return CallbackResolver([&index](const std::string& path, std::vector<std::uint8_t>* out, std::string* error) {
        return index.Read(path, out, error);
      });
    }

    bool WriteFile(const std::string& path, const std::vector<std::uint8_t>& bytes)
    {
      std::FILE* const file = std::fopen(path.c_str(), "wb");
      if (file == nullptr) {
        return false;
      }
      const bool ok = bytes.empty() || std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
      return (std::fclose(file) == 0) && ok;
    }
  } // namespace

  std::string VfsPathFromDiskHint(const std::string& diskPath)
  {
    const std::string lower = Lower(diskPath);
    std::size_t best = std::string::npos;
    std::size_t length = 0;
    for (const char* const marker : {".scd\\", ".nx2\\", ".nxt\\", ".zip\\", ".scd/", ".nx2/", ".nxt/", ".zip/"}) {
      const std::size_t found = lower.rfind(marker);
      if (found != std::string::npos && (best == std::string::npos || found > best)) {
        best = found;
        length = std::strlen(marker);
      }
    }
    if (best == std::string::npos) {
      // Already a VFS path ("/textures/ui/x.dds")?
      if (lower.size() > 1 && lower[0] == '/' && lower.find(':') == std::string::npos && lower.find('\\') == std::string::npos) {
        return lower;
      }
      return {};
    }
    std::string path = "/";
    for (std::size_t index = best + length; index < lower.size(); ++index) {
      const char c = lower[index] == '\\' ? '/' : lower[index];
      if (c == '/' && path.back() == '/') {
        continue;
      }
      path += c;
    }
    return path.size() > 1 ? path : std::string();
  }

  // ---- Convert -----------------------------------------------------------------------------------------

  bool Convert(const std::string& inPath, const std::string& outPath, GameFileIndex& index, const ConvertOptions& options, ConvertReport* const report)
  {
    *report = ConvertReport{};
    auto fail = [&](const std::string& message) {
      report->ok = false;
      report->error = message;
      return false;
    };
    auto log = [&](const std::string& line) {
      if (options.log) {
        options.log(line);
      }
    };

    // Pass 1: how every payload is used.
    Reader reader;
    std::string error;
    if (!reader.Open(inPath, &error)) {
      return fail(error);
    }
    if (std::FILE* const file = std::fopen(inPath.c_str(), "rb")) {
      (void)std::fseek(file, 0, SEEK_END);
      report->inputBytes = static_cast<std::uint64_t>(std::ftell(file));
      std::fclose(file);
    }
    std::map<std::uint32_t, std::vector<BlobUse>> uses;
    std::map<std::uint32_t, TextureState> textures;
    std::uint32_t endPresents = 0;
    std::uint32_t endObjects = 0;
    std::uint64_t digest = 0xCBF29CE484222325ULL;
    {
      Record record;
      std::vector<FlatField> fields;
      while (reader.Next(&record, true)) {
        digest = ContentDigestStep(digest, record);
        ++report->records;
        if (IsPayloadDefinition(record.op)) {
          if (record.op != static_cast<std::uint16_t>(Op::Blob)) {
            return fail("the input is already a version 2 trace with payload references");
          }
          continue;
        }
        if (!FlattenRecord(record, &fields)) {
          return fail("record " + std::to_string(record.index) + " does not match its schema");
        }
        const Op op = static_cast<Op>(record.op);
        if (op == Op::End) {
          endPresents = FieldU32(fields, "presents");
          endObjects = FieldU32(fields, "objects");
          continue;
        }
        if (op == Op::DevCreateTexture) {
          TextureState& state = textures[FieldU32(fields, "texture")];
          state.format = FieldU32(fields, "created.format");
        } else if (op == Op::TexGetContext) {
          textures[FieldU32(fields, "texture")].format = FieldU32(fields, "context.format");
        } else if (op == Op::TexLock) {
          TextureState& state = textures[FieldU32(fields, "texture")];
          state.rowBytes = FieldU32(fields, "rowBytes");
          state.rows = FieldU32(fields, "rows");
        }
        for (const FlatField& field : fields) {
          if (field.type != FieldType::Blob || field.u32 == 0) {
            continue;
          }
          BlobUse use;
          use.record = record.index;
          switch (op) {
            case Op::DevCreateEffect:
              use.use = field.path == "context.source" ? Use::GameFile : Use::EffectCache;
              use.hint = FieldText(fields, "context.sourcePath");
              break;
            case Op::DevCreateTexture:
              use.use = Use::GameFile;
              use.hint = FieldText(fields, field.path.substr(0, field.path.find('.')) + ".location");
              break;
            case Op::DevGetTexture2D:
              if (field.path == "source") {
                use.use = Use::GameFile;
              } else {
                use.use = Use::BackendOutput;
                use.width = FieldU32(fields, "width");
                use.height = FieldU32(fields, "height");
              }
              break;
            case Op::DevSaveTexture:
            case Op::TexSaveToBuffer:
              use.use = Use::SavedOutput;
              break;
            case Op::TexUnlockRect:
            case Op::TexUnlockLevel: {
              const auto content = static_cast<LockContent>(FieldU32(fields, "content"));
              const TextureState& state = textures[FieldU32(fields, "texture")];
              if (content == LockContent::ReadBack) {
                use.use = Use::Readback;
                ++report->readbacks;
              } else if (content == LockContent::Written) {
                use.use = Use::TextureWrite;
                use.rowBytes = state.rowBytes;
                use.rows = state.rows;
                use.format = state.format;
              }
              break;
            }
            case Op::VbUnlock:
            case Op::IbUnlock:
              use.use = Use::BufferData;
              break;
            default:
              use.use = Use::Other;
              break;
          }
          uses[field.u32].push_back(std::move(use));
        }
      }
      if (reader.Failed()) {
        return fail("reader: " + reader.Error());
      }
      if (!reader.SawEnd()) {
        return fail("the input has no End record (truncated)");
      }
    }
    report->presents = endPresents;
    log("pass 1: " + std::to_string(report->records) + " records, " + std::to_string(reader.PayloadCount()) + " payloads, " +
        std::to_string(uses.size()) + " used");

    // Plans, payload by payload in definition order.
    const std::uint32_t payloads = reader.PayloadCount();
    std::vector<Plan> plans(payloads + 1);
    std::map<std::string, std::string> pathOf;      // a record's disk path -> the VFS path it was resolved to
    std::map<std::uint32_t, Shape> outputShapes;   // BackendOutput digests with a known block layout
    std::map<std::uint32_t, std::vector<std::uint8_t>> outputBytes;
    std::map<std::uint64_t, std::vector<std::string>> candidatesBySize;
    std::map<std::string, BlobKey> fileKeys;       // VFS path -> hashes of its bytes
    auto keyOfFile = [&](const std::string& path, BlobKey* const key) {
      const auto cached = fileKeys.find(path);
      if (cached != fileKeys.end()) {
        *key = cached->second;
        return true;
      }
      std::vector<std::uint8_t> bytes;
      std::string readError;
      if (!index.Read(path, &bytes, &readError)) {
        return false;
      }
      *key = HashBlob(bytes.data(), bytes.size());
      fileKeys.emplace(path, *key);
      return true;
    };

    for (std::uint32_t id = 1; id <= payloads; ++id) {
      Plan& plan = plans[id];
      const PayloadEntry* const entry = reader.GetPayload(id);
      const auto found = uses.find(id);
      if (found == uses.end() || found->second.empty()) {
        plan.use = "unused";
        report->notes.push_back("payload #" + std::to_string(id) + " is defined but never used: embedded");
        continue;
      }
      const std::vector<BlobUse>& list = found->second;
      std::set<Use> kinds;
      for (const BlobUse& use : list) {
        kinds.insert(use.use);
      }
      plan.use = UseName(list.front().use);

      if (kinds.count(Use::GameFile) != 0) {
        plan.use = UseName(Use::GameFile);
        std::string how;
        // The record's own path first, then every VFS file of the same size.
        std::vector<std::string> tried;
        for (const BlobUse& use : list) {
          if (use.use == Use::GameFile && !use.hint.empty()) {
            const std::string vfsPath = VfsPathFromDiskHint(use.hint);
            if (!vfsPath.empty()) {
              tried.push_back(vfsPath);
            }
          }
        }
        std::string resolved;
        for (const std::string& path : tried) {
          BlobKey key;
          if (keyOfFile(path, &key) && key == entry->key) {
            resolved = path;
            how = "by its record's path";
            break;
          }
        }
        if (resolved.empty()) {
          auto cached = candidatesBySize.find(entry->key.size);
          if (cached == candidatesBySize.end()) {
            cached = candidatesBySize.emplace(entry->key.size, index.CandidatesBySize(entry->key.size)).first;
          }
          for (const std::string& path : cached->second) {
            BlobKey key;
            if (keyOfFile(path, &key) && key == entry->key) {
              resolved = path;
              how = "by content (" + std::to_string(cached->second.size()) + " file(s) of this size)";
              break;
            }
          }
        }
        // Two files one after the other, the record's own file one of them: the engine hands gal an effect
        // source as /effects/d3d9states.compat followed by the .fx file. The other part is a file of the
        // remaining size with those bytes (one in the record's own directory first).
        std::vector<PayloadRefPart> parts;
        if (resolved.empty() && !tried.empty()) {
          std::vector<std::uint8_t> bytes;
          if (!reader.ReadBlob(id, &bytes)) {
            return fail("payload #" + std::to_string(id) + " cannot be read");
          }
          for (const std::string& hinted : tried) {
            std::vector<std::uint8_t> file;
            std::string readError;
            if (!index.Read(hinted, &file, &readError) || file.empty() || file.size() >= bytes.size()) {
              continue;
            }
            const std::size_t rest = bytes.size() - file.size();
            const bool asSuffix = std::equal(file.begin(), file.end(), bytes.end() - static_cast<std::ptrdiff_t>(file.size()));
            const bool asPrefix = std::equal(file.begin(), file.end(), bytes.begin());
            if (!asSuffix && !asPrefix) {
              continue;
            }
            for (const bool suffix : {true, false}) {
              if ((suffix && !asSuffix) || (!suffix && !asPrefix) || !parts.empty()) {
                continue;
              }
              const std::uint8_t* const other = suffix ? bytes.data() : bytes.data() + file.size();
              const BlobKey otherKey = HashBlob(other, rest);
              auto cached = candidatesBySize.find(rest);
              if (cached == candidatesBySize.end()) {
                cached = candidatesBySize.emplace(rest, index.CandidatesBySize(rest)).first;
              }
              std::vector<std::string> candidates = cached->second;
              const std::string directory = hinted.substr(0, hinted.find_last_of('/') + 1);
              std::stable_partition(candidates.begin(), candidates.end(), [&](const std::string& path) {
                return path.compare(0, directory.size(), directory) == 0 && path.find('/', directory.size()) == std::string::npos;
              });
              for (const std::string& path : candidates) {
                BlobKey key;
                if (keyOfFile(path, &key) && key == otherKey) {
                  const PayloadRefPart named{hinted, index.ArchiveOf(hinted), HashBlob(file.data(), file.size())};
                  const PayloadRefPart match{path, index.ArchiveOf(path), otherKey};
                  parts = suffix ? std::vector<PayloadRefPart>{match, named} : std::vector<PayloadRefPart>{named, match};
                  resolved = hinted;
                  how = "as " + (suffix ? path + " + " + hinted : hinted + " + " + path);
                  break;
                }
              }
            }
            if (!parts.empty()) {
              break;
            }
          }
        }
        if (resolved.empty()) {
          report->unresolved.push_back("payload #" + std::to_string(id) + " (" + SizeText(entry->key.size) + ", first used by record " +
                                       std::to_string(list.front().record) + (tried.empty() ? std::string() : ", path " + tried.front()) +
                                       "): no game file has these bytes");
          plan.kind = PayloadKind::Embedded;
          continue;
        }
        plan.kind = PayloadKind::Reference;
        plan.path = resolved;
        plan.archive = index.ArchiveOf(resolved);
        plan.parts = std::move(parts);
        for (const BlobUse& use : list) {
          if (use.use == Use::GameFile && !use.hint.empty()) {
            pathOf[use.hint] = resolved;
          }
        }
        report->referenceLines.push_back("#" + std::to_string(id) + " " + resolved + " (" + plan.archive + ") " + SizeText(entry->key.size) +
                                         ", " + how);
        continue;
      }

      const bool compareOnly = std::all_of(kinds.begin(), kinds.end(), [](const Use use) { return use == Use::Readback || use == Use::SavedOutput; });
      if (compareOnly && options.digestReadbacks) {
        plan.kind = PayloadKind::Digest;
        plan.origin = kinds.count(Use::Readback) != 0 ? DigestOrigin::Readback : DigestOrigin::SavedOutput;
        continue;
      }

      if (list.front().use == Use::BackendOutput &&
          std::all_of(kinds.begin(), kinds.end(), [](const Use use) {
            return use == Use::BackendOutput || use == Use::Readback || use == Use::SavedOutput || use == Use::TextureWrite;
          })) {
        plan.kind = PayloadKind::Digest;
        plan.origin = DigestOrigin::BackendOutput;
        Shape shape;
        if (OutputShape(entry->key.size, list.front().width, list.front().height, &shape)) {
          std::vector<std::uint8_t> bytes;
          if (!reader.ReadBlob(id, &bytes)) {
            return fail("payload #" + std::to_string(id) + " cannot be read");
          }
          outputShapes[id] = shape;
          outputBytes[id] = std::move(bytes);
        } else {
          report->notes.push_back("payload #" + std::to_string(id) + ": GetTexture2D output of " + std::to_string(list.front().width) + "x" +
                                  std::to_string(list.front().height) + " and " + SizeText(entry->key.size) +
                                  " has no block layout I know; it is not searched for in texture writes");
        }
        continue;
      }

      if (options.composeTextureWrites && kinds.size() == 1 && kinds.count(Use::TextureWrite) != 0) {
        const BlobUse& use = list.front();
        std::vector<std::uint8_t> target;
        if (!reader.ReadBlob(id, &target)) {
          return fail("payload #" + std::to_string(id) + " cannot be read");
        }
        if (use.rowBytes != 0 && static_cast<std::uint64_t>(use.rowBytes) * use.rows == target.size()) {
          std::vector<std::pair<std::uint32_t, const std::vector<std::uint8_t>*>> sources;
          for (const auto& [source, bytes] : outputBytes) {
            if (source < id) {
              sources.emplace_back(source, &bytes);
            }
          }
          log("payload #" + std::to_string(id) + ": texture write of " + SizeText(target.size()) + ", searching " + std::to_string(sources.size()) +
              " GetTexture2D output(s) in it");
          std::vector<PayloadCopy> copies = FindPlacements(target, use.rowBytes, sources, outputShapes);
          log("payload #" + std::to_string(id) + ": " + std::to_string(copies.size()) + " placement(s)");
          if (!copies.empty()) {
            std::vector<const std::vector<std::uint8_t>*> copySources;
            for (const PayloadCopy& copy : copies) {
              copySources.push_back(&outputBytes.at(copy.source));
            }
            std::vector<std::uint8_t> built;
            std::string buildError;
            if (!BuildComposition(static_cast<std::uint32_t>(target.size()), nullptr, copies, copySources, {}, &built, &buildError)) {
              return fail("payload #" + std::to_string(id) + ": " + buildError);
            }
            plan.literals = DiffLiterals(built, target);
            std::vector<std::uint8_t> check;
            if (!BuildComposition(static_cast<std::uint32_t>(target.size()), nullptr, copies, copySources, plan.literals, &check, &buildError) ||
                check != target) {
              return fail("payload #" + std::to_string(id) + ": the composition does not rebuild the recorded bytes");
            }
            plan.kind = PayloadKind::Composed;
            plan.copies = std::move(copies);
            std::uint64_t copied = 0;
            std::uint64_t literal = 0;
            for (const PayloadCopy& copy : plan.copies) {
              copied += static_cast<std::uint64_t>(copy.rowBytes) * copy.rows;
            }
            for (const PayloadLiteral& run : plan.literals) {
              literal += run.bytes.size();
            }
            report->compositionLines.push_back("#" + std::to_string(id) + " " + SizeText(target.size()) + " (" + std::to_string(use.rowBytes) +
                                               " x " + std::to_string(use.rows) + ", gal format " + std::to_string(use.format) + "): " +
                                               std::to_string(plan.copies.size()) + " GetTexture2D output(s) placed (" +
                                               SizeText(copied) + "), " + std::to_string(plan.literals.size()) + " literal run(s) (" +
                                               SizeText(literal) + "), the rest zeros");
            continue;
          }
        }
      }
      plan.kind = PayloadKind::Embedded;
    }
    if (!report->unresolved.empty() && !options.allowEmbeddedGameFiles) {
      return fail(std::to_string(report->unresolved.size()) + " game-file payload(s) match no file of the game data");
    }

    log("plans done");
    // The header.
    Metadata metadata;
    std::vector<std::string> refArchives;
    std::uint32_t refCount = 0;
    for (std::uint32_t id = 1; id <= payloads; ++id) {
      if (plans[id].kind == PayloadKind::Reference) {
        ++refCount;
        std::vector<std::string> archives = {plans[id].archive};
        for (const PayloadRefPart& part : plans[id].parts) {
          archives.push_back(part.archive);
        }
        for (const std::string& archive : archives) {
          if (std::find(refArchives.begin(), refArchives.end(), archive) == refArchives.end()) {
            refArchives.push_back(archive);
          }
        }
      }
    }
    std::string archives;
    for (const std::string& archive : refArchives) {
      archives += (archives.empty() ? "" : ",") + archive;
    }
    const std::string commandLine = reader.Meta("command_line");
    std::map<std::string, std::string> computed = {
      {kMetaPresents, std::to_string(endPresents)},
      {kMetaReadbacks, std::to_string(report->readbacks)},
      {kMetaPayloadRefs, std::to_string(refCount)},
      {kMetaRefArchives, archives},
      {"converted_by", "galtrace-refs (port/graphics/trace/tools): game files as references, readbacks and GetTexture2D outputs as digests"},
      {"source_version", std::to_string(reader.Version())},
      {"source_content_digest", Hex64(digest)},
    };
    const std::string frameRate = CommandLineValue(commandLine, "/framerate");
    if (!frameRate.empty()) {
      computed[kMetaFrameRate] = frameRate;
    }
    for (const auto& [key, value] : reader.GetMetadata()) {
      if (computed.count(key) != 0) {
        continue;
      }
      metadata.emplace_back(key, key == "command_line" && options.sanitizeCommandLine ? SanitizeCommandLine(value) : value);
    }
    for (const auto& [key, value] : computed) {
      metadata.emplace_back(key, value);
    }
    for (const auto& [key, value] : options.setMetadata) {
      bool replaced = false;
      for (auto& pair : metadata) {
        if (pair.first == key) {
          pair.second = value;
          replaced = true;
        }
      }
      if (!replaced) {
        metadata.emplace_back(key, value);
      }
    }

    // Pass 2: write.
    Reader input;
    if (!input.Open(inPath, &error)) {
      return fail(error);
    }
    Writer writer;
    if (!writer.Open(outPath, metadata, &error, kFormatVersion2)) {
      return fail(error);
    }
    Record record;
    std::vector<FlatField> fields;
    std::uint32_t replacedStrings = 0;
    while (input.Next(&record, true)) {
      if (record.op == static_cast<std::uint16_t>(Op::Blob)) {
        const std::uint32_t id = Load32(record.payload.data());
        const Plan& plan = plans[id];
        const BlobKey key{Load64(record.payload.data() + 4), Load64(record.payload.data() + 12), Load32(record.payload.data() + 20)};
        std::uint32_t defined = 0;
        PayloadCount* count = nullptr;
        switch (plan.kind) {
          case PayloadKind::Embedded:
            defined = writer.DefineEmbedded(record.payload.data() + 24, record.payload.size() - 24);
            count = &report->embedded;
            break;
          case PayloadKind::Reference:
            defined = writer.DefineReference(key, plan.path, plan.archive, plan.parts);
            count = &report->references;
            break;
          case PayloadKind::Digest:
            defined = writer.DefineDigest(key, plan.origin);
            count = &report->digests;
            break;
          case PayloadKind::Composed:
            defined = writer.DefineComposition(key, 0, plan.copies, plan.literals);
            count = &report->compositions;
            report->compositionCopies += static_cast<std::uint32_t>(plan.copies.size());
            for (const PayloadLiteral& literal : plan.literals) {
              report->compositionLiteralBytes += literal.bytes.size();
            }
            break;
        }
        if (defined != id) {
          return fail("payload #" + std::to_string(id) + " was written as #" + std::to_string(defined));
        }
        ++count->count;
        count->bytes += key.size;
        PayloadCount& byUse = report->byUse[plan.use + " -> " + PayloadKindName(plan.kind)];
        ++byUse.count;
        byUse.bytes += key.size;
        continue;
      }
      if (record.op == static_cast<std::uint16_t>(Op::End)) {
        writer.Close(endPresents, endObjects);
        break;
      }
      if (!pathOf.empty()) {
        if (!FlattenRecord(record, &fields)) {
          return fail("record " + std::to_string(record.index) + " does not match its schema");
        }
        std::uint32_t replaced = 0;
        const std::vector<std::uint8_t> payload = ReplaceStrings(record.payload, fields, pathOf, &replaced);
        if (replaced != 0) {
          replacedStrings += replaced;
          writer.CommitRaw(record.op, record.flags, payload.data(), payload.size());
          continue;
        }
      }
      writer.CommitRaw(record.op, record.flags, record.payload.data(), record.payload.size());
    }
    if (input.Failed()) {
      return fail("reader: " + input.Error());
    }
    if (writer.IsOpen()) {
      return fail("the input ended without an End record");
    }
    report->notes.push_back(std::to_string(replacedStrings) + " path string(s) of the recording machine replaced by VFS paths");
    if (std::FILE* const file = std::fopen(outPath.c_str(), "rb")) {
      (void)std::fseek(file, 0, SEEK_END);
      report->outputBytes = static_cast<std::uint64_t>(std::ftell(file));
      std::fclose(file);
    }
    report->ok = true;
    return true;
  }

  // ---- Check ---------------------------------------------------------------------------------------------

  bool Check(const std::string& tracePath, GameFileIndex& index, const CheckOptions& options, CheckReport* const report)
  {
    *report = CheckReport{};
    CallbackResolver resolver = IndexResolver(index);

    // 1. The stream rules, every reference resolved against the game data.
    {
      Reader reader;
      std::string error;
      if (!reader.Open(tracePath, &error)) {
        report->error = error;
        return false;
      }
      reader.SetResolver(&resolver);
      Validator validator;
      validator.SetResolveReferences(true);
      Record record;
      bool valid = true;
      while (reader.Next(&record, true)) {
        if (valid && !validator.Check(record, reader)) {
          valid = false;
        }
      }
      valid = valid && validator.Finish(reader);
      report->valid = valid;
      report->validatorError = valid ? std::string() : validator.Error();
      report->references = validator.References();
      report->referencesResolved = validator.ReferencesResolved();
      report->lines.push_back(std::string("validator (references resolved through the game data): ") + (valid ? "VALID" : "INVALID: " + validator.Error()));
      report->lines.push_back("payloads: " + std::to_string(validator.Embedded()) + " embedded (" + SizeText(validator.EmbeddedBytes()) + "), " +
                              std::to_string(validator.References()) + " references (" + SizeText(validator.ReferenceBytes()) + ", " +
                              std::to_string(validator.ReferencesResolved()) + " resolved and equal), " + std::to_string(validator.Digests()) +
                              " digests (" + SizeText(validator.DigestBytes()) + "), " + std::to_string(validator.Compositions()) +
                              " compositions (" + SizeText(validator.CompositionBytes()) + " built from " + SizeText(validator.LiteralBytes()) +
                              " of literals)");
    }

    // 2. Equivalence with the recording it was made from.
    std::map<std::uint32_t, std::vector<std::uint8_t>> againstOutputs; // GetTexture2D outputs and readbacks of the recording (scan sources)
    if (!options.againstPath.empty()) {
      report->againstChecked = true;
      Reader a;
      Reader b;
      std::string error;
      if (!a.Open(options.againstPath, &error) || !b.Open(tracePath, &error)) {
        report->error = error;
        return false;
      }
      b.SetResolver(&resolver);
      Record ra;
      Record rb;
      std::vector<FlatField> fa;
      std::vector<FlatField> fb;
      bool same = true;
      auto problem = [&](const std::string& text) {
        same = false;
        if (report->equivalenceProblems.size() < 20) {
          report->equivalenceProblems.push_back(text);
        }
      };
      while (true) {
        const bool hasA = a.Next(&ra, true);
        const bool hasB = b.Next(&rb, true);
        if (!hasA || !hasB) {
          if (hasA != hasB) {
            problem("the traces have different record counts");
          }
          break;
        }
        ++report->recordsCompared;
        if (ra.op == static_cast<std::uint16_t>(Op::Blob)) {
          const std::uint32_t id = Load32(ra.payload.data());
          if (!IsPayloadDefinition(rb.op) || Load32(rb.payload.data()) != id) {
            problem("record " + std::to_string(ra.index) + ": blob #" + std::to_string(id) + " has no payload definition of the same number");
            continue;
          }
          ++report->payloadsCompared;
          std::vector<std::uint8_t> original;
          if (!a.ReadBlob(id, &original)) {
            problem("blob #" + std::to_string(id) + " of the recording cannot be read");
            continue;
          }
          const PayloadEntry* const entry = b.GetPayload(id);
          const BlobKey key = HashBlob(original.data(), original.size());
          if (!(entry->key == key)) {
            problem("payload #" + std::to_string(id) + ": hashes differ from the recording's bytes");
            continue;
          }
          switch (entry->kind) {
            case PayloadKind::Embedded:
            case PayloadKind::Reference: {
              std::vector<std::uint8_t> bytes;
              PayloadStatus status;
              if (!b.ReadPayload(id, &bytes, &status) || bytes != original) {
                problem("payload #" + std::to_string(id) + " (" + PayloadKindName(entry->kind) + "): bytes differ: " + status.error);
              }
              break;
            }
            case PayloadKind::Digest:
              againstOutputs[id] = original;
              break;
            case PayloadKind::Composed: {
              std::map<std::uint32_t, std::vector<std::uint8_t>> parts;
              std::vector<const std::vector<std::uint8_t>*> sources;
              for (const PayloadCopy& copy : entry->copies) {
                auto found = parts.find(copy.source);
                if (found == parts.end()) {
                  std::vector<std::uint8_t> part;
                  (void)a.ReadBlob(copy.source, &part); // the recording's own bytes for each part
                  found = parts.emplace(copy.source, std::move(part)).first;
                }
                sources.push_back(&found->second);
              }
              std::vector<std::uint8_t> base;
              if (entry->base != 0) {
                (void)a.ReadBlob(entry->base, &base);
              }
              std::vector<std::uint8_t> built;
              std::string buildError;
              if (!BuildComposition(entry->key.size, entry->base != 0 ? &base : nullptr, entry->copies, sources, entry->literals, &built, &buildError) ||
                  built != original) {
                problem("payload #" + std::to_string(id) + " (composed): does not rebuild the recording's bytes " + buildError);
              }
              break;
            }
          }
          continue;
        }
        if (ra.op != rb.op || ra.flags != rb.flags) {
          problem("record " + std::to_string(ra.index) + ": op or flags differ");
          continue;
        }
        if (ra.payload == rb.payload) {
          continue;
        }
        if (ra.op == static_cast<std::uint16_t>(Op::End) && ra.payload.size() == 28 && rb.payload.size() == 28) {
          // End: records, blobs, blob bytes, presents, objects. A version 2 file counts its embedded
          // payloads only; the record count, presents and objects must stay.
          if (std::memcmp(ra.payload.data(), rb.payload.data(), 8) == 0 && std::memcmp(ra.payload.data() + 20, rb.payload.data() + 20, 8) == 0) {
            continue;
          }
          problem("End: the record, present or object totals differ");
          continue;
        }
        // Allowed: a path string of the recording machine replaced by the VFS path it resolves to.
        if (!FlattenRecord(ra, &fa) || !FlattenRecord(rb, &fb) || fa.size() != fb.size()) {
          problem("record " + std::to_string(ra.index) + " (" + ra.desc->name + "): payload differs");
          continue;
        }
        bool onlyPaths = true;
        for (std::size_t field = 0; field < fa.size() && onlyPaths; ++field) {
          if (fa[field].type == FieldType::Str) {
            onlyPaths = fa[field].text == fb[field].text || VfsPathFromDiskHint(fa[field].text) == fb[field].text;
          } else {
            onlyPaths = fa[field].length == fb[field].length &&
                        std::memcmp(ra.payload.data() + fa[field].offset, rb.payload.data() + fb[field].offset, fa[field].length) == 0;
          }
        }
        if (onlyPaths) {
          ++report->recordsDifferingOnlyInPaths;
        } else {
          problem("record " + std::to_string(ra.index) + " (" + ra.desc->name + "): payload differs beyond path strings");
        }
      }
      if (a.Failed() || b.Failed()) {
        problem("reader: " + a.Error() + b.Error());
      }
      report->equivalent = same;
      report->lines.push_back(std::string("equivalent to ") + options.againstPath + ": " + (same ? "yes" : "NO") + " (" +
                              std::to_string(report->recordsCompared) + " records, " + std::to_string(report->payloadsCompared) + " payloads; " +
                              std::to_string(report->recordsDifferingOnlyInPaths) + " records differ only in path strings)");
    }

    // 3. Game content in what the trace embeds.
    if (options.scanGameContent) {
      WindowIndex windows;
      std::vector<PayloadRefInfo> refs;
      std::string error;
      if (!ScanPayloadRefs(tracePath, &refs, &error)) {
        report->error = error;
        return false;
      }
      std::set<std::string> added;
      for (const PayloadRefInfo& ref : refs) {
        std::vector<std::uint8_t> bytes;
        std::string readError;
        if (added.insert(ref.path).second && index.Read(ref.path, &bytes, &readError)) {
          windows.Add(ref.path, std::move(bytes));
        }
      }
      for (auto& [id, bytes] : againstOutputs) {
        windows.Add("recorded payload #" + std::to_string(id) + " (a digest in this trace)", std::move(bytes));
      }
      againstOutputs.clear();
      report->gameChunksIndexed = windows.Count();

      Reader reader;
      if (!reader.Open(tracePath, &error)) {
        report->error = error;
        return false;
      }
      Record record;
      std::map<std::uint64_t, std::vector<std::string>> candidates;
      std::map<std::string, BlobKey> fileKeys;
      while (reader.Next(&record, true)) {
        if (record.op == static_cast<std::uint16_t>(Op::Blob)) {
          const std::uint32_t id = Load32(record.payload.data());
          const std::uint8_t* const data = record.payload.data() + 24;
          const std::size_t size = record.payload.size() - 24;
          ++report->embeddedPayloads;
          report->embeddedBytes += size;
          // A whole game file?
          auto found = candidates.find(size);
          if (found == candidates.end()) {
            found = candidates.emplace(size, index.CandidatesBySize(size)).first;
          }
          const BlobKey key = HashBlob(data, size);
          for (const std::string& path : found->second) {
            auto cached = fileKeys.find(path);
            if (cached == fileKeys.end()) {
              std::vector<std::uint8_t> bytes;
              std::string readError;
              BlobKey fileKey{};
              if (index.Read(path, &bytes, &readError)) {
                fileKey = HashBlob(bytes.data(), bytes.size());
              }
              cached = fileKeys.emplace(path, fileKey).first;
            }
            if (cached->second == key) {
              ++report->wholeFileMatches;
              report->gameContentFindings.push_back("embedded payload #" + std::to_string(id) + " is the game file " + path);
            }
          }
          std::string first;
          const std::uint64_t covered = windows.Scan(data, size, &first);
          if (covered != 0) {
            report->chunkMatches += covered;
            report->gameContentFindings.push_back("embedded payload #" + std::to_string(id) + ": " + SizeText(covered) +
                                                  " equal to game-derived bytes (" + first + ")");
          }
        } else if (record.op == static_cast<std::uint16_t>(Op::PayloadCompose)) {
          const std::uint32_t id = Load32(record.payload.data());
          const PayloadEntry* const entry = reader.GetPayload(id);
          for (const PayloadLiteral& literal : entry->literals) {
            report->literalBytes += literal.bytes.size();
            report->embeddedBytes += literal.bytes.size();
            std::string first;
            const std::uint64_t covered = windows.Scan(literal.bytes.data(), literal.bytes.size(), &first);
            if (covered != 0) {
              report->chunkMatches += covered;
              report->gameContentFindings.push_back("composed payload #" + std::to_string(id) + " literal at " + std::to_string(literal.offset) +
                                                    ": " + SizeText(covered) + " equal to game-derived bytes (" + first + ")");
            }
          }
        }
      }
      report->lines.push_back("game content: " + std::to_string(report->embeddedPayloads) + " embedded payloads and " +
                              SizeText(report->literalBytes) + " of composition literals (" + SizeText(report->embeddedBytes) +
                              ") scanned against " + std::to_string(report->gameChunksIndexed) +
                              " 64-byte windows of the referenced files and the recording's digested payloads, and whole files of the same size: " +
                              std::to_string(report->wholeFileMatches) + " whole-file matches, " + SizeText(report->chunkMatches) +
                              " in matching windows");
    }

    report->ok = report->valid && report->referencesResolved == report->references && report->wholeFileMatches == 0 && report->chunkMatches == 0 &&
                 (!report->againstChecked || report->equivalent);
    return report->ok;
  }

  // ---- SetMetadata, Extract ---------------------------------------------------------------------------------

  bool SetMetadata(
    const std::string& inPath, const std::string& outPath, const Metadata& set, const std::vector<std::string>& remove, std::string* const error
  )
  {
    Reader reader;
    if (!reader.Open(inPath, error)) {
      return false;
    }
    Metadata metadata;
    for (const auto& [key, value] : reader.GetMetadata()) {
      if (std::find(remove.begin(), remove.end(), key) == remove.end()) {
        metadata.emplace_back(key, value);
      }
    }
    for (const auto& [key, value] : set) {
      bool replaced = false;
      for (auto& pair : metadata) {
        if (pair.first == key) {
          pair.second = value;
          replaced = true;
        }
      }
      if (!replaced) {
        metadata.emplace_back(key, value);
      }
    }
    // The records follow the header unchanged: copy the file from the first record on.
    std::FILE* const in = std::fopen(inPath.c_str(), "rb");
    if (in == nullptr) {
      *error = "cannot open " + inPath;
      return false;
    }
    std::uint8_t head[16];
    std::uint64_t offset = 16;
    bool ok = std::fread(head, 1, 16, in) == 16;
    const std::uint32_t count = ok ? Load32(head + 12) : 0;
    for (std::uint32_t pair = 0; ok && pair < count * 2u; ++pair) {
      std::uint8_t word[4];
      ok = std::fread(word, 1, 4, in) == 4;
      const std::uint32_t length = ok ? Load32(word) : 0;
      ok = ok && std::fseek(in, static_cast<long>(length), SEEK_CUR) == 0;
      offset += 4u + length;
    }
    std::vector<std::uint8_t> header(kMagic, kMagic + sizeof(kMagic));
    Put32(header, ok ? Load32(head + 8) : 0);
    Put32(header, static_cast<std::uint32_t>(metadata.size()));
    for (const auto& [key, value] : metadata) {
      Put32(header, static_cast<std::uint32_t>(key.size()));
      header.insert(header.end(), key.begin(), key.end());
      Put32(header, static_cast<std::uint32_t>(value.size()));
      header.insert(header.end(), value.begin(), value.end());
    }
    std::FILE* const out = ok ? std::fopen(outPath.c_str(), "wb") : nullptr;
    if (out == nullptr) {
      std::fclose(in);
      *error = ok ? "cannot create " + outPath : "bad header in " + inPath;
      return false;
    }
    ok = std::fwrite(header.data(), 1, header.size(), out) == header.size();
    std::vector<std::uint8_t> chunk(1u << 20);
    std::size_t got = 0;
    while (ok && (got = std::fread(chunk.data(), 1, chunk.size(), in)) != 0) {
      ok = std::fwrite(chunk.data(), 1, got, out) == got;
    }
    std::fclose(in);
    ok = (std::fclose(out) == 0) && ok;
    if (!ok) {
      *error = "write error on " + outPath;
    }
    (void)offset;
    return ok;
  }

  bool Extract(const std::string& tracePath, GameFileIndex& index, const std::string& outDir, std::vector<std::string>* const written, std::string* const error)
  {
    std::vector<PayloadRefInfo> refs;
    if (!ScanPayloadRefs(tracePath, &refs, error)) {
      return false;
    }
    for (const PayloadRefInfo& ref : refs) {
      std::vector<std::uint8_t> bytes;
      std::string readError;
      if (!index.Read(ref.path, &bytes, &readError)) {
        *error = ref.path + ": " + readError;
        return false;
      }
      if (!(HashBlob(bytes.data(), bytes.size()) == ref.key)) {
        *error = ref.path + ": differs from the file the trace refers to";
        return false;
      }
      const std::string path = DirectoryResolver::MapPath(outDir, ref.path);
      if (path.empty()) {
        *error = ref.path + ": not a path the tree can hold";
        return false;
      }
      std::error_code code;
      std::filesystem::create_directories(std::filesystem::path(path).parent_path(), code);
      if (!WriteFile(path, bytes)) {
        *error = "cannot write " + path;
        return false;
      }
      if (written != nullptr) {
        written->push_back(path);
      }
    }
    return true;
  }
} // namespace galtrace::refs
