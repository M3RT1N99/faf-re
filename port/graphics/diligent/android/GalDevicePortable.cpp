// The gal base-class members galplay needs on Android (M7a1), which the engine defines in TUs that
// cannot build there: src/sdk/gpg/gal/Device.cpp (it includes the D3D9 and D3D10 backends and
// <Windows.h>) and src/sdk/gpg/gal/backends/d3d9/D3D9Interfaces.cpp (the trivial constructors and
// destructors of the interfaces' base classes). The definitions are those files' own, copied member
// for member; Device::GetInstance answers the device galplay installed (SetPortableInstance), because
// galplay creates its device through DeviceFactory.h, not Device::Create.
//
// Built for Android only (port/android/CMakeLists.txt, with the engine's flags and shim);
// port_graphics.props leaves port/graphics/diligent/android out of the Windows graphics build, and the
// default main.exe does not see it at all.

#include "gpg/gal/CubeRenderTarget.hpp"
#include "gpg/gal/Device.hpp"
#include "gpg/gal/DeviceContext.hpp"
#include "gpg/gal/EffectTechnique.hpp"
#include "gpg/gal/Error.hpp"
#include "gpg/gal/Head.hpp"
#include "gpg/gal/IndexBuffer.hpp"
#include "gpg/gal/OutputContext.hpp"
#include "gpg/gal/PipelineState.hpp"
#include "gpg/gal/RenderTarget.hpp"
#include "gpg/core/streams/MemBufferStream.h"
#include "gpg/core/utils/Logging.h"
#include "port/graphics/diligent/android/GalDevicePortable.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace gpg::gal
{
    namespace
    {
        std::atomic<Device*> gPortableInstance{nullptr};

        // Device.cpp: the shipped file name in the message.
        [[noreturn]] void ThrowDeviceContextError(const int line, const char* const message)
        {
            throw Error(msvc8::string("c:\\work\\rts\\main\\code\\src\\libs\\gpggal\\Device.cpp"), line, msvc8::string(message));
        }

        [[nodiscard]] int CountHeadVectorEntries(const msvc8::vector<Head>& heads) noexcept
        {
            const Head* const start = heads.begin();
            if (start == nullptr) {
                return 0;
            }
            return static_cast<int>(heads.end() - start);
        }
    } // namespace

    // ---- Device.cpp -------------------------------------------------------------------------------

    HeadSampleOption::HeadSampleOption(const HeadSampleOption& other)
        : sampleType(other.sampleType),
          sampleQuality(other.sampleQuality),
          label(other.label)
    {}

    DeviceContext::DeviceContext(const DeviceApi deviceType)
        : mDeviceType(deviceType)
    {}

    DeviceContext::DeviceContext(const DeviceContext& other)
        : mDeviceType(other.mDeviceType),
          mValidate(other.mValidate),
          mAdapter(other.mAdapter),
          mVSync(other.mVSync),
          mHWBasedInstancing(other.mHWBasedInstancing),
          mSupportsFloat16(other.mSupportsFloat16),
          mVertexShaderProfile(other.mVertexShaderProfile),
          mPixelShaderProfile(other.mPixelShaderProfile),
          mMaxPrimitiveCount(other.mMaxPrimitiveCount),
          mMaxVertexCount(other.mMaxVertexCount),
          mHeads(other.mHeads)
    {}

    DeviceContext& DeviceContext::operator=(const DeviceContext& other)
    {
        mDeviceType = other.mDeviceType;
        mValidate = other.mValidate;
        mAdapter = other.mAdapter;
        mVSync = other.mVSync;
        mHWBasedInstancing = other.mHWBasedInstancing;
        mSupportsFloat16 = other.mSupportsFloat16;
        mVertexShaderProfile = other.mVertexShaderProfile;
        mPixelShaderProfile = other.mPixelShaderProfile;
        mMaxPrimitiveCount = other.mMaxPrimitiveCount;
        mMaxVertexCount = other.mMaxVertexCount;
        mHeads = other.mHeads;
        return *this;
    }

    Device* Device::GetInstance()
    {
        return gPortableInstance.load();
    }

    Device::Device() = default;

    Device::~Device() = default;

    int DeviceContext::GetHeadCount() const
    {
        return CountHeadVectorEntries(mHeads);
    }

    const Head& DeviceContext::GetHead(const std::uint32_t index) const
    {
        const Head* const start = mHeads.begin();
        const Head* const finish = mHeads.end();
        const std::uint32_t count = (start == nullptr) ? 0U : static_cast<std::uint32_t>(finish - start);
        if ((start == nullptr) || (index >= count)) {
            ThrowDeviceContextError(91, "invalid head index");
        }
        return start[index];
    }

    Head& DeviceContext::GetHead(const std::uint32_t index)
    {
        Head* const start = mHeads.begin();
        const Head* const finish = mHeads.end();
        const std::uint32_t count = (start == nullptr) ? 0U : static_cast<std::uint32_t>(finish - start);
        if ((start == nullptr) || (index >= count)) {
            ThrowDeviceContextError(97, "invalid head index");
        }
        return start[index];
    }

    void DeviceContext::AddHead(const Head& head)
    {
        mHeads.insert(mHeads.end(), 1U, head);
    }

    void Device::ClearTarget(const OutputContext* const context)
    {
        outputContext_ = *context;
    }

    void Device::GetContext(OutputContext* const outContext)
    {
        *outContext = outputContext_;
    }

    // ---- D3D9Interfaces.cpp: the interfaces' base classes -------------------------------------------

    EffectTechnique::EffectTechnique() = default;
    EffectTechnique::~EffectTechnique() = default;
    IndexBuffer::IndexBuffer() = default;
    IndexBuffer::~IndexBuffer() = default;
    RenderTarget::RenderTarget() = default;
    RenderTarget::~RenderTarget() = default;
    CubeRenderTarget::CubeRenderTarget() = default;
    CubeRenderTarget::~CubeRenderTarget() = default;
    PipelineState::PipelineState() = default;
    PipelineState::~PipelineState() = default;

    namespace diligent
    {
        void SetPortableInstance(Device* const device)
        {
            gPortableInstance.store(device);
        }
    } // namespace diligent
} // namespace gpg::gal

// ---- gpg/core stand-ins ---------------------------------------------------------------------------
//
// The backend logs through gpg::Logf/Warnf and allocates GetTexture2D's output with gpg::AllocMemBuffer.
// Their engine TUs (Logging.cpp, MemBufferStream.cpp) pull in the engine's log targets, streams and
// reflection registry, none of which galplay has. These keep the engine's signatures and behaviour for
// what the backend uses: AllocMemBuffer as MemBufferStream.cpp:256-269 (zeroed malloc, freed with
// free), the log lines to galplay's sink (SetPortableLogSink) or logcat.

namespace
{
    std::atomic<gpg::gal::diligent::PortableLogSink> gLogSink{nullptr};

    void Emit(const int level, const char* const fmt, va_list args)
    {
        char line[2048];
        std::vsnprintf(line, sizeof(line), fmt, args);
        if (const gpg::gal::diligent::PortableLogSink sink = gLogSink.load()) {
            sink(level, line);
        }
    }
} // namespace

namespace gpg
{
    void Logf(const char* const fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        Emit(1, fmt, args);
        va_end(args);
    }

    void Warnf(const char* const fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        Emit(2, fmt, args);
        va_end(args);
    }

    void Debugf(const char* const fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        Emit(0, fmt, args);
        va_end(args);
    }

    MemBuffer<char> AllocMemBuffer(const std::size_t size)
    {
        char* const buffer = static_cast<char*>(std::malloc(size));
        if (buffer != nullptr && size != 0) {
            std::memset(buffer, 0, size);
        }
        boost::shared_ptr<char> owner(buffer, &std::free);
        char* const end = (buffer == nullptr) ? nullptr : (buffer + size);
        return MemBuffer<char>(owner, buffer, end);
    }
} // namespace gpg

namespace gpg::gal::diligent
{
    void SetPortableLogSink(const PortableLogSink sink)
    {
        gLogSink.store(sink);
    }
} // namespace gpg::gal::diligent
