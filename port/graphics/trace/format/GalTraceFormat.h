#pragma once

// galtrace, format versions 1 and 2: the gpg::gal call stream of one process, written by the
// recorder (port/graphics/trace/record) and read by galplay (port/graphics/trace/play) and
// galtrace-dump (port/graphics/trace/tools). README.md in port/graphics/trace has the format in prose.
//
// Portable C++17, no engine, Windows or Diligent types: the same sources build into the Win32
// graphics main.exe, the host tools (MSVC Win32 and x64) and the Android NDK tools (x86_64, arm64).
//
// Nothing in a trace depends on pointer width or host byte order:
//   - every integer is little-endian with a fixed width (u8, u32, i32, u64), every float the IEEE
//     single's bit pattern as a u32;
//   - every gal context struct is written field by field (no memcpy of engine structs);
//   - gal objects are numbered (u32 ids, 1..n, never reused); 0 is "no object";
//   - payloads (file data, locked texture and buffer contents, effect sources) are numbered once and
//     named by two 64-bit content hashes; a record names a payload by its number (FieldType::Blob).
//
// File: "GALTRACE", u32 version, u32 metadata count, metadata pairs (str key, str value), then
// records until the End record. A record is u16 op, u16 flags, u32 payload bytes, payload. The
// payload of every op is the field list of its OpDesc, in order; the generic decoder
// (GalTraceIO.h, DecodeRecord) needs nothing else.
//
// Version 2 (M7a1) adds three ways to define a payload besides the embedded Blob, so that a trace can
// be shipped without the game's data in it. Payload numbers are shared by all four (1..n, in file
// order); every op of version 1 is unchanged, and a reader of version 2 reads version 1 files.
//   PayloadRef      the bytes of a game file, by VFS path ("/effects/ui.fx") and content hashes: the
//                   reader asks its PayloadResolver (the VFS on the phone) and checks size and hashes;
//                   or of several game files one after the other (`parts`, each with its own path,
//                   size and hashes: the engine hands an effect source to gal as d3d9states.compat
//                   followed by the .fx file);
//   PayloadDigest   bytes the trace does not hold, only their hashes: what the backend answers
//                   (GetTexture2D's output: the replay supplies its own bytes, Reader::ProvidePayload)
//                   or what a readback is compared with;
//   PayloadCompose  bytes built from earlier payloads (rows copied into place over zeros or a base
//                   payload) plus literal runs: a texture atlas written by the engine is the
//                   GetTexture2D outputs placed in it plus the engine's own glyph blocks.
// The recorder writes version 1 (every payload embedded); galtrace-refs (tools/) turns a recording into
// version 2 against the game data, checking every reference.

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace galtrace
{
  inline constexpr char kMagic[8] = {'G', 'A', 'L', 'T', 'R', 'A', 'C', 'E'};
  inline constexpr std::uint32_t kFormatVersion1 = 1; // every payload embedded (Blob records only)
  inline constexpr std::uint32_t kFormatVersion2 = 2; // adds PayloadRef, PayloadDigest, PayloadCompose
  inline constexpr std::uint32_t kFormatVersion = kFormatVersion2; // the newest version this code reads and writes

  // Metadata keys (header pairs). Values are text.
  inline constexpr const char* kMetaHarnessFrames = "harness_frames";   // "10,15,...": the readbacks' frame numbers, in order
  inline constexpr const char* kMetaGal = "gal";                        // the backend the recording ran on ("d3d9", "diligent:vk")
  inline constexpr const char* kMetaFrameRate = "frame_rate";           // presents per second of the recording's pace ("30")
  inline constexpr const char* kMetaPresents = "presents";              // presents in the trace (the End record's count, up front)
  inline constexpr const char* kMetaReadbacks = "readbacks";            // read-only texture locks (readbacks) in the trace
  inline constexpr const char* kMetaPayloadRefs = "payload_refs";       // number of PayloadRef records (version 2)
  inline constexpr const char* kMetaRefArchives = "ref_archives";       // "effects.nx2,textures.scd": where the referenced files were found
  inline constexpr const char* kMetaGameVersion = "game_version";       // "3839": the FAF game version of the data the references were made against
  // "reference_frames.<backend>" = "10:d92c3a2233b80c55,15:...": the frame hash (FNV-1a 64 over R,G,B,
  // as the frame harness hashes frames) of each readback as that backend renders it on the PC.
  inline constexpr const char* kMetaReferenceFramesPrefix = "reference_frames.";

  /** Record flags (u16 after the op). */
  inline constexpr std::uint16_t kFlagOffThread = 0x0001; // made on a thread other than the device's creation thread
  inline constexpr std::uint16_t kFlagThrew = 0x0002;     // the backend threw gpg::gal::Error; result fields are zero

  /** The kinds of gal objects a trace numbers. */
  enum class ObjectType : std::uint8_t
  {
    None = 0,
    Device = 1,
    Texture = 2,
    RenderTarget = 3,
    CubeRenderTarget = 4,
    DepthStencilTarget = 5,
    VertexFormat = 6,
    VertexBuffer = 7,
    IndexBuffer = 8,
    Effect = 9,
    EffectTechnique = 10,
    EffectVariable = 11,
    PipelineState = 12,
    Any = 0xFF, // Release: any live object
  };

  [[nodiscard]] const char* ObjectTypeName(ObjectType type);

  enum class FieldType : std::uint8_t
  {
    U8,        // 1 byte
    Bool,      // 1 byte, 0 or 1
    U32,       // 4 bytes
    I32,       // 4 bytes
    U64,       // 8 bytes
    F32,       // 4 bytes, the float's bits
    Str,       // u32 length, bytes (no terminator)
    Bytes,     // u32 length, bytes
    Blob,      // u32 blob id, 0 = none
    IdRef,     // u32 object id, 0 = none; refers to a live object of `objectType`
    IdDef,     // u32 object id, 0 = none; (re)binds the id to the object the call returned
    Rect,      // 4 x i32: left, top, right, bottom
    OptRect,   // u8 present, then Rect when present
    Matrix,    // 16 x f32, row major as gal::Matrix holds it
    OptMatrix, // u8 present, then Matrix when present
    Vec4,      // 4 x f32
    F32Array,  // u32 count, count x f32
    U32Array,  // u32 count, count x u32
    Struct,    // the `sub` fields, inline
    List,      // u32 count, count x the `sub` fields
  };

  struct FieldDesc
  {
    const char* name;
    FieldType type;
    ObjectType objectType = ObjectType::None; // IdRef / IdDef
    const FieldDesc* sub = nullptr;           // Struct / List
    std::uint8_t subCount = 0;
    bool result = false; // the backend's answer, written after the call returned
  };

  // Op numbers are stable: a new op gets a new number, an op's fields never change within a
  // format version. 0x00xx stream, 0x01xx Device slots, 0x02xx Texture, 0x03xx VertexBuffer,
  // 0x04xx IndexBuffer, 0x05xx RenderTarget, 0x06xx CubeRenderTarget, 0x07xx DepthStencilTarget,
  // 0x08xx Effect, 0x09xx EffectTechnique, 0x0Axx EffectVariable.
  enum class Op : std::uint16_t
  {
    // stream
    Blob = 0x0001,
    End = 0x0002,
    FpuState = 0x0003,
    Note = 0x0004,
    DeviceCreate = 0x0005,
    DeviceDestroy = 0x0006,
    Release = 0x0007,
    PayloadRef = 0x0008,     // version 2: a payload that is a game file (VFS path + hashes)
    PayloadDigest = 0x0009,  // version 2: a payload known by its hashes only
    PayloadCompose = 0x000A, // version 2: a payload built from earlier payloads and literal runs

    // gpg::gal::Device (the slot numbers of Device.hpp in the comments)
    DevGetLog = 0x0101,              // 1
    DevGetDeviceContext = 0x0102,    // 2
    DevGetCurThreadId = 0x0103,      // 3
    DevFunc1 = 0x0104,               // 4
    DevGetModesForAdapter = 0x0105,  // 5
    DevGetHeadOutputContext = 0x0106, // 6 / 7
    DevGetPipelineState = 0x0107,    // 8
    DevCreateEffect = 0x0108,        // 9
    DevCreateTexture = 0x0109,       // 10
    DevCreateRenderTarget = 0x010A,  // 11
    DevCreateCubeRenderTarget = 0x010B, // 12
    DevCreateDepthStencilTarget = 0x010C, // 13
    DevCreateVertexFormat = 0x010D,  // 14
    DevCreateVertexBuffer = 0x010E,  // 15
    DevCreateIndexBuffer = 0x010F,   // 16
    DevGetRenderTargetData = 0x0110, // 17
    DevStretchRect = 0x0111,         // 18
    DevUpdateSurface = 0x0112,       // 19
    DevSaveCubeRenderTarget = 0x0113, // 20
    DevSaveRenderTarget = 0x0114,    // 21
    DevSaveTexture = 0x0115,         // 22
    DevGetTexture2D = 0x0116,        // 23
    DevFunc7 = 0x0117,               // 24
    DevResetWithContext = 0x0118,    // 26
    DevReset = 0x0119,               // 25
    DevTestCooperativeLevel = 0x011A, // 27
    DevBeginScene = 0x011B,          // 28
    DevEndScene = 0x011C,            // 29
    DevPresent = 0x011D,             // 30
    DevSetCursor = 0x011E,           // 31
    DevInitCursor = 0x011F,          // 32
    DevShowCursor = 0x0120,          // 33
    DevSetViewport = 0x0121,         // 34
    DevGetViewport = 0x0122,         // 35
    DevClearTarget = 0x0123,         // 36
    DevGetContext = 0x0124,          // 37
    DevClear = 0x0125,               // 38
    DevClearTextures = 0x0126,       // 39
    DevSetVertexDeclaration = 0x0127, // 40
    DevSetVertexBuffer = 0x0128,     // 41
    DevSetBufferIndices = 0x0129,    // 42
    DevSetFogState = 0x012A,         // 43
    DevSetWireframeState = 0x012B,   // 44
    DevSetColorWriteState = 0x012C,  // 45
    DevDrawIndexedPrimitive = 0x012D, // 46
    DevDrawPrimitive = 0x012E,       // 47
    DevBeginTechnique = 0x012F,      // 48
    DevEndTechnique = 0x0130,        // 49

    // gpg::gal::Texture
    TexGetContext = 0x0201,
    TexLock = 0x0202,
    TexUnlockRect = 0x0203,  // Unlock(TextureLockRect)
    TexUnlockLevel = 0x0204, // Unlock(int level)
    TexSaveToBuffer = 0x0205,

    // gpg::gal::VertexBuffer
    VbGetContext = 0x0301,
    VbLock = 0x0302,
    VbUnlock = 0x0303,

    // gpg::gal::IndexBuffer
    IbGetContext = 0x0401,
    IbLock = 0x0402,
    IbUnlock = 0x0403,

    // gpg::gal::RenderTarget
    RtGetContext = 0x0501,
    RtGetDC = 0x0502,

    // gpg::gal::CubeRenderTarget
    CubeGetContext = 0x0601,

    // gpg::gal::DepthStencilTarget
    DsGetContext = 0x0701,

    // gpg::gal::Effect
    EffGetContext = 0x0801,
    EffGetTechniques = 0x0802,
    EffGetVariable = 0x0803,
    EffGetTechnique = 0x0804,
    EffOnReset = 0x0805,
    EffOnLost = 0x0806,

    // gpg::gal::EffectTechnique
    TechGetName = 0x0901,
    TechBegin = 0x0902,
    TechEnd = 0x0903,
    TechBeginPass = 0x0904,
    TechEndPass = 0x0905,
    TechGetAnnotationBool = 0x0906,
    TechGetAnnotationInt = 0x0907,
    TechGetAnnotationFloat = 0x0908,
    TechGetAnnotationString = 0x0909,

    // gpg::gal::EffectVariable
    VarGetName = 0x0A01,
    VarSetCubeRenderTarget = 0x0A02,
    VarSetRenderTarget = 0x0A03,
    VarSetTexture = 0x0A04,
    VarSetMatrix4x4 = 0x0A05,
    VarSetFloatArray = 0x0A06,
    VarSetVector = 0x0A07,
    VarSetValue = 0x0A08,
    VarSetFloat = 0x0A09,
    VarSetInt = 0x0A0A,
    VarSetBool = 0x0A0B,
    VarSetMatrixArray = 0x0A0C,
    VarSetVectorArray = 0x0A0D,
    VarGetAnnotationBool = 0x0A0E,
    VarGetAnnotationInt = 0x0A0F,
    VarGetAnnotationFloat = 0x0A10,
    VarGetAnnotationString = 0x0A11,
  };

  struct OpDesc
  {
    Op op;
    const char* name;
    const FieldDesc* fields;
    std::uint8_t fieldCount;
  };

  /** The schema of `op`, or nullptr for an unknown op. */
  [[nodiscard]] const OpDesc* FindOp(std::uint16_t op);
  [[nodiscard]] const OpDesc* FindOp(Op op);

  /** Every op of this format version, in op order. */
  [[nodiscard]] const std::vector<const OpDesc*>& AllOps();

  /** True for the four ops that define a payload number (Blob, PayloadRef, PayloadDigest, PayloadCompose). */
  [[nodiscard]] bool IsPayloadDefinition(std::uint16_t op);

  /** The lowest format version that may contain `op` (1, or 2 for the payload ops of version 2). */
  [[nodiscard]] std::uint32_t OpMinVersion(std::uint16_t op);

  /** How a payload number is defined (version 2). */
  enum class PayloadKind : std::uint8_t
  {
    Embedded = 1, // Blob: the bytes are in the trace
    Reference = 2, // PayloadRef: a game file, read through the PayloadResolver
    Digest = 3,    // PayloadDigest: hashes only; the replay may supply the bytes
    Composed = 4,  // PayloadCompose: built from earlier payloads and literal runs
  };
  [[nodiscard]] const char* PayloadKindName(PayloadKind kind);

  /** PayloadDigest's `origin`: what the bytes were in the recording. */
  enum class DigestOrigin : std::uint8_t
  {
    BackendOutput = 1, // a backend's answer the replay produces again (GetTexture2D's output)
    Readback = 2,      // a read-only texture lock (the harness's frame readback): compared only
    SavedOutput = 3,   // SaveTexture / SaveToBuffer output: compared only
    Other = 4,
  };
  [[nodiscard]] const char* DigestOriginName(std::uint8_t origin);

  /** Texture lock content mode (TexUnlock*): what the blob holds. */
  enum class LockContent : std::uint8_t
  {
    Written = 0,   // what the engine left in the locked rectangle: replayed into the lock
    ReadBack = 1,  // a read-only lock: what the backend gave the engine (compared on replay)
    None = 2,      // nothing (the lock region could not be sized: unknown format)
  };

  /**
   * Bytes per block and block size of a gal texture format (TextureContext::format_, the "Moho"
   * format numbers of D3D9Interfaces.cpp's kD3D9FormatToMohoPairs). False for formats the trace
   * cannot size (format 20, "other").
   */
  struct TexelLayout
  {
    std::uint32_t blockWidth = 1;
    std::uint32_t blockHeight = 1;
    std::uint32_t bytesPerBlock = 0;
  };
  [[nodiscard]] bool GalTextureFormatLayout(std::uint32_t galFormat, TexelLayout* out);

  /**
   * The packed size of a locked texture rectangle: rows of whole blocks. `levelWidth/Height` are the
   * locked level's size; an empty rect (left == right, as TextureD3D9::Lock reads it) is the level.
   */
  struct LockRegion
  {
    std::uint32_t rowBytes = 0;
    std::uint32_t rows = 0;
    bool known = false;
  };
  [[nodiscard]] LockRegion TextureLockRegion(
    std::uint32_t galFormat, std::uint32_t levelWidth, std::uint32_t levelHeight, std::int32_t left, std::int32_t top,
    std::int32_t right, std::int32_t bottom
  );

  /** The two content hashes that name a blob. */
  struct BlobKey
  {
    std::uint64_t a = 0;
    std::uint64_t b = 0;
    std::uint32_t size = 0;
    bool operator==(const BlobKey& other) const
    {
      return a == other.a && b == other.b && size == other.size;
    }
  };
  [[nodiscard]] BlobKey HashBlob(const void* data, std::size_t size);

  /** FNV-1a 64, the harness's frame hash. */
  [[nodiscard]] std::uint64_t Fnv1a64(const void* data, std::size_t size, std::uint64_t seed = 0xCBF29CE484222325ULL);

  [[nodiscard]] std::string Hex64(std::uint64_t value);

  /**
   * The frame harness's frame hash of a B,G,R,A image (gal format 2/3 as a lock gives it, rows packed):
   * FNV-1a 64 over R, G, B of every pixel in memory order (GalCapture.cpp; gfx_capture.py's hashes).
   */
  [[nodiscard]] std::uint64_t FrameRgbHash(const std::uint8_t* bgra, std::size_t bytes);

  /** "10:d92c3a2233b80c55,15:..." (kMetaReferenceFramesPrefix values) as (frame, hex hash) pairs, in order. */
  [[nodiscard]] std::vector<std::pair<std::uint32_t, std::string>> ParseFrameHashes(const std::string& text);
  [[nodiscard]] std::string FormatFrameHashes(const std::vector<std::pair<std::uint32_t, std::string>>& hashes);
} // namespace galtrace
