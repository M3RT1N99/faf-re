// faf_movie_tests: decodes real movies to the end with the engine's FfmpegSfdDecoder and prints one
// line per movie, for comparing the ABIs and for seeing what a movie costs. A CRC is over all frames'
// pixels, another over all audio, so a decode that differs anywhere shows.
//
//   faf_movie_tests [--frame N out.raw] movie.sfd ...
//
// --frame writes frame N of the first movie that has one, BGRA, top row first. Exits non-zero when a
// file fails, has no frames or a frame has a wrong size, and when the frame asked for does not exist.

#include "moho/movie/sfd/FfmpegSfdDecoder.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>
#include <sys/stat.h>

namespace
{
  // CRC-32 (IEEE, reflected), byte by byte: the point is that it is the same everywhere.
  class Crc32
  {
  public:
    void Add(
      const void* data,
      std::size_t size
    )
    {
      static const auto table = MakeTable();
      const auto* bytes = static_cast<const std::uint8_t*>(data);
      for (std::size_t i = 0; i < size; ++i)
        state = table.values[(state ^ bytes[i]) & 0xffu] ^ (state >> 8);
    }
    std::uint32_t Value() const
    {
      return ~state;
    }

  private:
    struct Table
    {
      std::uint32_t values[256];
    };
    static Table MakeTable()
    {
      Table table{};
      for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = i;
        for (int bit = 0; bit < 8; ++bit)
          c = (c & 1u) ? 0xedb88320u ^ (c >> 1) : c >> 1;
        table.values[i] = c;
      }
      return table;
    }
    std::uint32_t state{0xffffffffu};
  };

  // The peak of this process so far, so a line's figure is the highest of all movies up to it.
  long PeakKilobytes()
  {
    std::FILE* status = std::fopen("/proc/self/status", "r");
    if (!status)
      return -1;
    char line[256];
    long peak = -1;
    while (std::fgets(line, sizeof(line), status)) {
      if (std::strncmp(line, "VmHWM:", 6) == 0) {
        peak = std::strtol(line + 6, nullptr, 10);
        break;
      }
    }
    std::fclose(status);
    return peak;
  }

  void WriteFrame(
    const moho::sfd::DecodedVideoFrame& frame,
    const char* path
  )
  {
    std::FILE* out = std::fopen(path, "wb");
    if (!out)
      throw std::runtime_error(std::string("cannot create ") + path);
    const bool written = std::fwrite(frame.bgra.data(), 1, frame.bgra.size(), out) == frame.bgra.size();
    if (std::fclose(out) != 0 || !written)
      throw std::runtime_error(std::string("cannot write ") + path);
    std::printf("frame %dx%d written to %s\n", frame.width, frame.height, path);
  }

  struct FrameRequest
  {
    long index{-1};
    const char* path{};
    bool done{};
  };

  void DecodeMovie(
    const char* filename,
    FrameRequest& request
  )
  {
    const auto start = std::chrono::steady_clock::now();
    struct stat file{};
    if (stat(filename, &file) != 0)
      throw std::runtime_error("cannot stat the file");

    moho::sfd::FfmpegDecoder decoder;
    decoder.Open(filename, true);
    const int width = decoder.Width();
    const int height = decoder.Height();

    long frames = 0;
    std::size_t samples = 0;
    Crc32 videoCrc;
    Crc32 audioCrc;
    moho::sfd::DecodeBatch batch;
    while (decoder.Read(batch)) {
      for (const auto& frame : batch.video) {
        if (frame->width != width || frame->height != height || frame->pitch != width * 4 ||
            frame->bgra.size() != static_cast<std::size_t>(frame->pitch) * frame->height) {
          throw std::runtime_error(
            "frame " + std::to_string(frames) + " has a wrong size: " + std::to_string(frame->width) + "x" +
            std::to_string(frame->height) + " pitch " + std::to_string(frame->pitch) + ", " +
            std::to_string(frame->bgra.size()) + " bytes, the movie is " + std::to_string(width) + "x" +
            std::to_string(height)
          );
        }
        videoCrc.Add(frame->bgra.data(), frame->bgra.size());
        if (!request.done && frames == request.index) {
          WriteFrame(*frame, request.path);
          request.done = true;
        }
        ++frames;
      }
      for (const auto& audio : batch.audio) {
        audioCrc.Add(audio.samples.data(), audio.samples.size() * sizeof(audio.samples[0]));
        samples += audio.samples.size() / 2;
      }
    }
    if (frames == 0)
      throw std::runtime_error("no video frames");

    const auto wall =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    std::printf(
      "OK %s bytes=%lld size=%dx%d frames=%ld audio=%dHz/%dch samples=%zu video_crc=%08x audio_crc=%08x wall_ms=%lld "
      "peak_kb=%ld\n",
      filename,
      static_cast<long long>(file.st_size),
      width,
      height,
      frames,
      decoder.AudioSampleRate(),
      decoder.AudioChannels(),
      samples,
      videoCrc.Value(),
      audioCrc.Value(),
      static_cast<long long>(wall),
      PeakKilobytes()
    );
    std::fflush(stdout);
  }
} // namespace

int main(
  int argc,
  char** argv
)
{
  FrameRequest request;
  int first = 1;
  if (argc >= 4 && std::strcmp(argv[1], "--frame") == 0) {
    char* end = nullptr;
    request.index = std::strtol(argv[2], &end, 10);
    if (*end != '\0' || request.index < 0) {
      std::fprintf(stderr, "faf_movie_tests: bad frame number %s\n", argv[2]);
      return 2;
    }
    request.path = argv[3];
    first = 4;
  }
  if (first >= argc) {
    std::fprintf(stderr, "usage: faf_movie_tests [--frame N out.raw] movie.sfd ...\n");
    return 2;
  }

  int failures = 0;
  for (int i = first; i < argc; ++i) {
    try {
      DecodeMovie(argv[i], request);
    } catch (const std::exception& e) {
      std::printf("FAIL %s: %s\n", argv[i], e.what());
      std::fflush(stdout);
      ++failures;
    }
  }
  if (request.path && !request.done) {
    std::printf("FAIL no movie has frame %ld\n", request.index);
    ++failures;
  }
  return failures == 0 ? 0 : 1;
}
