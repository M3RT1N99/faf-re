// The movie decoder (moho/movie/sfd/FfmpegSfdDecoder.h) for the Android client build, in place of
// FfmpegSfdDecoder.cpp: FFmpeg is not built for Android yet (its configuration in
// dependencies/ffmpeg/faf is generated for clang-cl; CLIENT_PLAN.md, C3). Open fails the way the
// Windows decoder fails without a decoder for the stream, and the movie player skips the movie.

#include "moho/movie/sfd/FfmpegSfdDecoder.h"

#include <stdexcept>

namespace moho::sfd
{
  struct FfmpegDecoder::Impl
  {
  };

  FfmpegDecoder::FfmpegDecoder()
    : impl(std::make_unique<Impl>())
  {}

  FfmpegDecoder::~FfmpegDecoder() = default;

  void FfmpegDecoder::Open(const char*, bool)
  {
    throw std::runtime_error("no FFmpeg on Android yet (android/CLIENT_PLAN.md, C3)");
  }

  bool FfmpegDecoder::Read(DecodeBatch&)
  {
    return false;
  }

  bool FfmpegDecoder::HasAudio() const
  {
    return false;
  }

  int FfmpegDecoder::Width() const
  {
    return 0;
  }

  int FfmpegDecoder::Height() const
  {
    return 0;
  }

  int FfmpegDecoder::AudioSampleRate() const
  {
    return 0;
  }

  int FfmpegDecoder::AudioChannels() const
  {
    return 0;
  }
} // namespace moho::sfd
