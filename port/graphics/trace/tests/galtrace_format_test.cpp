// Unit test of the galtrace format library (port/graphics/trace/format): round trip through Writer,
// Reader, Cursor, Validator and the generic decoder, blob deduplication, and the failures a reader
// must report (truncation, a changed blob byte, an id of the wrong type, a schema mismatch); and
// version 2 (M7a1): payloads by reference (whole files and files in parts), digest and composition,
// the resolvers, the data check and its messages, the version rules and the frame-hash helpers.
//
//   galtrace_format_test <scratch dir>        exit 0 when every check passes

#include "../format/GalTraceIO.h"
#include "../format/GalTraceResolver.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace
{
  int gChecks = 0;
  int gFailures = 0;

  void Check(const bool condition, const char* const what)
  {
    ++gChecks;
    if (!condition) {
      ++gFailures;
      std::printf("FAIL: %s\n", what);
    }
  }

  std::vector<unsigned char> ReadFile(const std::string& path)
  {
    std::vector<unsigned char> bytes;
    if (std::FILE* file = std::fopen(path.c_str(), "rb")) {
      unsigned char buffer[65536];
      std::size_t got;
      while ((got = std::fread(buffer, 1, sizeof(buffer), file)) != 0) {
        bytes.insert(bytes.end(), buffer, buffer + got);
      }
      std::fclose(file);
    }
    return bytes;
  }

  void WriteFile(const std::string& path, const std::vector<unsigned char>& bytes)
  {
    if (std::FILE* file = std::fopen(path.c_str(), "wb")) {
      std::fwrite(bytes.data(), 1, bytes.size(), file);
      std::fclose(file);
    }
  }

  /** Reads and validates the whole file; returns the validator's error ("" when valid). */
  std::string ValidateFile(const std::string& path, std::vector<std::string>* lines = nullptr)
  {
    galtrace::Reader reader;
    std::string error;
    if (!reader.Open(path, &error)) {
      return "open: " + error;
    }
    galtrace::Validator validator;
    galtrace::Record record;
    while (reader.Next(&record, true)) {
      if (!validator.Check(record, reader)) {
        return validator.Error();
      }
      if (lines != nullptr) {
        std::string decodeError;
        lines->push_back(galtrace::DecodeRecord(record, &decodeError));
        if (!decodeError.empty()) {
          return "decode: " + decodeError;
        }
      }
    }
    if (!validator.Finish(reader)) {
      return validator.Error();
    }
    return {};
  }

  bool Contains(const std::string& text, const char* const part)
  {
    return text.find(part) != std::string::npos;
  }

  // ---- version 2 (M7a1): payloads by reference, digest and composition --------------------------------

  using Bytes = std::vector<std::uint8_t>;

  Bytes Pattern(const std::size_t size, const unsigned multiplier, const unsigned offset)
  {
    Bytes bytes(size);
    for (std::size_t index = 0; index < size; ++index) {
      bytes[index] = static_cast<std::uint8_t>(index * multiplier + offset);
    }
    return bytes;
  }

  /** The game data of the version 2 test, in memory. */
  struct TestData
  {
    std::map<std::string, Bytes> files; // lower-case VFS path -> bytes
    galtrace::CallbackResolver Resolver()
    {
      return galtrace::CallbackResolver([this](const std::string& path, Bytes* out, std::string* error) {
        std::string lower;
        for (const char c : path) {
          lower += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        }
        const auto found = files.find(lower);
        if (found == files.end()) {
          *error = path + ": not found";
          return false;
        }
        *out = found->second;
        return true;
      });
    }
  };

  /** Writes and checks a version 2 trace with every kind of payload definition. */
  void TestVersion2(const std::string& dir)
  {
    using namespace galtrace;
    const std::string path = dir + "/format_test_v2.galtrace";
    TestData data;
    const Bytes compat = Pattern(300, 7, 0);
    const Bytes fx = Pattern(500, 13, 1);
    Bytes dds = Pattern(128, 3, 9);
    dds[0] = 'D';
    dds[1] = 'D';
    dds[2] = 'S';
    dds[3] = ' ';
    data.files["/effects/d3d9states.compat"] = compat;
    data.files["/effects/ui.fx"] = fx;
    data.files["/textures/ui/a.dds"] = dds;
    Bytes source = compat;
    source.insert(source.end(), fx.begin(), fx.end());
    // A GetTexture2D output of 8x8 pixels as DXT5: 2 rows of 2 blocks (32 bytes a row).
    const Bytes output = Pattern(64, 5, 3);
    // The atlas the engine writes: 4 rows of 128 bytes; the output at row 1, byte 32; a glyph run at 400.
    Bytes atlas(512, 0);
    for (int row = 0; row < 2; ++row) {
      std::memcpy(atlas.data() + (1 + row) * 128 + 32, output.data() + row * 32, 32);
    }
    const Bytes glyph(10, 0xAB);
    std::memcpy(atlas.data() + 400, glyph.data(), glyph.size());
    const Bytes readback = Pattern(256, 11, 2);
    const Bytes vertices = Pattern(96, 17, 4);

    {
      Writer v1;
      std::string error;
      Check(v1.Open(dir + "/format_test_v1only.galtrace", {}, &error), "v2: open a version 1 writer");
      Check(v1.DefineReference(HashBlob(fx.data(), fx.size()), "/effects/ui.fx", "effects.nx2") == 0, "v2: a version 1 writer defines no reference");
      Check(v1.DefineDigest(HashBlob(fx.data(), fx.size()), DigestOrigin::Readback) == 0, "v2: a version 1 writer defines no digest");
      v1.Close(0, 0);
      Writer bad;
      Check(!bad.Open(dir + "/format_test_v3.galtrace", {}, &error, 3) && Contains(error, "version 3"), "v2: version 3 cannot be written");
    }

    {
      Writer writer;
      std::string error;
      Check(writer.Open(path, {{kMetaHarnessFrames, "10"}, {"reference_frames.diligent:vk", "10:0123456789abcdef"}}, &error, kFormatVersion2),
            "v2: open writer");
      const std::uint32_t sourceId = writer.DefineReference(
        HashBlob(source.data(), source.size()), "/effects/ui.fx", "effects.nx2",
        {PayloadRefPart{"/effects/d3d9states.compat", "effects.nx2", HashBlob(compat.data(), compat.size())},
         PayloadRefPart{"/effects/ui.fx", "effects.nx2", HashBlob(fx.data(), fx.size())}});
      Check(sourceId == 1, "v2: the first payload is #1");
      RecordBuilder effect(Op::DevCreateEffect);
      effect.Id(0);
      effect.U32(2);
      effect.Bool(false);
      effect.Str("/effects/ui.fx");
      effect.Str("");
      effect.Blob(sourceId);
      effect.Count(0);
      effect.Blob(0);
      effect.Id(1);
      writer.Commit(effect);

      const std::uint32_t ddsId = writer.DefineReference(HashBlob(dds.data(), dds.size()), "/textures/ui/a.dds", "textures.scd");
      const std::uint32_t outputId = writer.DefineDigest(HashBlob(output.data(), output.size()), DigestOrigin::BackendOutput);
      RecordBuilder tex2d(Op::DevGetTexture2D);
      tex2d.Id(0);
      tex2d.Blob(ddsId);
      tex2d.Blob(outputId);
      tex2d.U32(8);
      tex2d.I32(8);
      writer.Commit(tex2d);

      RecordBuilder texture(Op::DevCreateTexture);
      texture.Id(0);
      for (int pass = 0; pass < 2; ++pass) {
        if (pass == 1) {
          texture.Id(2);
        }
        texture.U32(2);
        texture.Str("");
        texture.Blob(0);
        for (int value = 0; value < 8; ++value) {
          texture.U32(value == 2 ? 12u : 1u);
        }
      }
      writer.Commit(texture);

      PayloadCopy copy;
      copy.source = outputId;
      copy.sourcePitch = 32;
      copy.offset = 128 + 32;
      copy.pitch = 128;
      copy.rowBytes = 32;
      copy.rows = 2;
      PayloadLiteral literal;
      literal.offset = 400;
      literal.bytes = glyph;
      const std::uint32_t atlasId = writer.DefineComposition(HashBlob(atlas.data(), atlas.size()), 0, {copy}, {literal});
      RecordBuilder unlock(Op::TexUnlockLevel);
      unlock.Id(2);
      unlock.I32(0);
      unlock.U8(static_cast<std::uint8_t>(LockContent::Written));
      unlock.Blob(atlasId);
      writer.Commit(unlock);

      const std::uint32_t readbackId = writer.DefineDigest(HashBlob(readback.data(), readback.size()), DigestOrigin::Readback);
      RecordBuilder read(Op::TexUnlockLevel);
      read.Id(2);
      read.I32(0);
      read.U8(static_cast<std::uint8_t>(LockContent::ReadBack));
      read.Blob(readbackId);
      writer.Commit(read);
      const std::uint32_t verticesId = writer.DefineEmbedded(vertices.data(), vertices.size());
      Check(atlasId == 4 && readbackId == 5 && verticesId == 6, "v2: payload numbers in definition order");
      writer.Close(0, 2);
      const WriterStats& stats = writer.Stats();
      Check(stats.payloads == 6 && stats.references == 2 && stats.digests == 2 && stats.compositions == 1 && stats.blobs == 1 &&
              stats.literalBytes == 10,
            "v2: writer stats by kind");
    }

    // The stream rules hold without the game data (references are checked for their form only).
    std::vector<std::string> lines;
    const std::string valid = ValidateFile(path, &lines);
    Check(valid.empty(), ("v2: validates without game data: " + valid).c_str());
    Check(lines.size() == 12, "v2: 12 records decoded (payload definitions and End included)");
    if (!lines.empty()) {
      std::printf("decoded v2: %s\n", lines[0].c_str());
    }

    // The app's data check: every game file once, parts as files of their own.
    {
      std::vector<PayloadRefInfo> refs;
      std::string error;
      Check(ScanPayloadRefs(path, &refs, &error) && refs.size() == 3, "v2: ScanPayloadRefs lists 3 game files");
      Check(refs.size() == 3 && refs[0].path == "/effects/d3d9states.compat" && refs[0].key.size == 300 && refs[1].path == "/effects/ui.fx" &&
              refs[2].path == "/textures/ui/a.dds" && refs[2].payload == 2,
            "v2: the files in order, with their own sizes");
      const std::vector<std::string> archives = RefArchives(refs);
      Check(archives.size() == 2 && archives[0] == "effects.nx2" && archives[1] == "textures.scd", "v2: archives named");
      CallbackResolver resolver = data.Resolver();
      std::vector<PayloadRefProblem> problems;
      Check(VerifyPayloadRefs(refs, resolver, &problems) && problems.empty(), "v2: every reference present and equal");

      TestData broken = data;
      broken.files.erase("/effects/d3d9states.compat");
      broken.files["/textures/ui/a.dds"][50] ^= 1;
      CallbackResolver brokenResolver = broken.Resolver();
      Check(!VerifyPayloadRefs(refs, brokenResolver, &problems) && problems.size() == 2 && problems[0].missing && !problems[1].missing,
            "v2: one file missing, one different");
      const std::string text = DescribeRefProblems(problems);
      Check(Contains(text, "effects.nx2") && Contains(text, "/effects/d3d9states.compat") && Contains(text, "/textures/ui/a.dds") &&
              Contains(text, "different content"),
            "v2: the problems name the archive and the files");
      std::printf("data problems:\n%s", text.c_str());
    }

    // Reading payloads of every kind.
    {
      Reader reader;
      std::string error;
      Check(reader.Open(path, &error) && reader.Version() == 2, "v2: open reader, version 2");
      Record record;
      while (reader.Next(&record)) {
      }
      Check(reader.SawEnd() && !reader.Failed() && reader.PayloadCount() == 6, "v2: 6 payloads indexed");
      Bytes bytes;
      PayloadStatus status;
      Check(!reader.ReadPayload(1, &bytes, &status) && Contains(status.error, "no game data"), "v2: a reference without a resolver fails");
      CallbackResolver resolver = data.Resolver();
      reader.SetResolver(&resolver);
      Check(reader.ReadPayload(1, &bytes, &status) && status.exact && bytes == source, "v2: the effect source is its two parts");
      Check(reader.GetPayload(1)->parts.size() == 2 && reader.GetPayload(1)->path == "/effects/ui.fx", "v2: parts indexed");
      Check(reader.ReadBlob(2, &bytes) && bytes == dds, "v2: a whole-file reference");
      Check(!reader.ReadPayload(3, &bytes, &status) && status.kind == PayloadKind::Digest, "v2: a digest is unavailable until provided");
      Check(!reader.ReadPayload(4, &bytes, &status), "v2: a composition of an unprovided digest is unavailable");
      Check(reader.ProvidePayload(3, output.data(), output.size()), "v2: the replay provides the recorded output");
      Check(reader.ReadPayload(4, &bytes, &status) && status.exact && bytes == atlas, "v2: the composition rebuilds the atlas");
      Bytes other = output;
      other[0] ^= 0xFF;
      Check(!reader.ProvidePayload(3, other.data(), other.size()), "v2: a different output is reported");
      Check(reader.ReadPayload(4, &bytes, &status) && !status.exact && status.available, "v2: ... and the composition is not exact");
      Check(!reader.ReadBlob(4, &bytes), "v2: ReadBlob wants exact bytes");
      Check(!reader.ProvidePayload(2, dds.data(), dds.size()), "v2: only digests take provided bytes");
      Check(reader.ReadBlob(6, &bytes) && bytes == vertices, "v2: an embedded payload");
      Check(!reader.ReadPayload(5, &bytes, &status) && status.kind == PayloadKind::Digest, "v2: a readback digest has no bytes");
    }
    {
      // Missing and different game files, in words that name them.
      TestData missing = data;
      missing.files.erase("/effects/d3d9states.compat");
      CallbackResolver missingResolver = missing.Resolver();
      Reader reader;
      std::string error;
      (void)reader.Open(path, &error);
      reader.SetResolver(&missingResolver);
      Record record;
      while (reader.Next(&record)) {
      }
      Bytes bytes;
      PayloadStatus status;
      Check(!reader.ReadPayload(1, &bytes, &status) && Contains(status.error, "/effects/d3d9states.compat") &&
              Contains(status.error, "missing"),
            "v2: a missing part is named");
      std::printf("missing part: %s\n", status.error.c_str());
      TestData changed = data;
      changed.files["/effects/ui.fx"][7] ^= 1;
      CallbackResolver changedResolver = changed.Resolver();
      reader.SetResolver(&changedResolver);
      Check(!reader.ReadPayload(1, &bytes, &status) && Contains(status.error, "/effects/ui.fx") && Contains(status.error, "differs"),
            "v2: a different part is named");
      std::printf("different part: %s\n", status.error.c_str());
    }
    {
      // The validator reads every reference when asked to.
      Reader reader;
      std::string error;
      (void)reader.Open(path, &error);
      CallbackResolver resolver = data.Resolver();
      reader.SetResolver(&resolver);
      Validator validator;
      validator.SetResolveReferences(true);
      Record record;
      bool ok = true;
      while (ok && reader.Next(&record, true)) {
        ok = validator.Check(record, reader);
      }
      ok = ok && validator.Finish(reader);
      Check(ok && validator.References() == 2 && validator.ReferencesResolved() == 2 && validator.Digests() == 2 &&
              validator.Compositions() == 1 && validator.Embedded() == 1 && validator.LiteralBytes() == 10,
            ("v2: validator with references resolved: " + validator.Error()).c_str());
    }

    // Version rules: a version 2 op in a version 1 file, and a reference path with "..".
    {
      Bytes file = ReadFile(path);
      file[8] = 1; // the header's version
      WriteFile(dir + "/format_test_v2as1.galtrace", file);
      const std::string result = ValidateFile(dir + "/format_test_v2as1.galtrace");
      Check(Contains(result, "PayloadRef") && Contains(result, "version 1"), "v2: PayloadRef in a version 1 file fails");
      std::printf("v2 op in v1: %s\n", result.c_str());

      Writer writer;
      std::string error;
      (void)writer.Open(dir + "/format_test_v2path.galtrace", {}, &error, kFormatVersion2);
      (void)writer.DefineReference(HashBlob(fx.data(), fx.size()), "/effects/../ui.fx", "effects.nx2");
      writer.Close(0, 0);
      const std::string path2 = ValidateFile(dir + "/format_test_v2path.galtrace");
      Check(Contains(path2, "VFS path"), "v2: a reference path with '..' fails");
      std::printf("bad path: %s\n", path2.c_str());

      Writer parts;
      (void)parts.Open(dir + "/format_test_v2parts.galtrace", {}, &error, kFormatVersion2);
      (void)parts.DefineReference(HashBlob(source.data(), source.size()), "/effects/ui.fx", "effects.nx2",
                                  {PayloadRefPart{"/effects/ui.fx", "effects.nx2", HashBlob(fx.data(), fx.size())}});
      parts.Close(0, 0);
      const std::string parts2 = ValidateFile(dir + "/format_test_v2parts.galtrace");
      Check(Contains(parts2, "its parts have"), "v2: parts that do not add up fail");
      std::printf("bad parts: %s\n", parts2.c_str());
    }

    // Compositions that reach outside their source or the payload are refused.
    {
      Bytes out;
      std::string error;
      PayloadCopy copy;
      copy.sourcePitch = 32;
      copy.pitch = 128;
      copy.rowBytes = 32;
      copy.rows = 3; // the source has 2 rows
      Check(!BuildComposition(512, nullptr, {copy}, {&output}, {}, &out, &error) && !error.empty(), "v2: a copy past its source fails");
      PayloadLiteral literal;
      literal.offset = 510;
      literal.bytes = glyph;
      Check(!BuildComposition(512, nullptr, {}, {}, {literal}, &out, &error), "v2: a literal past the payload fails");
    }

    // The frame hash, the metadata helpers and the directory resolver.
    {
      const std::uint8_t pixels[8] = {1, 2, 3, 255, 4, 5, 6, 255}; // B, G, R, A
      std::uint64_t expected = 0xCBF29CE484222325ULL;
      for (const unsigned value : {3u, 2u, 1u, 6u, 5u, 4u}) {
        expected = (expected ^ value) * 0x100000001B3ULL;
      }
      Check(FrameRgbHash(pixels, sizeof(pixels)) == expected, "v2: FrameRgbHash is FNV-1a over R, G, B");
      const auto frames = ParseFrameHashes("10:d92c3a2233b80c55,30:d5f9c71c488263e2, junk,60:9ab312ba8320f018");
      Check(frames.size() == 3 && frames[1].first == 30 && frames[2].second == "9ab312ba8320f018", "v2: ParseFrameHashes");
      Check(FormatFrameHashes({{10, "aa"}, {900, "bb"}}) == "10:aa,900:bb", "v2: FormatFrameHashes");
      Reader reader;
      std::string error;
      (void)reader.Open(path, &error);
      Check(reader.Meta("reference_frames.diligent:vk") == "10:0123456789abcdef", "v2: reference frames in the header");

      Check(DirectoryResolver::MapPath("/data", "/Effects/UI.fx") == "/data/effects/ui.fx", "v2: MapPath lower-cases");
      Check(DirectoryResolver::MapPath("/data", "/effects/../x").empty(), "v2: MapPath refuses '..'");
      WriteFile(dir + "/galtrace_v2_file.dds", Bytes(dds.begin(), dds.end()));
      DirectoryResolver directory(dir);
      Bytes bytes;
      Check(directory.ReadGameFile("/GALTRACE_V2_FILE.DDS", &bytes, &error) && bytes == dds, "v2: DirectoryResolver reads a file");
      Check(!directory.ReadGameFile("/no/such/file.dds", &bytes, &error) && Contains(error, "/no/such/file.dds"), "v2: and names a missing one");
    }
  }
} // namespace

int main(int argc, char** argv)
{
  using namespace galtrace;
  const std::string dir = argc > 1 ? argv[1] : ".";
  const std::string path = dir + "/format_test.galtrace";

  // The layout helpers.
  {
    TexelLayout layout{};
    Check(GalTextureFormatLayout(12, &layout) && layout.bytesPerBlock == 16 && layout.blockWidth == 4, "DXT5 layout");
    Check(GalTextureFormatLayout(2, &layout) && layout.bytesPerBlock == 4 && layout.blockWidth == 1, "A8R8G8B8 layout");
    Check(!GalTextureFormatLayout(20, &layout), "format 20 is not sized");
    const LockRegion whole = TextureLockRegion(12, 1024, 1024, 0, 0, 0, 0);
    Check(whole.known && whole.rowBytes == 4096 && whole.rows == 256, "DXT5 whole level 1024x1024");
    const LockRegion rect = TextureLockRegion(2, 64, 64, 8, 4, 24, 36);
    Check(rect.known && rect.rowBytes == 64 && rect.rows == 32, "A8R8G8B8 rect 16x32");
    const LockRegion odd = TextureLockRegion(8, 10, 18, 0, 0, 0, 0);
    Check(odd.known && odd.rowBytes == 24 && odd.rows == 5, "DXT1 10x18 rounds up to whole blocks");
  }
  {
    const char text[] = "hello";
    const BlobKey a = HashBlob(text, 5);
    const BlobKey b = HashBlob(text, 4);
    Check(!(a == b), "different content, different key");
    Check(Fnv1a64("", 0) == 0xCBF29CE484222325ULL, "FNV-1a basis");
  }

  // Write a small trace that covers every field type.
  {
    Writer writer;
    std::string error;
    Check(writer.Open(path, {{"recorder", "unit test"}, {"frames", "60,300"}}, &error), "open writer");

    RecordBuilder create(Op::DeviceCreate);
    create.Id(1);
    for (int pass = 0; pass < 2; ++pass) { // requested, actual
      create.I32(1);       // deviceType
      create.Bool(false);  // validate
      create.I32(0);       // adapter
      create.Bool(false);  // vsync
      create.Bool(pass == 1);
      create.Bool(false);
      create.I32(pass == 1 ? 3 : 0);
      create.I32(pass == 1 ? 3 : 0);
      create.U32(pass == 1 ? 1048575u : 0u);
      create.U32(pass == 1 ? 1048575u : 0u);
      create.Count(1); // heads
      create.Bool(true);
      create.Bool(true);
      create.Bool(false);
      create.U32(1280);
      create.U32(720);
      create.U32(60);
      create.U32(0);
      create.U32(0);
      create.Str("head 0");
      create.Count(pass == 1 ? 1 : 0); // sample options
      if (pass == 1) {
        create.U32(0);
        create.U32(0);
        create.Str("none");
      }
      create.Count(0); // adapter modes
      const std::uint32_t formats[] = {21, 22};
      create.U32Array(formats, pass == 1 ? 2 : 0);
      create.U32Array(formats, 0);
    }
    writer.Commit(create);

    const std::vector<unsigned char> file(1000, 0x5A);
    RecordBuilder texture(Op::DevCreateTexture);
    texture.Id(1);
    texture.U32(1);
    texture.Str("/textures/ui/a.dds");
    texture.Blob(writer.InternBlob(file.data(), file.size()));
    for (int value = 0; value < 8; ++value) {
      texture.U32(static_cast<std::uint32_t>(value));
    }
    texture.Id(2);
    texture.U32(1);
    texture.Str("/textures/ui/a.dds");
    texture.Blob(0);
    for (int value = 0; value < 8; ++value) {
      texture.U32(static_cast<std::uint32_t>(value * 2));
    }
    writer.Commit(texture);

    RecordBuilder again(Op::DevGetTexture2D);
    again.Id(1);
    const std::uint32_t sameBlob = writer.InternBlob(file.data(), file.size());
    again.Blob(sameBlob);
    again.Blob(0);
    again.U32(4);
    again.I32(4);
    writer.Commit(again);
    Check(sameBlob == 1, "a repeated payload is one blob");

    RecordBuilder lock(Op::TexLock);
    lock.Id(2);
    lock.I32(0);
    lock.RectValue(Rect{0, 0, 0, 0});
    lock.I32(1);
    lock.U32(4096);
    lock.U32(256);
    writer.Commit(lock);

    RecordBuilder fog(Op::DevSetFogState);
    fog.Id(1);
    fog.Bool(true);
    const float matrix[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0.5f, -0.25f, 0, 1};
    fog.OptMatrix(matrix);
    fog.F32(10.0f);
    fog.F32(100.0f);
    fog.I32(-1);
    writer.Commit(fog);

    RecordBuilder stretch(Op::DevUpdateSurface);
    stretch.Id(1);
    stretch.Id(2);
    stretch.Id(2);
    const Rect source{1, 2, 3, 4};
    stretch.OptRect(&source);
    stretch.OptRect(nullptr);
    writer.Commit(stretch);

    RecordBuilder present(Op::DevPresent);
    present.Id(1);
    writer.Commit(present);

    RecordBuilder release(Op::Release);
    release.Id(2);
    writer.Commit(release);

    writer.Close(1, 2);
    Check(writer.Stats().blobs == 1 && writer.Stats().blobRefs == 2, "writer stats: 1 blob, 2 references");
  }

  // Read it back through the Cursor.
  {
    Reader reader;
    std::string error;
    Check(reader.Open(path, &error), "open reader");
    Check(reader.Meta("frames") == "60,300", "metadata round trip");
    Record record;
    Check(reader.Next(&record) && record.op == static_cast<std::uint16_t>(Op::DeviceCreate), "first record is DeviceCreate");
    Cursor create(record);
    Check(create.Id() == 1, "device id");
    Check(create.I32() == 1, "deviceType");
    create.Bool();
    create.I32();
    create.Bool();
    create.Bool();
    create.Bool();
    create.I32();
    create.I32();
    create.U32();
    create.U32();
    Check(create.Count() == 1, "one head");
    create.Bool();
    create.Bool();
    create.Bool();
    Check(create.U32() == 1280 && create.U32() == 720, "head size");
    create.U32();
    create.U32();
    create.U32();
    Check(create.Str() == "head 0", "head name");
    Check(create.Count() == 0, "no sample options requested");
    Check(create.Count() == 0, "no adapter modes requested");
    Check(create.U32Array().empty() && create.U32Array().empty(), "empty format lists");
    // actual
    create.I32();
    create.Bool();
    create.I32();
    create.Bool();
    Check(create.Bool(), "actual hwBasedInstancing");
    create.Bool();
    create.I32();
    create.I32();
    create.U32();
    create.U32();
    Check(create.Count() == 1, "actual heads");
    create.Bool();
    create.Bool();
    create.Bool();
    create.U32();
    create.U32();
    create.U32();
    create.U32();
    create.U32();
    create.Str();
    Check(create.Count() == 1, "one sample option");
    create.U32();
    create.U32();
    Check(create.Str() == "none", "sample label");
    Check(create.Count() == 0, "no adapter modes");
    Check(create.U32Array().size() == 2, "valid formats");
    create.U32Array();
    Check(create.Ok() && create.Finished(), "DeviceCreate read completely");

    Check(reader.Next(&record) && record.op == static_cast<std::uint16_t>(Op::DevCreateTexture), "blob skipped, texture next");
    Cursor texture(record);
    texture.Id();
    texture.U32();
    Check(texture.Str() == "/textures/ui/a.dds", "location");
    const std::uint32_t blob = texture.Blob();
    std::vector<std::uint8_t> bytes;
    Check(blob == 1 && reader.ReadBlob(blob, &bytes) && bytes.size() == 1000 && bytes[999] == 0x5A, "blob content");
    // A schema mismatch is caught: the next field is a u32, not a string.
    (void)texture.Str();
    Check(!texture.Ok(), "a read of the wrong type is an error");

    Check(reader.Next(&record) && reader.Next(&record) && record.op == static_cast<std::uint16_t>(Op::TexLock), "TexLock");
    Check(reader.Next(&record) && record.op == static_cast<std::uint16_t>(Op::DevSetFogState), "SetFogState");
    Cursor fog(record);
    fog.Id();
    fog.Bool();
    float matrix[16];
    Check(fog.OptMatrix(matrix) && matrix[12] == 0.5f && matrix[13] == -0.25f, "matrix round trip");
    Check(fog.F32() == 10.0f && fog.F32() == 100.0f && fog.I32() == -1 && fog.Finished(), "fog fields");
    Check(reader.Next(&record), "UpdateSurface");
    Cursor update(record);
    update.Id();
    update.Id();
    update.Id();
    Rect rect{};
    Check(update.OptRect(&rect) && rect.right == 3 && !update.OptRect(&rect) && update.Finished(), "optional rects");
    while (reader.Next(&record)) {
    }
    Check(reader.SawEnd() && !reader.Failed(), "End reached");
  }

  std::vector<std::string> lines;
  const std::string valid = ValidateFile(path, &lines);
  Check(valid.empty(), ("valid trace validates: " + valid).c_str());
  Check(lines.size() == 10, "10 records decoded (blob and End included)");
  if (!lines.empty()) {
    std::printf("decoded: %s\n", lines[0].c_str());
  }

  // Failures a reader must report.
  const std::vector<unsigned char> good = ReadFile(path);
  {
    std::vector<unsigned char> cut(good.begin(), good.end() - 13);
    WriteFile(dir + "/format_test_cut.galtrace", cut);
    const std::string result = ValidateFile(dir + "/format_test_cut.galtrace");
    Check(!result.empty(), "a truncated trace fails");
    std::printf("truncated: %s\n", result.c_str());
  }
  {
    std::vector<unsigned char> changed = good;
    // The blob's bytes are the only 0x5A run; change one in the middle.
    for (std::size_t index = 0; index + 100 < changed.size(); ++index) {
      if (changed[index] == 0x5A && changed[index + 99] == 0x5A) {
        changed[index + 50] ^= 1;
        break;
      }
    }
    WriteFile(dir + "/format_test_blob.galtrace", changed);
    const std::string result = ValidateFile(dir + "/format_test_blob.galtrace");
    Check(!result.empty(), "a changed blob byte fails");
    std::printf("changed blob: %s\n", result.c_str());
  }
  {
    Writer writer;
    std::string error;
    (void)writer.Open(dir + "/format_test_type.galtrace", {}, &error);
    RecordBuilder create(Op::DevCreateVertexBuffer);
    create.Id(0); // no device: allowed (null)
    create.U32(0);
    create.U32(0);
    create.U32(4);
    create.U32(16);
    create.Id(5);
    writer.Commit(create);
    RecordBuilder bind(Op::DevSetBufferIndices);
    bind.Id(0);
    bind.Id(5); // a vertex buffer where an index buffer is required
    writer.Commit(bind);
    writer.Close(0, 1);
    const std::string result = ValidateFile(dir + "/format_test_type.galtrace");
    Check(result.find("not a IndexBuffer") != std::string::npos, "an id of the wrong type fails");
    std::printf("wrong type: %s\n", result.c_str());
  }

  TestVersion2(dir);

  std::printf("galtrace format test: %d checks, %d failed\n", gChecks, gFailures);
  return gFailures == 0 ? 0 : 1;
}
