#include "GalTraceIO.h"

#include <chrono>
#include <cinttypes>
#include <cstring>

namespace galtrace
{
  namespace
  {
    bool Seek64(std::FILE* const file, const std::uint64_t offset)
    {
#if defined(_WIN32)
      return _fseeki64(file, static_cast<__int64>(offset), SEEK_SET) == 0;
#else
      return fseeko(file, static_cast<off_t>(offset), SEEK_SET) == 0;
#endif
    }

    std::uint32_t LoadU32(const std::uint8_t* const p)
    {
      return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
             (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
    }

    std::uint64_t LoadU64(const std::uint8_t* const p)
    {
      return static_cast<std::uint64_t>(LoadU32(p)) | (static_cast<std::uint64_t>(LoadU32(p + 4)) << 32);
    }

    float BitsToFloat(const std::uint32_t bits)
    {
      float value;
      std::memcpy(&value, &bits, sizeof(value));
      return value;
    }

    std::uint32_t FloatToBits(const float value)
    {
      std::uint32_t bits;
      std::memcpy(&bits, &value, sizeof(bits));
      return bits;
    }

    void AppendU32(std::vector<std::uint8_t>& out, const std::uint32_t value)
    {
      out.push_back(static_cast<std::uint8_t>(value));
      out.push_back(static_cast<std::uint8_t>(value >> 8));
      out.push_back(static_cast<std::uint8_t>(value >> 16));
      out.push_back(static_cast<std::uint8_t>(value >> 24));
    }

    const char* FieldTypeName(const FieldType type)
    {
      switch (type) {
        case FieldType::U8: return "u8";
        case FieldType::Bool: return "bool";
        case FieldType::U32: return "u32";
        case FieldType::I32: return "i32";
        case FieldType::U64: return "u64";
        case FieldType::F32: return "f32";
        case FieldType::Str: return "str";
        case FieldType::Bytes: return "bytes";
        case FieldType::Blob: return "blob";
        case FieldType::IdRef: return "idref";
        case FieldType::IdDef: return "iddef";
        case FieldType::Rect: return "rect";
        case FieldType::OptRect: return "optrect";
        case FieldType::Matrix: return "matrix";
        case FieldType::OptMatrix: return "optmatrix";
        case FieldType::Vec4: return "vec4";
        case FieldType::F32Array: return "f32[]";
        case FieldType::U32Array: return "u32[]";
        case FieldType::Struct: return "struct";
        case FieldType::List: return "list";
      }
      return "?";
    }

    std::string OpName(const std::uint16_t op)
    {
      const OpDesc* const desc = FindOp(op);
      if (desc != nullptr) {
        return desc->name;
      }
      char text[16];
      std::snprintf(text, sizeof(text), "op%04x", op);
      return text;
    }
  } // namespace

  // ---- RecordBuilder ----------------------------------------------------------------------------

  void RecordBuilder::U32(const std::uint32_t value)
  {
    AppendU32(bytes_, value);
  }

  void RecordBuilder::U64(const std::uint64_t value)
  {
    AppendU32(bytes_, static_cast<std::uint32_t>(value));
    AppendU32(bytes_, static_cast<std::uint32_t>(value >> 32));
  }

  void RecordBuilder::F32(const float value)
  {
    AppendU32(bytes_, FloatToBits(value));
  }

  void RecordBuilder::Str(const char* const text, const std::size_t length)
  {
    U32(static_cast<std::uint32_t>(length));
    if (length != 0) {
      bytes_.insert(bytes_.end(), reinterpret_cast<const std::uint8_t*>(text), reinterpret_cast<const std::uint8_t*>(text) + length);
    }
  }

  void RecordBuilder::Bytes(const void* const data, const std::size_t size)
  {
    U32(static_cast<std::uint32_t>(size));
    if (size != 0) {
      const auto* bytes = static_cast<const std::uint8_t*>(data);
      bytes_.insert(bytes_.end(), bytes, bytes + size);
    }
  }

  void RecordBuilder::RectValue(const Rect& rect)
  {
    I32(rect.left);
    I32(rect.top);
    I32(rect.right);
    I32(rect.bottom);
  }

  void RecordBuilder::OptRect(const Rect* const rect)
  {
    U8(rect != nullptr ? 1 : 0);
    if (rect != nullptr) {
      RectValue(*rect);
    }
  }

  void RecordBuilder::Matrix(const float* const sixteen)
  {
    for (int index = 0; index < 16; ++index) {
      F32(sixteen[index]);
    }
  }

  void RecordBuilder::OptMatrix(const float* const sixteenOrNull)
  {
    U8(sixteenOrNull != nullptr ? 1 : 0);
    if (sixteenOrNull != nullptr) {
      Matrix(sixteenOrNull);
    }
  }

  void RecordBuilder::Vec4(const float* const four)
  {
    for (int index = 0; index < 4; ++index) {
      F32(four[index]);
    }
  }

  void RecordBuilder::F32Array(const float* const values, const std::uint32_t count)
  {
    U32(count);
    for (std::uint32_t index = 0; index < count; ++index) {
      F32(values[index]);
    }
  }

  void RecordBuilder::U32Array(const std::uint32_t* const values, const std::uint32_t count)
  {
    U32(count);
    for (std::uint32_t index = 0; index < count; ++index) {
      U32(values[index]);
    }
  }

  // ---- Writer -----------------------------------------------------------------------------------

  Writer::~Writer()
  {
    if (file_ != nullptr) {
      std::fclose(file_);
      file_ = nullptr;
    }
  }

  bool Writer::Open(const std::string& path, const Metadata& metadata, std::string* const error)
  {
    file_ = std::fopen(path.c_str(), "wb");
    if (file_ == nullptr) {
      if (error != nullptr) {
        *error = "cannot create " + path;
      }
      return false;
    }
    (void)std::setvbuf(file_, nullptr, _IOFBF, 4u << 20);
    std::vector<std::uint8_t> header(kMagic, kMagic + sizeof(kMagic));
    AppendU32(header, kFormatVersion);
    AppendU32(header, static_cast<std::uint32_t>(metadata.size()));
    for (const auto& [key, value] : metadata) {
      AppendU32(header, static_cast<std::uint32_t>(key.size()));
      header.insert(header.end(), key.begin(), key.end());
      AppendU32(header, static_cast<std::uint32_t>(value.size()));
      header.insert(header.end(), value.begin(), value.end());
    }
    WriteRaw(header.data(), header.size());
    return true;
  }

  void Writer::WriteRaw(const void* const data, const std::size_t size)
  {
    if (file_ != nullptr && size != 0) {
      const auto start = std::chrono::steady_clock::now();
      (void)std::fwrite(data, 1, size, file_);
      stats_.writeNanoseconds += static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count());
      stats_.fileBytes += size;
    }
  }

  void Writer::WriteRecord(const Op op, const std::uint16_t flags, const std::uint8_t* const payload, const std::size_t size)
  {
    std::uint8_t header[8];
    const auto code = static_cast<std::uint16_t>(op);
    header[0] = static_cast<std::uint8_t>(code);
    header[1] = static_cast<std::uint8_t>(code >> 8);
    header[2] = static_cast<std::uint8_t>(flags);
    header[3] = static_cast<std::uint8_t>(flags >> 8);
    const auto length = static_cast<std::uint32_t>(size);
    header[4] = static_cast<std::uint8_t>(length);
    header[5] = static_cast<std::uint8_t>(length >> 8);
    header[6] = static_cast<std::uint8_t>(length >> 16);
    header[7] = static_cast<std::uint8_t>(length >> 24);
    WriteRaw(header, sizeof(header));
    WriteRaw(payload, size);
    ++stats_.records;
  }

  std::uint32_t Writer::InternBlob(const void* const data, const std::size_t size)
  {
    if (data == nullptr || size == 0 || file_ == nullptr) {
      return 0;
    }
    ++stats_.blobRefs;
    stats_.blobRefBytes += size;
    const auto hashStart = std::chrono::steady_clock::now();
    const BlobKey key = HashBlob(data, size);
    stats_.hashNanoseconds += static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - hashStart).count());
    const auto found = blobs_.find(key);
    if (found != blobs_.end()) {
      return found->second;
    }
    const std::uint32_t id = static_cast<std::uint32_t>(blobs_.size()) + 1;
    blobs_.emplace(key, id);
    ++stats_.blobs;
    stats_.blobBytes += size;

    // The Blob record's payload: u32 id, u64 hashA, u64 hashB, u32 size, the bytes (kBlob's fields).
    std::uint8_t head[24];
    std::vector<std::uint8_t> prefix;
    prefix.reserve(sizeof(head));
    AppendU32(prefix, id);
    AppendU32(prefix, static_cast<std::uint32_t>(key.a));
    AppendU32(prefix, static_cast<std::uint32_t>(key.a >> 32));
    AppendU32(prefix, static_cast<std::uint32_t>(key.b));
    AppendU32(prefix, static_cast<std::uint32_t>(key.b >> 32));
    AppendU32(prefix, static_cast<std::uint32_t>(size));
    std::memcpy(head, prefix.data(), sizeof(head));

    std::uint8_t header[8];
    const auto code = static_cast<std::uint16_t>(Op::Blob);
    const auto length = static_cast<std::uint32_t>(sizeof(head) + size);
    header[0] = static_cast<std::uint8_t>(code);
    header[1] = static_cast<std::uint8_t>(code >> 8);
    header[2] = 0;
    header[3] = 0;
    header[4] = static_cast<std::uint8_t>(length);
    header[5] = static_cast<std::uint8_t>(length >> 8);
    header[6] = static_cast<std::uint8_t>(length >> 16);
    header[7] = static_cast<std::uint8_t>(length >> 24);
    WriteRaw(header, sizeof(header));
    WriteRaw(head, sizeof(head));
    WriteRaw(data, size);
    ++stats_.records;
    return id;
  }

  void Writer::Commit(const RecordBuilder& record)
  {
    if (file_ == nullptr) {
      return;
    }
    WriteRecord(record.GetOp(), record.Flags(), record.Payload().data(), record.Payload().size());
  }

  void Writer::Flush()
  {
    if (file_ != nullptr) {
      (void)std::fflush(file_);
    }
  }

  void Writer::Close(const std::uint32_t presents, const std::uint32_t objects)
  {
    if (file_ == nullptr) {
      return;
    }
    RecordBuilder end(Op::End);
    end.U64(stats_.records); // the records before End
    end.U32(stats_.blobs);
    end.U64(stats_.blobBytes);
    end.U32(presents);
    end.U32(objects);
    Commit(end);
    (void)std::fflush(file_);
    std::fclose(file_);
    file_ = nullptr;
  }

  // ---- Reader -----------------------------------------------------------------------------------

  Reader::~Reader()
  {
    if (file_ != nullptr) {
      std::fclose(file_);
    }
    if (blobFile_ != nullptr) {
      std::fclose(blobFile_);
    }
  }

  bool Reader::Fail(const std::string& message)
  {
    if (error_.empty()) {
      error_ = message;
    }
    return false;
  }

  bool Reader::ReadExact(void* const data, const std::size_t size, bool* const cleanEof)
  {
    if (size == 0) {
      return true;
    }
    const std::size_t got = std::fread(data, 1, size, file_);
    position_ += got;
    if (got == size) {
      return true;
    }
    if (cleanEof != nullptr) {
      *cleanEof = (got == 0);
    }
    return false;
  }

  bool Reader::Open(const std::string& path, std::string* const error)
  {
    file_ = std::fopen(path.c_str(), "rb");
    blobFile_ = std::fopen(path.c_str(), "rb");
    if (file_ == nullptr || blobFile_ == nullptr) {
      Fail("cannot open " + path);
      if (error != nullptr) {
        *error = error_;
      }
      return false;
    }
    (void)std::setvbuf(file_, nullptr, _IOFBF, 1u << 20);
    char magic[8];
    std::uint8_t word[4];
    bool ok = ReadExact(magic, sizeof(magic), nullptr) && std::memcmp(magic, kMagic, sizeof(kMagic)) == 0;
    if (!ok) {
      Fail(path + " is not a galtrace file");
    }
    if (ok && (ok = ReadExact(word, 4, nullptr))) {
      version_ = LoadU32(word);
      if (version_ != kFormatVersion) {
        ok = Fail("unsupported galtrace version " + std::to_string(version_));
      }
    }
    std::uint32_t count = 0;
    if (ok && (ok = ReadExact(word, 4, nullptr))) {
      count = LoadU32(word);
    }
    for (std::uint32_t index = 0; ok && index < count; ++index) {
      std::string parts[2];
      for (std::string& part : parts) {
        if (!(ok = ReadExact(word, 4, nullptr))) {
          break;
        }
        const std::uint32_t length = LoadU32(word);
        if (length > (16u << 20)) {
          ok = Fail("metadata string too long");
          break;
        }
        part.resize(length);
        if (length != 0 && !(ok = ReadExact(part.data(), length, nullptr))) {
          break;
        }
      }
      if (ok) {
        metadata_.emplace_back(parts[0], parts[1]);
      }
    }
    if (!ok) {
      Fail("truncated header");
      if (error != nullptr) {
        *error = error_;
      }
      return false;
    }
    return true;
  }

  std::string Reader::Meta(const std::string& key) const
  {
    for (const auto& [name, value] : metadata_) {
      if (name == key) {
        return value;
      }
    }
    return {};
  }

  bool Reader::Next(Record* const record, const bool returnBlobs)
  {
    while (true) {
      if (file_ == nullptr || !error_.empty() || sawEnd_) {
        return false;
      }
      std::uint8_t header[8];
      const std::uint64_t offset = position_;
      bool cleanEof = false;
      if (!ReadExact(header, sizeof(header), &cleanEof)) {
        if (!cleanEof) {
          Fail("truncated record header at offset " + std::to_string(offset));
        }
        return false; // the end of the file (SawEnd() says whether End closed it)
      }
      record->op = static_cast<std::uint16_t>(header[0] | (header[1] << 8));
      record->flags = static_cast<std::uint16_t>(header[2] | (header[3] << 8));
      const std::uint32_t length = LoadU32(header + 4);
      record->index = nextIndex_++;
      record->offset = offset;
      record->desc = FindOp(record->op);
      if (record->desc == nullptr) {
        return Fail("unknown op " + std::to_string(record->op) + " at offset " + std::to_string(offset));
      }
      if (record->op == static_cast<std::uint16_t>(Op::Blob) && !returnBlobs) {
        // Index the blob and skip its bytes: id, hashA, hashB, size, data.
        std::uint8_t head[24];
        if (length < sizeof(head) || !ReadExact(head, sizeof(head), nullptr)) {
          return Fail("truncated blob record at offset " + std::to_string(offset));
        }
        const std::uint32_t id = LoadU32(head);
        BlobEntry entry{};
        entry.key.a = LoadU64(head + 4);
        entry.key.b = LoadU64(head + 12);
        entry.key.size = LoadU32(head + 20);
        entry.dataOffset = position_;
        if (id != blobs_.size() + 1) {
          return Fail("blob " + std::to_string(id) + " out of order at offset " + std::to_string(offset));
        }
        if (entry.key.size != length - sizeof(head)) {
          return Fail("blob " + std::to_string(id) + " size does not match its record");
        }
        blobs_.push_back(entry);
        if (!Seek64(file_, position_ + entry.key.size)) {
          return Fail("cannot skip blob " + std::to_string(id));
        }
        position_ += entry.key.size;
        // The file may end inside the blob; the read of the next header notices.
        continue;
      }
      record->payload.resize(length);
      if (!ReadExact(record->payload.data(), length, nullptr)) {
        return Fail("truncated record " + OpName(record->op) + " at offset " + std::to_string(offset));
      }
      if (record->op == static_cast<std::uint16_t>(Op::Blob)) {
        // Returned to the caller: index it as well.
        if (length >= 24) {
          const std::uint32_t id = LoadU32(record->payload.data());
          BlobEntry entry{};
          entry.key.a = LoadU64(record->payload.data() + 4);
          entry.key.b = LoadU64(record->payload.data() + 12);
          entry.key.size = LoadU32(record->payload.data() + 20);
          entry.dataOffset = offset + 8 + 24;
          if (id != blobs_.size() + 1 || entry.key.size != length - 24) {
            return Fail("bad blob record at offset " + std::to_string(offset));
          }
          blobs_.push_back(entry);
        } else {
          return Fail("short blob record at offset " + std::to_string(offset));
        }
      }
      if (record->op == static_cast<std::uint16_t>(Op::End)) {
        sawEnd_ = true;
      }
      return true;
    }
  }

  bool Reader::ReadBlob(const std::uint32_t id, std::vector<std::uint8_t>* const out)
  {
    out->clear();
    if (id == 0) {
      return true;
    }
    if (!HasBlob(id)) {
      return false;
    }
    BlobEntry& entry = blobs_[id - 1];
    out->resize(entry.key.size);
    if (!Seek64(blobFile_, entry.dataOffset) ||
        (entry.key.size != 0 && std::fread(out->data(), 1, entry.key.size, blobFile_) != entry.key.size)) {
      out->clear();
      return false;
    }
    if (!entry.verified) {
      const BlobKey key = HashBlob(out->data(), out->size());
      if (!(key == entry.key)) {
        out->clear();
        return false;
      }
      entry.verified = true;
    }
    return true;
  }

  // ---- Cursor -----------------------------------------------------------------------------------

  Cursor::Cursor(const Record& record) : record_(record)
  {
    if (record.desc == nullptr) {
      error_ = "unknown op";
      return;
    }
    stack_.push_back(Frame{record.desc->fields, record.desc->fieldCount, 0, 1});
  }

  void Cursor::SetError(const std::string& message)
  {
    if (error_.empty()) {
      error_ = std::string(record_.desc != nullptr ? record_.desc->name : "?") + ": " + message;
    }
  }

  const FieldDesc* Cursor::Expect(const FieldType type, const FieldType alternative)
  {
    if (!error_.empty()) {
      return nullptr;
    }
    while (!stack_.empty()) {
      Frame& frame = stack_.back();
      if (frame.index < frame.count) {
        const FieldDesc& field = frame.fields[frame.index];
        if (field.type == FieldType::Struct) {
          ++frame.index;
          stack_.push_back(Frame{field.sub, field.subCount, 0, 1});
          continue;
        }
        if (field.type != type && field.type != alternative) {
          SetError(std::string("read ") + FieldTypeName(type) + " where the schema has " + FieldTypeName(field.type) + " " + field.name);
          return nullptr;
        }
        ++frame.index;
        return &field;
      }
      if (frame.repeats > 1) {
        --frame.repeats;
        frame.index = 0;
        continue;
      }
      stack_.pop_back();
    }
    SetError(std::string("read ") + FieldTypeName(type) + " past the last field");
    return nullptr;
  }

  bool Cursor::Take(void* const out, const std::size_t size)
  {
    if (!error_.empty()) {
      std::memset(out, 0, size);
      return false;
    }
    if (record_.payload.size() - at_ < size) {
      SetError("payload too short");
      std::memset(out, 0, size);
      return false;
    }
    std::memcpy(out, record_.payload.data() + at_, size);
    at_ += size;
    return true;
  }

  std::uint8_t Cursor::U8()
  {
    std::uint8_t value = 0;
    if (Expect(FieldType::U8)) {
      (void)Take(&value, 1);
    }
    return value;
  }

  bool Cursor::Bool()
  {
    std::uint8_t value = 0;
    if (Expect(FieldType::Bool)) {
      (void)Take(&value, 1);
    }
    return value != 0;
  }

#define GALTRACE_CURSOR_WORD(Name, Type, FieldKind)                                                     \
  Type Cursor::Name()                                                                                   \
  {                                                                                                     \
    std::uint8_t bytes[4] = {};                                                                         \
    if (Expect(FieldKind)) {                                                                            \
      (void)Take(bytes, 4);                                                                             \
    }                                                                                                   \
    return static_cast<Type>(LoadU32(bytes));                                                           \
  }

  GALTRACE_CURSOR_WORD(U32, std::uint32_t, FieldType::U32)
  GALTRACE_CURSOR_WORD(I32, std::int32_t, FieldType::I32)
  GALTRACE_CURSOR_WORD(Blob, std::uint32_t, FieldType::Blob)
#undef GALTRACE_CURSOR_WORD

  std::uint32_t Cursor::Count()
  {
    std::uint8_t bytes[4] = {};
    const FieldDesc* const field = Expect(FieldType::List);
    if (field == nullptr || !Take(bytes, 4)) {
      return 0;
    }
    const std::uint32_t count = LoadU32(bytes);
    if (count != 0) {
      // The elements' fields follow `count` times.
      stack_.push_back(Frame{field->sub, field->subCount, 0, count});
    }
    return count;
  }

  std::uint32_t Cursor::Id()
  {
    std::uint8_t bytes[4] = {};
    // IdRef or IdDef, whichever the schema has next.
    if (Expect(FieldType::IdRef, FieldType::IdDef)) {
      (void)Take(bytes, 4);
    }
    return LoadU32(bytes);
  }

  std::uint64_t Cursor::U64()
  {
    std::uint8_t bytes[8] = {};
    if (Expect(FieldType::U64)) {
      (void)Take(bytes, 8);
    }
    return LoadU64(bytes);
  }

  float Cursor::F32()
  {
    std::uint8_t bytes[4] = {};
    if (Expect(FieldType::F32)) {
      (void)Take(bytes, 4);
    }
    return BitsToFloat(LoadU32(bytes));
  }

  std::string Cursor::Str()
  {
    std::string value;
    std::uint8_t bytes[4] = {};
    if (Expect(FieldType::Str) && Take(bytes, 4)) {
      const std::uint32_t length = LoadU32(bytes);
      if (record_.payload.size() - at_ < length) {
        SetError("string longer than the payload");
        return {};
      }
      value.assign(reinterpret_cast<const char*>(record_.payload.data() + at_), length);
      at_ += length;
    }
    return value;
  }

  std::vector<std::uint8_t> Cursor::Bytes()
  {
    std::vector<std::uint8_t> value;
    std::uint8_t bytes[4] = {};
    if (Expect(FieldType::Bytes) && Take(bytes, 4)) {
      const std::uint32_t length = LoadU32(bytes);
      if (record_.payload.size() - at_ < length) {
        SetError("bytes longer than the payload");
        return {};
      }
      value.assign(record_.payload.data() + at_, record_.payload.data() + at_ + length);
      at_ += length;
    }
    return value;
  }

  Rect Cursor::RectValue()
  {
    Rect rect{};
    std::uint8_t bytes[16] = {};
    if (Expect(FieldType::Rect) && Take(bytes, 16)) {
      rect.left = static_cast<std::int32_t>(LoadU32(bytes));
      rect.top = static_cast<std::int32_t>(LoadU32(bytes + 4));
      rect.right = static_cast<std::int32_t>(LoadU32(bytes + 8));
      rect.bottom = static_cast<std::int32_t>(LoadU32(bytes + 12));
    }
    return rect;
  }

  bool Cursor::OptRect(Rect* const rect)
  {
    *rect = Rect{};
    std::uint8_t present = 0;
    if (!Expect(FieldType::OptRect) || !Take(&present, 1) || present == 0) {
      return false;
    }
    std::uint8_t bytes[16] = {};
    if (!Take(bytes, 16)) {
      return false;
    }
    rect->left = static_cast<std::int32_t>(LoadU32(bytes));
    rect->top = static_cast<std::int32_t>(LoadU32(bytes + 4));
    rect->right = static_cast<std::int32_t>(LoadU32(bytes + 8));
    rect->bottom = static_cast<std::int32_t>(LoadU32(bytes + 12));
    return true;
  }

  void Cursor::Matrix(float* const sixteen)
  {
    std::uint8_t bytes[64] = {};
    if (Expect(FieldType::Matrix)) {
      (void)Take(bytes, 64);
    }
    for (int index = 0; index < 16; ++index) {
      sixteen[index] = BitsToFloat(LoadU32(bytes + 4 * index));
    }
  }

  bool Cursor::OptMatrix(float* const sixteen)
  {
    std::uint8_t present = 0;
    std::uint8_t bytes[64] = {};
    bool has = false;
    if (Expect(FieldType::OptMatrix) && Take(&present, 1) && present != 0) {
      has = Take(bytes, 64);
    }
    for (int index = 0; index < 16; ++index) {
      sixteen[index] = BitsToFloat(LoadU32(bytes + 4 * index));
    }
    return has;
  }

  void Cursor::Vec4(float* const four)
  {
    std::uint8_t bytes[16] = {};
    if (Expect(FieldType::Vec4)) {
      (void)Take(bytes, 16);
    }
    for (int index = 0; index < 4; ++index) {
      four[index] = BitsToFloat(LoadU32(bytes + 4 * index));
    }
  }

  std::vector<float> Cursor::F32Array()
  {
    std::vector<float> values;
    std::uint8_t bytes[4] = {};
    if (Expect(FieldType::F32Array) && Take(bytes, 4)) {
      const std::uint32_t count = LoadU32(bytes);
      if ((record_.payload.size() - at_) / 4 < count) {
        SetError("array longer than the payload");
        return {};
      }
      values.resize(count);
      for (std::uint32_t index = 0; index < count; ++index) {
        values[index] = BitsToFloat(LoadU32(record_.payload.data() + at_));
        at_ += 4;
      }
    }
    return values;
  }

  std::vector<std::uint32_t> Cursor::U32Array()
  {
    std::vector<std::uint32_t> values;
    std::uint8_t bytes[4] = {};
    if (Expect(FieldType::U32Array) && Take(bytes, 4)) {
      const std::uint32_t count = LoadU32(bytes);
      if ((record_.payload.size() - at_) / 4 < count) {
        SetError("array longer than the payload");
        return {};
      }
      values.resize(count);
      for (std::uint32_t index = 0; index < count; ++index) {
        values[index] = LoadU32(record_.payload.data() + at_);
        at_ += 4;
      }
    }
    return values;
  }

  bool Cursor::Finished() const
  {
    if (!error_.empty() || at_ != record_.payload.size()) {
      return false;
    }
    // Every remaining frame must be complete (only empty structs or finished lists may remain).
    for (const Frame& frame : stack_) {
      for (std::uint8_t index = frame.index; index < frame.count; ++index) {
        if (frame.fields[index].type != FieldType::Struct || frame.fields[index].subCount != 0) {
          return false;
        }
      }
      if (frame.repeats > 1 && frame.count != 0) {
        return false;
      }
    }
    return true;
  }

  // ---- the generic decoder ------------------------------------------------------------------------

  namespace
  {
    struct TextWalker
    {
      explicit TextWalker(const std::vector<std::uint8_t>& bytes) : payload(bytes) {}

      const std::vector<std::uint8_t>& payload;
      std::size_t at = 0;
      std::string out;
      std::string error;

      bool Need(const std::size_t size)
      {
        if (payload.size() - at < size) {
          error = "payload too short";
          return false;
        }
        return true;
      }

      void AppendFloat(const std::uint32_t bits)
      {
        char text[32];
        std::snprintf(text, sizeof(text), "%.9g", static_cast<double>(BitsToFloat(bits)));
        out += text;
      }

      void AppendString(const std::uint8_t* const data, const std::uint32_t length)
      {
        out += '"';
        const std::uint32_t shown = length > 160 ? 160 : length;
        for (std::uint32_t index = 0; index < shown; ++index) {
          const char c = static_cast<char>(data[index]);
          if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
          } else if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) >= 0x7F) {
            char escaped[8];
            std::snprintf(escaped, sizeof(escaped), "\\x%02x", static_cast<unsigned char>(c));
            out += escaped;
          } else {
            out += c;
          }
        }
        if (shown < length) {
          out += "...(" + std::to_string(length) + ")";
        }
        out += '"';
      }

      bool Fields(const FieldDesc* const fields, const std::uint8_t count)
      {
        for (std::uint8_t index = 0; index < count && error.empty(); ++index) {
          const FieldDesc& field = fields[index];
          out += ' ';
          out += field.name;
          out += '=';
          switch (field.type) {
            case FieldType::U8:
            case FieldType::Bool:
              if (Need(1)) {
                out += std::to_string(payload[at]);
                at += 1;
              }
              break;
            case FieldType::U32:
              if (Need(4)) {
                out += std::to_string(LoadU32(payload.data() + at));
                at += 4;
              }
              break;
            case FieldType::I32:
              if (Need(4)) {
                out += std::to_string(static_cast<std::int32_t>(LoadU32(payload.data() + at)));
                at += 4;
              }
              break;
            case FieldType::U64:
              if (Need(8)) {
                out += std::to_string(LoadU64(payload.data() + at));
                at += 8;
              }
              break;
            case FieldType::F32:
              if (Need(4)) {
                AppendFloat(LoadU32(payload.data() + at));
                at += 4;
              }
              break;
            case FieldType::Str:
            case FieldType::Bytes:
              if (Need(4)) {
                const std::uint32_t length = LoadU32(payload.data() + at);
                at += 4;
                if (Need(length)) {
                  if (field.type == FieldType::Str) {
                    AppendString(payload.data() + at, length);
                  } else {
                    out += "bytes[" + std::to_string(length) + "]";
                  }
                  at += length;
                }
              }
              break;
            case FieldType::Blob:
              if (Need(4)) {
                const std::uint32_t id = LoadU32(payload.data() + at);
                out += id == 0 ? std::string("-") : "#" + std::to_string(id);
                at += 4;
              }
              break;
            case FieldType::IdRef:
            case FieldType::IdDef:
              if (Need(4)) {
                const std::uint32_t id = LoadU32(payload.data() + at);
                out += id == 0 ? std::string("null") : "@" + std::to_string(id);
                at += 4;
              }
              break;
            case FieldType::Rect:
            case FieldType::OptRect: {
              bool present = true;
              if (field.type == FieldType::OptRect) {
                present = Need(1) && payload[at] != 0;
                at += error.empty() ? 1 : 0;
              }
              if (!present) {
                out += "null";
              } else if (Need(16)) {
                out += "(" + std::to_string(static_cast<std::int32_t>(LoadU32(payload.data() + at))) + "," +
                       std::to_string(static_cast<std::int32_t>(LoadU32(payload.data() + at + 4))) + "," +
                       std::to_string(static_cast<std::int32_t>(LoadU32(payload.data() + at + 8))) + "," +
                       std::to_string(static_cast<std::int32_t>(LoadU32(payload.data() + at + 12))) + ")";
                at += 16;
              }
              break;
            }
            case FieldType::Matrix:
            case FieldType::OptMatrix:
            case FieldType::Vec4: {
              bool present = true;
              if (field.type == FieldType::OptMatrix) {
                present = Need(1) && payload[at] != 0;
                at += error.empty() ? 1 : 0;
              }
              const std::uint32_t floats = field.type == FieldType::Vec4 ? 4 : 16;
              if (!present) {
                out += "null";
              } else if (Need(4 * floats)) {
                out += '[';
                for (std::uint32_t value = 0; value < floats; ++value) {
                  if (value != 0) {
                    out += ',';
                  }
                  AppendFloat(LoadU32(payload.data() + at));
                  at += 4;
                }
                out += ']';
              }
              break;
            }
            case FieldType::F32Array:
            case FieldType::U32Array:
              if (Need(4)) {
                const std::uint32_t countValues = LoadU32(payload.data() + at);
                at += 4;
                if ((payload.size() - at) / 4 < countValues) {
                  error = "array longer than the payload";
                  break;
                }
                out += '[';
                for (std::uint32_t value = 0; value < countValues; ++value) {
                  if (value != 0) {
                    out += ',';
                  }
                  if (value == 16 && countValues > 17) {
                    out += "...(" + std::to_string(countValues) + ")";
                    at += 4 * (countValues - value);
                    break;
                  }
                  if (field.type == FieldType::F32Array) {
                    AppendFloat(LoadU32(payload.data() + at));
                  } else {
                    out += std::to_string(LoadU32(payload.data() + at));
                  }
                  at += 4;
                }
                out += ']';
              }
              break;
            case FieldType::Struct:
              out += '{';
              (void)Fields(field.sub, field.subCount);
              out += " }";
              break;
            case FieldType::List:
              if (Need(4)) {
                const std::uint32_t elements = LoadU32(payload.data() + at);
                at += 4;
                out += '[';
                for (std::uint32_t element = 0; element < elements && error.empty(); ++element) {
                  out += element == 0 ? "{" : ", {";
                  (void)Fields(field.sub, field.subCount);
                  out += " }";
                }
                out += ']';
              }
              break;
          }
        }
        return error.empty();
      }
    };
  } // namespace

  std::string DecodeRecord(const Record& record, std::string* const error)
  {
    std::string head = OpName(record.op);
    if (record.flags & kFlagOffThread) {
      head += " [off-thread]";
    }
    if (record.flags & kFlagThrew) {
      head += " [threw]";
    }
    if (record.desc == nullptr) {
      if (error != nullptr) {
        *error = "unknown op";
      }
      return head;
    }
    TextWalker walker(record.payload);
    walker.out = head;
    if (record.op == static_cast<std::uint16_t>(Op::Blob)) {
      // Do not print a blob's bytes.
      if (record.payload.size() >= 24) {
        walker.out += " blob=#" + std::to_string(LoadU32(record.payload.data())) + " hashA=" +
                      Hex64(LoadU64(record.payload.data() + 4)) + " hashB=" + Hex64(LoadU64(record.payload.data() + 12)) +
                      " size=" + std::to_string(LoadU32(record.payload.data() + 20));
      }
      return walker.out;
    }
    walker.Fields(record.desc->fields, record.desc->fieldCount);
    if (walker.error.empty() && walker.at != record.payload.size()) {
      walker.error = std::to_string(record.payload.size() - walker.at) + " bytes after the last field";
    }
    if (error != nullptr) {
      *error = walker.error;
    }
    return walker.out;
  }

  // ---- Validator --------------------------------------------------------------------------------

  bool Validator::Fail(const Record& record, const std::string& message)
  {
    if (error_.empty()) {
      error_ = "record " + std::to_string(record.index) + " (" + OpName(record.op) + ", offset " +
               std::to_string(record.offset) + "): " + message;
    }
    return false;
  }

  bool Validator::Walk(
    const FieldDesc* const fields, const std::uint8_t count, const Record& record, std::size_t* const at, Reader& reader
  )
  {
    const std::vector<std::uint8_t>& payload = record.payload;
    auto need = [&](const std::size_t size) {
      return payload.size() - *at >= size;
    };
    for (std::uint8_t index = 0; index < count; ++index) {
      const FieldDesc& field = fields[index];
      switch (field.type) {
        case FieldType::U8:
          if (!need(1)) return Fail(record, "short payload");
          *at += 1;
          break;
        case FieldType::Bool:
          if (!need(1)) return Fail(record, "short payload");
          if (payload[*at] > 1) return Fail(record, std::string("bool ") + field.name + " is " + std::to_string(payload[*at]));
          *at += 1;
          break;
        case FieldType::U32:
        case FieldType::I32:
        case FieldType::F32:
          if (!need(4)) return Fail(record, "short payload");
          *at += 4;
          break;
        case FieldType::U64:
          if (!need(8)) return Fail(record, "short payload");
          *at += 8;
          break;
        case FieldType::Str:
        case FieldType::Bytes: {
          if (!need(4)) return Fail(record, "short payload");
          const std::uint32_t length = LoadU32(payload.data() + *at);
          *at += 4;
          if (!need(length)) return Fail(record, std::string(field.name) + " longer than the payload");
          *at += length;
          break;
        }
        case FieldType::Blob: {
          if (!need(4)) return Fail(record, "short payload");
          const std::uint32_t id = LoadU32(payload.data() + *at);
          *at += 4;
          if (id != 0 && !reader.HasBlob(id)) {
            return Fail(record, std::string(field.name) + " refers to undefined blob #" + std::to_string(id));
          }
          break;
        }
        case FieldType::IdRef: {
          if (!need(4)) return Fail(record, "short payload");
          const std::uint32_t id = LoadU32(payload.data() + *at);
          *at += 4;
          if (id == 0) {
            break;
          }
          const auto found = live_.find(id);
          if (found == live_.end()) {
            return Fail(record, std::string(field.name) + " refers to @" + std::to_string(id) + ", which is not live");
          }
          if (field.objectType != ObjectType::Any && found->second != field.objectType) {
            return Fail(record, std::string(field.name) + " @" + std::to_string(id) + " is a " + ObjectTypeName(found->second) +
                                  ", not a " + ObjectTypeName(field.objectType));
          }
          break;
        }
        case FieldType::IdDef: {
          if (!need(4)) return Fail(record, "short payload");
          const std::uint32_t id = LoadU32(payload.data() + *at);
          *at += 4;
          if (id == 0) {
            break;
          }
          const auto found = live_.find(id);
          if (found != live_.end()) {
            if (found->second != field.objectType) {
              return Fail(record, std::string(field.name) + " redefines @" + std::to_string(id) + " (a " +
                                    ObjectTypeName(found->second) + ") as a " + ObjectTypeName(field.objectType));
            }
          } else {
            if (id <= maxId_) {
              return Fail(record, std::string(field.name) + " defines @" + std::to_string(id) + ", an id already used");
            }
            maxId_ = id;
            live_.emplace(id, field.objectType);
            ++objectsDefined_;
          }
          break;
        }
        case FieldType::Rect:
          if (!need(16)) return Fail(record, "short payload");
          *at += 16;
          break;
        case FieldType::OptRect:
        case FieldType::OptMatrix: {
          if (!need(1)) return Fail(record, "short payload");
          const std::uint8_t present = payload[*at];
          *at += 1;
          if (present > 1) return Fail(record, std::string(field.name) + " presence byte is " + std::to_string(present));
          const std::size_t size = field.type == FieldType::OptRect ? 16 : 64;
          if (present != 0) {
            if (!need(size)) return Fail(record, "short payload");
            *at += size;
          }
          break;
        }
        case FieldType::Matrix:
          if (!need(64)) return Fail(record, "short payload");
          *at += 64;
          break;
        case FieldType::Vec4:
          if (!need(16)) return Fail(record, "short payload");
          *at += 16;
          break;
        case FieldType::F32Array:
        case FieldType::U32Array: {
          if (!need(4)) return Fail(record, "short payload");
          const std::uint32_t values = LoadU32(payload.data() + *at);
          *at += 4;
          if ((payload.size() - *at) / 4 < values) return Fail(record, std::string(field.name) + " longer than the payload");
          *at += 4u * values;
          break;
        }
        case FieldType::Struct:
          if (!Walk(field.sub, field.subCount, record, at, reader)) return false;
          break;
        case FieldType::List: {
          if (!need(4)) return Fail(record, "short payload");
          const std::uint32_t elements = LoadU32(payload.data() + *at);
          *at += 4;
          for (std::uint32_t element = 0; element < elements; ++element) {
            if (!Walk(field.sub, field.subCount, record, at, reader)) return false;
          }
          break;
        }
      }
    }
    return true;
  }

  bool Validator::Check(const Record& record, Reader& reader)
  {
    if (!error_.empty()) {
      return false;
    }
    if (sawEnd_) {
      return Fail(record, "record after End");
    }
    ++records_;
    if (record.desc == nullptr) {
      return Fail(record, "unknown op");
    }
    const std::vector<const OpDesc*>& ops = AllOps();
    if (opCounts_.size() != ops.size()) {
      opCounts_.assign(ops.size(), 0);
    }
    for (std::size_t index = 0; index < ops.size(); ++index) {
      if (ops[index] == record.desc) {
        ++opCounts_[index];
        break;
      }
    }

    if (record.op == static_cast<std::uint16_t>(Op::Blob)) {
      if (record.payload.size() < 24) {
        return Fail(record, "short blob record");
      }
      const std::uint32_t id = LoadU32(record.payload.data());
      if (id != blobsSeen_ + 1) {
        return Fail(record, "blob #" + std::to_string(id) + " out of order");
      }
      ++blobsSeen_;
      const BlobEntry* const entry = reader.GetBlobEntry(id);
      if (entry == nullptr) {
        return Fail(record, "blob #" + std::to_string(id) + " not indexed");
      }
      blobBytes_ += entry->key.size;
      std::vector<std::uint8_t> bytes;
      if (!reader.ReadBlob(id, &bytes)) {
        return Fail(record, "blob #" + std::to_string(id) + " does not match its hashes");
      }
      return true;
    }

    std::size_t at = 0;
    if (!Walk(record.desc->fields, record.desc->fieldCount, record, &at, reader)) {
      return false;
    }
    if (at != record.payload.size()) {
      return Fail(record, std::to_string(record.payload.size() - at) + " bytes after the last field");
    }

    switch (static_cast<Op>(record.op)) {
      case Op::Release:
      case Op::DeviceDestroy: {
        const std::uint32_t id = LoadU32(record.payload.data());
        if (id == 0 || live_.erase(id) == 0) {
          return Fail(record, "releases @" + std::to_string(id) + ", which is not live");
        }
        break;
      }
      case Op::DevPresent:
        ++presents_;
        break;
      case Op::End:
        sawEnd_ = true;
        endRecords_ = LoadU64(record.payload.data());
        endBlobs_ = LoadU32(record.payload.data() + 8);
        endBlobBytes_ = LoadU64(record.payload.data() + 12);
        endPresents_ = LoadU32(record.payload.data() + 20);
        break;
      default:
        break;
    }
    return true;
  }

  bool Validator::Finish(const Reader& reader)
  {
    if (!error_.empty()) {
      return false;
    }
    if (reader.Failed()) {
      error_ = "reader: " + reader.Error();
      return false;
    }
    if (!sawEnd_) {
      error_ = "no End record: the trace is truncated";
      return false;
    }
    if (endRecords_ != records_ - 1) {
      error_ = "End counts " + std::to_string(endRecords_) + " records, the file has " + std::to_string(records_ - 1);
      return false;
    }
    if (endBlobs_ != blobsSeen_ || endBlobBytes_ != blobBytes_) {
      error_ = "End counts " + std::to_string(endBlobs_) + " blobs / " + std::to_string(endBlobBytes_) + " bytes, the file has " +
               std::to_string(blobsSeen_) + " / " + std::to_string(blobBytes_);
      return false;
    }
    if (endPresents_ != presents_) {
      error_ = "End counts " + std::to_string(endPresents_) + " presents, the file has " + std::to_string(presents_);
      return false;
    }
    return true;
  }
} // namespace galtrace
