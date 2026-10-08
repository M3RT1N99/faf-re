#pragma once

// Writing and reading galtrace files (GalTraceFormat.h). Portable C++17.
//
//   Writer          the recorder's side: records are built in a RecordBuilder (one per call, so a
//                   record can be started while another is still open, as a wrapper's destructor
//                   does inside a call), payloads are interned as blobs, Commit appends.
//   Reader          sequential records; Blob records are indexed as they pass and read back on
//                   demand (hash-verified) through a second file handle.
//   Cursor          typed reads of one record's payload, checked against the op's field list.
//   DecodeRecord    the generic decoder: any record as text, from the schema alone.
//   Validator       the stream rules: known ops, payloads that match their schema, blobs defined
//                   before use and equal to their hashes, ids referring to live objects of the right
//                   type, an End record whose totals match.

#include "GalTraceFormat.h"

#include <cstdio>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace galtrace
{
  using Metadata = std::vector<std::pair<std::string, std::string>>;

  struct Rect
  {
    std::int32_t left = 0;
    std::int32_t top = 0;
    std::int32_t right = 0;
    std::int32_t bottom = 0;
  };

  // ---- writing ----------------------------------------------------------------------------------

  class RecordBuilder
  {
  public:
    RecordBuilder(Op op, std::uint16_t flags = 0) : op_(op), flags_(flags) {}

    void U8(std::uint8_t value) { bytes_.push_back(value); }
    void Bool(bool value) { bytes_.push_back(value ? 1 : 0); }
    void U32(std::uint32_t value);
    void I32(std::int32_t value) { U32(static_cast<std::uint32_t>(value)); }
    void U64(std::uint64_t value);
    void F32(float value);
    void Str(const char* text, std::size_t length);
    void Str(const std::string& text) { Str(text.data(), text.size()); }
    void Bytes(const void* data, std::size_t size);
    void Blob(std::uint32_t blob) { U32(blob); }
    void Id(std::uint32_t id) { U32(id); }
    void RectValue(const Rect& rect);
    void OptRect(const Rect* rect);
    void Matrix(const float* sixteen);
    void OptMatrix(const float* sixteenOrNull);
    void Vec4(const float* four);
    void F32Array(const float* values, std::uint32_t count);
    void U32Array(const std::uint32_t* values, std::uint32_t count);
    void Count(std::uint32_t count) { U32(count); } // a List's element count

    void AddFlags(std::uint16_t flags) { flags_ = static_cast<std::uint16_t>(flags_ | flags); }
    [[nodiscard]] Op GetOp() const { return op_; }
    [[nodiscard]] std::uint16_t Flags() const { return flags_; }
    [[nodiscard]] const std::vector<std::uint8_t>& Payload() const { return bytes_; }

  private:
    Op op_;
    std::uint16_t flags_;
    std::vector<std::uint8_t> bytes_;
  };

  struct WriterStats
  {
    std::uint64_t records = 0;
    std::uint32_t blobs = 0;
    std::uint64_t blobBytes = 0;   // unique payload bytes stored
    std::uint64_t blobRefs = 0;    // InternBlob calls with data
    std::uint64_t blobRefBytes = 0; // payload bytes before deduplication
    std::uint64_t fileBytes = 0;
    std::uint64_t hashNanoseconds = 0;  // time spent hashing payloads
    std::uint64_t writeNanoseconds = 0; // time spent in fwrite
  };

  class Writer
  {
  public:
    Writer() = default;
    ~Writer();
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;

    bool Open(const std::string& path, const Metadata& metadata, std::string* error);
    [[nodiscard]] bool IsOpen() const { return file_ != nullptr; }

    /** The blob id of `data` (0 for no data), writing its Blob record the first time it is seen. */
    std::uint32_t InternBlob(const void* data, std::size_t size);

    void Commit(const RecordBuilder& record);
    void Flush();

    /** Writes the End record and closes the file. */
    void Close(std::uint32_t presents, std::uint32_t objects);

    [[nodiscard]] const WriterStats& Stats() const { return stats_; }

  private:
    void WriteRecord(Op op, std::uint16_t flags, const std::uint8_t* payload, std::size_t size);
    void WriteRaw(const void* data, std::size_t size);

    struct KeyHash
    {
      std::size_t operator()(const BlobKey& key) const { return static_cast<std::size_t>(key.a ^ (key.b * 31u)); }
    };

    std::FILE* file_ = nullptr;
    std::unordered_map<BlobKey, std::uint32_t, KeyHash> blobs_;
    WriterStats stats_;
  };

  // ---- reading ----------------------------------------------------------------------------------

  struct Record
  {
    std::uint16_t op = 0;
    std::uint16_t flags = 0;
    std::uint64_t index = 0;  // 0-based position among all records (Blob records included)
    std::uint64_t offset = 0; // file offset of the record header
    const OpDesc* desc = nullptr;
    std::vector<std::uint8_t> payload;
  };

  struct BlobEntry
  {
    std::uint64_t dataOffset = 0;
    BlobKey key{};
    bool verified = false;
  };

  class Reader
  {
  public:
    Reader() = default;
    ~Reader();
    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;

    bool Open(const std::string& path, std::string* error);
    [[nodiscard]] std::uint32_t Version() const { return version_; }
    [[nodiscard]] const Metadata& GetMetadata() const { return metadata_; }
    [[nodiscard]] std::string Meta(const std::string& key) const;

    /**
     * The next record. Blob records are returned too (`returnBlobs`) or only indexed. False at the
     * end of the file (then SawEnd() tells whether an End record closed it) or on an error (Error()).
     */
    bool Next(Record* record, bool returnBlobs = false);

    [[nodiscard]] bool SawEnd() const { return sawEnd_; }
    [[nodiscard]] bool Failed() const { return !error_.empty(); }
    [[nodiscard]] const std::string& Error() const { return error_; }

    [[nodiscard]] bool HasBlob(std::uint32_t id) const { return id != 0 && id <= blobs_.size(); }
    [[nodiscard]] const BlobEntry* GetBlobEntry(std::uint32_t id) const { return HasBlob(id) ? &blobs_[id - 1] : nullptr; }
    [[nodiscard]] std::uint32_t BlobCount() const { return static_cast<std::uint32_t>(blobs_.size()); }

    /** The blob's bytes (empty for id 0); false when it is unknown, unreadable or not equal to its hashes. */
    bool ReadBlob(std::uint32_t id, std::vector<std::uint8_t>* out);

  private:
    bool ReadExact(void* data, std::size_t size, bool* cleanEof);
    bool Fail(const std::string& message);

    std::FILE* file_ = nullptr;
    std::FILE* blobFile_ = nullptr;
    std::uint32_t version_ = 0;
    Metadata metadata_;
    std::uint64_t position_ = 0;
    std::uint64_t nextIndex_ = 0;
    std::vector<BlobEntry> blobs_;
    bool sawEnd_ = false;
    std::string error_;
  };

  /**
   * Typed reads of one record's payload. Each read is checked against the next field of the op's
   * schema (Struct fields are entered transparently; a List is read with Count(), after which its
   * elements' fields follow `count` times). A mismatch or a short payload sets the error and returns
   * zeros from then on.
   */
  class Cursor
  {
  public:
    explicit Cursor(const Record& record);

    std::uint8_t U8();
    bool Bool();
    std::uint32_t U32();
    std::int32_t I32();
    std::uint64_t U64();
    float F32();
    std::string Str();
    std::vector<std::uint8_t> Bytes();
    std::uint32_t Blob();
    std::uint32_t Id(); // IdRef or IdDef
    Rect RectValue();
    bool OptRect(Rect* rect);
    void Matrix(float* sixteen);
    bool OptMatrix(float* sixteen);
    void Vec4(float* four);
    std::vector<float> F32Array();
    std::vector<std::uint32_t> U32Array();
    std::uint32_t Count(); // a List

    [[nodiscard]] bool Ok() const { return error_.empty(); }
    [[nodiscard]] const std::string& Error() const { return error_; }
    /** True when every byte and every schema field was read. */
    [[nodiscard]] bool Finished() const;

  private:
    const FieldDesc* Expect(FieldType type, FieldType alternative = FieldType::Struct);
    bool Take(void* out, std::size_t size);
    void SetError(const std::string& message);

    struct Frame
    {
      const FieldDesc* fields;
      std::uint8_t count;
      std::uint8_t index;
      std::uint32_t repeats; // a List's elements still to go, counting this one
    };

    const Record& record_;
    std::size_t at_ = 0;
    std::vector<Frame> stack_;
    std::string error_;
  };

  /** The record as one line of text: "Name field=value ...", blobs as #id[size], ids as @id. */
  std::string DecodeRecord(const Record& record, std::string* error);

  /** The stream rules (see the top of this header), record by record. */
  class Validator
  {
  public:
    /** Checks one record (Blob records included); false and Error() on the first broken rule. */
    bool Check(const Record& record, Reader& reader);
    /** After the last record: the End record exists and its totals match. */
    bool Finish(const Reader& reader);

    [[nodiscard]] const std::string& Error() const { return error_; }
    [[nodiscard]] std::uint64_t Records() const { return records_; }
    [[nodiscard]] std::uint32_t Presents() const { return presents_; }
    [[nodiscard]] std::uint32_t ObjectsDefined() const { return objectsDefined_; }
    [[nodiscard]] std::uint32_t LiveObjects() const { return static_cast<std::uint32_t>(live_.size()); }
    [[nodiscard]] const std::vector<std::uint64_t>& OpCounts() const { return opCounts_; } // by op index in AllOps()

  private:
    bool Walk(const FieldDesc* fields, std::uint8_t count, const Record& record, std::size_t* at, Reader& reader);
    bool Fail(const Record& record, const std::string& message);

    std::unordered_map<std::uint32_t, ObjectType> live_;
    std::uint32_t maxId_ = 0;
    std::uint32_t objectsDefined_ = 0;
    std::uint64_t records_ = 0;
    std::uint32_t presents_ = 0;
    std::uint32_t blobsSeen_ = 0;
    std::uint64_t blobBytes_ = 0;
    bool sawEnd_ = false;
    std::uint64_t endRecords_ = 0;
    std::uint32_t endBlobs_ = 0;
    std::uint64_t endBlobBytes_ = 0;
    std::uint32_t endPresents_ = 0;
    std::vector<std::uint64_t> opCounts_;
    std::string error_;
  };
} // namespace galtrace
