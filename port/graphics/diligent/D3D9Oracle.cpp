#include "D3D9Oracle.h"

#include <cstdio>
#include <cstring>

#include "gpg/gal/Error.hpp"

namespace gpg::gal::diligent
{
    namespace
    {
        // D3D9Interfaces.cpp:166-172.
        constexpr std::uint32_t kVertexShaderModel20 = 0xFFFE0200U;
        constexpr std::uint32_t kVertexShaderModel30 = 0xFFFE0300U;
        constexpr std::uint32_t kPixelShaderModel20 = 0xFFFF0200U;
        constexpr std::uint32_t kPixelShaderModel30 = 0xFFFF0300U;
        constexpr std::uint32_t kDeclTypeFloat16_2 = 0x100U;
        constexpr std::uint32_t kDeclTypeFloat16_4 = 0x200U;
        constexpr std::uint32_t kVendorIdNvidia = 4318U;
        constexpr std::uint32_t kVendorIdAti = 32902U;
        constexpr std::uint32_t kAtiDeviceRadeonX800 = 10626U;
        constexpr std::uint32_t kAtiDeviceRadeonX850 = 10658U;
        constexpr std::uint32_t kAtiDeviceRadeonX1650 = 10754U;
        constexpr D3DFORMAT kInstancingFourCC = static_cast<D3DFORMAT>(MAKEFOURCC('I', 'N', 'S', 'T'));

        struct D3D9FormatToMohoPair final
        {
            std::uint32_t d3dFormat = 0U;
            std::uint32_t mohoFormat = 0U;
        };

        // D3D9Interfaces.cpp:686-746 (binary 0x00D47FF0), verbatim.
        constexpr D3D9FormatToMohoPair kD3D9FormatToMohoPairs[60] = {
            {0x00000000U, 0x00000014U}, {0x00000014U, 0x00000001U}, {0x00000015U, 0x00000002U},
            {0x00000016U, 0x00000003U}, {0x00000017U, 0x00000004U}, {0x00000018U, 0x00000014U},
            {0x00000019U, 0x00000014U}, {0x0000001AU, 0x00000014U}, {0x0000001BU, 0x00000014U},
            {0x0000001CU, 0x00000005U}, {0x0000001DU, 0x00000014U}, {0x0000001EU, 0x00000014U},
            {0x0000001FU, 0x00000014U}, {0x00000020U, 0x00000014U}, {0x00000021U, 0x00000014U},
            {0x00000022U, 0x00000014U}, {0x00000023U, 0x00000014U}, {0x00000024U, 0x00000014U},
            {0x00000028U, 0x00000014U}, {0x00000029U, 0x00000014U}, {0x00000032U, 0x00000006U},
            {0x00000033U, 0x00000007U}, {0x00000034U, 0x00000014U}, {0x0000003CU, 0x00000014U},
            {0x0000003DU, 0x00000014U}, {0x0000003EU, 0x00000014U}, {0x0000003FU, 0x00000014U},
            {0x00000040U, 0x00000014U}, {0x00000043U, 0x00000014U}, {0x59565955U, 0x00000014U},
            {0x47424752U, 0x00000014U}, {0x32595559U, 0x00000014U}, {0x42475247U, 0x00000014U},
            {0x31545844U, 0x00000008U}, {0x32545844U, 0x00000009U}, {0x33545844U, 0x0000000AU},
            {0x34545844U, 0x0000000BU}, {0x35545844U, 0x0000000CU}, {0x00000046U, 0x00000014U},
            {0x00000047U, 0x00000014U}, {0x00000049U, 0x00000014U}, {0x0000004BU, 0x00000014U},
            {0x0000004DU, 0x00000014U}, {0x0000004FU, 0x00000014U}, {0x00000050U, 0x00000014U},
            {0x00000052U, 0x00000014U}, {0x00000053U, 0x00000014U}, {0x00000051U, 0x00000014U},
            {0x00000064U, 0x00000014U}, {0x00000065U, 0x00000014U}, {0x00000066U, 0x00000014U},
            {0x0000006EU, 0x00000014U}, {0x3154454DU, 0x00000014U}, {0x0000006FU, 0x0000000DU},
            {0x00000070U, 0x0000000EU}, {0x00000071U, 0x0000000FU}, {0x00000072U, 0x00000010U},
            {0x00000073U, 0x00000011U}, {0x00000074U, 0x00000012U}, {0x00000075U, 0x00000014U},
        };

        // D3D9Interfaces.cpp:750-760 (binary 0x00D42E84): render-target format tokens 1..7.
        constexpr std::uint32_t kGalFormatToD3D[9] = {
            0x00000000U, 0x00000023U, 0x00000015U, 0x00000016U, 0x00000019U,
            0x00000018U, 0x00000017U, 0x00000022U, 0x00000000U,
        };

        // D3D9Interfaces.cpp:763-771 (binary 0x00D42194): depth-stencil format tokens 1..6.
        constexpr std::uint32_t kDepthStencilFormatToD3D[8] = {
            0x00000000U, 0x00000047U, 0x00000049U, 0x0000004BU,
            0x0000004DU, 0x0000004FU, 0x00000050U, 0x00000000U,
        };

        unsigned int AlignToDword(const unsigned int value) noexcept
        {
            return (value + 3U) & ~3U;
        }

        template <class Call>
        void CheckD3DCall(const char* const file, const int line, Call&& call)
        {
            const HRESULT result = call();
            if (FAILED(result)) {
                ThrowGalErrorFromHresult(file, line, result);
            }
        }

        /**
         * The profile token D3DXGetVertexShaderProfile/D3DXGetPixelShaderProfile give a HAL device
         * of these caps, mapped through the D3D9 backend's tables (D3D9Interfaces.cpp:358-414: vs
         * "undefined", vs_1_1, vs_2_0, vs_2_a, vs_3_0; ps up to ps_3_0 = 8). Exact for shader model
         * 3.0 hardware, where D3DX answers vs_3_0/ps_3_0; for 2.x parts it answers the base 2.0
         * profile, where D3DX would also consider vs_2_a/ps_2_a/ps_2_b from the PS20/VS20 caps.
         */
        int VertexShaderProfileToken(const D3DCAPS9& caps) noexcept
        {
            if (caps.VertexShaderVersion >= kVertexShaderModel30) {
                return 4;
            }
            if (caps.VertexShaderVersion >= kVertexShaderModel20) {
                return 2;
            }
            return (caps.VertexShaderVersion >= D3DVS_VERSION(1, 1)) ? 1 : 0;
        }

        int PixelShaderProfileToken(const D3DCAPS9& caps) noexcept
        {
            if (caps.PixelShaderVersion >= kPixelShaderModel30) {
                return 8;
            }
            if (caps.PixelShaderVersion >= kPixelShaderModel20) {
                return 5;
            }
            for (int minor = 4; minor >= 1; --minor) {
                if (caps.PixelShaderVersion >= D3DPS_VERSION(1, static_cast<unsigned>(minor))) {
                    return minor;
                }
            }
            return 0;
        }

        LRESULT CALLBACK HiddenWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
        {
            return ::DefWindowProcW(window, message, wParam, lParam);
        }
    } // namespace

    [[noreturn]] void ThrowGalError(const char* const file, const int line, const char* const message)
    {
        throw Error(msvc8::string(file), line, msvc8::string(message != nullptr ? message : ""));
    }

    [[noreturn]] void ThrowGalErrorFromHresult(const char* const file, const int line, const long hresult)
    {
        throw Error(msvc8::string(file), line, msvc8::string(::gpg::D3DErrorToString(hresult)));
    }

    D3D9Oracle::~D3D9Oracle()
    {
        Shutdown();
    }

    bool D3D9Oracle::Init(std::string* const error)
    {
        Shutdown();
        mDirect3D = ::Direct3DCreate9(D3D_SDK_VERSION);
        if (mDirect3D == nullptr) {
            *error = "Direct3DCreate9 failed";
            return false;
        }

        if (FAILED(mDirect3D->GetDeviceCaps(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &mHalCaps))) {
            *error = "IDirect3D9::GetDeviceCaps(HAL) failed";
            Shutdown();
            return false;
        }

        const HINSTANCE instance = ::GetModuleHandleW(nullptr);
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = HiddenWindowProc;
        windowClass.hInstance = instance;
        windowClass.lpszClassName = L"FafGalDiligentOracle";
        ::RegisterClassExW(&windowClass); // fails harmlessly when already registered
        // WS_POPUP without WS_VISIBLE, never passed to ShowWindow: it exists only as the device's
        // focus window.
        const HWND window = ::CreateWindowExW(
            WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, windowClass.lpszClassName, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr,
            instance, nullptr
        );
        if (window == nullptr) {
            *error = "cannot create the oracle's hidden window";
            Shutdown();
            return false;
        }
        mWindow = window;

        D3DPRESENT_PARAMETERS parameters{};
        parameters.BackBufferWidth = 1;
        parameters.BackBufferHeight = 1;
        parameters.BackBufferFormat = D3DFMT_UNKNOWN;
        parameters.BackBufferCount = 1;
        parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
        parameters.hDeviceWindow = window;
        parameters.Windowed = TRUE;
        parameters.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

        // MULTITHREADED as the D3D9 backend (behaviour 0x44, D3D9Interfaces.cpp:1772): the resource
        // prefetch thread calls CreateTexture and GetTexture2D. No FPU_PRESERVE, also as D3D9: the
        // engine's x87 mode is the one a D3D9 device leaves. NOWINDOWCHANGES keeps D3D9 off the
        // hidden window entirely.
        const DWORD flags[] = {
            D3DCREATE_MULTITHREADED | D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_NOWINDOWCHANGES,
            D3DCREATE_MULTITHREADED | D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_NOWINDOWCHANGES,
        };
        const char* const flagNames[] = {"NULLREF, HWVP", "NULLREF, SWVP"};
        HRESULT result = E_FAIL;
        for (int index = 0; index < 2 && mDevice == nullptr; ++index) {
            D3DPRESENT_PARAMETERS attempt = parameters;
            result = mDirect3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_NULLREF, window, flags[index], &attempt, &mDevice);
            if (SUCCEEDED(result)) {
                mDeviceDescription = flagNames[index];
            }
        }
        if (mDevice == nullptr) {
            char text[128];
            std::snprintf(text, sizeof(text), "cannot create the NULLREF D3D9 device (hr 0x%08lX)", static_cast<unsigned long>(result));
            *error = text;
            Shutdown();
            return false;
        }
        return true;
    }

    void D3D9Oracle::Shutdown()
    {
        if (mDevice != nullptr) {
            mDevice->Release();
            mDevice = nullptr;
        }
        if (mDirect3D != nullptr) {
            mDirect3D->Release();
            mDirect3D = nullptr;
        }
        if (mWindow != nullptr) {
            ::DestroyWindow(static_cast<HWND>(mWindow));
            mWindow = nullptr;
        }
    }

    void D3D9Oracle::FillCapabilities(const DeviceContext& requested, DeviceContext& out) const
    {
        out = requested;

        const unsigned int headCount = static_cast<unsigned int>(requested.GetHeadCount());
        const unsigned int adapterCount = mDirect3D->GetAdapterCount();
        if (headCount > adapterCount) {
            ThrowGalError("DeviceDiligent.cpp", __LINE__, "invalid head count specified in device context");
        }

        for (unsigned int adapterIndex = 0; adapterIndex < headCount; ++adapterIndex) {
            Head& head = out.mHeads[adapterIndex];

            head.adapterModes.clear();
            msvc8::vector<HeadAdapterMode> modes;
            GetModesForAdapter(modes, static_cast<int>(adapterIndex));
            for (const HeadAdapterMode& mode : modes) {
                head.adapterModes.push_back(mode);
            }

            head.validFormats1.clear();
            for (int formatToken = 1; formatToken < 8; ++formatToken) {
                const HRESULT result = mDirect3D->CheckDeviceFormat(
                    adapterIndex, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE,
                    static_cast<D3DFORMAT>(RenderTargetFormatToD3D(static_cast<std::uint32_t>(formatToken)))
                );
                if (result >= 0) {
                    head.validFormats1.push_back(formatToken);
                }
            }

            head.validFormats2.clear();
            for (int formatToken = 1; formatToken < 20; ++formatToken) {
                const HRESULT result = mDirect3D->CheckDeviceFormat(
                    adapterIndex, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, 0U, D3DRTYPE_TEXTURE,
                    static_cast<D3DFORMAT>(FormatGalToD3D(static_cast<std::uint32_t>(formatToken)))
                );
                if (result >= 0) {
                    head.validFormats2.push_back(formatToken);
                }
            }

            // As D3D9: the vendor of adapter 0 picks the list (D3D9Interfaces.cpp:2059-2132).
            D3DADAPTER_IDENTIFIER9 identifier{};
            static_cast<void>(mDirect3D->GetAdapterIdentifier(0U, 0U, &identifier));
            const BOOL windowedCheck = (!head.mWindowed) ? TRUE : FALSE;
            head.mStrs.clear();
            if (identifier.VendorId != kVendorIdNvidia) {
                for (unsigned int sampleType = 2U; sampleType <= 16U; ++sampleType) {
                    if (mDirect3D->CheckDeviceMultiSampleType(
                            adapterIndex, D3DDEVTYPE_HAL, D3DFMT_A8R8G8B8, windowedCheck,
                            static_cast<D3DMULTISAMPLE_TYPE>(sampleType), nullptr
                        ) < 0) {
                        continue;
                    }
                    HeadSampleOption option{};
                    option.sampleType = sampleType;
                    option.sampleQuality = 0U;
                    char label[16] = {};
                    std::snprintf(label, sizeof(label), "%u", sampleType);
                    option.label.assign_owned(label);
                    head.mStrs.push_back(option);
                }
            } else {
                struct SampleCandidate final
                {
                    unsigned int sampleType = 0U;
                    unsigned int sampleQuality = 0U;
                    const char* label = nullptr;
                };
                static constexpr SampleCandidate kNvidiaSampleCandidates[] = {
                    {2U, 0U, "2"}, {4U, 0U, "4"}, {4U, 2U, "8"}, {8U, 0U, "8Q"}, {4U, 4U, "16"}, {8U, 2U, "16Q"},
                };
                for (const SampleCandidate& candidate : kNvidiaSampleCandidates) {
                    DWORD qualityLevels = 0U;
                    const HRESULT checkResult = mDirect3D->CheckDeviceMultiSampleType(
                        adapterIndex, D3DDEVTYPE_HAL, D3DFMT_A8R8G8B8, windowedCheck,
                        static_cast<D3DMULTISAMPLE_TYPE>(candidate.sampleType), &qualityLevels
                    );
                    if ((checkResult < 0) || (qualityLevels <= candidate.sampleQuality)) {
                        continue;
                    }
                    if (candidate.sampleType == 4U && candidate.sampleQuality == 4U) {
                        if (mDirect3D->CheckDeviceMultiSampleType(
                                adapterIndex, D3DDEVTYPE_HAL, D3DFMT_A8R8G8B8, windowedCheck,
                                static_cast<D3DMULTISAMPLE_TYPE>(16U), nullptr
                            ) >= 0) {
                            HeadSampleOption option{};
                            option.sampleType = 16U;
                            option.sampleQuality = 0U;
                            option.label.assign_owned("16");
                            head.mStrs.push_back(option);
                        }
                        continue;
                    }
                    HeadSampleOption option{};
                    option.sampleType = candidate.sampleType;
                    option.sampleQuality = candidate.sampleQuality;
                    option.label.assign_owned(candidate.label);
                    head.mStrs.push_back(option);
                }
            }
        }

        D3DCAPS9 caps{};
        const unsigned int capsAdapter = static_cast<unsigned int>(out.mAdapter) < adapterCount
                                             ? static_cast<unsigned int>(out.mAdapter)
                                             : 0U;
        if (mDirect3D->GetDeviceCaps(capsAdapter, D3DDEVTYPE_HAL, &caps) < 0) {
            ThrowGalError("DeviceDiligent.cpp", __LINE__, "unable to retreive device caps");
        }

        // CheckHardwareInstancingSupport (D3D9Interfaces.cpp:538-578) without the D3DRS_POINTSIZE
        // probe, which needs a HAL device: shader model 3.0 parts always instance; older ones when
        // the driver exposes the 'INST' FourCC, or for the three ATI ids D3D9 whitelists.
        out.mHWBasedInstancing = true;
        if (caps.VertexShaderVersion < kVertexShaderModel30) {
            const bool fourCC = mDirect3D->CheckDeviceFormat(
                                    capsAdapter, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, 0U, D3DRTYPE_SURFACE, kInstancingFourCC
                                ) >= 0;
            D3DADAPTER_IDENTIFIER9 identifier{};
            static_cast<void>(mDirect3D->GetAdapterIdentifier(capsAdapter, 0U, &identifier));
            const bool whitelisted = identifier.VendorId == kVendorIdAti &&
                                     (identifier.DeviceId == kAtiDeviceRadeonX800 ||
                                      identifier.DeviceId == kAtiDeviceRadeonX850 ||
                                      identifier.DeviceId == kAtiDeviceRadeonX1650);
            out.mHWBasedInstancing = fourCC || whitelisted;
        }
        if (out.mValidate && !out.mHWBasedInstancing) {
            ThrowGalError("DeviceDiligent.cpp", __LINE__, "device does not support hardware based instancing");
        }

        out.mSupportsFloat16 = ((caps.DeclTypes & kDeclTypeFloat16_2) != 0U) && ((caps.DeclTypes & kDeclTypeFloat16_4) != 0U);
        out.mMaxPrimitiveCount = caps.MaxPrimitiveCount;
        out.mMaxVertexCount = caps.MaxVertexIndex;
        if (out.mValidate && (caps.VertexShaderVersion < kVertexShaderModel20)) {
            ThrowGalError("DeviceDiligent.cpp", __LINE__, "Vertex shader 2.0 required");
        }
        if (out.mValidate && (caps.PixelShaderVersion < kPixelShaderModel20)) {
            ThrowGalError("DeviceDiligent.cpp", __LINE__, "Pixel shader 2.0 required");
        }
        out.mVertexShaderProfile = VertexShaderProfileToken(caps);
        out.mPixelShaderProfile = PixelShaderProfileToken(caps);

        // DeviceD3D9::Setup's DXT check (D3D9Interfaces.cpp:1803-1815): texture token 12 on every head.
        for (int headIndex = 0; headIndex < out.GetHeadCount(); ++headIndex) {
            const Head& head = out.GetHead(static_cast<std::uint32_t>(headIndex));
            bool dxt = false;
            for (const int token : head.validFormats2) {
                dxt = dxt || token == 12;
            }
            if (!dxt) {
                ThrowGalError("DeviceDiligent.cpp", __LINE__, "Device does not support DXT texture formats");
            }
        }
    }

    void D3D9Oracle::GetModesForAdapter(msvc8::vector<HeadAdapterMode>& outModes, const int adapterIndex) const
    {
        outModes.clear();
        if (mDirect3D == nullptr || adapterIndex < 0 || static_cast<unsigned int>(adapterIndex) >= mDirect3D->GetAdapterCount()) {
            return;
        }
        const unsigned int modeCount = mDirect3D->GetAdapterModeCount(static_cast<unsigned int>(adapterIndex), D3DFMT_X8R8G8B8);
        for (unsigned int modeIndex = 0; modeIndex < modeCount; ++modeIndex) {
            D3DDISPLAYMODE mode{};
            if (mDirect3D->EnumAdapterModes(static_cast<unsigned int>(adapterIndex), D3DFMT_X8R8G8B8, modeIndex, &mode) < 0) {
                ThrowGalError("DeviceDiligent.cpp", __LINE__, "unable to enumerate adapters");
            }
            outModes.push_back(HeadAdapterMode{mode.Width, mode.Height, mode.RefreshRate});
        }
    }

    void D3D9Oracle::GetTexture2D(
        const void* const sourceData,
        const std::uint32_t sourceBytes,
        gpg::MemBuffer<char>* const outTextureData,
        std::uint32_t* const outWidth,
        int* const outHeight
    ) const
    {
        if (sourceData == nullptr) {
            return;
        }

        // The argument list of D3D9Interfaces.cpp:3164-3169, on the NULLREF device.
        D3DXIMAGE_INFO sourceImageInfo;
        IDirect3DTexture9* sourceTexture = nullptr;
        CheckD3DCall("DeviceDiligent.cpp", __LINE__, [&] {
            return D3DXCreateTextureFromFileInMemoryEx(
                mDevice, sourceData, sourceBytes, D3DX_DEFAULT_NONPOW2, D3DX_DEFAULT_NONPOW2, 1U, 0U, D3DFMT_UNKNOWN,
                D3DPOOL_SYSTEMMEM, D3DX_FILTER_NONE, D3DX_FILTER_NONE, 0U, &sourceImageInfo, nullptr, &sourceTexture
            );
        });

        IDirect3DSurface9* surface = nullptr;
        CheckD3DCall("DeviceDiligent.cpp", __LINE__, [&] { return sourceTexture->GetSurfaceLevel(0U, &surface); });
        D3DSURFACE_DESC sourceDesc;
        CheckD3DCall("DeviceDiligent.cpp", __LINE__, [&] { return surface->GetDesc(&sourceDesc); });

        *outWidth = sourceDesc.Width;
        *outHeight = static_cast<int>(sourceDesc.Height);

        IDirect3DTexture9* decodeTexture = nullptr;
        if (sourceDesc.Format != D3DFMT_DXT5) {
            CheckD3DCall("DeviceDiligent.cpp", __LINE__, [&] {
                return mDevice->CreateTexture(
                    AlignToDword(sourceDesc.Width), AlignToDword(sourceDesc.Height), 1U, 0U, D3DFMT_DXT5,
                    D3DPOOL_SYSTEMMEM, &decodeTexture, nullptr
                );
            });
            IDirect3DSurface9* decodeSurface = nullptr;
            CheckD3DCall("DeviceDiligent.cpp", __LINE__, [&] { return decodeTexture->GetSurfaceLevel(0U, &decodeSurface); });
            CheckD3DCall("DeviceDiligent.cpp", __LINE__, [&] {
                return D3DXLoadSurfaceFromSurface(decodeSurface, nullptr, nullptr, surface, nullptr, nullptr, D3DX_FILTER_NONE, 0U);
            });
            surface->Release();
            surface = decodeSurface;
        }

        D3DLOCKED_RECT lockedRect;
        CheckD3DCall("DeviceDiligent.cpp", __LINE__, [&] { return surface->LockRect(&lockedRect, nullptr, D3DLOCK_READONLY); });

        const unsigned int alignedWidth = AlignToDword(*outWidth);
        const unsigned int alignedHeight = AlignToDword(static_cast<unsigned int>(*outHeight));
        const std::size_t bytesPerRow = static_cast<std::size_t>(alignedWidth >> 2U) * 16U;
        const std::size_t rowCount = static_cast<std::size_t>(alignedHeight >> 2U);
        const std::size_t totalBytes = bytesPerRow * rowCount;
        if (outTextureData->Size() != totalBytes) {
            *outTextureData = gpg::AllocMemBuffer(totalBytes);
        }
        char* const destination = outTextureData->GetPtr(0U, 0U);
        const char* const source = static_cast<const char*>(lockedRect.pBits);
        if (static_cast<std::size_t>(lockedRect.Pitch) == bytesPerRow) {
            std::memcpy(destination, source, totalBytes);
        } else {
            for (std::size_t row = 0; row < rowCount; ++row) {
                std::memcpy(destination + row * bytesPerRow, source + row * static_cast<std::size_t>(lockedRect.Pitch), bytesPerRow);
            }
        }
        CheckD3DCall("DeviceDiligent.cpp", __LINE__, [&] { return surface->UnlockRect(); });

        surface->Release();
        sourceTexture->Release();
        if (decodeTexture != nullptr) {
            decodeTexture->Release();
        }
    }

    std::uint32_t D3D9Oracle::FormatGalToD3D(const std::uint32_t mohoFormat)
    {
        for (const D3D9FormatToMohoPair& pair : kD3D9FormatToMohoPairs) {
            if (pair.mohoFormat == mohoFormat) {
                return pair.d3dFormat;
            }
        }
        return 0U;
    }

    std::uint32_t D3D9Oracle::FormatD3D9ToMoho(const std::uint32_t d3dFormat)
    {
        for (const D3D9FormatToMohoPair& pair : kD3D9FormatToMohoPairs) {
            if (pair.d3dFormat == d3dFormat) {
                return pair.mohoFormat;
            }
        }
        return 0x14U;
    }

    std::uint32_t D3D9Oracle::RenderTargetFormatToD3D(const std::uint32_t formatToken)
    {
        return formatToken < (sizeof(kGalFormatToD3D) / sizeof(kGalFormatToD3D[0])) ? kGalFormatToD3D[formatToken] : 0U;
    }

    std::uint32_t D3D9Oracle::DepthStencilFormatToD3D(const std::uint32_t formatToken)
    {
        return formatToken < (sizeof(kDepthStencilFormatToD3D) / sizeof(kDepthStencilFormatToD3D[0]))
                   ? kDepthStencilFormatToD3D[formatToken]
                   : 0U;
    }
} // namespace gpg::gal::diligent
