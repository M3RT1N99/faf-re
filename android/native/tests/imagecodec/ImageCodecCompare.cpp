// imagecodec_compare (Windows): the Android build's texture codec (src/gal/ImageCodecAndroid.cpp)
// against D3DX11 on a WARP device, decoding as gpg/gal/backends/common/ImageCodec.cpp does, and its
// BC decode (src/gal/BlockCompressionAndroid.cpp) against WARP's.
//
//   imagecodec_compare files <list.txt> [<examples>]
//       Each file of the list (one path per line) decoded with the options DeviceVulkan's
//       CreateTexture passes (none: as the file), and the UI's (a path with textures/ui/) also
//       with GetTexture2D's (BC3, one level, no filter) and its GetImageSize. Format, shape and
//       bytes compared; the results counted by the route our codec took. BC levels that differ
//       are decoded to compare their texels.
//   imagecodec_compare blocks <seed> <count>
//       <count> random BC1, BC2 and BC3 blocks (with the special cases forced often: equal
//       endpoints, BC1's 3-colour mode, BC3's 6-alpha mode), decoded by WARP (a compute shader
//       loads each texel into an R8G8B8A8 texture) and by ours, texel for texel.

#include <windows.h>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <d3dx11tex.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "../../src/gal/BlockCompressionAndroid.h"
#include "gpg/gal/backends/common/ImageCodec.h"
#include "gpg/gal/backends/dxgi/DxgiFormats.h"

using Microsoft::WRL::ComPtr;
namespace imaging = gpg::gal::imaging;

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
    std::string path;
    int sizeMismatches = 0;
    int sizes = 0;
    while (std::getline(list, path)) {
      if (path.empty()) {
        continue;
      }
      std::ifstream input(path, std::ios::binary);
      const std::vector<std::uint8_t> file((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
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
        if (SUCCEEDED(reference) != SUCCEEDED(ours) || (SUCCEEDED(ours) && (width != info.Width || height != info.Height))) {
          ++sizeMismatches;
          std::printf("size differs: %s: D3DX %ux%u, ours %ux%u\n", path.c_str(), info.Width, info.Height, width, height);
        }
      }
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
    std::printf("GetImageSize: %d files, %d differ\n", sizes, sizeMismatches);
    return 0;
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
  std::fprintf(stderr, "usage: imagecodec_compare files <list.txt> [<examples>] | blocks <seed> <count>\n");
  return 2;
}
