// imagecodec_compare (Windows): the Android build's texture codec (src/gal/ImageCodecAndroid.cpp)
// against D3DX11 on a WARP device, decoding as gpg/gal/backends/common/ImageCodec.cpp does, and its
// BC decode (src/gal/BlockCompressionAndroid.cpp) against WARP's.
//
//   imagecodec_compare files <list.txt> [<examples>]
//       Each file of the list (one path per line) decoded with the options DeviceVulkan's
//       CreateTexture passes (none: as the file), and the UI's (a path with textures/ui/) also
//       with GetTexture2D's (BC3, one level, no filter) and its GetImageSize. Format, shape and
//       bytes compared; the results counted by the route our codec took. BC levels that differ
//       are decoded to compare their texels. Exits with 1 when a file cannot be read, our codec
//       fails where D3DX reads, or a shape or size differs (bytes that differ are only reported),
//       and with 2 when the list cannot be read or names no file.
//   imagecodec_compare blocks <seed> <count>
//       <count> random BC1, BC2 and BC3 blocks (with the special cases forced often: equal
//       endpoints, BC1's 3-colour mode, BC3's 6-alpha mode), decoded by WARP (a compute shader
//       loads each texel into an R8G8B8A8 texture) and by ours, texel for texel.
//   imagecodec_compare refuse
//       Files crafted to break a decoder, each ending where a page without access begins: our
//       codec must answer E_FAIL with both options above, without a fault and without taking
//       memory. Whole files at the limits must decode, in under a gigabyte. A load that runs out of
//       memory must fail the same way. D3DX's answers are shown beside ours: it reads no TGA, and
//       more of a broken PNG than we do.
//   imagecodec_compare dump <file> <prefix> [batch]
//       Both decodes of one file, as <prefix>.d3dx and <prefix>.ours.

#include <windows.h>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <d3dx11tex.h>
#include <wrl/client.h>
#include <malloc.h>
#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <new>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "../../src/gal/BlockCompressionAndroid.h"
#include "gpg/gal/backends/common/ImageCodec.h"
#include "gpg/gal/backends/dxgi/DxgiFormats.h"

using Microsoft::WRL::ComPtr;
namespace imaging = gpg::gal::imaging;

namespace
{
  // What a file makes the codec take: the bytes in use, the most since `refuse` last looked, and
  // the size above which an allocation fails.
  std::size_t gAllocated = 0U;
  std::size_t gAllocatedPeak = 0U;
  std::size_t gAllocationLimit = SIZE_MAX;
} // namespace

void* operator new(const std::size_t bytes)
{
  void* const memory = bytes <= gAllocationLimit ? std::malloc(std::max<std::size_t>(bytes, 1U)) : nullptr;
  if (memory == nullptr) {
    throw std::bad_alloc();
  }
  gAllocated += _msize(memory);
  gAllocatedPeak = std::max(gAllocatedPeak, gAllocated);
  return memory;
}

void operator delete(void* const memory) noexcept
{
  if (memory != nullptr) {
    gAllocated -= _msize(memory);
  }
  std::free(memory);
}

void operator delete(void* const memory, std::size_t) noexcept
{
  operator delete(memory);
}

namespace gpg::gal::imaging
{
  // ImageCodecAndroid.cpp's (the same definition): MSVC wants it complete where the codec is
  // constructed.
  struct ImageCodec::State
  {
  };

  // The game takes the codec from GalAvailabilityAndroid.cpp; the tool has its own.
  ImageCodec* ImageCodec::Instance()
  {
    static ImageCodec* const sCodec = new ImageCodec();
    return sCodec;
  }
} // namespace gpg::gal::imaging

namespace
{
  struct D3dx
  {
    HMODULE module = nullptr;
    decltype(&::D3DX11GetImageInfoFromMemory) getImageInfo = nullptr;
    decltype(&::D3DX11CreateTextureFromMemory) createTexture = nullptr;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
  };

  D3dx gD3dx;

  bool LoadD3dx()
  {
    gD3dx.module = ::LoadLibraryA("d3dx11_43.dll");
    if (gD3dx.module == nullptr) {
      std::fprintf(stderr, "d3dx11_43.dll is not installed\n");
      return false;
    }
    gD3dx.getImageInfo = reinterpret_cast<decltype(gD3dx.getImageInfo)>(::GetProcAddress(gD3dx.module, "D3DX11GetImageInfoFromMemory"));
    gD3dx.createTexture = reinterpret_cast<decltype(gD3dx.createTexture)>(::GetProcAddress(gD3dx.module, "D3DX11CreateTextureFromMemory"));
    return gD3dx.getImageInfo != nullptr && gD3dx.createTexture != nullptr &&
      SUCCEEDED(::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0U,
                                    D3D11_SDK_VERSION, &gD3dx.device, nullptr, &gD3dx.context));
  }

  /** ImageCodec::DecodeWithD3dx, 2D textures (the game has no volume texture). */
  HRESULT DecodeWithD3dx(const void* const data, const std::size_t bytes, const imaging::DecodeOptions& options, imaging::Image& outImage)
  {
    D3DX11_IMAGE_INFO info{};
    HRESULT result = gD3dx.getImageInfo(data, bytes, nullptr, &info, nullptr);
    if (FAILED(result)) {
      return result;
    }
    const auto load = [&](const std::uint32_t firstMip, ComPtr<ID3D11Resource>& resource) {
      D3DX11_IMAGE_LOAD_INFO loadInfo;
      if (options.width != 0U) {
        loadInfo.Width = options.width;
      }
      if (options.height != 0U) {
        loadInfo.Height = options.height;
      }
      if (options.mipLevels != 0U) {
        loadInfo.MipLevels = options.mipLevels;
      }
      if (options.format != DXGI_FORMAT_UNKNOWN) {
        loadInfo.Format = options.format;
      }
      if (options.filter == imaging::ResizeFilter::None) {
        loadInfo.Filter = D3DX11_FILTER_NONE;
      }
      if (firstMip != 0U) {
        loadInfo.FirstMipLevel = firstMip;
      }
      loadInfo.Usage = D3D11_USAGE_STAGING;
      loadInfo.BindFlags = 0U;
      loadInfo.CpuAccessFlags = D3D11_CPU_ACCESS_READ;
      loadInfo.MiscFlags = 0U;
      return gD3dx.createTexture(gD3dx.device.Get(), data, bytes, &loadInfo, nullptr, &resource, nullptr);
    };
    ComPtr<ID3D11Resource> resource;
    result = E_FAIL;
    if (options.skipMipLevels != 0U) {
      result = load(options.skipMipLevels, resource);
    }
    if (FAILED(result)) {
      resource.Reset();
      result = load(0U, resource);
    }
    if (FAILED(result)) {
      return result;
    }
    ComPtr<ID3D11Texture2D> texture;
    if (FAILED(resource.As(&texture))) {
      return E_NOINTERFACE;
    }
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    outImage.Allocate(desc.Format, desc.Width, desc.Height, desc.MipLevels, desc.ArraySize);
    outImage.cube = (info.MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE) != 0U && desc.ArraySize == 6U;
    for (std::uint32_t index = 0U; index < outImage.subresources.size(); ++index) {
      const imaging::Image::Subresource& subresource = outImage.subresources[index];
      D3D11_MAPPED_SUBRESOURCE mapped{};
      result = gD3dx.context->Map(texture.Get(), index, D3D11_MAP_READ, 0U, &mapped);
      if (FAILED(result)) {
        return result;
      }
      for (std::uint32_t row = 0U; row < subresource.rowCount; ++row) {
        std::memcpy(outImage.bytes.data() + subresource.offset + static_cast<std::size_t>(row) * subresource.rowPitch,
                    static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(row) * mapped.RowPitch, subresource.rowPitch);
      }
      gD3dx.context->Unmap(texture.Get(), index);
    }
    return S_OK;
  }

  // ---- files --------------------------------------------------------------------------------

  /** A subresource as R8G8B8A8 when it is BC1-3 or 8-bit RGBA/BGRA; empty otherwise. */
  std::vector<std::uint8_t> Texels(const imaging::Image& image, const imaging::Image::Subresource& level)
  {
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(level.width) * level.height * 4U);
    if (imaging::IsBc123(image.format)) {
      imaging::DecodeBcLevel(image.format, image.bytes.data() + level.offset, level.rowPitch, level.width, level.height, rgba.data());
    } else if (gpg::gal::dxgi::FormatElementBytes(image.format) == 4U) {
      std::memcpy(rgba.data(), image.bytes.data() + level.offset, rgba.size());
    } else {
      rgba.clear();
    }
    return rgba;
  }

  struct Tally
  {
    int files = 0;
    int identical = 0;
    int differing = 0;
    int shape = 0;      // format or shape differ
    int oursFail = 0;   // D3DX decodes, ours does not
    int bothFail = 0;
    int d3dxFail = 0;   // ours decodes, D3DX does not
    int topIdentical = 0;  // of the differing: the top level is the same
    int maxDiff = 0;    // the largest channel difference of a texel
    double sumMean = 0; // the mean channel difference of the differing levels, summed over files
    std::vector<std::string> examples;
  };

  std::map<std::string, Tally> gTallies;
  int gExamples = 3;

  std::string Shape(const imaging::Image& image)
  {
    char text[96];
    std::snprintf(text, sizeof(text), "fmt %d %ux%u m%u a%u%s", static_cast<int>(image.format), image.width, image.height,
                  image.mipLevels, image.arraySize, image.cube ? " cube" : "");
    return text;
  }

  void Compare(const std::string& set, const std::string& path, const std::vector<std::uint8_t>& file, const imaging::DecodeOptions& options)
  {
    imaging::Image reference;
    imaging::Image ours;
    const HRESULT referenceResult = DecodeWithD3dx(file.data(), file.size(), options, reference);
    const HRESULT oursResult = imaging::ImageCodec::Instance()->Decode(file.data(), file.size(), options, ours);
    const std::string route = set + " " + imaging::ImageCodec::LastDecodeRoute();
    Tally& tally = gTallies[route];
    ++tally.files;
    const auto example = [&](const std::string& what) {
      if (static_cast<int>(tally.examples.size()) < gExamples) {
        tally.examples.push_back(path + ": " + what);
      }
    };
    if (FAILED(referenceResult) || FAILED(oursResult)) {
      if (FAILED(referenceResult) && FAILED(oursResult)) {
        ++tally.bothFail;
      } else if (FAILED(oursResult)) {
        ++tally.oursFail;
        char text[64];
        std::snprintf(text, sizeof(text), "ours 0x%08lX, D3DX %s", static_cast<unsigned long>(oursResult), Shape(reference).c_str());
        example(text);
      } else {
        ++tally.d3dxFail;
        example("D3DX fails");
      }
      return;
    }
    if (reference.format != ours.format || reference.width != ours.width || reference.height != ours.height ||
        reference.mipLevels != ours.mipLevels || reference.arraySize != ours.arraySize || reference.bytes.size() != ours.bytes.size()) {
      ++tally.shape;
      example("D3DX " + Shape(reference) + ", ours " + Shape(ours));
      return;
    }
    if (reference.bytes == ours.bytes) {
      ++tally.identical;
      return;
    }
    ++tally.differing;
    int maxDiff = 0;
    std::uint64_t sum = 0U;
    std::uint64_t count = 0U;
    std::uint32_t firstLevel = ~0U;
    bool topSame = true;
    for (std::uint32_t index = 0U; index < ours.subresources.size(); ++index) {
      const imaging::Image::Subresource& level = ours.subresources[index];
      if (std::memcmp(ours.bytes.data() + level.offset, reference.bytes.data() + level.offset, level.slicePitch) == 0) {
        continue;
      }
      if (index % ours.mipLevels == 0U) {
        topSame = false;
      }
      firstLevel = std::min(firstLevel, index);
      const std::vector<std::uint8_t> a = Texels(ours, level);
      const std::vector<std::uint8_t> b = Texels(reference, level);
      for (std::size_t at = 0U; at < a.size(); ++at) {
        const int diff = std::abs(static_cast<int>(a[at]) - static_cast<int>(b[at]));
        maxDiff = std::max(maxDiff, diff);
        sum += static_cast<std::uint64_t>(diff);
      }
      count += a.size();
    }
    tally.topIdentical += topSame ? 1 : 0;
    tally.maxDiff = std::max(tally.maxDiff, maxDiff);
    const double mean = count != 0U ? static_cast<double>(sum) / static_cast<double>(count) : 0.0;
    tally.sumMean += mean;
    char text[160];
    std::snprintf(text, sizeof(text), "%s, first differing subresource %u, max %d, mean %.3f", Shape(ours).c_str(), firstLevel, maxDiff, mean);
    example(text);
  }

  int Files(const char* const listPath)
  {
    std::ifstream list(listPath);
    if (!list) {
      std::fprintf(stderr, "cannot read the list %s\n", listPath);
      return 2;
    }
    std::string path;
    int listed = 0;
    int unreadable = 0;
    int sizeMismatches = 0;
    int sizesOnlyOurs = 0;
    int sizes = 0;
    while (std::getline(list, path)) {
      if (path.empty()) {
        continue;
      }
      ++listed;
      std::ifstream input(path, std::ios::binary);
      const std::vector<std::uint8_t> file((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
      if (file.empty()) {
        ++unreadable;
        std::printf("cannot read: %s\n", path.c_str());
        continue;
      }
      std::string lower = path;
      std::transform(lower.begin(), lower.end(), lower.begin(), [](const char c) { return c == '\\' ? '/' : static_cast<char>(std::tolower(c)); });
      Compare("texture", path, file, imaging::DecodeOptions{});
      if (lower.find("textures/ui/") != std::string::npos) {
        imaging::DecodeOptions batch;
        batch.format = DXGI_FORMAT_BC3_UNORM;
        batch.mipLevels = 1U;
        batch.filter = imaging::ResizeFilter::None;
        Compare("batch", path, file, batch);
        D3DX11_IMAGE_INFO info{};
        std::uint32_t width = 0U, height = 0U;
        const HRESULT reference = gD3dx.getImageInfo(file.data(), file.size(), nullptr, &info, nullptr);
        const HRESULT ours = imaging::ImageCodec::Instance()->GetImageSize(file.data(), file.size(), width, height);
        ++sizes;
        if (SUCCEEDED(ours) && FAILED(reference)) {
          ++sizesOnlyOurs; // as the table's d3dx column: D3DX11 reads no TGA, the game's D3D9 did
        } else if (FAILED(ours) != FAILED(reference) || (SUCCEEDED(ours) && (width != info.Width || height != info.Height))) {
          ++sizeMismatches;
          std::printf("size differs: %s: D3DX %ux%u, ours %ux%u\n", path.c_str(), info.Width, info.Height, width, height);
        }
      }
    }
    if (listed == 0) {
      std::fprintf(stderr, "the list %s names no file\n", listPath);
      return 2;
    }
    std::printf("%-44s %6s %6s %6s %5s %5s %5s %5s | %8s %4s %7s\n", "options, our route", "files", "same", "differ", "shape", "fail",
                "both", "d3dx", "top same", "max", "mean");
    for (const auto& [route, tally] : gTallies) {
      std::printf("%-44s %6d %6d %6d %5d %5d %5d %5d | %8d %4d %7.3f\n", route.c_str(), tally.files, tally.identical, tally.differing,
                  tally.shape, tally.oursFail, tally.bothFail, tally.d3dxFail, tally.topIdentical, tally.maxDiff,
                  tally.differing != 0 ? tally.sumMean / tally.differing : 0.0);
    }
    for (const auto& [route, tally] : gTallies) {
      for (const std::string& line : tally.examples) {
        std::printf("  [%s] %s\n", route.c_str(), line.c_str());
      }
    }
    std::printf("GetImageSize: %d files, %d differ, %d only ours reads\n", sizes, sizeMismatches, sizesOnlyOurs);
    int failures = unreadable + sizeMismatches;
    for (const auto& [route, tally] : gTallies) {
      failures += tally.oursFail + tally.shape;
    }
    return failures == 0 ? 0 : 1;
  }

  // ---- refuse -------------------------------------------------------------------------------

  void PutLe(std::vector<std::uint8_t>& file, const std::size_t at, const std::uint32_t value, const std::size_t bytes)
  {
    for (std::size_t index = 0U; index < bytes; ++index) {
      file[at + index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
  }

  struct BmpFields
  {
    std::size_t fileBytes = 58U;
    std::uint32_t pixelsAt = 54U;
    std::uint32_t headerBytes = 40U;
    std::uint32_t width = 1U;
    std::uint32_t height = 1U;
    std::uint32_t bits = 8U;
    std::uint32_t compression = 0U;
    std::uint32_t colors = 0U;
  };

  /** A BMP with these header fields; every other byte is 0xFF, the highest palette index. */
  std::vector<std::uint8_t> Bmp(const BmpFields& fields)
  {
    std::vector<std::uint8_t> file(fields.fileBytes, 0xFFU);
    file[0] = 'B';
    file[1] = 'M';
    PutLe(file, 10U, fields.pixelsAt, 4U);
    PutLe(file, 14U, fields.headerBytes, 4U);
    PutLe(file, 18U, fields.width, 4U);
    PutLe(file, 22U, fields.height, 4U);
    PutLe(file, 26U, 1U, 2U);
    PutLe(file, 28U, fields.bits, 2U);
    PutLe(file, 30U, fields.compression, 4U);
    PutLe(file, 46U, fields.colors, 4U);
    return file;
  }

  struct TgaFields
  {
    std::size_t fileBytes = 18U;
    std::uint32_t type = 2U;
    std::uint32_t bits = 24U;
    std::uint32_t width = 1U;
    std::uint32_t height = 1U;
    std::uint32_t mapLength = 0U;
    std::uint32_t mapBits = 0U;
  };

  /** A TGA with these header fields; every byte after it is 0xFF, the longest run packet. */
  std::vector<std::uint8_t> Tga(const TgaFields& fields)
  {
    std::vector<std::uint8_t> file(fields.fileBytes, 0xFFU);
    std::fill_n(file.begin(), 18U, std::uint8_t{0U});
    file[1] = fields.type == 1U || fields.type == 9U ? 1U : 0U;
    file[2] = static_cast<std::uint8_t>(fields.type);
    PutLe(file, 5U, fields.mapLength, 2U);
    file[7] = static_cast<std::uint8_t>(fields.mapBits);
    PutLe(file, 12U, fields.width, 2U);
    PutLe(file, 14U, fields.height, 2U);
    file[16] = static_cast<std::uint8_t>(fields.bits);
    return file;
  }

  void PutBe32(std::vector<std::uint8_t>& file, const std::size_t at, const std::uint32_t value)
  {
    PutLe(file, at, _byteswap_ulong(value), 4U);
  }

  void PngChunk(std::vector<std::uint8_t>& file, const char* const type, const std::vector<std::uint8_t>& body)
  {
    const std::size_t at = file.size();
    file.resize(at + 12U + body.size());
    PutBe32(file, at, static_cast<std::uint32_t>(body.size()));
    std::memcpy(file.data() + at + 4U, type, 4U);
    std::copy(body.begin(), body.end(), file.begin() + static_cast<std::ptrdiff_t>(at) + 8);
    PutBe32(file, at + 8U + body.size(), crc32(0L, file.data() + at + 4U, static_cast<uInt>(body.size() + 4U)));
  }

  std::vector<std::uint8_t> PngHeaderBody(const std::uint32_t width, const std::uint32_t height, const std::uint32_t depth,
                                          const std::uint32_t colorType, const std::uint32_t interlace)
  {
    std::vector<std::uint8_t> body(13U, 0U);
    PutBe32(body, 0U, width);
    PutBe32(body, 4U, height);
    body[8] = static_cast<std::uint8_t>(depth);
    body[9] = static_cast<std::uint8_t>(colorType);
    body[12] = static_cast<std::uint8_t>(interlace);
    return body;
  }

  /** `rawBytes` zero bytes as a zlib stream: for the rows of a PNG, filter 0 and black. */
  std::vector<std::uint8_t> Deflated(const std::size_t rawBytes)
  {
    const std::vector<std::uint8_t> raw(rawBytes, 0U);
    uLongf bytes = compressBound(static_cast<uLong>(rawBytes));
    std::vector<std::uint8_t> stream(bytes);
    compress2(stream.data(), &bytes, raw.data(), static_cast<uLong>(rawBytes), 9);
    stream.resize(bytes);
    return stream;
  }

  /** A PNG of these chunks and its end. */
  std::vector<std::uint8_t> PngOf(const std::vector<std::pair<const char*, std::vector<std::uint8_t>>>& chunks)
  {
    std::vector<std::uint8_t> file{0x89U, 'P', 'N', 'G', 0x0DU, 0x0AU, 0x1AU, 0x0AU};
    for (const auto& [type, body] : chunks) {
      PngChunk(file, type, body);
    }
    PngChunk(file, "IEND", {});
    return file;
  }

  /** A PNG of these fields and a stream of `rawBytes` zeros. */
  std::vector<std::uint8_t> Png(const std::uint32_t width, const std::uint32_t height, const std::uint32_t depth, const std::uint32_t colorType,
                                const std::size_t rawBytes, const std::uint32_t interlace = 0U)
  {
    return PngOf({{"IHDR", PngHeaderBody(width, height, depth, colorType, interlace)}, {"IDAT", Deflated(rawBytes)}});
  }

  /** A DDS by a FourCC or as L8, a cube with `caps2` 0xFE00; `bytes` long, zeros after the header. */
  std::vector<std::uint8_t> Dds(const std::size_t bytes, const char* const fourCc, const std::uint32_t width, const std::uint32_t height,
                                const std::uint32_t caps2 = 0U, const std::uint32_t mips = 1U)
  {
    std::vector<std::uint8_t> file(bytes, 0U);
    std::memcpy(file.data(), "DDS ", 4U);
    PutLe(file, 4U, 124U, 4U);
    PutLe(file, 8U, 0x21007U, 4U);
    PutLe(file, 12U, height, 4U);
    PutLe(file, 16U, width, 4U);
    PutLe(file, 28U, mips, 4U);
    PutLe(file, 76U, 32U, 4U);
    if (fourCc != nullptr) {
      PutLe(file, 80U, 0x4U, 4U);
      std::memcpy(file.data() + 84U, fourCc, 4U);
    } else {
      PutLe(file, 80U, 0x20000U, 4U);
      PutLe(file, 88U, 8U, 4U);
      PutLe(file, 92U, 0xFFU, 4U);
    }
    PutLe(file, 108U, 0x1000U, 4U);
    PutLe(file, 112U, caps2, 4U);
    return file;
  }

  struct Crafted
  {
    std::string name;
    std::vector<std::uint8_t> file;
    bool decodes = false; // whole, at a limit: must decode instead
  };

  std::vector<Crafted> CraftedFiles()
  {
    std::vector<Crafted> crafted;
    const auto refused = [&](std::string name, std::vector<std::uint8_t> file) { crafted.push_back({std::move(name), std::move(file), false}); };
    const auto decodes = [&](std::string name, std::vector<std::uint8_t> file) { crafted.push_back({std::move(name), std::move(file), true}); };

    refused("bmp header size wraps the palette check", Bmp({.fileBytes = 782U, .headerBytes = 0xFFFFFF00U}));
    refused("bmp colour count wraps the palette check", Bmp({.colors = 0x40000000U}));
    refused("bmp fourth mask past the end", Bmp({.fileBytes = 66U, .pixelsAt = 62U, .headerBytes = 56U, .bits = 32U, .compression = 3U}));
    refused("bmp no bits per pixel, no pixels", Bmp({.fileBytes = 54U, .bits = 0U}));
    for (const std::uint32_t bits : {3U, 5U, 6U, 7U, 9U, 15U, 23U, 64U}) {
      refused("bmp " + std::to_string(bits) + " bits per pixel", Bmp({.fileBytes = 256U, .width = 8U, .bits = bits}));
    }
    refused("bmp header of 12 bytes", Bmp({.headerBytes = 12U}));
    refused("bmp height -2^31", Bmp({.height = 0x80000000U}));
    refused("bmp width -1", Bmp({.width = 0xFFFFFFFFU}));
    refused("bmp pixels past the end", Bmp({.pixelsAt = 0xFFFFFFF0U}));
    refused("bmp wider than the limit", Bmp({.fileBytes = 62U + 1028U, .pixelsAt = 62U, .width = 8193U, .bits = 1U}));
    decodes("bmp at the limit", Bmp({.fileBytes = 62U + 1024U, .pixelsAt = 62U, .width = 8192U, .bits = 1U}));
    decodes("bmp 8 bits, the whole palette", Bmp({.fileBytes = 1082U, .pixelsAt = 1078U}));
    decodes("bmp masks in a 56-byte header", Bmp({.fileBytes = 74U, .pixelsAt = 70U, .headerBytes = 56U, .bits = 32U, .compression = 3U}));
    decodes("bmp masks on 24 bits", Bmp({.fileBytes = 70U, .pixelsAt = 66U, .bits = 24U, .compression = 3U}));
    decodes("bmp more colours than the pixels index", Bmp({.fileBytes = 66U, .pixelsAt = 62U, .bits = 1U, .colors = 1000U}));
    decodes("bmp masks on 8 bits", Bmp({.fileBytes = 1082U, .pixelsAt = 1078U, .compression = 3U}));

    refused("tga 65535x65535", Tga({.width = 65535U, .height = 65535U}));
    refused("tga 65535x65535 run-length", Tga({.type = 10U, .width = 65535U, .height = 65535U}));
    refused("tga 8192x8192, no pixels", Tga({.width = 8192U, .height = 8192U}));
    refused("tga 8192x8192 run-length, short", Tga({.fileBytes = 18U + 1024U, .type = 10U, .width = 8192U, .height = 8192U}));
    refused("tga true colour of 8 bits", Tga({.fileBytes = 19U, .bits = 8U}));
    refused("tga true colour of 8 bits, run-length", Tga({.fileBytes = 20U, .type = 10U, .bits = 8U}));
    refused("tga colour map past the end", Tga({.fileBytes = 19U, .type = 1U, .bits = 8U, .mapLength = 65535U, .mapBits = 32U}));
    refused("tga a pixel short", Tga({.fileBytes = 20U}));
    refused("tga wider than the limit", Tga({.fileBytes = 18U + 8193U * 3U, .width = 8193U}));
    decodes("tga at the limit", Tga({.fileBytes = 18U + 8192U * 3U, .width = 8192U}));
    decodes("tga 8192x8192 run-length, the least it takes",
            Tga({.fileBytes = 18U + 8192U * 8192U / 64U, .type = 11U, .bits = 8U, .width = 8192U, .height = 8192U}));

    refused("png 16384x16384 RGBA16, a tiny stream", Png(16384U, 16384U, 16U, 6U, 2U));
    refused("png 8192x8192 RGBA8, a tiny stream", Png(8192U, 8192U, 8U, 6U, 2U));
    refused("png 8192x8192 gray, no stream", PngOf({{"IHDR", PngHeaderBody(8192U, 8192U, 8U, 0U, 0U)}}));
    refused("png 64x64, a short stream", Png(64U, 64U, 8U, 0U, 2U));
    refused("png wider than the limit, whole", Png(8193U, 1U, 8U, 0U, 8194U));
    refused("png header not the first chunk",
            PngOf({{"tEXt", PngHeaderBody(2048U, 2048U, 8U, 0U, 0U)}, {"IHDR", PngHeaderBody(1U, 1U, 8U, 0U, 0U)}, {"IDAT", Deflated(2U)}}));
    refused("png a second header",
            PngOf({{"IHDR", PngHeaderBody(2048U, 2048U, 8U, 0U, 0U)}, {"IHDR", PngHeaderBody(1U, 1U, 8U, 0U, 0U)}, {"IDAT", Deflated(2U)}}));
    refused("png RGB of 4 bits", Png(1U, 1U, 4U, 2U, 3U));
    refused("png interlace 2", Png(1U, 1U, 8U, 0U, 2U, 2U));
    decodes("png at the limit", Png(8192U, 1U, 8U, 0U, 8193U));
    decodes("png 1x1 RGBA16", Png(1U, 1U, 16U, 6U, 9U));

    refused("dds 65535x65535, no pixels", Dds(128U, "DXT5", 65535U, 65535U));
    refused("dds BC1 over the limit, whole", Dds(128U + 16392U, "DXT1", 8196U, 4U));
    refused("dds BC1 over the limit, a whole chain", Dds(128U + 70000U, "DXT1", 16384U, 4U, 0U, 15U));
    refused("dds BC1, truncated", Dds(128U + 1024U, "DXT1", 1024U, 1024U));
    refused("dds L8 cube over the limit, whole", Dds(128U + 4096U * 4096U * 6U, nullptr, 4096U, 4096U, 0xFE00U));
    decodes("dds BC1 at the limit", Dds(128U + 16384U, "DXT1", 8192U, 4U));
    decodes("dds BC1 at the limit, a whole chain", Dds(128U + 40000U, "DXT1", 8192U, 4U, 0U, 14U));
    decodes("dds L8 cube within the limit", Dds(128U + 1024U * 1024U * 6U, nullptr, 1024U, 1024U, 0xFE00U));
    return crafted;
  }

  /** A copy of a file that ends where a page without access begins: a read past its end faults. */
  struct GuardedFile
  {
    explicit GuardedFile(const std::vector<std::uint8_t>& file)
      : bytes(file.size())
    {
      constexpr std::size_t kPage = 4096U;
      const std::size_t readable = (bytes + kPage - 1U) / kPage * kPage;
      base = static_cast<std::uint8_t*>(::VirtualAlloc(nullptr, readable + kPage, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
      DWORD previous = 0U;
      ::VirtualProtect(base + readable, kPage, PAGE_NOACCESS, &previous);
      data = base + readable - bytes;
      std::memcpy(data, file.data(), bytes);
    }

    ~GuardedFile()
    {
      ::VirtualFree(base, 0U, MEM_RELEASE);
    }

    GuardedFile(const GuardedFile&) = delete;
    GuardedFile& operator=(const GuardedFile&) = delete;

    std::uint8_t* base = nullptr;
    std::uint8_t* data = nullptr;
    std::size_t bytes = 0U;
  };

  using Decoder = HRESULT (*)(const void*, std::size_t, const imaging::DecodeOptions&, imaging::Image&);

  HRESULT OurDecode(const void* const data, const std::size_t bytes, const imaging::DecodeOptions& options, imaging::Image& outImage)
  {
    return imaging::ImageCodec::Instance()->Decode(data, bytes, options, outImage);
  }

  /** A decode that may fault: an exception frame of its own, as MSVC keeps __try out of functions with objects to unwind. */
  HRESULT DecodeGuarded(const Decoder decoder, const GuardedFile& file, const imaging::DecodeOptions& options, imaging::Image& outImage, bool& outFaulted)
  {
    __try {
      return decoder(file.data, file.bytes, options, outImage);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      outFaulted = true;
      return E_FAIL;
    }
  }

  int Refuse()
  {
    // What a refusal may take, the file's own chunks, nothing the size of an image; and a load.
    constexpr std::size_t kMostRefused = 1024U * 1024U;
    constexpr std::size_t kMostLoaded = 1024U * 1024U * 1024U;
    imaging::DecodeOptions batch;
    batch.format = DXGI_FORMAT_BC3_UNORM;
    batch.mipLevels = 1U;
    batch.filter = imaging::ResizeFilter::None;
    int failures = 0;
    const std::vector<Crafted> crafted = CraftedFiles();
    std::printf("%-46s %9s  %-8s %-8s %10s  %-8s %-8s\n", "crafted file", "bytes", "ours", "ours BC3", "took", "D3DX", "D3DX BC3");
    for (const Crafted& entry : crafted) {
      const GuardedFile file(entry.file);
      bool faulted = false;
      std::size_t took = 0U;
      const auto oursWith = [&](const imaging::DecodeOptions& options, imaging::Image& outImage) {
        const std::size_t before = gAllocated;
        gAllocatedPeak = before;
        const HRESULT result = DecodeGuarded(OurDecode, file, options, outImage, faulted);
        took = std::max(took, gAllocatedPeak - before);
        return result;
      };
      imaging::Image image;
      imaging::Image padded;
      const HRESULT ours = oursWith(imaging::DecodeOptions{}, image);
      const HRESULT oursBatch = oursWith(batch, padded);
      std::uint32_t width = 0U;
      std::uint32_t height = 0U;
      const HRESULT size = imaging::ImageCodec::Instance()->GetImageSize(file.data, file.bytes, width, height);
      imaging::Image reference;
      bool referenceFaulted = false;
      const HRESULT d3dx = DecodeGuarded(DecodeWithD3dx, file, imaging::DecodeOptions{}, reference, referenceFaulted);
      reference = imaging::Image{};
      const HRESULT d3dxBatch = DecodeGuarded(DecodeWithD3dx, file, batch, reference, referenceFaulted);

      const char* verdict = "";
      if (faulted) {
        verdict = "FAULT";
      } else if (entry.decodes) {
        // What the engine sizes its copy of a batch texture's blocks by must be what was decoded.
        if (ours != S_OK || oursBatch != S_OK || size != S_OK || width != image.width || height != image.height) {
          verdict = "SHOULD DECODE";
        } else if (took > kMostLoaded) {
          verdict = "TOOK TOO MUCH";
        }
      } else if (ours != E_FAIL || oursBatch != E_FAIL) {
        verdict = "NOT REFUSED";
      } else if (took > kMostRefused) {
        verdict = "TOOK TOO MUCH";
      }
      failures += verdict[0] != '\0' ? 1 : 0;
      std::printf("%-46s %9zu  %08lX %08lX %10zu  %08lX %08lX  %s%s\n", entry.name.c_str(), file.bytes, static_cast<unsigned long>(ours),
                  static_cast<unsigned long>(oursBatch), took, static_cast<unsigned long>(d3dx), static_cast<unsigned long>(d3dxBatch),
                  referenceFaulted ? "(D3DX faulted) " : "", verdict);
      std::fflush(stdout);
    }

    // An allocation that fails is a failed load, not an exception out of the codec.
    const GuardedFile whole(Bmp({.fileBytes = 62U + 1024U, .pixelsAt = 62U, .width = 8192U, .bits = 1U}));
    imaging::Image image;
    bool faulted = false;
    gAllocationLimit = 1024U;
    const HRESULT outOfMemory = DecodeGuarded(OurDecode, whole, imaging::DecodeOptions{}, image, faulted);
    gAllocationLimit = SIZE_MAX;
    if (faulted || outOfMemory != E_FAIL) {
      ++failures;
      std::printf("out of memory: %08lX%s\n", static_cast<unsigned long>(outOfMemory), faulted ? ", an exception left the codec" : "");
    }
    std::printf("refuse: %zu files, %d failed\n", crafted.size(), failures);
    return failures == 0 ? 0 : 1;
  }

  // ---- blocks -------------------------------------------------------------------------------

  using D3DCompileProc = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT,
                                           ID3DBlob**, ID3DBlob**);

  constexpr char kLoadShader[] =
    "Texture2D<float4> source : register(t0);\n"
    "RWTexture2D<unorm float4> target : register(u0);\n"
    "[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) { target[id.xy] = source.Load(int3(id.xy, 0)); }\n";

  int Blocks(const unsigned seed, const unsigned count)
  {
    HMODULE compilerModule = ::LoadLibraryA("d3dcompiler_47.dll");
    const auto compile = compilerModule != nullptr ? reinterpret_cast<D3DCompileProc>(::GetProcAddress(compilerModule, "D3DCompile")) : nullptr;
    if (compile == nullptr) {
      std::fprintf(stderr, "d3dcompiler_47.dll is not available\n");
      return 2;
    }
    ComPtr<ID3DBlob> code;
    ComPtr<ID3DBlob> errors;
    if (FAILED(compile(kLoadShader, sizeof(kLoadShader) - 1U, "load", nullptr, nullptr, "main", "cs_5_0", 0U, 0U, &code, &errors))) {
      std::fprintf(stderr, "shader: %s\n", errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
      return 2;
    }
    ID3D11Device* const device = gD3dx.device.Get();
    ID3D11DeviceContext* const context = gD3dx.context.Get();
    ComPtr<ID3D11ComputeShader> shader;
    device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader);

    const std::uint32_t blocksWide = 256U;
    const std::uint32_t blocksHigh = std::max(1U, (count + blocksWide - 1U) / blocksWide);
    const std::uint32_t width = blocksWide * 4U;
    const std::uint32_t height = blocksHigh * 4U;
    std::mt19937 random(seed);
    int failures = 0;
    for (const DXGI_FORMAT format : {DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_BC2_UNORM, DXGI_FORMAT_BC3_UNORM}) {
      const std::uint32_t blockBytes = (format == DXGI_FORMAT_BC1_UNORM) ? 8U : 16U;
      std::vector<std::uint8_t> blocks(static_cast<std::size_t>(blocksWide) * blocksHigh * blockBytes);
      for (std::uint8_t& byte : blocks) {
        byte = static_cast<std::uint8_t>(random());
      }
      for (std::size_t block = 0U; block * blockBytes < blocks.size(); ++block) {
        std::uint8_t* const at = blocks.data() + block * blockBytes;
        std::uint8_t* const color = at + (blockBytes - 8U);
        switch (random() % 8U) {
        case 0: // equal endpoints
          color[2] = color[0];
          color[3] = color[1];
          break;
        case 1: // BC3: equal alphas
          at[1] = at[0];
          break;
        case 2: // endpoints that differ in one channel
          color[2] = static_cast<std::uint8_t>(color[0] ^ (1U << (random() % 8U)));
          color[3] = color[1];
          break;
        default:
          break;
        }
      }
      D3D11_TEXTURE2D_DESC desc{};
      desc.Width = width;
      desc.Height = height;
      desc.MipLevels = 1U;
      desc.ArraySize = 1U;
      desc.Format = format;
      desc.SampleDesc.Count = 1U;
      desc.Usage = D3D11_USAGE_DEFAULT;
      desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
      D3D11_SUBRESOURCE_DATA initial{blocks.data(), blocksWide * blockBytes, 0U};
      ComPtr<ID3D11Texture2D> source;
      ComPtr<ID3D11ShaderResourceView> view;
      device->CreateTexture2D(&desc, &initial, &source);
      device->CreateShaderResourceView(source.Get(), nullptr, &view);
      desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
      desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
      ComPtr<ID3D11Texture2D> target;
      ComPtr<ID3D11UnorderedAccessView> unordered;
      device->CreateTexture2D(&desc, nullptr, &target);
      device->CreateUnorderedAccessView(target.Get(), nullptr, &unordered);
      desc.BindFlags = 0U;
      desc.Usage = D3D11_USAGE_STAGING;
      desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
      ComPtr<ID3D11Texture2D> staging;
      device->CreateTexture2D(&desc, nullptr, &staging);
      context->CSSetShader(shader.Get(), nullptr, 0U);
      context->CSSetShaderResources(0U, 1U, view.GetAddressOf());
      context->CSSetUnorderedAccessViews(0U, 1U, unordered.GetAddressOf(), nullptr);
      context->Dispatch(width / 8U, height / 8U, 1U);
      context->CopyResource(staging.Get(), target.Get());
      D3D11_MAPPED_SUBRESOURCE mapped{};
      context->Map(staging.Get(), 0U, D3D11_MAP_READ, 0U, &mapped);

      std::vector<std::uint8_t> ours(static_cast<std::size_t>(width) * height * 4U);
      imaging::DecodeBcLevel(format, blocks.data(), blocksWide * blockBytes, width, height, ours.data());
      std::uint64_t differing = 0U;
      int shown = 0;
      for (std::uint32_t y = 0U; y < height; ++y) {
        const auto* const warpRow = static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch;
        for (std::uint32_t x = 0U; x < width; ++x) {
          const std::uint8_t* const warp = warpRow + x * 4U;
          const std::uint8_t* const mine = ours.data() + (static_cast<std::size_t>(y) * width + x) * 4U;
          if (std::memcmp(warp, mine, 4U) != 0) {
            ++differing;
            if (shown++ < 8) {
              const std::uint8_t* const block = blocks.data() + (static_cast<std::size_t>(y / 4U) * blocksWide + x / 4U) * blockBytes;
              std::printf("  BC%d texel (%u,%u) block", format == DXGI_FORMAT_BC1_UNORM ? 1 : (format == DXGI_FORMAT_BC2_UNORM ? 2 : 3), x, y);
              for (std::uint32_t at = 0U; at < blockBytes; ++at) {
                std::printf(" %02x", block[at]);
              }
              std::printf(": WARP %3u %3u %3u %3u, ours %3u %3u %3u %3u\n", warp[0], warp[1], warp[2], warp[3], mine[0], mine[1], mine[2], mine[3]);
            }
          }
        }
      }
      context->Unmap(staging.Get(), 0U);
      std::printf("BC%d: %u blocks, %llu of %llu texels differ from WARP\n",
                  format == DXGI_FORMAT_BC1_UNORM ? 1 : (format == DXGI_FORMAT_BC2_UNORM ? 2 : 3), blocksWide * blocksHigh,
                  static_cast<unsigned long long>(differing), static_cast<unsigned long long>(width) * height);
      failures += differing != 0U ? 1 : 0;
    }
    return failures == 0 ? 0 : 1;
  }
} // namespace

int main(const int argc, char** const argv)
{
  if (!LoadD3dx()) {
    return 2;
  }
  if (argc >= 3 && std::strcmp(argv[1], "files") == 0) {
    if (argc >= 4) {
      gExamples = std::atoi(argv[3]);
    }
    return Files(argv[2]);
  }
  if (argc >= 2 && std::strcmp(argv[1], "refuse") == 0) {
    return Refuse();
  }
  if (argc >= 4 && std::strcmp(argv[1], "dump") == 0) {
    // Both decodes of one file with CreateTexture's options, as <prefix>.d3dx and <prefix>.ours:
    // the subresources' bytes one after another.
    std::ifstream input(argv[2], std::ios::binary);
    const std::vector<std::uint8_t> file((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    imaging::DecodeOptions options;
    if (argc >= 5 && std::strcmp(argv[4], "batch") == 0) {
      options.format = DXGI_FORMAT_BC3_UNORM;
      options.mipLevels = 1U;
      options.filter = imaging::ResizeFilter::None;
    }
    imaging::Image reference;
    imaging::Image ours;
    DecodeWithD3dx(file.data(), file.size(), options, reference);
    imaging::ImageCodec::Instance()->Decode(file.data(), file.size(), options, ours);
    std::ofstream(std::string(argv[3]) + ".d3dx", std::ios::binary).write(reinterpret_cast<const char*>(reference.bytes.data()), reference.bytes.size());
    std::ofstream(std::string(argv[3]) + ".ours", std::ios::binary).write(reinterpret_cast<const char*>(ours.bytes.data()), ours.bytes.size());
    std::printf("D3DX %s, ours %s\n", Shape(reference).c_str(), Shape(ours).c_str());
    return 0;
  }
  if (argc >= 4 && std::strcmp(argv[1], "blocks") == 0) {
    return Blocks(static_cast<unsigned>(std::strtoul(argv[2], nullptr, 10)), static_cast<unsigned>(std::strtoul(argv[3], nullptr, 10)));
  }
  std::fprintf(stderr, "usage: imagecodec_compare files <list.txt> [<examples>] | blocks <seed> <count> | refuse | dump <file> <prefix> [batch]\n");
  return 2;
}
