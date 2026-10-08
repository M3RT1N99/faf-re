// Unit test of the galtrace format library (port/graphics/trace/format): round trip through Writer,
// Reader, Cursor, Validator and the generic decoder, blob deduplication, and the failures a reader
// must report (truncation, a changed blob byte, an id of the wrong type, a schema mismatch).
//
//   galtrace_format_test <scratch dir>        exit 0 when every check passes

#include "../format/GalTraceIO.h"

#include <cstdio>
#include <cstring>
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

  std::printf("galtrace format test: %d checks, %d failed\n", gChecks, gFailures);
  return gFailures == 0 ? 0 : 1;
}
