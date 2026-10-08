#pragma once

// Writing and reading galtrace files (GalTraceFormat.h). Portable C++17.
//
//   Writer          the recorder's side: records are built in a RecordBuilder (one per call, so a
//                   record can be started while another is still open, as a wrapper's destructor
//                   does inside a call), payloads are interned as blobs, Commit appends. Version 2
//                   files (galtrace-refs) also define payloads by reference, digest or composition.
//   Reader          sequential records; payload definitions (Blob, PayloadRef, PayloadDigest,
//                   PayloadCompose) are indexed as they pass and read back on demand, hash-verified:
//                   embedded bytes through a second file handle, references through the
//                   PayloadResolver (GalTraceResolver.h), digests from what the replay provides,
//                   compositions built from their parts.
//   Cursor          typed reads of one record's payload, checked against the op's field list.
//   DecodeRecord    the generic decoder: any record as text, from the schema alone.
//   Validator       the stream rules: known ops, payloads that match their schema, payloads defined
//                   before use and equal to their hashes, ids referring to live objects of the right
//                   type, an End record whose totals match.

#include "GalTraceFormat.h"

#include <cstdio>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace galtrace
{
  class PayloadResolver; // GalTraceResolver.h

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

  /**
   * One step of a PayloadCompose (version 2): `rows` rows of `rowBytes` bytes, row r copied from payload
   * `source` at sourceOffset + r * sourcePitch to offset + r * pitch of the payload being built.
   */
  struct PayloadCopy
  {
    std::uint32_t source = 0;
    std::uint32_t sourceOffset = 0;
    std::uint32_t sourcePitch = 0;
    std::uint32_t offset = 0;
    std::uint32_t pitch = 0;
    std::uint32_t rowBytes = 0;
    std::uint32_t rows = 0;
  };

  /** One game file of a PayloadRef made of several (version 2): the payload is its parts in order. */
  struct PayloadRefPart
  {
    std::string path;    // VFS path
    std::string archive; // the archive (or directory) the recording machine found it in
    BlobKey key{};       // hashes and size of this file's bytes
  };

  /** A literal run of a PayloadCompose: `bytes` at `offset`. Applied after every copy. */
  struct PayloadLiteral
  {
    std::uint32_t offset = 0;
    std::vector<std::uint8_t> bytes;
  };

  /**
   * Builds a composed payload: `base` (or `size` zero bytes when base is null), every copy, then every
   * literal. False (and *error) when a step falls outside its source or the result.
   */
  bool BuildComposition(
    std::uint32_t size, const std::vector<std::uint8_t>* base, const std::vector<PayloadCopy>& copies,
    const std::vector<const std::vector<std::uint8_t>*>& copySources, const std::vector<PayloadLiteral>& literals,
    std::vector<std::uint8_t>* out, std::string* error
  );

  struct WriterStats
  {
    std::uint64_t records = 0;
    std::uint32_t blobs = 0;        // Blob records (embedded payloads)
    std::uint64_t blobBytes = 0;    // unique payload bytes stored
    std::uint64_t blobRefs = 0;     // InternBlob calls with data
    std::uint64_t blobRefBytes = 0; // payload bytes before deduplication
    std::uint32_t payloads = 0;     // payload numbers defined, of every kind
    std::uint32_t references = 0;   // PayloadRef records
    std::uint64_t referenceBytes = 0;
    std::uint32_t digests = 0;      // PayloadDigest records
    std::uint64_t digestBytes = 0;
    std::uint32_t compositions = 0; // PayloadCompose records
    std::uint64_t compositionBytes = 0; // the sizes of the payloads they build
    std::uint64_t literalBytes = 0;     // literal bytes stored in them
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

    /**
     * Creates the file. `version` is written into the header: the recorder writes kFormatVersion1 (it
     * embeds every payload); a file with version 2 payload definitions must be opened with
     * kFormatVersion2 (the Define* calls refuse otherwise).
     */
    bool Open(const std::string& path, const Metadata& metadata, std::string* error, std::uint32_t version = kFormatVersion1);
    [[nodiscard]] bool IsOpen() const { return file_ != nullptr; }
    [[nodiscard]] std::uint32_t Version() const { return version_; }

    /** The blob id of `data` (0 for no data), writing its Blob record the first time it is seen. */
    std::uint32_t InternBlob(const void* data, std::size_t size);

    // Version 2 payload definitions. Each writes its record now and returns the new payload number
    // (0 when the file is not version 2 or not open). No deduplication: the caller decides.
    std::uint32_t DefineEmbedded(const void* data, std::size_t size);
    std::uint32_t DefineReference(const BlobKey& key, const std::string& vfsPath, const std::string& archive);
    /** A payload that is several game files one after the other; `vfsPath`/`archive` name the part the record names. */
    std::uint32_t DefineReference(
      const BlobKey& key, const std::string& vfsPath, const std::string& archive, const std::vector<PayloadRefPart>& parts
    );
    std::uint32_t DefineDigest(const BlobKey& key, DigestOrigin origin);
    std::uint32_t DefineComposition(
      const BlobKey& key, std::uint32_t base, const std::vector<PayloadCopy>& copies, const std::vector<PayloadLiteral>& literals
    );

    void Commit(const RecordBuilder& record);
    /** Appends a record as it is (a converter copying records); payload definitions must use the calls above. */
    void CommitRaw(std::uint16_t op, std::uint16_t flags, const std::uint8_t* payload, std::size_t size);
    void Flush();

    /** Writes the End record and closes the file. */
    void Close(std::uint32_t presents, std::uint32_t objects);

    [[nodiscard]] const WriterStats& Stats() const { return stats_; }

  private:
    void WriteRecord(Op op, std::uint16_t flags, const std::uint8_t* payload, std::size_t size);
    void WriteRaw(const void* data, std::size_t size);
    std::uint32_t WriteBlobRecord(const BlobKey& key, const void* data, std::size_t size);

    struct KeyHash
    {
      std::size_t operator()(const BlobKey& key) const { return static_cast<std::size_t>(key.a ^ (key.b * 31u)); }
    };

    std::FILE* file_ = nullptr;
    std::uint32_t version_ = kFormatVersion1;
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

  /** One payload number as its definition record gave it. */
  struct PayloadEntry
  {
    PayloadKind kind = PayloadKind::Embedded;
    BlobKey key{};                 // the bytes' hashes and size
    std::uint64_t dataOffset = 0;  // Embedded: file offset of the bytes
    std::uint64_t recordOffset = 0; // the definition record's header
    std::string path;              // Reference: VFS path (with parts: the part the record names)
    std::string archive;           // Reference: the archive (or directory) the recording machine found it in
    std::vector<PayloadRefPart> parts; // Reference: empty for one whole file, else the files it is made of, in order
    std::uint8_t origin = 0;       // Digest: DigestOrigin
    std::uint32_t base = 0;        // Composed: the base payload (0: zeros)
    std::vector<PayloadCopy> copies;      // Composed
    std::vector<PayloadLiteral> literals; // Composed
    bool verified = false;         // Embedded / Reference: the bytes were read and matched once
  };
  using BlobEntry = PayloadEntry; // the version 1 name

  /** What ReadPayload found. */
  struct PayloadStatus
  {
    PayloadKind kind = PayloadKind::Embedded;
    bool available = false; // bytes were produced
    bool exact = false;     // ... and they equal the recorded hashes and size
    std::string error;      // why not available, or why not exact (a sentence for a person)
  };

  class Reader
  {
  public:
    Reader() = default;
    ~Reader();
    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;

    /** Opens a version 1 or 2 file and reads its header. */
    bool Open(const std::string& path, std::string* error);
    [[nodiscard]] std::uint32_t Version() const { return version_; }
    [[nodiscard]] const Metadata& GetMetadata() const { return metadata_; }
    [[nodiscard]] std::string Meta(const std::string& key) const;

    /**
     * Where PayloadRef bytes come from (the game data: the VFS on the phone, a directory on the PC).
     * Not owned; may be null (references then cannot be read). Set it before reading payloads.
     */
    void SetResolver(PayloadResolver* resolver) { resolver_ = resolver; }
    [[nodiscard]] PayloadResolver* Resolver() const { return resolver_; }

    /**
     * The next record. Payload definitions (Blob, PayloadRef, PayloadDigest, PayloadCompose) are
     * returned too (`returnBlobs`) or only indexed. False at the end of the file (then SawEnd() tells
     * whether an End record closed it) or on an error (Error()).
     */
    bool Next(Record* record, bool returnBlobs = false);

    [[nodiscard]] bool SawEnd() const { return sawEnd_; }
    [[nodiscard]] bool Failed() const { return !error_.empty(); }
    [[nodiscard]] const std::string& Error() const { return error_; }

    /** Payload numbers defined so far (of every kind). */
    [[nodiscard]] bool HasBlob(std::uint32_t id) const { return id != 0 && id <= payloads_.size(); }
    [[nodiscard]] const PayloadEntry* GetBlobEntry(std::uint32_t id) const { return HasBlob(id) ? &payloads_[id - 1] : nullptr; }
    [[nodiscard]] const PayloadEntry* GetPayload(std::uint32_t id) const { return GetBlobEntry(id); }
    [[nodiscard]] std::uint32_t BlobCount() const { return static_cast<std::uint32_t>(payloads_.size()); }
    [[nodiscard]] std::uint32_t PayloadCount() const { return BlobCount(); }

    /**
     * The payload's bytes (empty for id 0). True when they could be produced:
     *   Embedded   read from the file and equal to the hashes (else false);
     *   Reference  read through the resolver and equal to the hashes (else false: missing or different);
     *   Digest     the bytes ProvidePayload supplied (false before that);
     *   Composed   built from its parts (false when a part is unavailable).
     * `status` says which, and whether the bytes equal the recorded hashes (a Digest or a Composed
     * payload may be available but not exact when the replay's backend answered differently).
     */
    bool ReadPayload(std::uint32_t id, std::vector<std::uint8_t>* out, PayloadStatus* status = nullptr);

    /** ReadPayload, true only when the bytes are available and exact (the version 1 contract). */
    bool ReadBlob(std::uint32_t id, std::vector<std::uint8_t>* out);

    /**
     * The replay's own bytes for a Digest payload (GetTexture2D's output as this backend computed it):
     * later reads of the payload, and compositions built from it, use them. Returns whether they equal
     * the recorded hashes. Ignored (false) for payloads of other kinds.
     */
    bool ProvidePayload(std::uint32_t id, const void* data, std::size_t size);

  private:
    bool ReadExact(void* data, std::size_t size, bool* cleanEof);
    bool Fail(const std::string& message);
    bool IndexPayload(std::uint16_t op, const std::uint8_t* payload, std::size_t size, std::uint64_t recordOffset);
    bool ReadPayloadDepth(std::uint32_t id, std::vector<std::uint8_t>* out, PayloadStatus* status, int depth);

    std::FILE* file_ = nullptr;
    std::FILE* blobFile_ = nullptr;
    std::uint32_t version_ = 0;
    Metadata metadata_;
    std::uint64_t position_ = 0;
    std::uint64_t fileSize_ = 0; // 0: unknown
    std::uint64_t nextIndex_ = 0;
    std::vector<PayloadEntry> payloads_;
    std::map<std::uint32_t, std::vector<std::uint8_t>> provided_; // Digest payloads the replay supplied
    PayloadResolver* resolver_ = nullptr;
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
    /**
     * With true, PayloadRef records are read through the reader's resolver and checked against their
     * hashes too (galtrace-dump --data); by default only their form is checked.
     */
    void SetResolveReferences(bool resolve) { resolveReferences_ = resolve; }

    /** Checks one record (payload definitions included); false and Error() on the first broken rule. */
    bool Check(const Record& record, Reader& reader);
    /** After the last record: the End record exists and its totals match. */
    bool Finish(const Reader& reader);

    [[nodiscard]] const std::string& Error() const { return error_; }
    [[nodiscard]] std::uint64_t Records() const { return records_; }
    [[nodiscard]] std::uint32_t Presents() const { return presents_; }
    [[nodiscard]] std::uint32_t ObjectsDefined() const { return objectsDefined_; }
    [[nodiscard]] std::uint32_t LiveObjects() const { return static_cast<std::uint32_t>(live_.size()); }
    [[nodiscard]] const std::vector<std::uint64_t>& OpCounts() const { return opCounts_; } // by op index in AllOps()
    // Payload definitions by kind, and the bytes they stand for.
    [[nodiscard]] std::uint32_t Embedded() const { return blobsSeen_; }
    [[nodiscard]] std::uint64_t EmbeddedBytes() const { return blobBytes_; }
    [[nodiscard]] std::uint32_t References() const { return references_; }
    [[nodiscard]] std::uint64_t ReferenceBytes() const { return referenceBytes_; }
    [[nodiscard]] std::uint32_t ReferencesResolved() const { return referencesResolved_; }
    [[nodiscard]] std::uint32_t Digests() const { return digests_; }
    [[nodiscard]] std::uint64_t DigestBytes() const { return digestBytes_; }
    [[nodiscard]] std::uint32_t Compositions() const { return compositions_; }
    [[nodiscard]] std::uint64_t CompositionBytes() const { return compositionBytes_; }
    [[nodiscard]] std::uint64_t LiteralBytes() const { return literalBytes_; }

  private:
    bool Walk(const FieldDesc* fields, std::uint8_t count, const Record& record, std::size_t* at, Reader& reader);
    bool Fail(const Record& record, const std::string& message);
    bool CheckPayloadDefinition(const Record& record, Reader& reader);

    std::unordered_map<std::uint32_t, ObjectType> live_;
    std::uint32_t maxId_ = 0;
    std::uint32_t objectsDefined_ = 0;
    std::uint64_t records_ = 0;
    std::uint32_t presents_ = 0;
    bool resolveReferences_ = false;
    std::uint32_t payloadsSeen_ = 0;
    std::uint32_t blobsSeen_ = 0;
    std::uint64_t blobBytes_ = 0;
    std::uint32_t references_ = 0;
    std::uint64_t referenceBytes_ = 0;
    std::uint32_t referencesResolved_ = 0;
    std::uint32_t digests_ = 0;
    std::uint64_t digestBytes_ = 0;
    std::uint32_t compositions_ = 0;
    std::uint64_t compositionBytes_ = 0;
    std::uint64_t literalBytes_ = 0;
    bool sawEnd_ = false;
    std::uint64_t endRecords_ = 0;
    std::uint32_t endBlobs_ = 0;
    std::uint64_t endBlobBytes_ = 0;
    std::uint32_t endPresents_ = 0;
    std::vector<std::uint64_t> opCounts_;
    std::string error_;
  };
} // namespace galtrace
