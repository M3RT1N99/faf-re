// Replay input for the runner executable: .fafreplay decoding, the version rewrite, the header scan
// and /replayinfo's JSON. See ReplayFile.h.

#include "ReplayFile.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <zlib.h>

#include "zstd.h"

namespace faf_runner
{
  namespace
  {
    constexpr size_t kMaxInput = size_t{64} << 20;    // a .fafreplay is < 1 MB; 64 MB is a sanity cap
    constexpr size_t kMaxDecoded = size_t{256} << 20; // the largest local replay decodes to 6.4 MB
    constexpr size_t kMaxJsonLine = size_t{1} << 20;
    const char kVersionPrefix[] = "Supreme Commander v1.50.";
    constexpr size_t kVersionPrefixLength = sizeof(kVersionPrefix) - 1;  // 24

    // ----------------------------------------------------------------------------------------------
    // SHA-256 (FIPS 180-4)
    // ----------------------------------------------------------------------------------------------

    class Sha256
    {
    public:
      Sha256() { Reset(); }

      void Reset()
      {
        static const uint32_t kInit[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                          0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
        std::memcpy(mState, kInit, sizeof(mState));
        mLength = 0;
        mUsed = 0;
      }

      void Update(const uint8_t* data, size_t size)
      {
        mLength += size;
        while (size > 0) {
          const size_t take = (64 - mUsed) < size ? (64 - mUsed) : size;
          std::memcpy(mBlock + mUsed, data, take);
          mUsed += take;
          data += take;
          size -= take;
          if (mUsed == 64) {
            Compress(mBlock);
            mUsed = 0;
          }
        }
      }

      std::string HexDigest()
      {
        const uint64_t bits = mLength * 8u;
        const uint8_t one = 0x80;
        Update(&one, 1);
        const uint8_t zero = 0;
        while (mUsed != 56) {
          Update(&zero, 1);
        }
        uint8_t length[8];
        for (int i = 0; i < 8; ++i) {
          length[i] = static_cast<uint8_t>(bits >> (56 - 8 * i));
        }
        Update(length, 8);
        static const char kHex[] = "0123456789abcdef";
        std::string out(64, '0');
        for (int i = 0; i < 8; ++i) {
          for (int j = 0; j < 4; ++j) {
            const uint8_t byte = static_cast<uint8_t>(mState[i] >> (24 - 8 * j));
            out[static_cast<size_t>(i * 8 + j * 2)] = kHex[byte >> 4];
            out[static_cast<size_t>(i * 8 + j * 2 + 1)] = kHex[byte & 15];
          }
        }
        return out;
      }

    private:
      static uint32_t Rotr(const uint32_t x, const int n) { return (x >> n) | (x << (32 - n)); }

      void Compress(const uint8_t* block)
      {
        static const uint32_t k[64] = {
          0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
          0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
          0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
          0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
          0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
          0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
          0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
          0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
          w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) | (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
                 (static_cast<uint32_t>(block[i * 4 + 2]) << 8) | static_cast<uint32_t>(block[i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
          const uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
          const uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
          w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = mState[0], b = mState[1], c = mState[2], d = mState[3];
        uint32_t e = mState[4], f = mState[5], g = mState[6], h = mState[7];
        for (int i = 0; i < 64; ++i) {
          const uint32_t s1 = Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25);
          const uint32_t ch = (e & f) ^ (~e & g);
          const uint32_t t1 = h + s1 + ch + k[i] + w[i];
          const uint32_t s0 = Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22);
          const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
          const uint32_t t2 = s0 + maj;
          h = g;
          g = f;
          f = e;
          e = d + t1;
          d = c;
          c = b;
          b = a;
          a = t1 + t2;
        }
        mState[0] += a;
        mState[1] += b;
        mState[2] += c;
        mState[3] += d;
        mState[4] += e;
        mState[5] += f;
        mState[6] += g;
        mState[7] += h;
      }

      uint32_t mState[8];
      uint64_t mLength;
      uint8_t mBlock[64];
      size_t mUsed;
    };

    // ----------------------------------------------------------------------------------------------
    // The JSON line: a small parser that keeps the top-level scalars and skips everything nested
    // ----------------------------------------------------------------------------------------------

    struct JsonValue
    {
      enum Kind { kNull, kBool, kNumber, kString, kOther } kind = kNull;
      bool boolean = false;
      double number = 0.0;
      std::string text;  // kString: the decoded string; kNumber: the literal
    };

    class JsonParser
    {
    public:
      JsonParser(const char* begin, const char* end)
        : mPos(begin), mEnd(end)
      {}

      // Parses one object and the whitespace after it; calls `field(key, value)` for each member
      // (later duplicates win, as in Python's json). False with `error` on malformed input.
      template <typename F>
      bool TopObject(F&& field, std::string& error)
      {
        SkipSpace();
        if (!Expect('{')) {
          return Fail("the first line is not a JSON object", error);
        }
        SkipSpace();
        if (Peek() == '}') {
          ++mPos;
        } else {
          for (;;) {
            SkipSpace();
            std::string key;
            if (Peek() != '"' || !String(key)) {
              return Fail("expected a member name", error);
            }
            SkipSpace();
            if (!Expect(':')) {
              return Fail("expected ':'", error);
            }
            JsonValue value;
            if (!Value(value, 0)) {
              return Fail("malformed value", error);
            }
            field(key, value);
            SkipSpace();
            if (Peek() == ',') {
              ++mPos;
              continue;
            }
            if (Peek() == '}') {
              ++mPos;
              break;
            }
            return Fail("expected ',' or '}'", error);
          }
        }
        SkipSpace();
        if (mPos != mEnd) {
          return Fail("extra data after the JSON object", error);
        }
        return true;
      }

    private:
      bool Fail(const char* what, std::string& error)
      {
        error = what;
        return false;
      }

      char Peek() const { return mPos < mEnd ? *mPos : '\0'; }

      bool Expect(const char c)
      {
        if (Peek() != c) {
          return false;
        }
        ++mPos;
        return true;
      }

      void SkipSpace()
      {
        while (mPos < mEnd && (*mPos == ' ' || *mPos == '\t' || *mPos == '\r' || *mPos == '\n')) {
          ++mPos;
        }
      }

      static void AppendUtf8(std::string& out, uint32_t cp)
      {
        if (cp < 0x80) {
          out += static_cast<char>(cp);
        } else if (cp < 0x800) {
          out += static_cast<char>(0xC0 | (cp >> 6));
          out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
          out += static_cast<char>(0xE0 | (cp >> 12));
          out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
          out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
          out += static_cast<char>(0xF0 | (cp >> 18));
          out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
          out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
          out += static_cast<char>(0x80 | (cp & 0x3F));
        }
      }

      bool Hex4(uint32_t& out)
      {
        if (mEnd - mPos < 4) {
          return false;
        }
        out = 0;
        for (int i = 0; i < 4; ++i) {
          const char c = *mPos++;
          out <<= 4;
          if (c >= '0' && c <= '9') {
            out |= static_cast<uint32_t>(c - '0');
          } else if (c >= 'a' && c <= 'f') {
            out |= static_cast<uint32_t>(c - 'a' + 10);
          } else if (c >= 'A' && c <= 'F') {
            out |= static_cast<uint32_t>(c - 'A' + 10);
          } else {
            return false;
          }
        }
        return true;
      }

      bool String(std::string& out)
      {
        if (!Expect('"')) {
          return false;
        }
        while (mPos < mEnd) {
          const char c = *mPos++;
          if (c == '"') {
            return true;
          }
          if (c != '\\') {
            out += c;
            continue;
          }
          if (mPos >= mEnd) {
            return false;
          }
          const char e = *mPos++;
          switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
              uint32_t cp = 0;
              if (!Hex4(cp)) {
                return false;
              }
              if (cp >= 0xD800 && cp < 0xDC00 && mEnd - mPos >= 6 && mPos[0] == '\\' && mPos[1] == 'u') {
                const char* const save = mPos;
                mPos += 2;
                uint32_t low = 0;
                if (Hex4(low) && low >= 0xDC00 && low < 0xE000) {
                  cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                } else {
                  mPos = save;
                }
              }
              AppendUtf8(out, (cp >= 0xD800 && cp < 0xE000) ? 0xFFFDu : cp);
              break;
            }
            default:
              return false;
          }
        }
        return false;
      }

      bool Number(JsonValue& value)
      {
        const char* const start = mPos;
        if (Peek() == '-') {
          ++mPos;
        }
        while (mPos < mEnd && ((*mPos >= '0' && *mPos <= '9') || *mPos == '.' || *mPos == 'e' || *mPos == 'E' ||
                               *mPos == '+' || *mPos == '-')) {
          ++mPos;
        }
        if (mPos == start) {
          return false;
        }
        value.kind = JsonValue::kNumber;
        value.text.assign(start, static_cast<size_t>(mPos - start));
        char* end = nullptr;
        value.number = std::strtod(value.text.c_str(), &end);
        return end != nullptr && *end == '\0';
      }

      bool Literal(const char* word)
      {
        const size_t n = std::strlen(word);
        if (static_cast<size_t>(mEnd - mPos) < n || std::memcmp(mPos, word, n) != 0) {
          return false;
        }
        mPos += n;
        return true;
      }

      bool Value(JsonValue& value, const int depth)
      {
        if (depth > 64) {
          return false;
        }
        SkipSpace();
        const char c = Peek();
        if (c == '"') {
          value.kind = JsonValue::kString;
          return String(value.text);
        }
        if (c == '{' || c == '[') {
          const char close = c == '{' ? '}' : ']';
          ++mPos;
          value.kind = JsonValue::kOther;
          SkipSpace();
          if (Peek() == close) {
            ++mPos;
            return true;
          }
          for (;;) {
            SkipSpace();
            if (c == '{') {
              std::string key;
              if (Peek() != '"' || !String(key)) {
                return false;
              }
              SkipSpace();
              if (!Expect(':')) {
                return false;
              }
            }
            JsonValue inner;
            if (!Value(inner, depth + 1)) {
              return false;
            }
            SkipSpace();
            if (Peek() == ',') {
              ++mPos;
              continue;
            }
            return Expect(close);
          }
        }
        if (Literal("true")) {
          value.kind = JsonValue::kBool;
          value.boolean = true;
          return true;
        }
        if (Literal("false")) {
          value.kind = JsonValue::kBool;
          value.boolean = false;
          return true;
        }
        if (Literal("null")) {
          value.kind = JsonValue::kNull;
          return true;
        }
        if (Literal("NaN") || Literal("Infinity") || Literal("-Infinity")) {  // Python's json accepts them
          value.kind = JsonValue::kOther;
          return true;
        }
        return Number(value);
      }

      const char* mPos;
      const char* mEnd;
    };

    // ----------------------------------------------------------------------------------------------
    // Bodies
    // ----------------------------------------------------------------------------------------------

    // base64 with Python's binascii.a2b_base64 semantics in its default (non-strict) mode, which
    // convert_replay.py's base64.b64decode uses: characters outside the alphabet are skipped, a
    // complete padding sequence ends the input, a dangling partial quad is an error.
    bool Base64DecodePython(const uint8_t* text, const size_t size, std::vector<uint8_t>& out, std::string& error)
    {
      static int8_t table[256];
      static bool ready = false;
      if (!ready) {
        std::memset(table, -1, sizeof(table));
        const char* const alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < 64; ++i) {
          table[static_cast<uint8_t>(alphabet[i])] = static_cast<int8_t>(i);
        }
        ready = true;
      }
      out.clear();
      out.reserve(size / 4 * 3 + 3);
      int quad = 0;
      int pads = 0;
      uint32_t left = 0;
      for (size_t i = 0; i < size; ++i) {
        const uint8_t c = text[i];
        if (c == '=') {
          if (quad >= 2 && quad + ++pads >= 4) {
            return true;  // the padding ends the data
          }
          continue;
        }
        const int v = table[c];
        if (v < 0) {
          continue;
        }
        pads = 0;
        switch (quad) {
          case 0:
            quad = 1;
            left = static_cast<uint32_t>(v);
            break;
          case 1:
            quad = 2;
            out.push_back(static_cast<uint8_t>((left << 2) | (static_cast<uint32_t>(v) >> 4)));
            left = static_cast<uint32_t>(v) & 0x0Fu;
            break;
          case 2:
            quad = 3;
            out.push_back(static_cast<uint8_t>((left << 4) | (static_cast<uint32_t>(v) >> 2)));
            left = static_cast<uint32_t>(v) & 0x03u;
            break;
          default:
            quad = 0;
            out.push_back(static_cast<uint8_t>((left << 6) | static_cast<uint32_t>(v)));
            left = 0;
            break;
        }
      }
      if (quad != 0) {
        error = quad == 1 ? "base64: one character more than a multiple of 4" : "base64: incorrect padding";
        return false;
      }
      return true;
    }

    // qCompress: 4-byte big-endian length, then a zlib stream (zlib.decompress: the first stream,
    // anything after it ignored; the length must match).
    bool DecodeLegacy(const uint8_t* body, const size_t size, ReplayInput& in)
    {
      std::vector<uint8_t> blob;
      if (!Base64DecodePython(body, size, blob, in.error)) {
        return false;
      }
      if (blob.size() < 4) {
        in.error = "legacy body: shorter than its 4-byte length";
        return false;
      }
      const uint32_t expected = (static_cast<uint32_t>(blob[0]) << 24) | (static_cast<uint32_t>(blob[1]) << 16) |
                                (static_cast<uint32_t>(blob[2]) << 8) | static_cast<uint32_t>(blob[3]);
      if (expected > kMaxDecoded) {
        in.error = "legacy body: decoded length " + std::to_string(expected) + " is over the 256 MB cap";
        return false;
      }
      z_stream z{};
      if (inflateInit(&z) != Z_OK) {
        in.error = "zlib: inflateInit failed";
        return false;
      }
      in.data.assign(static_cast<size_t>(expected) + 1u, 0);  // one spare byte to notice a longer stream
      z.next_in = blob.data() + 4;
      z.avail_in = static_cast<uInt>(blob.size() - 4);
      size_t produced = 0;
      int rc = Z_OK;
      for (;;) {
        if (produced == in.data.size()) {
          if (in.data.size() >= kMaxDecoded) {
            break;
          }
          in.data.resize(in.data.size() * 2 < kMaxDecoded ? in.data.size() * 2 : kMaxDecoded);
        }
        z.next_out = in.data.data() + produced;
        z.avail_out = static_cast<uInt>(in.data.size() - produced);
        rc = inflate(&z, Z_NO_FLUSH);
        produced = in.data.size() - z.avail_out;
        if (rc == Z_STREAM_END || (rc != Z_OK && rc != Z_BUF_ERROR)) {
          break;
        }
        if (rc == Z_BUF_ERROR && z.avail_in == 0) {
          break;  // input ended inside the stream
        }
      }
      in.trailingBytes = z.avail_in;
      const char* const message = z.msg;
      inflateEnd(&z);
      if (rc != Z_STREAM_END) {
        in.error = std::string("zlib: ") + (rc == Z_BUF_ERROR || rc == Z_OK ? "incomplete or truncated stream"
                                                                            : (message != nullptr ? message : "error ") +
                                                                                std::to_string(rc));
        return false;
      }
      in.data.resize(produced);
      if (produced != expected) {
        in.error = "legacy body: decoded " + std::to_string(produced) + " bytes, its header says " +
                   std::to_string(expected);
        return false;
      }
      return true;
    }

    bool IsZstdFrameStart(const uint8_t* p, const size_t size)
    {
      if (size < 4) {
        return false;
      }
      const uint32_t magic = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                             (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
      return magic == ZSTD_MAGICNUMBER || (magic & ZSTD_MAGIC_SKIPPABLE_MASK) == ZSTD_MAGIC_SKIPPABLE_START;
    }

    // One or more zstd frames, streamed (vault frames carry no content size). Like the script's
    // streaming decompressor, a frame cut short yields what it holds; bytes after the last frame
    // that do not start another frame are ignored.
    bool DecodeZstd(const uint8_t* body, const size_t size, ReplayInput& in)
    {
      ZSTD_DCtx* const dctx = ZSTD_createDCtx();
      if (dctx == nullptr) {
        in.error = "zstd: cannot create a decompression context";
        return false;
      }
      in.data.assign(size_t{1} << 20, 0);
      ZSTD_inBuffer input{body, size, 0};
      size_t produced = 0;
      bool ok = true;
      bool inFrame = false;
      while (input.pos < input.size) {
        if (!inFrame) {
          if (!IsZstdFrameStart(body + input.pos, input.size - input.pos)) {
            if (in.zstdFrames == 0) {
              in.error = "zstd: the body does not start with a zstd frame";
              ok = false;
            } else {
              in.trailingBytes = input.size - input.pos;
            }
            break;
          }
          inFrame = true;
        }
        if (produced == in.data.size()) {
          if (in.data.size() >= kMaxDecoded) {
            in.error = "zstd: decoded data over the 256 MB cap";
            ok = false;
            break;
          }
          in.data.resize(in.data.size() * 2 < kMaxDecoded ? in.data.size() * 2 : kMaxDecoded);
        }
        ZSTD_outBuffer output{in.data.data(), in.data.size(), produced};
        const size_t rc = ZSTD_decompressStream(dctx, &output, &input);
        produced = output.pos;
        if (ZSTD_isError(rc)) {
          in.error = std::string("zstd: ") + ZSTD_getErrorName(rc);
          ok = false;
          break;
        }
        if (rc == 0) {
          inFrame = false;
          ++in.zstdFrames;
        } else if (input.pos == input.size && output.pos < output.size) {
          in.truncated = true;  // the input ended inside a frame and everything decodable is out
          ++in.zstdFrames;
          break;
        }
      }
      ZSTD_freeDCtx(dctx);
      in.data.resize(produced);
      return ok;
    }

    // ----------------------------------------------------------------------------------------------
    // The decoded body's header and message stream (HeadlessReplay.cpp ScanReplay, same rules)
    // ----------------------------------------------------------------------------------------------

    class ByteReader
    {
    public:
      explicit ByteReader(const std::vector<uint8_t>& bytes)
        : mBytes(bytes)
      {}

      bool CString(std::string& out)
      {
        const size_t start = mPos;
        while (mPos < mBytes.size() && mBytes[mPos] != 0) {
          ++mPos;
        }
        if (mPos >= mBytes.size()) {
          return false;
        }
        out.assign(reinterpret_cast<const char*>(mBytes.data() + start), mPos - start);
        ++mPos;
        return true;
      }

      bool Raw(const size_t count, const uint8_t*& out)
      {
        if (mBytes.size() - mPos < count) {
          return false;
        }
        out = mBytes.data() + mPos;
        mPos += count;
        return true;
      }

      bool U8(uint8_t& out)
      {
        const uint8_t* raw = nullptr;
        if (!Raw(1, raw)) {
          return false;
        }
        out = raw[0];
        return true;
      }

      bool U32(uint32_t& out)
      {
        const uint8_t* raw = nullptr;
        if (!Raw(4, raw)) {
          return false;
        }
        out = static_cast<uint32_t>(raw[0]) | (static_cast<uint32_t>(raw[1]) << 8) |
              (static_cast<uint32_t>(raw[2]) << 16) | (static_cast<uint32_t>(raw[3]) << 24);
        return true;
      }

      size_t Pos() const { return mPos; }
      size_t Size() const { return mBytes.size(); }
      const uint8_t* At(const size_t pos) const { return mBytes.data() + pos; }
      void Skip(const size_t count) { mPos += count; }

    private:
      const std::vector<uint8_t>& mBytes;
      size_t mPos = 0;
    };

    int32_t ReadLe32(const uint8_t* p)
    {
      return static_cast<int32_t>(static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                                  (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24));
    }

    void Scan(const std::vector<uint8_t>& bytes, ReplayScan& scan)
    {
      constexpr uint8_t kOpAdvance = 0;
      constexpr uint8_t kOpSetCommandSource = 1;
      constexpr uint8_t kOpVerifyChecksum = 3;
      constexpr uint8_t kOpEndGame = 23;
      ByteReader reader(bytes);
      std::string ignored;
      const uint8_t* raw = nullptr;
      uint32_t length = 0;
      uint8_t count = 0;
      if (!reader.CString(scan.version) || !reader.CString(ignored) || !reader.Raw(13, raw) ||
          std::memcmp(raw, "Replay v1.9\r\n", 13) != 0 || !reader.CString(scan.mapPath) || !reader.CString(ignored) ||
          !reader.U32(length) || !reader.Raw(length, raw) || !reader.U32(length) || !reader.Raw(length, raw) ||
          !reader.U8(count)) {
        scan.error = "truncated or unrecognised replay header";
        return;
      }
      scan.commandSources = count;
      for (uint8_t i = 0; i < count; ++i) {
        std::string name;
        uint32_t timeouts = 0;
        if (!reader.CString(name) || !reader.U32(timeouts)) {
          scan.error = "truncated command-source list";
          return;
        }
      }
      uint8_t cheats = 0;
      uint8_t armies = 0;
      if (!reader.U8(cheats) || !reader.U8(armies)) {
        scan.error = "truncated army list";
        return;
      }
      scan.armies = armies;
      for (uint8_t i = 0; i < armies; ++i) {
        if (!reader.U32(length) || !reader.Raw(length, raw)) {
          scan.error = "truncated army list";
          return;
        }
        uint8_t source = 0;
        do {
          if (!reader.U8(source)) {
            scan.error = "truncated army list";
            return;
          }
        } while (source != 0xFFu);
      }
      if (!reader.U32(scan.seed)) {
        scan.error = "truncated header";
        return;
      }
      std::vector<int32_t> checksumBeats;
      int beats = 0;
      while (reader.Size() - reader.Pos() >= 3) {
        const uint8_t* const head = reader.At(reader.Pos());
        const uint8_t type = head[0];
        const size_t size = static_cast<size_t>(head[1]) | (static_cast<size_t>(head[2]) << 8);
        if (size < 3 || reader.Size() - reader.Pos() < size) {
          break;  // a truncated tail; the engine stops reading there too
        }
        const uint8_t* const payload = head + 3;
        const size_t payloadSize = size - 3;
        ++scan.messages;
        if (type == kOpAdvance && payloadSize >= 4) {
          beats += ReadLe32(payload);
        } else if (type == kOpSetCommandSource && payloadSize >= 1) {
          // the source of the following messages; not needed here
        } else if (type == kOpVerifyChecksum && payloadSize >= 20) {
          ++scan.verifyChecksums;
          checksumBeats.push_back(ReadLe32(payload + 16));
        } else if (type == kOpEndGame) {
          scan.hasEndGame = true;
        }
        reader.Skip(size);
      }
      scan.beats = beats;
      std::sort(checksumBeats.begin(), checksumBeats.end());
      scan.checksumBeats = static_cast<int>(std::unique(checksumBeats.begin(), checksumBeats.end()) - checksumBeats.begin());
    }

    bool ReadWholeFile(const char* path, std::vector<uint8_t>& out, std::string& error)
    {
      const int fd = open(path, O_RDONLY | O_CLOEXEC);
      if (fd < 0) {
        error = std::string("cannot open: ") + std::strerror(errno);
        return false;
      }
      out.clear();
      uint8_t chunk[65536];
      for (;;) {
        const ssize_t got = read(fd, chunk, sizeof(chunk));
        if (got < 0) {
          if (errno == EINTR) {
            continue;
          }
          error = std::string("read failed: ") + std::strerror(errno);
          close(fd);
          return false;
        }
        if (got == 0) {
          break;
        }
        if (out.size() + static_cast<size_t>(got) > kMaxInput) {
          error = "file over the 64 MB cap";
          close(fd);
          return false;
        }
        out.insert(out.end(), chunk, chunk + got);
      }
      close(fd);
      return true;
    }

    void AppendNumber(std::string& out, const double value)
    {
      char text[64];
      if (std::isfinite(value) && value == std::floor(value) && std::fabs(value) < 1e15) {
        std::snprintf(text, sizeof(text), "%.0f", value);
      } else {
        std::snprintf(text, sizeof(text), "%.3f", value);
      }
      out += text;
    }

    std::string MapDirOf(const std::string& mapPath)
    {
      // "/maps/<dir>/<file>.scmap", any case
      if (mapPath.size() < 7) {
        return std::string();
      }
      std::string lower = mapPath.substr(0, 6);
      for (char& c : lower) {
        c = static_cast<char>((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c);
      }
      if (lower != "/maps/") {
        return std::string();
      }
      const size_t slash = mapPath.find('/', 6);
      return slash == std::string::npos ? std::string() : mapPath.substr(6, slash - 6);
    }
  } // namespace

  std::string Sha256Hex(const uint8_t* data, const size_t size)
  {
    Sha256 hash;
    hash.Update(data, size);
    return hash.HexDigest();
  }

  std::string JsonString(const std::string& text)
  {
    std::string out = "\"";
    for (const char raw : text) {
      const unsigned char c = static_cast<unsigned char>(raw);
      switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
          if (c < 0x20 || c >= 0x7F) {
            // Bytes, not characters: the replay's strings are Latin-1 or UTF-8; \u00XX keeps the
            // output valid JSON either way.
            char escaped[8];
            std::snprintf(escaped, sizeof(escaped), "\\u%04x", c);
            out += escaped;
          } else {
            out += static_cast<char>(c);
          }
      }
    }
    out += '"';
    return out;
  }

  bool IsEngineReadyReplay(const char* path)
  {
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
      return false;
    }
    static const char kReady[] = "Supreme Commander v1.50.3764";  // with its NUL: 29 bytes
    char head[sizeof(kReady)] = {};
    size_t got = 0;
    while (got < sizeof(head)) {
      const ssize_t n = read(fd, head + got, sizeof(head) - got);
      if (n < 0 && errno == EINTR) {
        continue;
      }
      if (n <= 0) {
        break;
      }
      got += static_cast<size_t>(n);
    }
    close(fd);
    return got == sizeof(head) && std::memcmp(head, kReady, sizeof(kReady)) == 0;
  }

  bool ReadReplay(const char* path, const char* asVersion, ReplayInput& in)
  {
    in = ReplayInput{};
    in.path = path;
    std::vector<uint8_t> file;
    if (!ReadWholeFile(path, file, in.error)) {
      return false;
    }
    in.fileSize = file.size();
    in.sha256 = Sha256Hex(file.data(), file.size());

    static const char kRaw[] = "Supreme Commander v";
    if (file.size() >= sizeof(kRaw) - 1 && std::memcmp(file.data(), kRaw, sizeof(kRaw) - 1) == 0) {
      in.format = "scfareplay";
      in.data.swap(file);
    } else {
      const uint8_t* const newline = static_cast<const uint8_t*>(std::memchr(file.data(), '\n', file.size()));
      if (newline == nullptr || static_cast<size_t>(newline - file.data()) > kMaxJsonLine) {
        in.error = "not a replay: no JSON line within 1 MB and no \"Supreme Commander\" header";
        return false;
      }
      const char* const jsonBegin = reinterpret_cast<const char*>(file.data());
      const char* const jsonEnd = reinterpret_cast<const char*>(newline);
      JsonParser parser(jsonBegin, jsonEnd);
      double launched = 0.0, ended = 0.0;
      bool hasLaunched = false, hasEnded = false;
      in.hasJson = parser.TopObject(
        [&](const std::string& key, const JsonValue& value) {
          if (key == "uid") {
            // Converting a double outside long long's range (or NaN) is undefined, and so is atoll on
            // an overflow: such a uid is ignored (FAF's are 8 digits).
            if (value.kind == JsonValue::kNumber) {
              if (std::isfinite(value.number) && value.number >= 0.0 && value.number < 1e18) {
                in.hasId = true;
                in.id = static_cast<long long>(value.number);
              }
            } else if (value.kind == JsonValue::kString && !value.text.empty() && value.text.size() <= 18 &&
                       value.text.find_first_not_of("0123456789") == std::string::npos) {
              in.hasId = true;
              in.id = std::strtoll(value.text.c_str(), nullptr, 10);
            }
          } else if (key == "mapname" && value.kind == JsonValue::kString) {
            in.mapName = value.text;
          } else if (key == "featured_mod" && value.kind == JsonValue::kString) {
            in.featuredMod = value.text;
          } else if (key == "num_players" && value.kind == JsonValue::kNumber) {
            if (std::isfinite(value.number) && value.number >= 0.0 && value.number <= 1e6) {
              in.hasPlayers = true;
              in.players = static_cast<int>(value.number);
            }
          } else if (key == "launched_at" && value.kind == JsonValue::kNumber && std::isfinite(value.number)) {
            hasLaunched = true;
            launched = value.number;
          } else if (key == "game_end" && value.kind == JsonValue::kNumber && std::isfinite(value.number)) {
            hasEnded = true;
            ended = value.number;
          } else if (key == "complete" && value.kind == JsonValue::kBool) {
            in.hasComplete = true;
            in.complete = value.boolean;
          } else if (key == "compression") {
            in.compression = value.kind == JsonValue::kString ? value.text : std::string();
          }
        },
        in.jsonError);
      if (hasLaunched && hasEnded && ended >= launched && std::isfinite(ended - launched)) {
        in.hasGameTime = true;
        in.gameTimeSeconds = ended - launched;
      }
      const uint8_t* const body = newline + 1;
      const size_t bodySize = file.size() - static_cast<size_t>(body - file.data());
      // convert_replay.py: zstd when the header says so or the body starts with the frame magic.
      const bool zstd = in.compression == "zstd" ||
                        (bodySize >= 4 && body[0] == 0x28 && body[1] == 0xB5 && body[2] == 0x2F && body[3] == 0xFD);
      if (!in.hasJson && !zstd) {
        // Neither a JSON line nor a zstd body: some other file (the script rejects it too).
        in.error = "not a replay: the first line is not JSON (" + in.jsonError + ") and the file is no .scfareplay";
        return false;
      }
      in.format = zstd ? "fafreplay-zstd" : "fafreplay-legacy";
      if (!(zstd ? DecodeZstd(body, bodySize, in) : DecodeLegacy(body, bodySize, in))) {
        in.data.clear();
        return false;
      }
    }

    in.decodedSha256 = Sha256Hex(in.data.data(), in.data.size());
    const uint8_t* const nul = static_cast<const uint8_t*>(std::memchr(in.data.data(), 0, in.data.size()));
    if (nul != nullptr) {
      in.recordedVersion.assign(reinterpret_cast<const char*>(in.data.data()), static_cast<size_t>(nul - in.data.data()));
    }
    Scan(in.data, in.scan);

    if (asVersion != nullptr) {
      const size_t digits = std::strlen(asVersion);
      if (nul == nullptr) {
        in.error = "the decoded replay has no NUL-terminated version string";
        return false;
      }
      if (in.recordedVersion.compare(0, kVersionPrefixLength, kVersionPrefix) != 0 ||
          in.recordedVersion.size() != kVersionPrefixLength + digits) {
        in.error = "the decoded replay's version string is not \"Supreme Commander v1.50.\" plus " +
                   std::to_string(digits) + " characters";
        return false;
      }
      if (std::memcmp(in.data.data() + kVersionPrefixLength, asVersion, digits) != 0) {
        std::memcpy(in.data.data() + kVersionPrefixLength, asVersion, digits);
        in.versionRewritten = true;
      }
      in.convertedSha256 = Sha256Hex(in.data.data(), in.data.size());
    }
    return true;
  }

  std::string ReplayInfoJson(const ReplayInput& in, const char* outputPath)
  {
    std::string j = "{\"ok\":";
    j += in.error.empty() ? "true" : "false";
    if (!in.error.empty()) {
      j += ",\"error\":" + JsonString(in.error);
    }
    j += ",\"file\":" + JsonString(in.path);
    if (outputPath != nullptr) {
      j += ",\"output\":" + JsonString(outputPath);
    }
    j += ",\"format\":" + (in.format.empty() ? std::string("null") : JsonString(in.format));
    j += ",\"size\":" + std::to_string(in.fileSize);
    j += ",\"sha256\":" + (in.sha256.empty() ? std::string("null") : JsonString(in.sha256));
    j += ",\"id\":" + (in.hasId ? std::to_string(in.id) : std::string("null"));
    // An empty "mapname" (seen in coop replays) falls back to the map folder of the body's map path.
    std::string map = in.mapName;
    if (map.empty() && (in.error.empty() || !in.data.empty())) {
      map = MapDirOf(in.scan.mapPath);
    }
    j += ",\"map\":" + (map.empty() ? std::string("null") : JsonString(map));
    j += ",\"featured_mod\":" + (in.featuredMod.empty() ? std::string("null") : JsonString(in.featuredMod));
    // The featured mod's version is the game version the replay was recorded with: the digits of
    // "Supreme Commander v1.50.NNNN" (FAF's game version; featured_mod_versions in the JSON lists
    // file ids, not the game version).
    std::string versionDigits;
    if (in.recordedVersion.size() > kVersionPrefixLength &&
        in.recordedVersion.compare(0, kVersionPrefixLength, kVersionPrefix) == 0) {
      versionDigits = in.recordedVersion.substr(kVersionPrefixLength);
    }
    const bool numeric = !versionDigits.empty() && versionDigits.size() <= 9 &&
                         versionDigits.find_first_not_of("0123456789") == std::string::npos;
    j += ",\"featured_mod_version\":" + (numeric ? versionDigits : std::string("null"));
    j += ",\"players\":" + (in.hasPlayers ? std::to_string(in.players) : std::string("null"));
    j += ",\"game_time_seconds\":";
    if (in.hasGameTime) {
      AppendNumber(j, in.gameTimeSeconds);
    } else {
      j += "null";
    }
    j += ",\"complete\":" + std::string(in.hasComplete ? (in.complete ? "true" : "false") : "null");
    if (!in.jsonError.empty()) {
      j += ",\"json_error\":" + JsonString(in.jsonError);
    }
    j += ",\"header_version\":" + (in.recordedVersion.empty() ? std::string("null") : JsonString(in.recordedVersion));
    j += ",\"decoded_size\":" + std::to_string(in.data.size());
    j += ",\"decoded_sha256\":" + (in.decodedSha256.empty() ? std::string("null") : JsonString(in.decodedSha256));
    j += ",\"converted_sha256\":" + (in.convertedSha256.empty() ? std::string("null") : JsonString(in.convertedSha256));
    j += ",\"version_rewritten\":" + std::string(in.versionRewritten ? "true" : "false");
    if (in.zstdFrames != 0) {
      j += ",\"zstd_frames\":" + std::to_string(in.zstdFrames);
    }
    if (in.truncated) {
      j += ",\"truncated\":true";
    }
    if (in.trailingBytes != 0) {
      j += ",\"trailing_bytes\":" + std::to_string(in.trailingBytes);
    }
    if (in.error.empty() || !in.data.empty()) {
      const ReplayScan& s = in.scan;
      j += ",\"map_path\":" + (s.mapPath.empty() ? std::string("null") : JsonString(s.mapPath));
      const std::string dir = MapDirOf(s.mapPath);
      j += ",\"map_dir\":" + (dir.empty() ? std::string("null") : JsonString(dir));
      j += ",\"armies\":" + std::to_string(s.armies);
      j += ",\"command_sources\":" + std::to_string(s.commandSources);
      j += ",\"beats\":" + std::to_string(s.beats);
      j += ",\"sim_seconds\":";
      AppendNumber(j, s.beats / 10.0);
      j += ",\"has_end_game\":" + std::string(s.hasEndGame ? "true" : "false");
      j += ",\"recorded_checksums\":" + std::to_string(s.verifyChecksums);
      j += ",\"checksum_beats\":" + std::to_string(s.checksumBeats);
      j += ",\"scan_error\":" + (s.error.empty() ? std::string("null") : JsonString(s.error));
    }
    j += "}";
    return j;
  }

  bool WriteFileAtomic(const std::string& path, const std::vector<uint8_t>& data, std::string& error)
  {
    const std::string temp = path + ".tmp";
    const int fd = open(temp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
      error = "cannot create " + temp + ": " + std::strerror(errno);
      return false;
    }
    size_t done = 0;
    while (done < data.size()) {
      const ssize_t n = write(fd, data.data() + done, data.size() - done);
      if (n < 0) {
        if (errno == EINTR) {
          continue;
        }
        error = "cannot write " + temp + ": " + std::strerror(errno);
        close(fd);
        unlink(temp.c_str());
        return false;
      }
      done += static_cast<size_t>(n);
    }
    if (fsync(fd) != 0 || close(fd) != 0) {
      error = "cannot finish " + temp + ": " + std::strerror(errno);
      unlink(temp.c_str());
      return false;
    }
    if (rename(temp.c_str(), path.c_str()) != 0) {
      error = "cannot rename " + temp + " to " + path + ": " + std::strerror(errno);
      unlink(temp.c_str());
      return false;
    }
    return true;
  }
} // namespace faf_runner
