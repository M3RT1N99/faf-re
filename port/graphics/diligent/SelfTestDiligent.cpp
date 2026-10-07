// /galselftest: known-answer checks of the Diligent backend's paths the main menu does not reach
// (M6b, component R). The menu run already compares every pixel it draws with D3D9; these cover
// the slots and resource paths it never calls, against values computed here from D3D9's documented
// behaviour. Everything goes through the Device slots, as the engine would call them.

#include <d3d9.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "DeviceDiligent.h"
#include "DiligentHost.h"
#include "ResourcesDiligent.h"
#include "gpg/core/utils/Logging.h"
#include "gpg/gal/CubeRenderTargetContext.hpp"
#include "gpg/gal/DepthStencilTargetContext.hpp"
#include "gpg/gal/Error.hpp"
#include "gpg/gal/IndexBufferContext.hpp"
#include "gpg/gal/RenderTargetContext.hpp"
#include "gpg/gal/TextureContext.hpp"
#include "gpg/gal/VertexBufferContext.hpp"

namespace gpg::gal::diligent
{
    namespace
    {
        constexpr std::uint32_t kQuadrant[4] = {0x11223344U, 0x55667788U, 0x99AABBCCU, 0xDDEEFF00U}; // TL, TR, BL, BR

        std::string Hex(const std::uint32_t value)
        {
            char text[16];
            std::snprintf(text, sizeof(text), "%08X", value);
            return text;
        }
    } // namespace

    void DeviceDiligent::RunSelfTest()
    {
        std::uint64_t savedCalls[kSlotCount];
        for (int slot = 0; slot < kSlotCount; ++slot) {
            savedCalls[slot] = mSlotCalls[slot].load();
        }
        mHost->DrainDebugLayer(nullptr, 0);
        const DebugLayerCounts layerBefore = mHost->GetDebugLayerCounts();
        mSelfTestResults.clear();

        const auto record = [this](const char* const name, const bool pass, const std::string& detail) {
            mSelfTestResults.push_back({name, pass, detail});
            gpg::Logf("[gal-diligent] self test %s: %s %s", name, pass ? "pass" : "FAIL", detail.c_str());
        };
        const auto guarded = [&record](const char* const name, const std::function<void()>& test) {
            try {
                test();
            } catch (const Error& error) {
                record(name, false, std::string("gal::Error: ") + error.what());
            }
        };
        const auto makeTarget = [this](const std::uint32_t width, const std::uint32_t height) {
            RenderTargetContext context{};
            context.width_ = width;
            context.height_ = height;
            context.format_ = 2U; // A8R8G8B8
            return CreateRenderTarget(&context);
        };
        // A system-memory A8R8G8B8 texture, the destination GetRenderTargetData needs.
        const auto makeSystemTexture = [this](const std::uint32_t width, const std::uint32_t height) {
            TextureContext context{};
            context.source_ = 2U;
            context.usage_ = 3U;
            context.format_ = 2U;
            context.mipmapLevels_ = 1U;
            context.width_ = width;
            context.height_ = height;
            return CreateTexture(&context);
        };
        const auto readTarget = [&](const boost::shared_ptr<RenderTarget>& target) {
            const RenderTargetContext* const context = target->GetContext();
            const boost::shared_ptr<Texture> texture = makeSystemTexture(context->width_, context->height_);
            GetRenderTargetData(target, texture);
            const TextureLockRect lock = texture->Lock(0, RECT{}, 2);
            std::vector<std::uint32_t> pixels(static_cast<std::size_t>(context->width_) * context->height_);
            for (std::uint32_t row = 0; row < context->height_; ++row) {
                std::memcpy(&pixels[static_cast<std::size_t>(row) * context->width_],
                            static_cast<const std::uint8_t*>(lock.bits) + static_cast<std::size_t>(row) * static_cast<std::size_t>(lock.pitch),
                            context->width_ * 4U);
            }
            texture->Unlock(lock);
            return pixels;
        };
        const auto bind = [this](const boost::shared_ptr<RenderTarget>& target) {
            OutputContext output;
            output.surface = target;
            ClearTarget(&output);
        };
        const auto viewport = [this](const std::uint32_t x, const std::uint32_t y, const std::uint32_t width, const std::uint32_t height) {
            const D3DVIEWPORT9 value{x, y, width, height, 0.0f, 1.0f};
            SetViewport(&value);
        };
        // Counts pixels that differ from `expected(x, y)`, and names the first.
        const auto compare = [](const std::vector<std::uint32_t>& pixels, const std::uint32_t width, const std::function<std::uint32_t(std::uint32_t, std::uint32_t)>& expected) {
            std::size_t bad = 0;
            std::string first;
            for (std::size_t index = 0; index < pixels.size(); ++index) {
                const std::uint32_t x = static_cast<std::uint32_t>(index % width);
                const std::uint32_t y = static_cast<std::uint32_t>(index / width);
                const std::uint32_t want = expected(x, y);
                if (pixels[index] != want) {
                    if (bad == 0) {
                        first = " first (" + std::to_string(x) + "," + std::to_string(y) + ") " + Hex(pixels[index]) + " expected " + Hex(want);
                    }
                    ++bad;
                }
            }
            return std::to_string(bad) + " of " + std::to_string(pixels.size()) + " pixels differ" + first;
        };
        const auto quadrants = [](const std::uint32_t x, const std::uint32_t y, const std::uint32_t half) {
            return kQuadrant[(y >= half ? 2U : 0U) + (x >= half ? 1U : 0U)];
        };

        // 1. Clear honours the viewport (IDirect3DDevice9::Clear with no rects clears the viewport
        //    rectangle): the backend clears the views for a full viewport and draws a quad otherwise.
        boost::shared_ptr<RenderTarget> source;
        guarded("clear-viewport", [&] {
            const boost::shared_ptr<RenderTarget> target = makeTarget(64, 64);
            bind(target);
            viewport(0, 0, 64, 64);
            Clear(true, false, false, 0x00000000U, 1.0f, 0);
            viewport(16, 8, 32, 24);
            Clear(true, false, false, 0xFF102030U, 1.0f, 0);
            const std::string verdict = compare(readTarget(target), 64, [](const std::uint32_t x, const std::uint32_t y) {
                return (x >= 16U && x < 48U && y >= 8U && y < 32U) ? 0xFF102030U : 0x00000000U;
            });
            record("clear-viewport", verdict.rfind("0 of", 0) == 0, verdict);
        });

        // 2. Four quadrants by viewport clears, read back exactly (offscreen GetRenderTargetData).
        guarded("readback-quadrants", [&] {
            source = makeTarget(64, 64);
            bind(source);
            for (std::uint32_t quadrant = 0; quadrant < 4U; ++quadrant) {
                viewport((quadrant & 1U) * 32U, (quadrant >> 1U) * 32U, 32, 32);
                Clear(true, false, false, kQuadrant[quadrant], 1.0f, 0);
            }
            const std::string verdict = compare(readTarget(source), 64, [&](const std::uint32_t x, const std::uint32_t y) { return quadrants(x, y, 32); });
            record("readback-quadrants", verdict.rfind("0 of", 0) == 0, verdict);
        });

        // 3. StretchRect of equal sizes without rectangles copies.
        guarded("stretchrect-copy", [&] {
            const boost::shared_ptr<RenderTarget> copy = makeTarget(64, 64);
            StretchRect(source, copy, nullptr, nullptr);
            const std::string verdict = compare(readTarget(copy), 64, [&](const std::uint32_t x, const std::uint32_t y) { return quadrants(x, y, 32); });
            record("stretchrect-copy", verdict.rfind("0 of", 0) == 0, verdict);
        });

        // 4. StretchRect 64 -> 32 with D3DTEXF_LINEAR: each destination pixel centre maps to a source texel
        //    corner, the bilinear average of a 2x2 block, which is uniform inside each quadrant.
        guarded("stretchrect-scale", [&] {
            const boost::shared_ptr<RenderTarget> half = makeTarget(32, 32);
            StretchRect(source, half, nullptr, nullptr);
            const std::string verdict = compare(readTarget(half), 32, [&](const std::uint32_t x, const std::uint32_t y) { return quadrants(x, y, 16); });
            record("stretchrect-scale", verdict.rfind("0 of", 0) == 0, verdict);
        });

        // 5. StretchRect between rectangles: the top-left quadrant into [8, 24) of a cleared target.
        guarded("stretchrect-rects", [&] {
            const boost::shared_ptr<RenderTarget> target = makeTarget(32, 32);
            bind(target);
            viewport(0, 0, 32, 32);
            Clear(true, false, false, 0x00000000U, 1.0f, 0);
            const RECT from{0, 0, 32, 32};
            const RECT to{8, 8, 24, 24};
            StretchRect(source, target, &from, &to);
            const std::string verdict = compare(readTarget(target), 32, [](const std::uint32_t x, const std::uint32_t y) {
                return (x >= 8U && x < 24U && y >= 8U && y < 24U) ? kQuadrant[0] : 0x00000000U;
            });
            record("stretchrect-rects", verdict.rfind("0 of", 0) == 0, verdict);
        });

        // 6. Texture Lock/Unlock reaches the GPU texture: the whole level, then a sub-rectangle.
        guarded("texture-upload", [&] {
            TextureContext context{};
            context.source_ = 2U;
            context.format_ = 2U; // A8R8G8B8, managed
            context.mipmapLevels_ = 1U;
            context.width_ = 16U;
            context.height_ = 16U;
            const boost::shared_ptr<Texture> texture = CreateTexture(&context);
            const auto pattern = [](const std::uint32_t x, const std::uint32_t y) { return 0xFF000000U | (x << 16U) | (y << 8U) | (x ^ y); };
            TextureLockRect lock = texture->Lock(0, RECT{}, 0);
            for (std::uint32_t y = 0; y < 16U; ++y) {
                for (std::uint32_t x = 0; x < 16U; ++x) {
                    const std::uint32_t value = pattern(x, y);
                    std::memcpy(static_cast<std::uint8_t*>(lock.bits) + y * static_cast<std::uint32_t>(lock.pitch) + x * 4U, &value, 4U);
                }
            }
            texture->Unlock(lock);
            auto* const diligentTexture = dynamic_cast<TextureDiligent*>(texture.get());
            GpuTexture* gpu = diligentTexture->GetGpu();
            std::vector<std::uint8_t> bytes;
            std::uint32_t width = 0;
            std::uint32_t height = 0;
            const auto gpuPixels = [&] {
                std::vector<std::uint32_t> pixels;
                if (gpu != nullptr && mHost->ReadTextureBgra8(gpu->Texture(), &bytes, &width, &height)) {
                    pixels.resize(bytes.size() / 4U);
                    std::memcpy(pixels.data(), bytes.data(), bytes.size());
                }
                mDrawPath->InvalidateTargets();
                return pixels;
            };
            std::string verdict = compare(gpuPixels(), 16, pattern);
            const bool wholePass = verdict.rfind("0 of", 0) == 0;
            const RECT rect{4, 4, 8, 8};
            lock = texture->Lock(0, rect, 0);
            for (std::uint32_t y = 0; y < 4U; ++y) {
                for (std::uint32_t x = 0; x < 4U; ++x) {
                    const std::uint32_t white = 0xFFFFFFFFU;
                    std::memcpy(static_cast<std::uint8_t*>(lock.bits) + y * static_cast<std::uint32_t>(lock.pitch) + x * 4U, &white, 4U);
                }
            }
            texture->Unlock(lock);
            const std::uint64_t updatesBefore = mGpu->Stats().updateTexture.load();
            gpu = diligentTexture->GetGpu();
            const std::uint64_t updates = mGpu->Stats().updateTexture.load() - updatesBefore;
            const std::string rectVerdict = compare(gpuPixels(), 16, [&](const std::uint32_t x, const std::uint32_t y) {
                return (x >= 4U && x < 8U && y >= 4U && y < 8U) ? 0xFFFFFFFFU : pattern(x, y);
            });
            record("texture-upload", wholePass && rectVerdict.rfind("0 of", 0) == 0 && updates == 1U,
                   "whole level: " + verdict + "; sub-rectangle (" + std::to_string(updates) + " UpdateTexture): " + rectVerdict);
        });

        // 7. A format D3D11 lacks: L8 is expanded to BGRA8 (L, L, L, 1) on upload, D3D9's sampling result.
        guarded("texture-l8", [&] {
            TextureContext context{};
            context.source_ = 2U;
            context.format_ = 6U; // L8
            context.mipmapLevels_ = 1U;
            context.width_ = 8U;
            context.height_ = 8U;
            const boost::shared_ptr<Texture> texture = CreateTexture(&context);
            const TextureLockRect lock = texture->Lock(0, RECT{}, 0);
            for (std::uint32_t y = 0; y < 8U; ++y) {
                for (std::uint32_t x = 0; x < 8U; ++x) {
                    static_cast<std::uint8_t*>(lock.bits)[y * static_cast<std::uint32_t>(lock.pitch) + x] = static_cast<std::uint8_t>(x * 16U + y);
                }
            }
            texture->Unlock(lock);
            GpuTexture* const gpu = dynamic_cast<TextureDiligent*>(texture.get())->GetGpu();
            std::vector<std::uint8_t> bytes;
            std::uint32_t width = 0;
            std::uint32_t height = 0;
            std::vector<std::uint32_t> pixels;
            if (gpu != nullptr && mHost->ReadTextureBgra8(gpu->Texture(), &bytes, &width, &height)) {
                pixels.resize(bytes.size() / 4U);
                std::memcpy(pixels.data(), bytes.data(), bytes.size());
            }
            mDrawPath->InvalidateTargets();
            const std::string verdict = compare(pixels, 8, [](const std::uint32_t x, const std::uint32_t y) {
                const std::uint32_t l = x * 16U + y;
                return 0xFF000000U | (l << 16U) | (l << 8U) | l;
            });
            record("texture-l8", verdict.rfind("0 of", 0) == 0, verdict);
        });

        // 8. Dynamic vertex buffer: DISCARD, then NOOVERWRITE in the same frame, then the first map of a
        //    new frame rewrites the valid range, then a plain lock renames the buffer again.
        guarded("dynamic-buffer", [&] {
            VertexBufferContext context{};
            context.usage_ = 2U;
            context.type_ = 1U;
            context.vertexCount_ = 64U;
            context.stride_ = 4U;
            const boost::shared_ptr<VertexBuffer> buffer = CreateVertexBuffer(&context);
            auto* const diligentBuffer = dynamic_cast<VertexBufferDiligent*>(buffer.get());
            GpuUploadStats& stats = mGpu->Stats();
            const std::uint64_t discard0 = stats.mapDiscard.load();
            const std::uint64_t noOverwrite0 = stats.mapNoOverwrite.load();
            const std::uint64_t restore0 = stats.mapFrameRestore.load();
            std::memset(buffer->Lock(0, 64, MohoD3DLockFlags::Discard), 0xA1, 64);
            buffer->Unlock();
            diligentBuffer->GetBuffer();
            std::memset(buffer->Lock(64, 64, MohoD3DLockFlags::NoOverwrite), 0xB2, 64);
            buffer->Unlock();
            Diligent::IBuffer* gpuBuffer = diligentBuffer->GetBuffer();
            std::vector<std::uint8_t> bytes;
            const bool read1 = mHost->ReadBuffer(gpuBuffer, 128, &bytes);
            bool ok = read1 && std::all_of(bytes.begin(), bytes.begin() + 64, [](const std::uint8_t b) { return b == 0xA1; }) &&
                      std::all_of(bytes.begin() + 64, bytes.end(), [](const std::uint8_t b) { return b == 0xB2; });
            mGpu->AdvanceFrame();
            gpuBuffer = diligentBuffer->GetBuffer();
            const bool read2 = mHost->ReadBuffer(gpuBuffer, 128, &bytes);
            ok = ok && read2 && std::all_of(bytes.begin(), bytes.begin() + 64, [](const std::uint8_t b) { return b == 0xA1; }) &&
                 std::all_of(bytes.begin() + 64, bytes.end(), [](const std::uint8_t b) { return b == 0xB2; });
            std::memset(buffer->Lock(0, 32, MohoD3DLockFlags::None), 0xC3, 32);
            buffer->Unlock();
            gpuBuffer = diligentBuffer->GetBuffer();
            const bool read3 = mHost->ReadBuffer(gpuBuffer, 128, &bytes);
            ok = ok && read3 && std::all_of(bytes.begin(), bytes.begin() + 32, [](const std::uint8_t b) { return b == 0xC3; }) &&
                 std::all_of(bytes.begin() + 32, bytes.begin() + 64, [](const std::uint8_t b) { return b == 0xA1; }) &&
                 std::all_of(bytes.begin() + 64, bytes.end(), [](const std::uint8_t b) { return b == 0xB2; });
            const std::uint64_t discards = stats.mapDiscard.load() - discard0;
            const std::uint64_t noOverwrites = stats.mapNoOverwrite.load() - noOverwrite0;
            const std::uint64_t restores = stats.mapFrameRestore.load() - restore0;
            ok = ok && discards == 2U && noOverwrites == 1U && restores == 1U;
            record("dynamic-buffer", ok,
                   "contents " + std::string(read1 && read2 && read3 ? "read" : "unreadable") + "; maps: discard " + std::to_string(discards) +
                       " (expected 2), no-overwrite " + std::to_string(noOverwrites) + " (1), frame restore " + std::to_string(restores) + " (1)");
        });

        // 9. Static index buffer: created with its shadow (no UpdateBuffer), then one UpdateBuffer for a relock.
        guarded("static-buffer", [&] {
            IndexBufferContext context{};
            context.format_ = 1U;
            context.size_ = 32U;
            context.type_ = 1U;
            const boost::shared_ptr<IndexBuffer> buffer = CreateIndexBuffer(&context);
            auto* const diligentBuffer = dynamic_cast<IndexBufferDiligent*>(buffer.get());
            std::int16_t* indices = buffer->Lock(0, 0, MohoD3DLockFlags::None);
            for (int index = 0; index < 32; ++index) {
                indices[index] = static_cast<std::int16_t>(index);
            }
            buffer->Unlock();
            const std::uint64_t updates0 = mGpu->Stats().updateBuffer.load();
            std::vector<std::uint8_t> bytes;
            bool ok = mHost->ReadBuffer(diligentBuffer->GetBuffer(), 64, &bytes);
            for (int index = 0; ok && index < 32; ++index) {
                ok = bytes[index * 2] == index && bytes[index * 2 + 1] == 0;
            }
            const std::uint64_t updatesCreate = mGpu->Stats().updateBuffer.load() - updates0;
            indices = buffer->Lock(16, 16, MohoD3DLockFlags::None);
            for (int index = 0; index < 8; ++index) {
                indices[index] = static_cast<std::int16_t>(100 + index);
            }
            buffer->Unlock();
            ok = ok && mHost->ReadBuffer(diligentBuffer->GetBuffer(), 64, &bytes);
            for (int index = 0; ok && index < 32; ++index) {
                const int expected = (index >= 8 && index < 16) ? 100 + (index - 8) : index;
                ok = bytes[index * 2] == expected && bytes[index * 2 + 1] == 0;
            }
            const std::uint64_t updatesRelock = mGpu->Stats().updateBuffer.load() - updates0 - updatesCreate;
            record("static-buffer", ok && updatesCreate == 0U && updatesRelock == 1U,
                   "UpdateBuffer at creation " + std::to_string(updatesCreate) + " (expected 0), after a relock " + std::to_string(updatesRelock) + " (1)");
        });

        // 10. A cube target: every face binds and clears (render-target views per face).
        guarded("cube-faces", [&] {
            CubeRenderTargetContext context{};
            context.dimension_ = 16U;
            context.format_ = 2U;
            const boost::shared_ptr<CubeRenderTarget> cube = CreateCubeRenderTarget(&context);
            for (int face = 0; face < 6; ++face) {
                OutputContext output;
                output.cubeTarget = cube;
                output.face = face;
                ClearTarget(&output);
                Clear(true, false, false, 0xFF000000U | static_cast<std::uint32_t>(face * 40), 1.0f, 0);
            }
            GpuTexture* const gpu = dynamic_cast<CubeRenderTargetDiligent*>(cube.get())->GetGpu();
            const bool ok = gpu != nullptr && gpu->ShaderResourceView() != nullptr && gpu->RenderTargetView(5) != nullptr;
            record("cube-faces", ok, ok ? "6 faces bound and cleared" : "no cube texture or views");
        });

        // 11. A shader-readable depth target (DepthStencilTargetContext::field0x10_) has a DSV and an SRV.
        guarded("depth-shader-readable", [&] {
            const DepthStencilTargetContext context(32U, 32U, 3U, true);
            const boost::shared_ptr<DepthStencilTarget> depth = CreateDepthStencilTarget(&context);
            GpuTexture* const gpu = dynamic_cast<DepthStencilTargetDiligent*>(depth.get())->GetGpu();
            const bool ok = gpu != nullptr && gpu->DepthStencilView() != nullptr && gpu->ShaderResourceView() != nullptr;
            OutputContext output;
            output.depthStencil = depth;
            ClearTarget(&output);
            viewport(0, 0, 32, 32);
            Clear(false, true, true, 0U, 0.5f, 7);
            viewport(4, 4, 8, 8);
            Clear(false, true, true, 0U, 0.25f, 3); // the quad path for depth and stencil
            record("depth-shader-readable", ok, ok ? "DSV and SRV; full and viewport depth/stencil clears" : "missing view");
        });

        // 12. Reset(context) with another head size resizes the swap chain and remakes the head (how a
        //     resize arrives, DeviceD3D9::Reset, D3D9Interfaces.cpp:2210-2244), then back.
        guarded("reset-resize", [&] {
            const std::uint32_t width = mHost->GetHeadWidth();
            const std::uint32_t height = mHost->GetHeadHeight();
            DeviceContext context = mDeviceContext;
            context.mHeads[0].mWidth = width / 2U;
            context.mHeads[0].mHeight = height / 2U;
            Reset(&context);
            // `small` is a Windows header macro (rpcndr.h); keep the sizes, not the replaced heads' contexts.
            const std::uint32_t shrunkWidth = mHeads[0].surface->GetContext()->width_;
            const std::uint32_t shrunkHeight = mHeads[0].surface->GetContext()->height_;
            const bool resized = mHost->GetHeadWidth() == width / 2U && shrunkWidth == width / 2U && shrunkHeight == height / 2U;
            context.mHeads[0].mWidth = width;
            context.mHeads[0].mHeight = height;
            Reset(&context);
            const std::uint32_t backWidth = mHeads[0].surface->GetContext()->width_;
            const std::uint32_t backHeight = mHeads[0].surface->GetContext()->height_;
            const bool restored = mHost->GetHeadWidth() == width && backWidth == width && backHeight == height;
            record("reset-resize", resized && restored,
                   std::to_string(width) + "x" + std::to_string(height) + " -> " + std::to_string(shrunkWidth) + "x" + std::to_string(shrunkHeight) +
                       " -> " + std::to_string(backWidth) + "x" + std::to_string(backHeight));
        });

        // Back to the state Setup leaves: head 0 bound, its viewport, InitState, the slot census untouched.
        mBound = mHeads[0];
        BindOutput();
        const D3DVIEWPORT9 full{0U, 0U, mHost->GetHeadWidth(), mHost->GetHeadHeight(), 0.0f, 1.0f};
        mViewport = full;
        mDrawPath->SetViewport(ViewportDesc{0.0F, 0.0F, static_cast<float>(full.Width), static_cast<float>(full.Height), 0.0F, 1.0F});
        mShadow.ResetToSetupState();
        mHost->DrainDebugLayer(&mDebugLayerMessages, 8);
        const DebugLayerCounts layerAfter = mHost->GetDebugLayerCounts();
        mSelfTestDebugLayer.error = layerAfter.error - layerBefore.error;
        mSelfTestDebugLayer.corruption = layerAfter.corruption - layerBefore.corruption;
        mSelfTestDebugLayer.warning = layerAfter.warning - layerBefore.warning;
        for (int slot = 0; slot < kSlotCount; ++slot) {
            mSlotCalls[slot] = savedCalls[slot];
        }
        std::size_t passed = 0;
        for (const SelfTestResult& result : mSelfTestResults) {
            passed += result.pass ? 1U : 0U;
        }
        gpg::Logf("[gal-diligent] self test: %zu of %zu passed, debug layer errors %llu, warnings %llu", passed, mSelfTestResults.size(),
                  static_cast<unsigned long long>(mSelfTestDebugLayer.error + mSelfTestDebugLayer.corruption),
                  static_cast<unsigned long long>(mSelfTestDebugLayer.warning));
    }
} // namespace gpg::gal::diligent
