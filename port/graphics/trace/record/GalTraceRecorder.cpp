// galtrace's recorder (GalTraceRecorder.h): the decorator Device, a decorator per gal object type,
// and the session that writes the file. Graphics build only; idle without `/galtrace <file>`.
//
// Rules the decorators keep, so that a replay of the file makes the backend do what it did here:
//   - every engine call is forwarded unchanged, in the order it was made, under one recursive lock
//     (the file's order is the order the backend saw);
//   - objects passed in are unwrapped to the backend's own objects; objects handed out are wrapped,
//     one decorator per backend object (the same backend object always gets the same id);
//   - payloads are copied at the point the backend takes them: file images at creation, locked
//     texture and buffer contents at Unlock (read-only texture locks keep what was read, for the
//     replay to compare), effect sources and macros at CreateEffect, and the compiled effect file
//     when the context asks the backend to read it from its cache path;
//   - calls the backend makes back into the active device while it runs a forwarded call are not
//     recorded (a replay makes the backend repeat them itself), and Device::GetInstance() returns
//     the backend to them (ResolveActiveDevice; the D3D9 backend static_casts it);
//   - the x87 and SSE control words are recorded whenever they change before a call (D3DX's DXT
//     encoder and texture filters depend on the x87 precision, docs/port/renderer.md);
//   - a call that throws leaves a Note record instead of its record, and the exception propagates.

#include "port/graphics/trace/record/GalTraceRecorder.h"

#include "port/graphics/diligent/DeviceFactory.h"
#include "port/graphics/trace/format/GalTraceIO.h"

#include "gpg/gal/CubeRenderTarget.hpp"
#include "gpg/gal/CubeRenderTargetContext.hpp"
#include "gpg/gal/CursorContext.hpp"
#include "gpg/gal/DepthStencilTarget.hpp"
#include "gpg/gal/DepthStencilTargetContext.hpp"
#include "gpg/gal/Device.hpp"
#include "gpg/gal/DeviceContext.hpp"
#include "gpg/gal/DrawContext.hpp"
#include "gpg/gal/DrawIndexedContext.hpp"
#include "gpg/gal/Effect.hpp"
#include "gpg/gal/EffectContext.hpp"
#include "gpg/gal/EffectMacro.hpp"
#include "gpg/gal/EffectTechnique.hpp"
#include "gpg/gal/EffectVariable.hpp"
#include "gpg/gal/Error.hpp"
#include "gpg/gal/Head.hpp"
#include "gpg/gal/IndexBuffer.hpp"
#include "gpg/gal/IndexBufferContext.hpp"
#include "gpg/gal/OutputContext.hpp"
#include "gpg/gal/PipelineState.hpp"
#include "gpg/gal/RenderTarget.hpp"
#include "gpg/gal/RenderTargetContext.hpp"
#include "gpg/gal/Texture.hpp"
#include "gpg/gal/TextureContext.hpp"
#include "gpg/gal/VertexBuffer.hpp"
#include "gpg/gal/VertexBufferContext.hpp"
#include "gpg/gal/VertexFormat.hpp"

#include <Windows.h>
#include <float.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace port::graphics::trace
{
    namespace
    {
        namespace gal = gpg::gal;
        using galtrace::ObjectType;
        using galtrace::Op;
        using galtrace::RecordBuilder;

        // ---- the session ----------------------------------------------------------------------

        struct ObjectEntry
        {
            std::uint32_t id = 0;
            ObjectType type = ObjectType::None;
            boost::weak_ptr<void> wrapper; // the decorator handed to the engine
        };

        struct Session
        {
            std::recursive_mutex mutex;
            galtrace::Writer writer;
            std::string path;
            bool open = false;
            std::uint32_t nextId = 1;
            std::uint32_t presents = 0;
            std::uint32_t stopAfterPresents = 0; // /galtraceframes: End after this many presents (0: at exit)
            DWORD deviceThread = 0;
            bool fpuKnown = false;
            unsigned int lastX87 = 0;
            unsigned int lastSse = 0;
            std::unordered_map<const void*, ObjectEntry> objects; // backend object -> its decorator
            // Diagnostics, written to <file>.json at the end.
            std::uint64_t offThreadCalls = 0;
            std::uint64_t threwCalls = 0;
            std::uint64_t unknownObjects = 0; // objects passed in that the recorder never handed out
            std::uint64_t unsizedLocks = 0;   // texture locks of a format the trace cannot size
            std::uint64_t getDcCalls = 0;     // RenderTarget::GetDC: GDI drawing the trace cannot see
            std::uint32_t devicesCreated = 0;
            std::string finishReason;
            // Where the time goes (QueryPerformanceCounter ticks): in the backend (forwarded calls,
            // outermost only), interning payloads (hashing, writing new blobs), committing records,
            // flushing at Present.
            std::int64_t forwardTicks = 0;
            std::int64_t internTicks = 0;
            std::int64_t commitTicks = 0;
            std::int64_t flushTicks = 0;
        };

        Session* gSession = nullptr; // heap, never destroyed: the engine's static destructors may still call in
        thread_local int tDepth = 0; // > 0 while this thread runs inside a forwarded call

        class TraceDevice;
        TraceDevice* gActiveDevice = nullptr;

        std::int64_t Ticks()
        {
            LARGE_INTEGER now{};
            ::QueryPerformanceCounter(&now);
            return now.QuadPart;
        }

        /** Marks the calling thread as inside the backend for the scope. */
        struct ForwardScope
        {
            ForwardScope() : start_(tDepth == 0 && gSession != nullptr ? Ticks() : 0) { ++tDepth; }
            ~ForwardScope()
            {
                --tDepth;
                if (start_ != 0 && gSession != nullptr) {
                    gSession->forwardTicks += Ticks() - start_; // outermost scope only; a timing, not a lock-protected count
                }
            }
            ForwardScope(const ForwardScope&) = delete;
            ForwardScope& operator=(const ForwardScope&) = delete;

        private:
            std::int64_t start_;
        };

        void FpuControlWords(unsigned int* const x87, unsigned int* const sse)
        {
#if defined(_M_IX86)
            (void)__control87_2(0, 0, x87, sse);
#else
            *x87 = _controlfp(0, 0);
            *sse = *x87;
#endif
        }

        std::string JsonEscape(const std::string& text)
        {
            std::string out;
            for (const char c : text) {
                if (c == '"' || c == '\\') {
                    out += '\\';
                    out += c;
                } else if (static_cast<unsigned char>(c) < 0x20) {
                    char escaped[8];
                    std::snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                    out += escaped;
                } else {
                    out += c;
                }
            }
            return out;
        }

        void WriteSummary(Session& session)
        {
            const std::string path = session.path + ".json";
            std::FILE* const file = std::fopen(path.c_str(), "wb");
            if (file == nullptr) {
                return;
            }
            const galtrace::WriterStats& stats = session.writer.Stats();
            LARGE_INTEGER frequency{};
            ::QueryPerformanceFrequency(&frequency);
            const double tick = frequency.QuadPart != 0 ? 1.0 / static_cast<double>(frequency.QuadPart) : 0.0;
            std::fprintf(
                file,
                "{\n  \"trace\": \"%s\",\n  \"finished\": \"%s\",\n  \"records\": %llu,\n  \"blobs\": %u,\n  \"blob_bytes\": %llu,\n"
                "  \"blob_refs\": %llu,\n  \"blob_ref_bytes\": %llu,\n  \"file_bytes\": %llu,\n  \"presents\": %u,\n  \"objects\": %u,\n"
                "  \"devices_created\": %u,\n  \"off_thread_calls\": %llu,\n  \"calls_that_threw\": %llu,\n  \"unknown_objects\": %llu,\n"
                "  \"unsized_texture_locks\": %llu,\n  \"rendertarget_getdc_calls\": %llu,\n"
                "  \"seconds\": {\"backend\": %.3f, \"intern\": %.3f, \"commit\": %.3f, \"flush\": %.3f, \"hash\": %.3f, "
                "\"write\": %.3f}\n}\n",
                JsonEscape(session.path).c_str(), JsonEscape(session.finishReason).c_str(),
                static_cast<unsigned long long>(stats.records), stats.blobs, static_cast<unsigned long long>(stats.blobBytes),
                static_cast<unsigned long long>(stats.blobRefs), static_cast<unsigned long long>(stats.blobRefBytes),
                static_cast<unsigned long long>(stats.fileBytes), session.presents, session.nextId - 1, session.devicesCreated,
                static_cast<unsigned long long>(session.offThreadCalls), static_cast<unsigned long long>(session.threwCalls),
                static_cast<unsigned long long>(session.unknownObjects), static_cast<unsigned long long>(session.unsizedLocks),
                static_cast<unsigned long long>(session.getDcCalls), static_cast<double>(session.forwardTicks) * tick,
                static_cast<double>(session.internTicks) * tick, static_cast<double>(session.commitTicks) * tick,
                static_cast<double>(session.flushTicks) * tick, static_cast<double>(stats.hashNanoseconds) * 1e-9,
                static_cast<double>(stats.writeNanoseconds) * 1e-9
            );
            std::fclose(file);
        }

        void Finish(Session& session, const char* const reason)
        {
            std::lock_guard<std::recursive_mutex> lock(session.mutex);
            if (!session.open) {
                return;
            }
            session.open = false;
            session.finishReason = reason;
            session.writer.Close(session.presents, session.nextId - 1);
            WriteSummary(session);
        }

        void FinishAtExit()
        {
            if (gSession != nullptr) {
                Finish(*gSession, "process exit");
            }
        }

        /** Whether calls on this thread are recorded now. */
        bool Recording()
        {
            return gSession != nullptr && gSession->open && tDepth == 0;
        }

        void CommitLocked(const RecordBuilder& record)
        {
            if (gSession != nullptr && gSession->open) {
                const std::int64_t start = Ticks();
                gSession->writer.Commit(record);
                gSession->commitTicks += Ticks() - start;
            }
        }

        void NoteLocked(const std::string& text)
        {
            RecordBuilder note(Op::Note);
            note.Str(text);
            CommitLocked(note);
        }

        std::uint32_t InternLocked(const void* const data, const std::size_t size)
        {
            if (gSession == nullptr || !gSession->open) {
                return 0;
            }
            const std::int64_t start = Ticks();
            const std::uint32_t blob = gSession->writer.InternBlob(data, size);
            gSession->internTicks += Ticks() - start;
            return blob;
        }

        /**
         * One engine call: decides whether it is recorded, holds the session lock for the whole call
         * (arguments, forwarding, results), records the FPU state before it, and commits the record
         * at the end. A call that throws leaves a Note instead.
         */
        class Call
        {
        public:
            Call(const Op op, const std::uint32_t selfId, const bool ready = true) : record(op)
            {
                if (!ready || !Recording()) {
                    return;
                }
                lock_ = std::unique_lock<std::recursive_mutex>(gSession->mutex);
                if (!gSession->open) {
                    lock_.unlock();
                    return;
                }
                active_ = true;
                exceptions_ = std::uncaught_exceptions();
                unsigned int x87 = 0;
                unsigned int sse = 0;
                FpuControlWords(&x87, &sse);
                if (!gSession->fpuKnown || x87 != gSession->lastX87 || sse != gSession->lastSse) {
                    RecordBuilder fpu(Op::FpuState);
                    fpu.U32(x87);
                    fpu.U32(sse);
                    CommitLocked(fpu);
                    gSession->fpuKnown = true;
                    gSession->lastX87 = x87;
                    gSession->lastSse = sse;
                }
                if (::GetCurrentThreadId() != gSession->deviceThread) {
                    record.AddFlags(galtrace::kFlagOffThread);
                    ++gSession->offThreadCalls;
                }
                record.Id(selfId);
            }

            ~Call()
            {
                if (active_ && !committed_) {
                    // The forwarded call threw (or a result could not be taken): no record for it.
                    ++gSession->threwCalls;
                    const galtrace::OpDesc* const desc = galtrace::FindOp(record.GetOp());
                    NoteLocked(std::string("call threw, not recorded: ") + (desc != nullptr ? desc->name : "?"));
                }
            }

            Call(const Call&) = delete;
            Call& operator=(const Call&) = delete;

            explicit operator bool() const { return active_; }

            void Commit()
            {
                if (active_ && !committed_) {
                    CommitLocked(record);
                    committed_ = true;
                }
            }

            RecordBuilder record;

        private:
            std::unique_lock<std::recursive_mutex> lock_;
            bool active_ = false;
            bool committed_ = false;
            int exceptions_ = 0;
        };

        // ---- object identity ------------------------------------------------------------------

        template <class Interface>
        const void* KeyOf(const boost::shared_ptr<Interface>& object)
        {
            return static_cast<const void*>(object.get());
        }

        /** Records the Release of a decorator and forgets its backend object. */
        void ReleaseObject(const std::uint32_t id, const void* const key)
        {
            if (gSession == nullptr) {
                return;
            }
            std::lock_guard<std::recursive_mutex> lock(gSession->mutex);
            const auto found = gSession->objects.find(key);
            if (found != gSession->objects.end() && found->second.id == id) {
                gSession->objects.erase(found);
            }
            if (gSession->open) {
                RecordBuilder release(Op::Release);
                release.Id(id);
                CommitLocked(release);
            }
        }

        /**
         * The engine-facing decorator of `inner`: the existing one while it lives (same id), else a
         * new one with the next id. `*id` is 0 for a null object.
         */
        template <class Wrapper, class Interface>
        boost::shared_ptr<Interface> WrapObject(const boost::shared_ptr<Interface>& inner, const ObjectType type, std::uint32_t* const id)
        {
            *id = 0;
            if (!inner || gSession == nullptr) {
                return inner;
            }
            std::lock_guard<std::recursive_mutex> lock(gSession->mutex);
            const void* const key = KeyOf(inner);
            const auto found = gSession->objects.find(key);
            if (found != gSession->objects.end()) {
                if (boost::shared_ptr<void> existing = found->second.wrapper.lock()) {
                    *id = found->second.id;
                    return boost::static_pointer_cast<Interface>(boost::static_pointer_cast<Wrapper>(existing));
                }
            }
            const std::uint32_t newId = gSession->nextId++;
            boost::shared_ptr<Wrapper> wrapper(new Wrapper(inner, newId));
            ObjectEntry entry{};
            entry.id = newId;
            entry.type = type;
            entry.wrapper = boost::static_pointer_cast<void>(wrapper);
            gSession->objects[key] = entry;
            *id = newId;
            return wrapper;
        }

        /**
         * The backend object behind `outer` (a decorator), with its id. An object the recorder never
         * handed out is passed through as it is, with id 0, and counted.
         */
        template <class Wrapper, class Interface>
        boost::shared_ptr<Interface> UnwrapObject(const boost::shared_ptr<Interface>& outer, std::uint32_t* const id)
        {
            *id = 0;
            if (!outer) {
                return outer;
            }
            if (Wrapper* const wrapper = dynamic_cast<Wrapper*>(outer.get())) {
                *id = wrapper->Id();
                return wrapper->Inner();
            }
            if (gSession != nullptr) {
                std::lock_guard<std::recursive_mutex> lock(gSession->mutex);
                const auto found = gSession->objects.find(KeyOf(outer));
                if (found != gSession->objects.end()) {
                    *id = found->second.id;
                } else {
                    ++gSession->unknownObjects;
                    if (gSession->open && tDepth == 0) {
                        NoteLocked("an object the recorder never handed out was passed in (recorded as null)");
                    }
                }
            }
            return outer;
        }

        // ---- serialising contexts ------------------------------------------------------------------

        void PutDeviceContext(RecordBuilder& b, const gal::DeviceContext& context)
        {
            b.I32(static_cast<std::int32_t>(context.mDeviceType));
            b.Bool(context.mValidate);
            b.I32(context.mAdapter);
            b.Bool(context.mVSync);
            b.Bool(context.mHWBasedInstancing);
            b.Bool(context.mSupportsFloat16);
            b.I32(context.mVertexShaderProfile);
            b.I32(context.mPixelShaderProfile);
            b.U32(context.mMaxPrimitiveCount);
            b.U32(context.mMaxVertexCount);
            const int heads = context.GetHeadCount();
            b.Count(static_cast<std::uint32_t>(heads));
            for (int index = 0; index < heads; ++index) {
                const gal::Head& head = context.GetHead(static_cast<std::uint32_t>(index));
                b.Bool(head.mHandle != nullptr);
                b.Bool(head.mWindow != nullptr);
                b.Bool(head.mWindowed);
                b.U32(head.mWidth);
                b.U32(head.mHeight);
                b.U32(head.framesPerSecond);
                b.U32(head.antialiasingHigh);
                b.U32(head.antialiasingLow);
                b.Str(head.name.c_str(), head.name.size());
                b.Count(static_cast<std::uint32_t>(head.mStrs.size()));
                for (std::size_t sample = 0; sample < head.mStrs.size(); ++sample) {
                    const gal::HeadSampleOption& option = head.mStrs[sample];
                    b.U32(option.sampleType);
                    b.U32(option.sampleQuality);
                    b.Str(option.label.c_str(), option.label.size());
                }
                b.Count(static_cast<std::uint32_t>(head.adapterModes.size()));
                for (std::size_t mode = 0; mode < head.adapterModes.size(); ++mode) {
                    b.U32(head.adapterModes[mode].width);
                    b.U32(head.adapterModes[mode].height);
                    b.U32(head.adapterModes[mode].refreshRate);
                }
                std::vector<std::uint32_t> formats;
                for (std::size_t format = 0; format < head.validFormats2.size(); ++format) {
                    formats.push_back(static_cast<std::uint32_t>(head.validFormats2[format]));
                }
                b.U32Array(formats.data(), static_cast<std::uint32_t>(formats.size()));
                formats.clear();
                for (std::size_t format = 0; format < head.validFormats1.size(); ++format) {
                    formats.push_back(static_cast<std::uint32_t>(head.validFormats1[format]));
                }
                b.U32Array(formats.data(), static_cast<std::uint32_t>(formats.size()));
            }
        }

        /**
         * The file image a TextureContext carries. dataBegin_/dataEnd_ hold the low 32 bits of the
         * pointers (TextureContext.hpp); dataArray_ holds the full pointer of the owning buffer, so the
         * begin pointer is rebuilt from it (m6u-CRIT R9), which is exact on every pointer width.
         */
        const char* TextureData(const gal::TextureContext& context, std::size_t* const size)
        {
            *size = context.DataSizeBytes();
            if (*size == 0) {
                return nullptr;
            }
            const char* const base = static_cast<const char*>(context.dataArray_);
            if (base == nullptr) {
                return reinterpret_cast<const char*>(static_cast<std::uintptr_t>(context.dataBegin_));
            }
            const std::uint32_t baseLow = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(base));
            return base + static_cast<std::uint32_t>(context.dataBegin_ - baseLow);
        }

        void PutTextureContext(RecordBuilder& b, const gal::TextureContext& context, const bool withData)
        {
            b.U32(context.source_);
            b.Str(context.location_.c_str(), context.location_.size());
            std::uint32_t blob = 0;
            if (withData) {
                std::size_t size = 0;
                const char* const data = TextureData(context, &size);
                blob = InternLocked(data, size);
            }
            b.Blob(blob);
            b.U32(context.type_);
            b.U32(context.usage_);
            b.U32(context.format_);
            b.U32(context.mipmapLevels_);
            b.U32(context.reserved0x44_);
            b.U32(context.width_);
            b.U32(context.height_);
            b.U32(context.reserved0x50_);
        }

        /**
         * InternLocked for memory the backend mapped: copied out with one memcpy first. A D3D9 dynamic
         * buffer's lock is write-combined (uncached) memory, where the hash's byte-wise reads cost
         * about a microsecond per byte (measured: 21 s for 23 MB over 60 menu frames); one wide
         * sequential copy, then hashing the copy, costs a few milliseconds.
         */
        std::uint32_t InternLockedCopy(const void* const data, const std::size_t size)
        {
            if (data == nullptr || size == 0 || gSession == nullptr || !gSession->open) {
                return 0;
            }
            thread_local std::vector<std::uint8_t> copy;
            copy.resize(size);
            std::memcpy(copy.data(), data, size);
            return InternLocked(copy.data(), copy.size());
        }

        std::vector<char> ReadWholeFile(const char* const path)
        {
            std::vector<char> bytes;
            std::ifstream file(path, std::ios::binary);
            if (file) {
                bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
            }
            return bytes;
        }

        void PutEffectContext(RecordBuilder& b, const gal::EffectContext& context)
        {
            b.U32(context.mSourceType);
            b.Bool(context.mUseCache);
            b.Str(context.mSourcePath.c_str(), context.mSourcePath.size());
            // The cache path names a file in the run's /cachedir, a host path of this process like a
            // window handle: only its file name is kept (a replay writes its own under <out>/cache).
            const std::string cachePath(context.mCachePath.c_str(), context.mCachePath.size());
            const std::size_t slash = cachePath.find_last_of("/\\");
            const std::string cacheName = slash == std::string::npos ? cachePath : cachePath.substr(slash + 1);
            b.Str(cacheName);
            const char* const source = context.mSourceBuffer.mBegin;
            const std::size_t sourceBytes =
                (source != nullptr && context.mSourceBuffer.mEnd > source) ? static_cast<std::size_t>(context.mSourceBuffer.mEnd - source) : 0;
            b.Blob(InternLocked(source, sourceBytes));
            b.Count(static_cast<std::uint32_t>(context.mMacros.size()));
            for (std::size_t index = 0; index < context.mMacros.size(); ++index) {
                const gal::EffectMacro& macro = context.mMacros[index];
                b.Str(macro.keyText_.c_str(), macro.keyText_.size());
                b.Str(macro.valueText_.c_str(), macro.valueText_.size());
            }
            std::uint32_t cacheBlob = 0;
            if (context.mUseCache) {
                const std::vector<char> compiled = ReadWholeFile(context.mCachePath.c_str());
                cacheBlob = InternLocked(compiled.data(), compiled.size());
            }
            b.Blob(cacheBlob);
        }

        galtrace::Rect ToRect(const RECT& rect)
        {
            return galtrace::Rect{rect.left, rect.top, rect.right, rect.bottom};
        }

        void PutOptRect(RecordBuilder& b, const RECT* const rect)
        {
            if (rect == nullptr) {
                b.OptRect(nullptr);
            } else {
                const galtrace::Rect value = ToRect(*rect);
                b.OptRect(&value);
            }
        }

        const float* MatrixFloats(const gal::Matrix* const matrix)
        {
            return matrix != nullptr ? &matrix->r[0].x : nullptr;
        }

        // ---- decorators of the gal objects -------------------------------------------------------

        template <class Interface>
        class Decorated
        {
        public:
            Decorated(boost::shared_ptr<Interface> inner, const std::uint32_t id) : inner_(std::move(inner)), id_(id) {}

            [[nodiscard]] std::uint32_t Id() const { return id_; }
            [[nodiscard]] const boost::shared_ptr<Interface>& Inner() const { return inner_; }

        protected:
            void ReleaseInner()
            {
                const void* const key = KeyOf(inner_);
                ReleaseObject(id_, key);
                ForwardScope forward;
                inner_.reset();
            }

            boost::shared_ptr<Interface> inner_;
            std::uint32_t id_;
        };

        class TraceRenderTarget final : public gal::RenderTarget, public Decorated<gal::RenderTarget>
        {
        public:
            using Decorated::Decorated;
            ~TraceRenderTarget() override { ReleaseInner(); }

            gal::RenderTargetContext* GetContext() override
            {
                Call call(Op::RtGetContext, id_);
                gal::RenderTargetContext* result;
                {
                    ForwardScope forward;
                    result = inner_->GetContext();
                }
                if (call) {
                    call.record.U32(result != nullptr ? result->width_ : 0);
                    call.record.U32(result != nullptr ? result->height_ : 0);
                    call.record.U32(result != nullptr ? result->format_ : 0);
                    call.Commit();
                }
                return result;
            }

            HDC GetDC() override
            {
                Call call(Op::RtGetDC, id_);
                if (gSession != nullptr) {
                    std::lock_guard<std::recursive_mutex> lock(gSession->mutex);
                    ++gSession->getDcCalls;
                }
                HDC result;
                {
                    ForwardScope forward;
                    result = inner_->GetDC();
                }
                call.Commit();
                return result;
            }
        };

        class TraceCubeRenderTarget final : public gal::CubeRenderTarget, public Decorated<gal::CubeRenderTarget>
        {
        public:
            using Decorated::Decorated;
            ~TraceCubeRenderTarget() override { ReleaseInner(); }

            gal::CubeRenderTargetContext* GetContext() override
            {
                Call call(Op::CubeGetContext, id_);
                gal::CubeRenderTargetContext* result;
                {
                    ForwardScope forward;
                    result = inner_->GetContext();
                }
                if (call) {
                    call.record.U32(result != nullptr ? result->dimension_ : 0);
                    call.record.U32(result != nullptr ? result->format_ : 0);
                    call.Commit();
                }
                return result;
            }
        };

        class TraceDepthStencilTarget final : public gal::DepthStencilTarget, public Decorated<gal::DepthStencilTarget>
        {
        public:
            using Decorated::Decorated;
            ~TraceDepthStencilTarget() override { ReleaseInner(); }

            gal::DepthStencilTargetContext* GetContext() override
            {
                Call call(Op::DsGetContext, id_);
                gal::DepthStencilTargetContext* result;
                {
                    ForwardScope forward;
                    result = inner_->GetContext();
                }
                if (call) {
                    call.record.U32(result != nullptr ? result->width_ : 0);
                    call.record.U32(result != nullptr ? result->height_ : 0);
                    call.record.U32(result != nullptr ? result->format_ : 0);
                    call.record.Bool(result != nullptr && result->field0x10_);
                    call.Commit();
                }
                return result;
            }
        };

        class TracePipelineState final : public gal::PipelineState, public Decorated<gal::PipelineState>
        {
        public:
            using Decorated::Decorated;
            ~TracePipelineState() override { ReleaseInner(); }
        };

        class TraceVertexFormat final : public gal::VertexFormat, public Decorated<gal::VertexFormat>
        {
        public:
            TraceVertexFormat(boost::shared_ptr<gal::VertexFormat> inner, const std::uint32_t id) : Decorated(std::move(inner), id)
            {
                // The engine reads these public fields directly (VertexFormat.hpp).
                formatCode_ = inner_->formatCode_;
                streamStrides_ = inner_->streamStrides_;
            }
            ~TraceVertexFormat() override { ReleaseInner(); }
        };

        class TraceTexture final : public gal::Texture, public Decorated<gal::Texture>
        {
        public:
            using Decorated::Decorated;
            ~TraceTexture() override { ReleaseInner(); }

            gal::TextureContext* GetContext() override
            {
                Call call(Op::TexGetContext, id_);
                gal::TextureContext* result;
                {
                    ForwardScope forward;
                    result = inner_->GetContext();
                }
                if (call) {
                    gal::TextureContext empty{};
                    PutTextureContext(call.record, result != nullptr ? *result : empty, false);
                    call.Commit();
                }
                return result;
            }

            gal::TextureLockRect Lock(const int level, const RECT& rect, const int flags) override
            {
                Call call(Op::TexLock, id_);
                if (call) {
                    call.record.I32(level);
                    call.record.RectValue(ToRect(rect));
                    call.record.I32(flags);
                }
                gal::TextureLockRect lock;
                galtrace::LockRegion region{};
                {
                    ForwardScope forward;
                    lock = inner_->Lock(level, rect, flags);
                    if (const gal::TextureContext* const context = inner_->GetContext()) {
                        const std::uint32_t width = (context->width_ >> level) != 0 ? (context->width_ >> level) : 1u;
                        const std::uint32_t height = (context->height_ >> level) != 0 ? (context->height_ >> level) : 1u;
                        region = galtrace::TextureLockRegion(context->format_, width, height, rect.left, rect.top, rect.right, rect.bottom);
                    }
                }
                locked_ = true;
                lockBits_ = static_cast<const std::uint8_t*>(lock.bits);
                lockPitch_ = lock.pitch;
                lockFlags_ = flags;
                region_ = region;
                if (call) {
                    if (!region.known) {
                        ++gSession->unsizedLocks;
                    }
                    call.record.U32(region.rowBytes);
                    call.record.U32(region.rows);
                    call.Commit();
                }
                return lock;
            }

            int Unlock(const gal::TextureLockRect lock) override
            {
                Call call(Op::TexUnlockRect, id_);
                if (call) {
                    call.record.I32(lock.flags);
                    call.record.I32(lock.level);
                    PutLockedContent(call.record);
                }
                locked_ = false;
                int result;
                {
                    ForwardScope forward;
                    result = inner_->Unlock(lock);
                }
                call.Commit();
                return result;
            }

            int Unlock(const int level) override
            {
                Call call(Op::TexUnlockLevel, id_);
                if (call) {
                    call.record.I32(level);
                    PutLockedContent(call.record);
                }
                locked_ = false;
                int result;
                {
                    ForwardScope forward;
                    result = inner_->Unlock(level);
                }
                call.Commit();
                return result;
            }

            void SaveToBuffer(gpg::MemBuffer<char>* const outBuffer) override
            {
                Call call(Op::TexSaveToBuffer, id_);
                {
                    ForwardScope forward;
                    inner_->SaveToBuffer(outBuffer);
                }
                if (call) {
                    const char* const data = outBuffer != nullptr ? outBuffer->mBegin : nullptr;
                    const std::size_t size = (data != nullptr && outBuffer->mEnd > data) ? static_cast<std::size_t>(outBuffer->mEnd - data) : 0;
                    call.record.Blob(InternLocked(data, size));
                    call.Commit();
                }
            }

        private:
            /** The locked rectangle, packed (rowBytes per row), as the content of the Unlock record. */
            void PutLockedContent(RecordBuilder& b)
            {
                const bool readOnly = (static_cast<unsigned>(lockFlags_) & static_cast<unsigned>(gal::MohoD3DLockFlags::ReadOnly)) != 0;
                if (!locked_ || !region_.known || lockBits_ == nullptr || lockPitch_ < static_cast<int>(region_.rowBytes)) {
                    b.U8(static_cast<std::uint8_t>(galtrace::LockContent::None));
                    b.Blob(0);
                    return;
                }
                std::vector<std::uint8_t> packed(static_cast<std::size_t>(region_.rowBytes) * region_.rows);
                for (std::uint32_t row = 0; row < region_.rows; ++row) {
                    std::memcpy(packed.data() + static_cast<std::size_t>(row) * region_.rowBytes,
                                lockBits_ + static_cast<std::size_t>(row) * static_cast<std::size_t>(lockPitch_), region_.rowBytes);
                }
                b.U8(static_cast<std::uint8_t>(readOnly ? galtrace::LockContent::ReadBack : galtrace::LockContent::Written));
                b.Blob(InternLocked(packed.data(), packed.size()));
            }

            bool locked_ = false;
            const std::uint8_t* lockBits_ = nullptr;
            int lockPitch_ = 0;
            int lockFlags_ = 0;
            galtrace::LockRegion region_{};
        };

        class TraceVertexBuffer final : public gal::VertexBuffer, public Decorated<gal::VertexBuffer>
        {
        public:
            using Decorated::Decorated;
            ~TraceVertexBuffer() override { ReleaseInner(); }

            gal::VertexBufferContext* GetContext() override
            {
                Call call(Op::VbGetContext, id_);
                gal::VertexBufferContext* result;
                {
                    ForwardScope forward;
                    result = inner_->GetContext();
                }
                if (call) {
                    call.record.U32(result != nullptr ? result->type_ : 0);
                    call.record.U32(result != nullptr ? result->usage_ : 0);
                    call.record.U32(result != nullptr ? result->vertexCount_ : 0);
                    call.record.U32(result != nullptr ? result->stride_ : 0);
                    call.Commit();
                }
                return result;
            }

            void* Lock(const unsigned int offset, const unsigned int size, const gal::MohoD3DLockFlags lockFlags) override
            {
                Call call(Op::VbLock, id_);
                if (call) {
                    call.record.U32(offset);
                    call.record.U32(size);
                    call.record.U32(static_cast<std::uint32_t>(lockFlags));
                }
                void* result;
                std::uint32_t bytes = size;
                {
                    ForwardScope forward;
                    result = inner_->Lock(offset, size, lockFlags);
                    if (size == 0) { // D3D9: 0 locks to the end of the buffer
                        const gal::VertexBufferContext* const context = inner_->GetContext();
                        const std::uint32_t total = context != nullptr ? context->vertexCount_ * context->stride_ : 0;
                        bytes = total > offset ? total - offset : 0;
                    }
                }
                lockData_ = static_cast<const std::uint8_t*>(result);
                lockBytes_ = result != nullptr ? bytes : 0;
                if (call) {
                    call.record.U32(lockBytes_);
                    call.Commit();
                }
                return result;
            }

            void Unlock() override
            {
                Call call(Op::VbUnlock, id_);
                if (call) {
                    call.record.Blob(InternLockedCopy(lockData_, lockBytes_));
                }
                lockData_ = nullptr;
                lockBytes_ = 0;
                {
                    ForwardScope forward;
                    inner_->Unlock();
                }
                call.Commit();
            }

        private:
            const std::uint8_t* lockData_ = nullptr;
            std::uint32_t lockBytes_ = 0;
        };

        class TraceIndexBuffer final : public gal::IndexBuffer, public Decorated<gal::IndexBuffer>
        {
        public:
            using Decorated::Decorated;
            ~TraceIndexBuffer() override { ReleaseInner(); }

            gal::IndexBufferContext* GetContext() override
            {
                Call call(Op::IbGetContext, id_);
                gal::IndexBufferContext* result;
                {
                    ForwardScope forward;
                    result = inner_->GetContext();
                }
                if (call) {
                    call.record.U32(result != nullptr ? result->format_ : 0);
                    call.record.U32(result != nullptr ? result->size_ : 0);
                    call.record.U32(result != nullptr ? result->type_ : 0);
                    call.Commit();
                }
                return result;
            }

            std::int16_t* Lock(const unsigned int offset, const unsigned int size, const gal::MohoD3DLockFlags lockFlags) override
            {
                Call call(Op::IbLock, id_);
                if (call) {
                    call.record.U32(offset);
                    call.record.U32(size);
                    call.record.U32(static_cast<std::uint32_t>(lockFlags));
                }
                std::int16_t* result;
                std::uint32_t bytes = size;
                {
                    ForwardScope forward;
                    result = inner_->Lock(offset, size, lockFlags);
                    if (size == 0) {
                        const gal::IndexBufferContext* const context = inner_->GetContext();
                        const std::uint32_t total = context != nullptr ? context->size_ * (context->format_ == 1U ? 2U : 4U) : 0;
                        bytes = total > offset ? total - offset : 0;
                    }
                }
                lockData_ = reinterpret_cast<const std::uint8_t*>(result);
                lockBytes_ = result != nullptr ? bytes : 0;
                if (call) {
                    call.record.U32(lockBytes_);
                    call.Commit();
                }
                return result;
            }

            void Unlock() override
            {
                Call call(Op::IbUnlock, id_);
                if (call) {
                    call.record.Blob(InternLockedCopy(lockData_, lockBytes_));
                }
                lockData_ = nullptr;
                lockBytes_ = 0;
                {
                    ForwardScope forward;
                    inner_->Unlock();
                }
                call.Commit();
            }

        private:
            const std::uint8_t* lockData_ = nullptr;
            std::uint32_t lockBytes_ = 0;
        };

        // Annotation getters share their shape across techniques and variables.
        template <class Self>
        bool AnnotationBool(Self* self, const Op op, bool* const outValue, const msvc8::string& name)
        {
            Call call(op, self->Id());
            if (call) {
                call.record.Str(name.c_str(), name.size());
            }
            bool found;
            {
                ForwardScope forward;
                found = self->Inner()->GetAnnotationBool(outValue, name);
            }
            if (call) {
                call.record.Bool(found);
                call.record.Bool(outValue != nullptr && *outValue);
                call.Commit();
            }
            return found;
        }

        template <class Self>
        bool AnnotationInt(Self* self, const Op op, int* const outValue, const msvc8::string& name)
        {
            Call call(op, self->Id());
            if (call) {
                call.record.Str(name.c_str(), name.size());
            }
            bool found;
            {
                ForwardScope forward;
                found = self->Inner()->GetAnnotationInt(outValue, name);
            }
            if (call) {
                call.record.Bool(found);
                call.record.I32(outValue != nullptr ? *outValue : 0);
                call.Commit();
            }
            return found;
        }

        template <class Self>
        bool AnnotationFloat(Self* self, const Op op, float* const outValue, const msvc8::string& name)
        {
            Call call(op, self->Id());
            if (call) {
                call.record.Str(name.c_str(), name.size());
            }
            bool found;
            {
                ForwardScope forward;
                found = self->Inner()->GetAnnotationFloat(outValue, name);
            }
            if (call) {
                call.record.Bool(found);
                call.record.F32(outValue != nullptr ? *outValue : 0.0f);
                call.Commit();
            }
            return found;
        }

        template <class Self>
        bool AnnotationString(Self* self, const Op op, msvc8::string* const outValue, const msvc8::string& name)
        {
            Call call(op, self->Id());
            if (call) {
                call.record.Str(name.c_str(), name.size());
            }
            bool found;
            {
                ForwardScope forward;
                found = self->Inner()->GetAnnotationString(outValue, name);
            }
            if (call) {
                call.record.Bool(found);
                if (outValue != nullptr) {
                    call.record.Str(outValue->c_str(), outValue->size());
                } else {
                    call.record.Str("", 0);
                }
                call.Commit();
            }
            return found;
        }

        class TraceEffectTechnique final : public gal::EffectTechnique, public Decorated<gal::EffectTechnique>
        {
        public:
            using Decorated::Decorated;
            ~TraceEffectTechnique() override { ReleaseInner(); }

            msvc8::string* GetName() override
            {
                Call call(Op::TechGetName, id_);
                msvc8::string* result;
                {
                    ForwardScope forward;
                    result = inner_->GetName();
                }
                if (call) {
                    call.record.Str(result != nullptr ? result->c_str() : "", result != nullptr ? result->size() : 0);
                    call.Commit();
                }
                return result;
            }

            int BeginTechnique() override
            {
                Call call(Op::TechBegin, id_);
                int passes;
                {
                    ForwardScope forward;
                    passes = inner_->BeginTechnique();
                }
                if (call) {
                    call.record.I32(passes);
                    call.Commit();
                }
                return passes;
            }

            void EndTechnique() override
            {
                Call call(Op::TechEnd, id_);
                {
                    ForwardScope forward;
                    inner_->EndTechnique();
                }
                call.Commit();
            }

            void BeginPass(const int pass) override
            {
                Call call(Op::TechBeginPass, id_);
                if (call) {
                    call.record.I32(pass);
                }
                {
                    ForwardScope forward;
                    inner_->BeginPass(pass);
                }
                call.Commit();
            }

            void EndPass() override
            {
                Call call(Op::TechEndPass, id_);
                {
                    ForwardScope forward;
                    inner_->EndPass();
                }
                call.Commit();
            }

            bool GetAnnotationBool(bool* const outValue, const msvc8::string& name) override
            {
                return AnnotationBool(this, Op::TechGetAnnotationBool, outValue, name);
            }
            bool GetAnnotationInt(int* const outValue, const msvc8::string& name) override
            {
                return AnnotationInt(this, Op::TechGetAnnotationInt, outValue, name);
            }
            bool GetAnnotationFloat(float* const outValue, const msvc8::string& name) override
            {
                return AnnotationFloat(this, Op::TechGetAnnotationFloat, outValue, name);
            }
            bool GetAnnotationString(msvc8::string* const outValue, const msvc8::string& name) override
            {
                return AnnotationString(this, Op::TechGetAnnotationString, outValue, name);
            }
        };

        class TraceEffectVariable final : public gal::EffectVariable, public Decorated<gal::EffectVariable>
        {
        public:
            using Decorated::Decorated;
            ~TraceEffectVariable() override { ReleaseInner(); }

            msvc8::string* GetName() override
            {
                Call call(Op::VarGetName, id_);
                msvc8::string* result;
                {
                    ForwardScope forward;
                    result = inner_->GetName();
                }
                if (call) {
                    call.record.Str(result != nullptr ? result->c_str() : "", result != nullptr ? result->size() : 0);
                    call.Commit();
                }
                return result;
            }

            void SetCubeRenderTarget(const boost::shared_ptr<gal::CubeRenderTarget> cubeTarget) override
            {
                std::uint32_t id = 0;
                const boost::shared_ptr<gal::CubeRenderTarget> inner = UnwrapObject<TraceCubeRenderTarget>(cubeTarget, &id);
                Call call(Op::VarSetCubeRenderTarget, id_);
                if (call) {
                    call.record.Id(id);
                }
                {
                    ForwardScope forward;
                    inner_->SetCubeRenderTarget(inner);
                }
                call.Commit();
            }

            void SetRenderTarget(const boost::shared_ptr<gal::RenderTarget> renderTarget) override
            {
                std::uint32_t id = 0;
                const boost::shared_ptr<gal::RenderTarget> inner = UnwrapObject<TraceRenderTarget>(renderTarget, &id);
                Call call(Op::VarSetRenderTarget, id_);
                if (call) {
                    call.record.Id(id);
                }
                {
                    ForwardScope forward;
                    inner_->SetRenderTarget(inner);
                }
                call.Commit();
            }

            void SetTexture(const boost::shared_ptr<gal::Texture> texture) override
            {
                std::uint32_t id = 0;
                const boost::shared_ptr<gal::Texture> inner = UnwrapObject<TraceTexture>(texture, &id);
                Call call(Op::VarSetTexture, id_);
                if (call) {
                    call.record.Id(id);
                }
                {
                    ForwardScope forward;
                    inner_->SetTexture(inner);
                }
                call.Commit();
            }

            void SetMatrix4x4(const gal::Matrix* const matrix) override
            {
                Call call(Op::VarSetMatrix4x4, id_);
                if (call) {
                    call.record.OptMatrix(MatrixFloats(matrix));
                }
                {
                    ForwardScope forward;
                    inner_->SetMatrix4x4(matrix);
                }
                call.Commit();
            }

            void SetFloatArray(const std::uint32_t count, const float* const values) override
            {
                Call call(Op::VarSetFloatArray, id_);
                if (call) {
                    call.record.F32Array(values, values != nullptr ? count : 0);
                }
                {
                    ForwardScope forward;
                    inner_->SetFloatArray(count, values);
                }
                call.Commit();
            }

            void SetVector(const float* const vector4) override
            {
                Call call(Op::VarSetVector, id_);
                if (call) {
                    const float zero[4] = {0, 0, 0, 0};
                    call.record.Vec4(vector4 != nullptr ? vector4 : zero);
                }
                {
                    ForwardScope forward;
                    inner_->SetVector(vector4);
                }
                call.Commit();
            }

            void SetValue(const void* const data, const std::uint32_t byteCount) override
            {
                Call call(Op::VarSetValue, id_);
                if (call) {
                    call.record.Bytes(data, data != nullptr ? byteCount : 0);
                }
                {
                    ForwardScope forward;
                    inner_->SetValue(data, byteCount);
                }
                call.Commit();
            }

            void SetFloat(const float value) override
            {
                Call call(Op::VarSetFloat, id_);
                if (call) {
                    call.record.F32(value);
                }
                {
                    ForwardScope forward;
                    inner_->SetFloat(value);
                }
                call.Commit();
            }

            void SetInt(const int value) override
            {
                Call call(Op::VarSetInt, id_);
                if (call) {
                    call.record.I32(value);
                }
                {
                    ForwardScope forward;
                    inner_->SetInt(value);
                }
                call.Commit();
            }

            void SetBool(const bool value) override
            {
                Call call(Op::VarSetBool, id_);
                if (call) {
                    call.record.Bool(value);
                }
                {
                    ForwardScope forward;
                    inner_->SetBool(value);
                }
                call.Commit();
            }

            void SetMatrixArray(const std::uint32_t count, const gal::Matrix* const matrices) override
            {
                Call call(Op::VarSetMatrixArray, id_);
                if (call) {
                    call.record.U32(count);
                    call.record.F32Array(MatrixFloats(matrices), matrices != nullptr ? count * 16u : 0u);
                }
                {
                    ForwardScope forward;
                    inner_->SetMatrixArray(count, matrices);
                }
                call.Commit();
            }

            void SetVectorArray(const std::uint32_t count, const float* const vectors4) override
            {
                Call call(Op::VarSetVectorArray, id_);
                if (call) {
                    call.record.U32(count);
                    call.record.F32Array(vectors4, vectors4 != nullptr ? count * 4u : 0u);
                }
                {
                    ForwardScope forward;
                    inner_->SetVectorArray(count, vectors4);
                }
                call.Commit();
            }

            bool GetAnnotationBool(bool* const outValue, const msvc8::string& name) override
            {
                return AnnotationBool(this, Op::VarGetAnnotationBool, outValue, name);
            }
            bool GetAnnotationInt(int* const outValue, const msvc8::string& name) override
            {
                return AnnotationInt(this, Op::VarGetAnnotationInt, outValue, name);
            }
            bool GetAnnotationFloat(float* const outValue, const msvc8::string& name) override
            {
                return AnnotationFloat(this, Op::VarGetAnnotationFloat, outValue, name);
            }
            bool GetAnnotationString(msvc8::string* const outValue, const msvc8::string& name) override
            {
                return AnnotationString(this, Op::VarGetAnnotationString, outValue, name);
            }
        };

        class TraceEffect final : public gal::Effect, public Decorated<gal::Effect>
        {
        public:
            using Decorated::Decorated;
            ~TraceEffect() override { ReleaseInner(); }

            gal::EffectContext* GetContext() override
            {
                Call call(Op::EffGetContext, id_);
                gal::EffectContext* result;
                {
                    ForwardScope forward;
                    result = inner_->GetContext();
                }
                call.Commit();
                return result;
            }

            void GetTechniques(msvc8::vector<boost::shared_ptr<gal::EffectTechnique>>& outTechniques) override
            {
                Call call(Op::EffGetTechniques, id_);
                msvc8::vector<boost::shared_ptr<gal::EffectTechnique>> inner;
                {
                    ForwardScope forward;
                    inner_->GetTechniques(inner);
                }
                // Wrap first, while the engine's vector still holds the decorators it had, so the
                // same backend technique keeps its decorator and id.
                std::vector<boost::shared_ptr<gal::EffectTechnique>> wrapped;
                std::vector<std::uint32_t> ids;
                for (std::size_t index = 0; index < inner.size(); ++index) {
                    std::uint32_t id = 0;
                    wrapped.push_back(WrapObject<TraceEffectTechnique>(inner[index], ObjectType::EffectTechnique, &id));
                    ids.push_back(id);
                }
                if (call) {
                    call.record.Count(static_cast<std::uint32_t>(wrapped.size()));
                    for (std::size_t index = 0; index < wrapped.size(); ++index) {
                        call.record.Id(ids[index]);
                        msvc8::string* name = nullptr;
                        if (inner[index]) {
                            ForwardScope forward;
                            name = inner[index]->GetName();
                        }
                        call.record.Str(name != nullptr ? name->c_str() : "", name != nullptr ? name->size() : 0);
                    }
                }
                outTechniques.clear();
                for (const boost::shared_ptr<gal::EffectTechnique>& technique : wrapped) {
                    outTechniques.push_back(technique);
                }
                call.Commit();
            }

            boost::shared_ptr<gal::EffectVariable> GetVariable(const char* const name) override
            {
                Call call(Op::EffGetVariable, id_);
                if (call) {
                    call.record.Str(name != nullptr ? name : "", name != nullptr ? std::strlen(name) : 0);
                }
                boost::shared_ptr<gal::EffectVariable> inner;
                {
                    ForwardScope forward;
                    inner = inner_->GetVariable(name);
                }
                std::uint32_t id = 0;
                boost::shared_ptr<gal::EffectVariable> result = WrapObject<TraceEffectVariable>(inner, ObjectType::EffectVariable, &id);
                if (call) {
                    call.record.Id(id);
                    call.Commit();
                }
                return result;
            }

            boost::shared_ptr<gal::EffectTechnique> GetTechnique(const char* const name) override
            {
                Call call(Op::EffGetTechnique, id_);
                if (call) {
                    call.record.Str(name != nullptr ? name : "", name != nullptr ? std::strlen(name) : 0);
                }
                boost::shared_ptr<gal::EffectTechnique> inner;
                {
                    ForwardScope forward;
                    inner = inner_->GetTechnique(name);
                }
                std::uint32_t id = 0;
                boost::shared_ptr<gal::EffectTechnique> result = WrapObject<TraceEffectTechnique>(inner, ObjectType::EffectTechnique, &id);
                if (call) {
                    call.record.Id(id);
                    call.Commit();
                }
                return result;
            }

            void OnReset() override
            {
                Call call(Op::EffOnReset, id_);
                {
                    ForwardScope forward;
                    inner_->OnReset();
                }
                call.Commit();
            }

            void OnLost() override
            {
                Call call(Op::EffOnLost, id_);
                {
                    ForwardScope forward;
                    inner_->OnLost();
                }
                call.Commit();
            }
        };

        // ---- the device decorator ------------------------------------------------------------------

        class TraceDevice final : public gal::Device
        {
        public:
            explicit TraceDevice(gal::Device* const backend) : backend_(backend) {}

            ~TraceDevice() override
            {
                if (gSession != nullptr && ready_) {
                    std::lock_guard<std::recursive_mutex> lock(gSession->mutex);
                    heads_.clear(); // the head decorators release their targets (Release records)
                    if (gSession->open && tDepth == 0) {
                        RecordBuilder destroy(Op::DeviceDestroy);
                        destroy.Id(id_);
                        CommitLocked(destroy);
                    }
                }
                heads_.clear();
                {
                    // The backend's teardown may reach Device::GetInstance() (still this decorator while
                    // the engine's auto_ptr deletes it): it resolves to the backend while gActiveDevice
                    // is this device and the scope is open.
                    ForwardScope forward;
                    delete backend_;
                }
                if (gActiveDevice == this) {
                    gActiveDevice = nullptr;
                }
                if (gSession != nullptr && ready_) {
                    Finish(*gSession, "device destroyed");
                }
            }

            [[nodiscard]] gal::Device* Backend() const { return backend_; }
            [[nodiscard]] bool InSetup() const { return !ready_; }

            void OnSetupDone(const gal::DeviceContext& requested)
            {
                if (gSession == nullptr) {
                    return;
                }
                std::lock_guard<std::recursive_mutex> lock(gSession->mutex);
                ready_ = true;
                ++gSession->devicesCreated;
                if (!gSession->open) {
                    return;
                }
                id_ = gSession->nextId++;
                gSession->deviceThread = ::GetCurrentThreadId();
                RecordBuilder create(Op::DeviceCreate);
                create.Id(id_);
                PutDeviceContext(create, requested);
                {
                    ForwardScope forward;
                    gal::DeviceContext* const actual = backend_->GetDeviceContext();
                    PutDeviceContext(create, actual != nullptr ? *actual : requested);
                    heads_.resize(actual != nullptr ? static_cast<std::size_t>(actual->GetHeadCount()) : 0u);
                }
                CommitLocked(create);
            }

            // -- slots 1-8 --------------------------------------------------------------------------

            void* GetLog() override
            {
                Call call(Op::DevGetLog, id_, ready_);
                void* result;
                {
                    ForwardScope forward;
                    result = backend_->GetLog();
                }
                call.Commit();
                return result;
            }

            gal::DeviceContext* GetDeviceContext() override
            {
                Call call(Op::DevGetDeviceContext, id_, ready_);
                gal::DeviceContext* result;
                {
                    ForwardScope forward;
                    result = backend_->GetDeviceContext();
                }
                call.Commit();
                return result;
            }

            int GetCurThreadId() override
            {
                Call call(Op::DevGetCurThreadId, id_, ready_);
                int result;
                {
                    ForwardScope forward;
                    result = backend_->GetCurThreadId();
                }
                call.Commit();
                return result;
            }

            void Func1() const override
            {
                Call call(Op::DevFunc1, id_, ready_);
                {
                    ForwardScope forward;
                    backend_->Func1();
                }
                call.Commit();
            }

            void GetModesForAdapter(msvc8::vector<gal::HeadAdapterMode>& outModes, const int adapterIndex) override
            {
                Call call(Op::DevGetModesForAdapter, id_, ready_);
                if (call) {
                    call.record.I32(adapterIndex);
                }
                {
                    ForwardScope forward;
                    backend_->GetModesForAdapter(outModes, adapterIndex);
                }
                if (call) {
                    call.record.Count(static_cast<std::uint32_t>(outModes.size()));
                    for (std::size_t index = 0; index < outModes.size(); ++index) {
                        call.record.U32(outModes[index].width);
                        call.record.U32(outModes[index].height);
                        call.record.U32(outModes[index].refreshRate);
                    }
                    call.Commit();
                }
            }

            gal::OutputContext* GetHeadOutputContext(const unsigned int headIndex) override
            {
                return HeadOutput(headIndex, false);
            }

            const gal::OutputContext* GetHeadOutputContext(const unsigned int headIndex) const override
            {
                return const_cast<TraceDevice*>(this)->HeadOutput(headIndex, true);
            }

            boost::shared_ptr<gal::PipelineState> GetPipelineState() override
            {
                Call call(Op::DevGetPipelineState, id_, ready_);
                boost::shared_ptr<gal::PipelineState> inner;
                {
                    ForwardScope forward;
                    inner = backend_->GetPipelineState();
                }
                if (!ready_ || tDepth > 0) {
                    return inner; // the backend's own call: its own object
                }
                std::uint32_t id = 0;
                boost::shared_ptr<gal::PipelineState> result = WrapObject<TracePipelineState>(inner, ObjectType::PipelineState, &id);
                if (call) {
                    call.record.Id(id);
                    call.Commit();
                }
                return result;
            }

            // -- creation (slots 9-16) --------------------------------------------------------------

            boost::shared_ptr<gal::Effect> CreateEffect(const gal::EffectContext& context) override
            {
                Call call(Op::DevCreateEffect, id_, ready_);
                if (call) {
                    PutEffectContext(call.record, context);
                }
                boost::shared_ptr<gal::Effect> inner;
                {
                    ForwardScope forward;
                    inner = backend_->CreateEffect(context);
                }
                if (!ready_ || tDepth > 0) {
                    return inner;
                }
                std::uint32_t id = 0;
                boost::shared_ptr<gal::Effect> result = WrapObject<TraceEffect>(inner, ObjectType::Effect, &id);
                if (call) {
                    call.record.Id(id);
                    call.Commit();
                }
                return result;
            }

            boost::shared_ptr<gal::Texture> CreateTexture(const gal::TextureContext* const context) override
            {
                Call call(Op::DevCreateTexture, id_, ready_);
                if (call) {
                    gal::TextureContext empty{};
                    PutTextureContext(call.record, context != nullptr ? *context : empty, true);
                }
                boost::shared_ptr<gal::Texture> inner;
                gal::TextureContext created{};
                {
                    ForwardScope forward;
                    inner = backend_->CreateTexture(context);
                    if (inner) {
                        if (const gal::TextureContext* const result = inner->GetContext()) {
                            created.AssignFrom(*result);
                        }
                    }
                }
                if (!ready_ || tDepth > 0) {
                    return inner;
                }
                std::uint32_t id = 0;
                boost::shared_ptr<gal::Texture> result = WrapObject<TraceTexture>(inner, ObjectType::Texture, &id);
                if (call) {
                    call.record.Id(id);
                    PutTextureContext(call.record, created, false);
                    call.Commit();
                }
                return result;
            }

            boost::shared_ptr<gal::RenderTarget> CreateRenderTarget(const gal::RenderTargetContext* const context) override
            {
                Call call(Op::DevCreateRenderTarget, id_, ready_);
                if (call) {
                    call.record.U32(context != nullptr ? context->width_ : 0);
                    call.record.U32(context != nullptr ? context->height_ : 0);
                    call.record.U32(context != nullptr ? context->format_ : 0);
                }
                boost::shared_ptr<gal::RenderTarget> inner;
                {
                    ForwardScope forward;
                    inner = backend_->CreateRenderTarget(context);
                }
                if (!ready_ || tDepth > 0) {
                    return inner;
                }
                std::uint32_t id = 0;
                boost::shared_ptr<gal::RenderTarget> result = WrapObject<TraceRenderTarget>(inner, ObjectType::RenderTarget, &id);
                if (call) {
                    call.record.Id(id);
                    call.Commit();
                }
                return result;
            }

            boost::shared_ptr<gal::CubeRenderTarget> CreateCubeRenderTarget(const gal::CubeRenderTargetContext* const context) override
            {
                Call call(Op::DevCreateCubeRenderTarget, id_, ready_);
                if (call) {
                    call.record.U32(context != nullptr ? context->dimension_ : 0);
                    call.record.U32(context != nullptr ? context->format_ : 0);
                }
                boost::shared_ptr<gal::CubeRenderTarget> inner;
                {
                    ForwardScope forward;
                    inner = backend_->CreateCubeRenderTarget(context);
                }
                if (!ready_ || tDepth > 0) {
                    return inner;
                }
                std::uint32_t id = 0;
                boost::shared_ptr<gal::CubeRenderTarget> result =
                    WrapObject<TraceCubeRenderTarget>(inner, ObjectType::CubeRenderTarget, &id);
                if (call) {
                    call.record.Id(id);
                    call.Commit();
                }
                return result;
            }

            boost::shared_ptr<gal::DepthStencilTarget> CreateDepthStencilTarget(const gal::DepthStencilTargetContext* const context) override
            {
                Call call(Op::DevCreateDepthStencilTarget, id_, ready_);
                if (call) {
                    call.record.U32(context != nullptr ? context->width_ : 0);
                    call.record.U32(context != nullptr ? context->height_ : 0);
                    call.record.U32(context != nullptr ? context->format_ : 0);
                    call.record.Bool(context != nullptr && context->field0x10_);
                }
                boost::shared_ptr<gal::DepthStencilTarget> inner;
                {
                    ForwardScope forward;
                    inner = backend_->CreateDepthStencilTarget(context);
                }
                if (!ready_ || tDepth > 0) {
                    return inner;
                }
                std::uint32_t id = 0;
                boost::shared_ptr<gal::DepthStencilTarget> result =
                    WrapObject<TraceDepthStencilTarget>(inner, ObjectType::DepthStencilTarget, &id);
                if (call) {
                    call.record.Id(id);
                    call.Commit();
                }
                return result;
            }

            boost::shared_ptr<gal::VertexFormat> CreateVertexFormat(const std::uint32_t formatCode) override
            {
                Call call(Op::DevCreateVertexFormat, id_, ready_);
                if (call) {
                    call.record.U32(formatCode);
                }
                boost::shared_ptr<gal::VertexFormat> inner;
                {
                    ForwardScope forward;
                    inner = backend_->CreateVertexFormat(formatCode);
                }
                if (!ready_ || tDepth > 0) {
                    return inner;
                }
                std::uint32_t id = 0;
                boost::shared_ptr<gal::VertexFormat> result = WrapObject<TraceVertexFormat>(inner, ObjectType::VertexFormat, &id);
                if (call) {
                    call.record.Id(id);
                    call.record.U32(inner ? inner->formatCode_ : 0);
                    std::vector<std::uint32_t> strides;
                    if (inner) {
                        for (std::size_t index = 0; index < inner->streamStrides_.size(); ++index) {
                            strides.push_back(inner->streamStrides_[index]);
                        }
                    }
                    call.record.U32Array(strides.data(), static_cast<std::uint32_t>(strides.size()));
                    call.Commit();
                }
                return result;
            }

            boost::shared_ptr<gal::VertexBuffer> CreateVertexBuffer(const gal::VertexBufferContext* const context) override
            {
                Call call(Op::DevCreateVertexBuffer, id_, ready_);
                if (call) {
                    call.record.U32(context != nullptr ? context->type_ : 0);
                    call.record.U32(context != nullptr ? context->usage_ : 0);
                    call.record.U32(context != nullptr ? context->vertexCount_ : 0);
                    call.record.U32(context != nullptr ? context->stride_ : 0);
                }
                boost::shared_ptr<gal::VertexBuffer> inner;
                {
                    ForwardScope forward;
                    inner = backend_->CreateVertexBuffer(context);
                }
                if (!ready_ || tDepth > 0) {
                    return inner;
                }
                std::uint32_t id = 0;
                boost::shared_ptr<gal::VertexBuffer> result = WrapObject<TraceVertexBuffer>(inner, ObjectType::VertexBuffer, &id);
                if (call) {
                    call.record.Id(id);
                    call.Commit();
                }
                return result;
            }

            boost::shared_ptr<gal::IndexBuffer> CreateIndexBuffer(const gal::IndexBufferContext* const context) override
            {
                Call call(Op::DevCreateIndexBuffer, id_, ready_);
                if (call) {
                    call.record.U32(context != nullptr ? context->format_ : 0);
                    call.record.U32(context != nullptr ? context->size_ : 0);
                    call.record.U32(context != nullptr ? context->type_ : 0);
                }
                boost::shared_ptr<gal::IndexBuffer> inner;
                {
                    ForwardScope forward;
                    inner = backend_->CreateIndexBuffer(context);
                }
                if (!ready_ || tDepth > 0) {
                    return inner;
                }
                std::uint32_t id = 0;
                boost::shared_ptr<gal::IndexBuffer> result = WrapObject<TraceIndexBuffer>(inner, ObjectType::IndexBuffer, &id);
                if (call) {
                    call.record.Id(id);
                    call.Commit();
                }
                return result;
            }

            // -- copies, saves (slots 17-24) --------------------------------------------------------

            void GetRenderTargetData(const boost::shared_ptr<gal::RenderTarget>& source, const boost::shared_ptr<gal::Texture>& destination) override
            {
                std::uint32_t sourceId = 0;
                std::uint32_t destinationId = 0;
                const boost::shared_ptr<gal::RenderTarget> innerSource = UnwrapObject<TraceRenderTarget>(source, &sourceId);
                const boost::shared_ptr<gal::Texture> innerDestination = UnwrapObject<TraceTexture>(destination, &destinationId);
                Call call(Op::DevGetRenderTargetData, id_, ready_);
                if (call) {
                    call.record.Id(sourceId);
                    call.record.Id(destinationId);
                }
                {
                    ForwardScope forward;
                    backend_->GetRenderTargetData(innerSource, innerDestination);
                }
                call.Commit();
            }

            void StretchRect(
                const boost::shared_ptr<gal::RenderTarget>& source, const boost::shared_ptr<gal::RenderTarget>& destination,
                const RECT* const sourceRect, const RECT* const destinationRect
            ) override
            {
                std::uint32_t sourceId = 0;
                std::uint32_t destinationId = 0;
                const boost::shared_ptr<gal::RenderTarget> innerSource = UnwrapObject<TraceRenderTarget>(source, &sourceId);
                const boost::shared_ptr<gal::RenderTarget> innerDestination = UnwrapObject<TraceRenderTarget>(destination, &destinationId);
                Call call(Op::DevStretchRect, id_, ready_);
                if (call) {
                    call.record.Id(sourceId);
                    call.record.Id(destinationId);
                    PutOptRect(call.record, sourceRect);
                    PutOptRect(call.record, destinationRect);
                }
                {
                    ForwardScope forward;
                    backend_->StretchRect(innerSource, innerDestination, sourceRect, destinationRect);
                }
                call.Commit();
            }

            void UpdateSurface(
                const boost::shared_ptr<gal::Texture>& source, const boost::shared_ptr<gal::Texture>& destination,
                const RECT* const sourceRect, const RECT* const destinationRect
            ) override
            {
                std::uint32_t sourceId = 0;
                std::uint32_t destinationId = 0;
                const boost::shared_ptr<gal::Texture> innerSource = UnwrapObject<TraceTexture>(source, &sourceId);
                const boost::shared_ptr<gal::Texture> innerDestination = UnwrapObject<TraceTexture>(destination, &destinationId);
                Call call(Op::DevUpdateSurface, id_, ready_);
                if (call) {
                    call.record.Id(sourceId);
                    call.record.Id(destinationId);
                    PutOptRect(call.record, sourceRect);
                    PutOptRect(call.record, destinationRect);
                }
                {
                    ForwardScope forward;
                    backend_->UpdateSurface(innerSource, innerDestination, sourceRect, destinationRect);
                }
                call.Commit();
            }

            void SaveCubeRenderTarget(const boost::shared_ptr<gal::CubeRenderTarget>& cubeTarget, const msvc8::string& filePath) override
            {
                std::uint32_t targetId = 0;
                const boost::shared_ptr<gal::CubeRenderTarget> inner = UnwrapObject<TraceCubeRenderTarget>(cubeTarget, &targetId);
                Call call(Op::DevSaveCubeRenderTarget, id_, ready_);
                if (call) {
                    call.record.Id(targetId);
                    call.record.Str(filePath.c_str(), filePath.size());
                }
                {
                    ForwardScope forward;
                    backend_->SaveCubeRenderTarget(inner, filePath);
                }
                call.Commit();
            }

            void SaveRenderTarget(const boost::shared_ptr<gal::RenderTarget>& renderTarget, const msvc8::string& filePath, const int fileFormat) override
            {
                std::uint32_t targetId = 0;
                const boost::shared_ptr<gal::RenderTarget> inner = UnwrapObject<TraceRenderTarget>(renderTarget, &targetId);
                Call call(Op::DevSaveRenderTarget, id_, ready_);
                if (call) {
                    call.record.Id(targetId);
                    call.record.Str(filePath.c_str(), filePath.size());
                    call.record.I32(fileFormat);
                }
                {
                    ForwardScope forward;
                    backend_->SaveRenderTarget(inner, filePath, fileFormat);
                }
                call.Commit();
            }

            void SaveTexture(
                const boost::shared_ptr<gal::Texture>& texture, const msvc8::string& filePath, const int fileFormat,
                gpg::MemBuffer<char>* const outBuffer
            ) override
            {
                std::uint32_t textureId = 0;
                const boost::shared_ptr<gal::Texture> inner = UnwrapObject<TraceTexture>(texture, &textureId);
                Call call(Op::DevSaveTexture, id_, ready_);
                if (call) {
                    call.record.Id(textureId);
                    call.record.Str(filePath.c_str(), filePath.size());
                    call.record.I32(fileFormat);
                    call.record.Bool(outBuffer != nullptr);
                }
                {
                    ForwardScope forward;
                    backend_->SaveTexture(inner, filePath, fileFormat, outBuffer);
                }
                if (call) {
                    const char* const data = outBuffer != nullptr ? outBuffer->mBegin : nullptr;
                    const std::size_t size = (data != nullptr && outBuffer->mEnd > data) ? static_cast<std::size_t>(outBuffer->mEnd - data) : 0;
                    call.record.Blob(InternLocked(data, size));
                    call.Commit();
                }
            }

            void GetTexture2D(
                const void* const sourceData, const std::uint32_t sourceBytes, gpg::MemBuffer<char>* const outTextureData,
                std::uint32_t* const outWidth, int* const outHeight
            ) override
            {
                Call call(Op::DevGetTexture2D, id_, ready_);
                if (call) {
                    call.record.Blob(InternLocked(sourceData, sourceData != nullptr ? sourceBytes : 0));
                }
                {
                    ForwardScope forward;
                    backend_->GetTexture2D(sourceData, sourceBytes, outTextureData, outWidth, outHeight);
                }
                if (call) {
                    const char* const data = outTextureData != nullptr ? outTextureData->mBegin : nullptr;
                    const std::size_t size =
                        (data != nullptr && outTextureData->mEnd > data) ? static_cast<std::size_t>(outTextureData->mEnd - data) : 0;
                    call.record.Blob(InternLocked(data, size));
                    call.record.U32(outWidth != nullptr ? *outWidth : 0);
                    call.record.I32(outHeight != nullptr ? *outHeight : 0);
                    call.Commit();
                }
            }

            boost::weak_ptr<void>* Func7(boost::weak_ptr<void>* const outWeakHandle, const boost::shared_ptr<void> temporarySharedHandle) override
            {
                Call call(Op::DevFunc7, id_, ready_);
                boost::weak_ptr<void>* result;
                {
                    ForwardScope forward;
                    result = backend_->Func7(outWeakHandle, temporarySharedHandle);
                }
                call.Commit();
                return result;
            }

            // -- reset, frame (slots 25-30) ---------------------------------------------------------

            void Reset(gal::DeviceContext* const context) override
            {
                Call call(Op::DevResetWithContext, id_, ready_);
                if (call) {
                    gal::DeviceContext empty{};
                    PutDeviceContext(call.record, context != nullptr ? *context : empty);
                }
                DropHeads(); // D3D9 Reset needs every reference to its back buffers released
                {
                    ForwardScope forward;
                    backend_->Reset(context);
                    ResizeHeads();
                }
                call.Commit();
            }

            void Reset() override
            {
                Call call(Op::DevReset, id_, ready_);
                DropHeads();
                {
                    ForwardScope forward;
                    backend_->Reset();
                    ResizeHeads();
                }
                call.Commit();
            }

            int TestCooperativeLevel() override
            {
                Call call(Op::DevTestCooperativeLevel, id_, ready_);
                int result;
                {
                    ForwardScope forward;
                    result = backend_->TestCooperativeLevel();
                }
                if (call) {
                    call.record.I32(result);
                    call.Commit();
                }
                return result;
            }

            void BeginScene() override { Simple(Op::DevBeginScene, &gal::Device::BeginScene); }
            void EndScene() override { Simple(Op::DevEndScene, &gal::Device::EndScene); }

            void Present() override
            {
                {
                    Call call(Op::DevPresent, id_, ready_);
                    {
                        ForwardScope forward;
                        backend_->Present();
                    }
                    call.Commit();
                }
                if (gSession != nullptr && ready_ && tDepth == 0) {
                    std::unique_lock<std::recursive_mutex> lock(gSession->mutex);
                    if (gSession->open) {
                        ++gSession->presents;
                        const std::int64_t start = Ticks();
                        gSession->writer.Flush(); // a run that dies keeps every finished frame
                        gSession->flushTicks += Ticks() - start;
                        if (gSession->stopAfterPresents != 0 && gSession->presents >= gSession->stopAfterPresents) {
                            lock.unlock();
                            Finish(*gSession, "/galtraceframes reached");
                        }
                    }
                }
            }

            // -- cursor, viewport (slots 31-35) -----------------------------------------------------

            void SetCursor(const gal::CursorContext* const context) override
            {
                std::uint32_t textureId = 0;
                gal::CursorContext inner{};
                if (context != nullptr) {
                    inner.hotspotX_ = context->hotspotX_;
                    inner.hotspotY_ = context->hotspotY_;
                    inner.texture_ = UnwrapObject<TraceTexture>(context->texture_, &textureId);
                }
                Call call(Op::DevSetCursor, id_, ready_);
                if (call) {
                    call.record.I32(inner.hotspotX_);
                    call.record.I32(inner.hotspotY_);
                    call.record.Id(textureId);
                }
                {
                    ForwardScope forward;
                    backend_->SetCursor(context != nullptr ? &inner : nullptr);
                }
                call.Commit();
            }

            void InitCursor() override { Simple(Op::DevInitCursor, &gal::Device::InitCursor); }

            int ShowCursor(const bool show) override
            {
                Call call(Op::DevShowCursor, id_, ready_);
                if (call) {
                    call.record.Bool(show);
                }
                int result;
                {
                    ForwardScope forward;
                    result = backend_->ShowCursor(show);
                }
                if (call) {
                    call.record.I32(result);
                    call.Commit();
                }
                return result;
            }

            void SetViewport(const D3DVIEWPORT9* const viewport) override
            {
                Call call(Op::DevSetViewport, id_, ready_);
                if (call) {
                    const D3DVIEWPORT9 empty{};
                    PutViewport(call.record, viewport != nullptr ? *viewport : empty);
                }
                {
                    ForwardScope forward;
                    backend_->SetViewport(viewport);
                }
                call.Commit();
            }

            void GetViewport(D3DVIEWPORT9* const outViewport) override
            {
                Call call(Op::DevGetViewport, id_, ready_);
                {
                    ForwardScope forward;
                    backend_->GetViewport(outViewport);
                }
                if (call) {
                    const D3DVIEWPORT9 empty{};
                    PutViewport(call.record, outViewport != nullptr ? *outViewport : empty);
                    call.Commit();
                }
            }

            // -- targets, clears (slots 36-39) ------------------------------------------------------

            void ClearTarget(const gal::OutputContext* const context) override
            {
                gal::OutputContext inner{};
                std::uint32_t cubeId = 0;
                std::uint32_t surfaceId = 0;
                std::uint32_t depthId = 0;
                if (context != nullptr) {
                    inner.cubeTarget = UnwrapObject<TraceCubeRenderTarget>(context->cubeTarget, &cubeId);
                    inner.face = context->face;
                    inner.surface = UnwrapObject<TraceRenderTarget>(context->surface, &surfaceId);
                    inner.depthStencil = UnwrapObject<TraceDepthStencilTarget>(context->depthStencil, &depthId);
                }
                Call call(Op::DevClearTarget, id_, ready_);
                if (call) {
                    call.record.Id(cubeId);
                    call.record.I32(inner.face);
                    call.record.Id(surfaceId);
                    call.record.Id(depthId);
                }
                {
                    ForwardScope forward;
                    backend_->ClearTarget(context != nullptr ? &inner : nullptr);
                }
                call.Commit();
            }

            void GetContext(gal::OutputContext* const outContext) override
            {
                Call call(Op::DevGetContext, id_, ready_);
                gal::OutputContext inner{};
                {
                    ForwardScope forward;
                    backend_->GetContext(&inner);
                }
                if (!ready_ || tDepth > 0) {
                    if (outContext != nullptr) {
                        *outContext = inner;
                    }
                    return;
                }
                std::uint32_t cubeId = 0;
                std::uint32_t surfaceId = 0;
                std::uint32_t depthId = 0;
                gal::OutputContext wrapped{};
                wrapped.cubeTarget = WrapObject<TraceCubeRenderTarget>(inner.cubeTarget, ObjectType::CubeRenderTarget, &cubeId);
                wrapped.face = inner.face;
                wrapped.surface = WrapObject<TraceRenderTarget>(inner.surface, ObjectType::RenderTarget, &surfaceId);
                wrapped.depthStencil = WrapObject<TraceDepthStencilTarget>(inner.depthStencil, ObjectType::DepthStencilTarget, &depthId);
                if (call) {
                    call.record.Id(cubeId);
                    call.record.I32(wrapped.face);
                    call.record.Id(surfaceId);
                    call.record.Id(depthId);
                    call.Commit();
                }
                if (outContext != nullptr) {
                    *outContext = wrapped;
                }
            }

            void Clear(
                const bool clearTarget, const bool clearZbuffer, const bool clearStencil, const std::uint32_t color, const float depth,
                const int stencil
            ) override
            {
                Call call(Op::DevClear, id_, ready_);
                if (call) {
                    call.record.Bool(clearTarget);
                    call.record.Bool(clearZbuffer);
                    call.record.Bool(clearStencil);
                    call.record.U32(color);
                    call.record.F32(depth);
                    call.record.I32(stencil);
                }
                {
                    ForwardScope forward;
                    backend_->Clear(clearTarget, clearZbuffer, clearStencil, color, depth, stencil);
                }
                call.Commit();
            }

            void ClearTextures() override { Simple(Op::DevClearTextures, &gal::Device::ClearTextures); }

            // -- input assembly, states, draws (slots 40-49) ------------------------------------------

            void SetVertexDeclaration(const boost::shared_ptr<gal::VertexFormat> vertexFormat) override
            {
                std::uint32_t formatId = 0;
                const boost::shared_ptr<gal::VertexFormat> inner = UnwrapObject<TraceVertexFormat>(vertexFormat, &formatId);
                Call call(Op::DevSetVertexDeclaration, id_, ready_);
                if (call) {
                    call.record.Id(formatId);
                }
                {
                    ForwardScope forward;
                    backend_->SetVertexDeclaration(inner);
                }
                call.Commit();
            }

            void SetVertexBuffer(
                const std::uint32_t streamSlot, const boost::shared_ptr<gal::VertexBuffer> vertexBuffer, const int streamFrequencyToken,
                const int startVertex
            ) override
            {
                std::uint32_t bufferId = 0;
                const boost::shared_ptr<gal::VertexBuffer> inner = UnwrapObject<TraceVertexBuffer>(vertexBuffer, &bufferId);
                Call call(Op::DevSetVertexBuffer, id_, ready_);
                if (call) {
                    call.record.U32(streamSlot);
                    call.record.Id(bufferId);
                    call.record.I32(streamFrequencyToken);
                    call.record.I32(startVertex);
                }
                {
                    ForwardScope forward;
                    backend_->SetVertexBuffer(streamSlot, inner, streamFrequencyToken, startVertex);
                }
                call.Commit();
            }

            void SetBufferIndices(const boost::shared_ptr<gal::IndexBuffer> indexBuffer) override
            {
                std::uint32_t bufferId = 0;
                const boost::shared_ptr<gal::IndexBuffer> inner = UnwrapObject<TraceIndexBuffer>(indexBuffer, &bufferId);
                Call call(Op::DevSetBufferIndices, id_, ready_);
                if (call) {
                    call.record.Id(bufferId);
                }
                {
                    ForwardScope forward;
                    backend_->SetBufferIndices(inner);
                }
                call.Commit();
            }

            void SetFogState(const bool enable, const gal::Matrix* const projection, const float fogStart, const float fogEnd, const int fogColor) override
            {
                Call call(Op::DevSetFogState, id_, ready_);
                if (call) {
                    call.record.Bool(enable);
                    call.record.OptMatrix(MatrixFloats(projection));
                    call.record.F32(fogStart);
                    call.record.F32(fogEnd);
                    call.record.I32(fogColor);
                }
                {
                    ForwardScope forward;
                    backend_->SetFogState(enable, projection, fogStart, fogEnd, fogColor);
                }
                call.Commit();
            }

            void SetWireframeState(const bool enabled) override
            {
                Call call(Op::DevSetWireframeState, id_, ready_);
                if (call) {
                    call.record.Bool(enabled);
                }
                {
                    ForwardScope forward;
                    backend_->SetWireframeState(enabled);
                }
                call.Commit();
            }

            void SetColorWriteState(const bool writeColor, const bool writeAlpha) override
            {
                Call call(Op::DevSetColorWriteState, id_, ready_);
                if (call) {
                    call.record.Bool(writeColor);
                    call.record.Bool(writeAlpha);
                }
                {
                    ForwardScope forward;
                    backend_->SetColorWriteState(writeColor, writeAlpha);
                }
                call.Commit();
            }

            void DrawIndexedPrimitive(const gal::DrawIndexedContext* const context) override
            {
                Call call(Op::DevDrawIndexedPrimitive, id_, ready_);
                if (call) {
                    call.record.U32(context != nullptr ? static_cast<std::uint32_t>(context->topology_) : 0);
                    call.record.U32(context != nullptr ? context->minVertexIndex_ : 0);
                    call.record.U32(context != nullptr ? context->vertexCount_ : 0);
                    call.record.U32(context != nullptr ? context->indexCount_ : 0);
                    call.record.U32(context != nullptr ? context->startIndex_ : 0);
                    call.record.I32(context != nullptr ? context->baseVertexIndex_ : 0);
                }
                {
                    ForwardScope forward;
                    backend_->DrawIndexedPrimitive(context);
                }
                call.Commit();
            }

            void DrawPrimitive(const gal::DrawContext* const context) override
            {
                Call call(Op::DevDrawPrimitive, id_, ready_);
                if (call) {
                    call.record.U32(context != nullptr ? static_cast<std::uint32_t>(context->topology_) : 0);
                    call.record.U32(context != nullptr ? context->vertexCount_ : 0);
                    call.record.U32(context != nullptr ? context->startVertex_ : 0);
                }
                {
                    ForwardScope forward;
                    backend_->DrawPrimitive(context);
                }
                call.Commit();
            }

            void BeginTechnique() override { Simple(Op::DevBeginTechnique, &gal::Device::BeginTechnique); }
            void EndTechnique() override { Simple(Op::DevEndTechnique, &gal::Device::EndTechnique); }

        private:
            void Simple(const Op op, void (gal::Device::*method)())
            {
                Call call(op, id_, ready_);
                {
                    ForwardScope forward;
                    (backend_->*method)();
                }
                call.Commit();
            }

            static void PutViewport(RecordBuilder& b, const D3DVIEWPORT9& viewport)
            {
                b.U32(viewport.X);
                b.U32(viewport.Y);
                b.U32(viewport.Width);
                b.U32(viewport.Height);
                b.F32(viewport.MinZ);
                b.F32(viewport.MaxZ);
            }

            gal::OutputContext* HeadOutput(const unsigned int headIndex, const bool constOverload)
            {
                Call call(Op::DevGetHeadOutputContext, id_, ready_);
                if (call) {
                    call.record.U32(headIndex);
                    call.record.Bool(constOverload);
                }
                gal::OutputContext* inner;
                {
                    ForwardScope forward;
                    inner = constOverload ? const_cast<gal::OutputContext*>(static_cast<const gal::Device*>(backend_)->GetHeadOutputContext(headIndex))
                                          : backend_->GetHeadOutputContext(headIndex);
                }
                if (!ready_ || tDepth > 0 || inner == nullptr) {
                    return inner;
                }
                std::lock_guard<std::recursive_mutex> lock(gSession->mutex);
                if (headIndex >= heads_.size()) {
                    heads_.resize(headIndex + 1u);
                }
                gal::OutputContext& head = heads_[headIndex];
                std::uint32_t cubeId = 0;
                std::uint32_t surfaceId = 0;
                std::uint32_t depthId = 0;
                head.cubeTarget = WrapObject<TraceCubeRenderTarget>(inner->cubeTarget, ObjectType::CubeRenderTarget, &cubeId);
                head.face = inner->face;
                head.surface = WrapObject<TraceRenderTarget>(inner->surface, ObjectType::RenderTarget, &surfaceId);
                head.depthStencil = WrapObject<TraceDepthStencilTarget>(inner->depthStencil, ObjectType::DepthStencilTarget, &depthId);
                if (call) {
                    call.record.Id(cubeId);
                    call.record.I32(head.face);
                    call.record.Id(surfaceId);
                    call.record.Id(depthId);
                    call.Commit();
                }
                return &head;
            }

            void DropHeads()
            {
                for (gal::OutputContext& head : heads_) {
                    head.cubeTarget.reset();
                    head.surface.reset();
                    head.depthStencil.reset();
                }
            }

            void ResizeHeads()
            {
                gal::DeviceContext* const context = backend_->GetDeviceContext();
                if (context != nullptr && heads_.size() < static_cast<std::size_t>(context->GetHeadCount())) {
                    heads_.resize(static_cast<std::size_t>(context->GetHeadCount()));
                }
            }

            gal::Device* backend_;
            std::uint32_t id_ = 0;
            bool ready_ = false;
            // The head contexts handed to the engine (GetHeadOutputContext returns a pointer the
            // engine may keep): stable storage, refreshed on every call.
            std::vector<gal::OutputContext> heads_;
        };

        // ---- the DeviceFactory decorator ---------------------------------------------------------

        class TraceDecorator final : public gpg::gal::diligent::DeviceDecorator
        {
        public:
            gal::Device* Wrap(gal::Device* const backend) override
            {
                if (backend == nullptr || gSession == nullptr) {
                    return backend;
                }
                TraceDevice* const device = new TraceDevice(backend);
                gActiveDevice = device;
                return device;
            }

            void OnSetup(gal::Device* const installed, gal::Device* const backend, const gal::DeviceContext& context) override
            {
                (void)backend;
                if (installed != nullptr && installed == gActiveDevice) {
                    gActiveDevice->OnSetupDone(context);
                }
            }

            // DeviceDecorator's two queries (DeviceFactory.h): Device::GetInstance and
            // SupportsVertexTextureFormat (src/sdk/gpg/gal/Device.cpp, FAF_PORT_GRAPHICS) ask them;
            // the free functions ResolveActiveDevice/BackendOfDevice (GalTraceRecorder.h) answer.
            gal::Device* BackendOf(gal::Device* const installed) override
            {
                gal::Device* const backend = BackendOfDevice(installed);
                return backend != installed ? backend : nullptr;
            }

            gal::Device* ResolveInstance(gal::Device* const installed) override
            {
                return ResolveActiveDevice(installed);
            }
        };

        TraceDecorator gDecorator;

        bool ArgValue(const char* const option, std::string* const value)
        {
            for (int index = 1; index + 1 < __argc; ++index) {
                if (__argv[index] != nullptr && _stricmp(__argv[index], option) == 0 && __argv[index + 1] != nullptr) {
                    *value = __argv[index + 1];
                    return true;
                }
            }
            return false;
        }

        std::string CommandLine()
        {
            std::string line;
            for (int index = 1; index < __argc; ++index) {
                if (__argv[index] != nullptr) {
                    line += (index > 1 ? " " : "");
                    line += __argv[index];
                }
            }
            return line;
        }

        /**
         * Static initialisation (port sources follow the engine's in the link, so __argv is set and
         * the engine's statics exist): `/galtrace <file>` opens the trace and installs the decorator.
         */
        struct RecorderBootstrap
        {
            RecorderBootstrap()
            {
                std::string path;
                if (!ArgValue("/galtrace", &path) || path.empty()) {
                    return;
                }
                auto* const session = new Session();
                session->path = path;
                std::string frames;
                if (ArgValue("/galtraceframes", &frames)) {
                    session->stopAfterPresents = static_cast<std::uint32_t>(std::strtoul(frames.c_str(), nullptr, 10));
                }
                std::string harnessFrames;
                (void)ArgValue("/galframes", &harnessFrames);
                std::string api;
                (void)ArgValue("/gal", &api);
                char built[64];
                std::snprintf(built, sizeof(built), "%s %s", __DATE__, __TIME__);
                const galtrace::Metadata metadata = {
                    {"recorder", "faf-re main.exe galtrace recorder (port/graphics/trace/record)"},
                    {"recorder_build", built},
                    {"recorder_pointer_bits", std::to_string(sizeof(void*) * 8)},
                    {"gal", api.empty() ? std::string("d3d9") : api},
                    {"harness_frames", harnessFrames},
                    {"command_line", CommandLine()},
                };
                std::string error;
                if (!session->writer.Open(path, metadata, &error)) {
                    // No trace without a file; the run goes on unrecorded.
                    delete session;
                    return;
                }
                session->open = true;
                gSession = session;
                gpg::gal::diligent::SetDeviceDecorator(&gDecorator);
                std::atexit(&FinishAtExit);
            }
        };

        RecorderBootstrap gRecorderBootstrap;
    } // namespace

    bool RecorderActive() noexcept
    {
        return gSession != nullptr;
    }

    gpg::gal::Device* ResolveActiveDevice(gpg::gal::Device* const installed) noexcept
    {
        TraceDevice* const active = gActiveDevice;
        if (installed != nullptr && installed == active && (tDepth > 0 || active->InSetup())) {
            return active->Backend();
        }
        return installed;
    }

    gpg::gal::Device* BackendOfDevice(gpg::gal::Device* const installed) noexcept
    {
        TraceDevice* const active = gActiveDevice;
        if (installed != nullptr && installed == active) {
            return active->Backend();
        }
        return installed;
    }

    void FinishRecording(const char* const reason)
    {
        if (gSession != nullptr) {
            Finish(*gSession, reason != nullptr ? reason : "FinishRecording");
        }
    }
} // namespace port::graphics::trace
