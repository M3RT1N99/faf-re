#pragma once

// DDS decoding on the CPU. SCFA and FAF ship DXT1/3/5 (BC1-3) textures, which
// Mali, PowerVR and Xclipse GPUs cannot sample (textureCompressionBC = false),
// so the bring-up decodes them to RGBA8. Transcoding to ETC2 at import time is
// the planned follow-up (docs/port/android-roadmap.md).

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace faf::port {

  struct Image
  {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba; ///< width * height * 4 bytes, top row first.
    std::string format;             ///< "DXT1", "DXT3", "DXT5", "ARGB32", "RGB24", ...
  };

  /// Decodes the top mip of a DDS (first face of a cubemap). Supports DXT1/3/5
  /// and uncompressed 32/24/16-bit RGB(A) masks. Rejects truncated data.
  bool DecodeDds(const std::uint8_t* data, std::size_t size, Image& out, std::string* error = nullptr);

  /// Writes RGBA8 as a PNG (zlib, no other dependency). Host tooling only.
  bool WritePng(const std::string& path, const Image& image);

} // namespace faf::port
