#pragma once

// BC1, BC2 and BC3 (DXT1, DXT3, DXT5) on the CPU, for the Android client build.
//
// The decode is Direct3D's, as WARP does it (checked block for block against WARP by
// tests/imagecodec): a GPU without BC textures (the Galaxy S22's Xclipse 920) gets the texels it
// would have sampled, as R8G8B8A8. The Vulkan backend decodes there (patches/README.md,
// DeviceVulkan.cpp), and the texture codec when it converts.
//
// The encoder stands in for D3DX's, which the codec needs where D3DX11 compresses: generated mip
// levels, and files that become BC on loading (the UI's batch textures are BC3). It follows D3DX's
// method (endpoints from a least-squares fit along the colour axis, perceptual weights, 3-colour
// mode for BC1 with alpha), not its exact arithmetic: the blocks can differ from D3DX11's in the
// last bit of an endpoint or in an index (tests/imagecodec measures how much).

#include <cstddef>
#include <cstdint>

#include <dxgiformat.h>

namespace gpg::gal::imaging
{
  /** BC1, BC2 or BC3 in any of its typeless, UNORM and sRGB forms. */
  [[nodiscard]] bool IsBc123(DXGI_FORMAT format) noexcept;

  /**
   * Decodes a level of `width` x `height` texels, stored as whole 4x4 blocks with `rowPitch`
   * bytes per row of blocks, into `rgba` (R8G8B8A8, `width` * 4 bytes per row). The blocks'
   * texels beyond the level's size are not written.
   */
  void DecodeBcLevel(DXGI_FORMAT format, const void* blocks, std::size_t rowPitch, std::uint32_t width, std::uint32_t height,
                     std::uint8_t* rgba);

  /**
   * Encodes `rgba` (R8G8B8A8, `width` * 4 bytes per row) as whole 4x4 blocks with `rowPitch`
   * bytes per row of blocks. A block that reaches past the level repeats the level's last column
   * and row, as D3DX fills partial blocks.
   */
  void EncodeBcLevel(DXGI_FORMAT format, const std::uint8_t* rgba, std::uint32_t width, std::uint32_t height, void* blocks,
                     std::size_t rowPitch);
} // namespace gpg::gal::imaging
