// galplay's core (GalPlayer.h): one switch over the trace's ops, each case reading its record
// through the schema-checked Cursor (port/graphics/trace/format/GalTraceIO.h) and making the same
// gal call on the replay device. Objects are found by the trace's ids; a payload is copied in at the
// point the engine wrote it (Unlock), so the replay backend's pitches and pointers never matter.

#include "port/graphics/trace/play/GalPlayer.h"

#include "port/graphics/trace/format/GalTraceIO.h"
#include "port/graphics/trace/format/GalTraceResolver.h"

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

#if defined(_WIN32)
#include <float.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <unordered_map>

namespace port::graphics::trace
{
    namespace
    {
        namespace gal = gpg::gal;
        using galtrace::Cursor;
        using galtrace::ObjectType;
        using galtrace::Op;

        struct ArrayDelete
        {
            void operator()(char* const pointer) const { delete[] pointer; }
        };

        gpg::MemBuffer<char> OwnedBuffer(const std::vector<std::uint8_t>& bytes)
        {
            if (bytes.empty()) {
                return gpg::MemBuffer<char>{};
            }
            boost::shared_ptr<char> data(new char[bytes.size()], ArrayDelete{});
            std::memcpy(data.get(), bytes.data(), bytes.size());
            return gpg::MemBuffer<char>(data, bytes.size());
        }

        msvc8::string ToString(const std::string& text)
        {
            return msvc8::string(text.data(), text.size());
        }

        std::string BaseName(const std::string& path)
        {
            const std::size_t slash = path.find_last_of("/\\");
            return slash == std::string::npos ? path : path.substr(slash + 1);
        }

        gal::Matrix ToMatrix(const float* const sixteen)
        {
            static_assert(sizeof(gal::Matrix) == 16 * sizeof(float), "gal::Matrix is 16 floats");
            gal::Matrix matrix;
            std::memcpy(&matrix, sixteen, sizeof(matrix));
            return matrix;
        }

        RECT ToRect(const galtrace::Rect& rect)
        {
            RECT out{};
            out.left = rect.left;
            out.top = rect.top;
            out.right = rect.right;
            out.bottom = rect.bottom;
            return out;
        }

        std::uint64_t RgbHash(const std::vector<std::uint8_t>& bgra)
        {
            return galtrace::FrameRgbHash(bgra.data(), bgra.size());
        }

        std::vector<unsigned> ParseFrameList(const std::string& text)
        {
            std::vector<unsigned> frames;
            unsigned value = 0;
            bool any = false;
            for (const char c : text) {
                if (c >= '0' && c <= '9') {
                    value = value * 10u + static_cast<unsigned>(c - '0');
                    any = true;
                } else {
                    if (any) {
                        frames.push_back(value);
                    }
                    value = 0;
                    any = false;
                }
            }
            if (any) {
                frames.push_back(value);
            }
            return frames;
        }

        void PutLe16(std::vector<std::uint8_t>& out, const std::uint32_t value)
        {
            out.push_back(static_cast<std::uint8_t>(value));
            out.push_back(static_cast<std::uint8_t>(value >> 8));
        }

        void PutLe32(std::vector<std::uint8_t>& out, const std::uint32_t value)
        {
            for (int shift = 0; shift < 32; shift += 8) {
                out.push_back(static_cast<std::uint8_t>(value >> shift));
            }
        }

        /** The frame harness's BMP (GalCapture.cpp WriteBmp): 32-bit BI_RGB, bottom-up, B G R A bytes. */
        bool WriteBmp(const std::string& path, const unsigned width, const unsigned height, const std::vector<std::uint8_t>& topDownBgra)
        {
            const std::uint32_t pixelBytes = width * height * 4u;
            std::vector<std::uint8_t> header;
            PutLe16(header, 0x4D42);
            PutLe32(header, 14u + 40u + pixelBytes);
            PutLe16(header, 0);
            PutLe16(header, 0);
            PutLe32(header, 14u + 40u);
            PutLe32(header, 40u);
            PutLe32(header, width);
            PutLe32(header, height);
            PutLe16(header, 1);
            PutLe16(header, 32);
            PutLe32(header, 0); // BI_RGB
            PutLe32(header, pixelBytes);
            PutLe32(header, 0);
            PutLe32(header, 0);
            PutLe32(header, 0);
            PutLe32(header, 0);
            std::FILE* const file = std::fopen(path.c_str(), "wb");
            if (file == nullptr) {
                return false;
            }
            bool ok = std::fwrite(header.data(), 1, header.size(), file) == header.size();
            for (unsigned row = height; ok && row-- > 0;) {
                ok = std::fwrite(topDownBgra.data() + static_cast<std::size_t>(row) * width * 4u, width * 4u, 1, file) == 1;
            }
            ok = (std::fclose(file) == 0) && ok;
            return ok;
        }

        struct Slot
        {
            ObjectType type = ObjectType::None;
            boost::shared_ptr<void> object;
        };

        struct TextureLockState
        {
            bool locked = false;
            gal::TextureLockRect lock{};
            std::uint32_t rowBytes = 0;
            std::uint32_t rows = 0;
            std::uint32_t width = 0;  // the locked level's size, for readback BMPs
            std::uint32_t height = 0;
            std::uint32_t format = 0;
        };

        struct BufferLockState
        {
            std::uint8_t* data = nullptr;
            std::uint32_t bytes = 0;
        };

        class Player
        {
        public:
            Player(const PlayOptions& options, const PlayHooks& hooks, PlayReport& report)
                : options_(options), hooks_(hooks), report_(report)
            {}

            bool Run()
            {
                std::string error;
                if (!reader_.Open(options_.tracePath, &error)) {
                    report_.fatal = error;
                    return false;
                }
                report_.formatVersion = reader_.Version();
                reader_.SetResolver(options_.resolver);
                readbackNames_ = options_.readbackNames;
                if (readbackNames_.empty()) {
                    readbackNames_ = ParseFrameList(reader_.Meta(galtrace::kMetaHarnessFrames));
                }
                if (!options_.referenceBackend.empty()) {
                    for (const auto& [frame, hash] :
                         galtrace::ParseFrameHashes(reader_.Meta(galtrace::kMetaReferenceFramesPrefix + options_.referenceBackend))) {
                        referenceFrames_[frame] = hash;
                    }
                }
                // The recording backend's own frame hashes, when the trace keeps only digests of its readbacks.
                for (const auto& [frame, hash] :
                     galtrace::ParseFrameHashes(reader_.Meta(galtrace::kMetaReferenceFramesPrefix + reader_.Meta(galtrace::kMetaGal)))) {
                    recordedFrames_[frame] = hash;
                }
                if (reader_.Version() >= galtrace::kFormatVersion2 && options_.verifyReferencesFirst && !CheckReferences()) {
                    TearDown();
                    return false;
                }
                galtrace::Record record;
                while (reader_.Next(&record)) {
                    if (hooks_.shouldStop && hooks_.shouldStop()) {
                        report_.stopped = true;
                        report_.fatal = "stopped on request after " + std::to_string(report_.presents) + " presents";
                        break;
                    }
                    ++report_.records;
                    ++report_.opCounts[record.desc->name];
                    if (record.op == static_cast<std::uint16_t>(Op::End)) {
                        ended_ = true;
                        break;
                    }
                    if (!options_.skipOp.empty() && options_.skipOp == record.desc->name) {
                        ++report_.skipped;
                        continue;
                    }
                    try {
                        if (!Play(record)) {
                            break;
                        }
                    } catch (const gal::Error& galError) {
                        ++report_.galErrors;
                        Error(record, std::string("gal::Error ") + galError.what() + " (" + galError.GetRuntimeMessage() + ")");
                    } catch (const std::exception& exception) {
                        ++report_.galErrors;
                        Error(record, std::string("exception ") + exception.what());
                    }
                }
                if (report_.fatal.empty() && reader_.Failed()) {
                    report_.fatal = "reader: " + reader_.Error();
                }
                if (report_.fatal.empty() && !ended_) {
                    report_.fatal = "the trace has no End record (truncated)";
                }
                TearDown();
                report_.completed = report_.fatal.empty();
                return report_.completed;
            }

        private:
            // ---- bookkeeping ------------------------------------------------------------------

            /** Version 2: every game file the trace refers to, read and checked before the first call. */
            bool CheckReferences()
            {
                const auto start = std::chrono::steady_clock::now();
                std::vector<galtrace::PayloadRefInfo> refs;
                std::string error;
                if (!galtrace::ScanPayloadRefs(options_.tracePath, &refs, &error)) {
                    report_.fatal = "reader: " + error;
                    return false;
                }
                bool ok = true;
                if (!refs.empty() && options_.resolver == nullptr) {
                    report_.missingData = true;
                    report_.fatal = "the trace takes " + std::to_string(refs.size()) + " files from the game data, and no game data was given";
                    ok = false;
                } else if (!refs.empty()) {
                    std::vector<galtrace::PayloadRefProblem> problems;
                    if (!galtrace::VerifyPayloadRefs(refs, *options_.resolver, &problems)) {
                        report_.missingData = true;
                        for (const galtrace::PayloadRefProblem& problem : problems) {
                            report_.dataProblems.push_back(problem.ref.path + " (" + problem.ref.archive + "): " +
                                                           (problem.missing ? "missing" : problem.detail));
                        }
                        report_.fatal = galtrace::DescribeRefProblems(problems);
                        while (!report_.fatal.empty() && report_.fatal.back() == '\n') {
                            report_.fatal.pop_back();
                        }
                        ok = false;
                    }
                }
                report_.referenceCheckSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
                return ok;
            }

            /** A payload the call needs: its bytes, or a fatal error that names what is missing. */
            bool NeedPayload(const galtrace::Record& record, const std::uint32_t id, std::vector<std::uint8_t>* const out, const char* const what)
            {
                galtrace::PayloadStatus status;
                if (!reader_.ReadPayload(id, out, &status)) {
                    if (status.kind == galtrace::PayloadKind::Reference) {
                        report_.missingData = true;
                        report_.dataProblems.push_back(status.error);
                    }
                    Fatal(record, std::string(what) + ": " + status.error);
                    return false;
                }
                if (status.kind == galtrace::PayloadKind::Reference) {
                    ++report_.references;
                }
                if (!status.exact) {
                    Mismatch(record, "derived payload", std::string(what) + ": " + status.error);
                }
                return true;
            }

            void Mismatch(const galtrace::Record& record, const std::string& category, const std::string& text)
            {
                ++report_.mismatchCounts[category];
                if (report_.mismatchExamples.size() < 60 && report_.mismatchCounts[category] <= 6) {
                    report_.mismatchExamples.push_back("record " + std::to_string(record.index) + " " + record.desc->name + ": " + text);
                }
            }

            void Error(const galtrace::Record& record, const std::string& text)
            {
                if (report_.errorExamples.size() < 40) {
                    report_.errorExamples.push_back("record " + std::to_string(record.index) + " " + record.desc->name + ": " + text);
                }
            }

            bool Fatal(const galtrace::Record& record, const std::string& text)
            {
                report_.fatal = "record " + std::to_string(record.index) + " " + record.desc->name + ": " + text;
                return false;
            }

            template <class T>
            boost::shared_ptr<T> Get(const galtrace::Record& record, const std::uint32_t id, const ObjectType type)
            {
                if (id == 0) {
                    return boost::shared_ptr<T>();
                }
                const auto found = objects_.find(id);
                if (found == objects_.end()) {
                    Mismatch(record, "unresolved id", "@" + std::to_string(id) + " is not bound in the replay");
                    return boost::shared_ptr<T>();
                }
                if (found->second.type != type) {
                    Mismatch(record, "id type", "@" + std::to_string(id) + " is a " + galtrace::ObjectTypeName(found->second.type));
                    return boost::shared_ptr<T>();
                }
                return boost::static_pointer_cast<T>(found->second.object);
            }

            template <class T>
            void Bind(const galtrace::Record& record, const std::uint32_t id, const ObjectType type, const boost::shared_ptr<T>& object)
            {
                if (id == 0) {
                    if (object) {
                        Mismatch(record, "object where the recording had none", galtrace::ObjectTypeName(type));
                    }
                    return;
                }
                if (!object) {
                    Mismatch(record, "null where the recording had an object", "@" + std::to_string(id));
                }
                Slot& slot = objects_[id];
                slot.type = type;
                slot.object = boost::static_pointer_cast<void>(object);
            }

            bool Finish(const galtrace::Record& record, const Cursor& cursor)
            {
                if (!cursor.Finished()) {
                    return Fatal(record, cursor.Ok() ? "the record has fields the player did not read" : cursor.Error());
                }
                ++report_.calls;
                return true;
            }

            gal::Device* DeviceFor(const galtrace::Record& record, const std::uint32_t id)
            {
                if (device_ == nullptr || id != deviceId_) {
                    Mismatch(record, "device", "@" + std::to_string(id) + " is not the replay device");
                }
                return device_;
            }

            void ReadDeviceContext(Cursor& c, gal::DeviceContext& context)
            {
                context.mDeviceType = static_cast<gal::DeviceApi>(c.I32());
                context.mValidate = c.Bool();
                context.mAdapter = c.I32();
                context.mVSync = c.Bool();
                context.mHWBasedInstancing = c.Bool();
                context.mSupportsFloat16 = c.Bool();
                context.mVertexShaderProfile = c.I32();
                context.mPixelShaderProfile = c.I32();
                context.mMaxPrimitiveCount = c.U32();
                context.mMaxVertexCount = c.U32();
                const std::uint32_t heads = c.Count();
                for (std::uint32_t index = 0; index < heads; ++index) {
                    gal::Head head{};
                    const bool hasHandle = c.Bool();
                    const bool hasWindow = c.Bool();
                    // Flags only: the caller's hooks put the replay's own windows in.
                    head.mHandle = hasHandle ? reinterpret_cast<void*>(static_cast<std::uintptr_t>(1)) : nullptr;
                    head.mWindow = hasWindow ? reinterpret_cast<void*>(static_cast<std::uintptr_t>(1)) : nullptr;
                    head.mWindowed = c.Bool();
                    head.mWidth = c.U32();
                    head.mHeight = c.U32();
                    head.framesPerSecond = c.U32();
                    head.antialiasingHigh = c.U32();
                    head.antialiasingLow = c.U32();
                    head.name = ToString(c.Str());
                    const std::uint32_t samples = c.Count();
                    for (std::uint32_t sample = 0; sample < samples; ++sample) {
                        gal::HeadSampleOption option{};
                        option.sampleType = c.U32();
                        option.sampleQuality = c.U32();
                        option.label = ToString(c.Str());
                        head.mStrs.push_back(option);
                    }
                    const std::uint32_t modes = c.Count();
                    for (std::uint32_t mode = 0; mode < modes; ++mode) {
                        gal::HeadAdapterMode value{};
                        value.width = c.U32();
                        value.height = c.U32();
                        value.refreshRate = c.U32();
                        head.adapterModes.push_back(value);
                    }
                    for (const std::uint32_t format : c.U32Array()) {
                        head.validFormats2.push_back(static_cast<std::int32_t>(format));
                    }
                    for (const std::uint32_t format : c.U32Array()) {
                        head.validFormats1.push_back(static_cast<std::int32_t>(format));
                    }
                    context.AddHead(head);
                }
            }

            /** Differences between the device context the replay backend reports and the recorded one. */
            void CompareDeviceContext(const galtrace::Record& record, const gal::DeviceContext& recorded, const gal::DeviceContext& actual)
            {
                auto field = [&](const char* name, const long long a, const long long b) {
                    if (a != b) {
                        Mismatch(record, "device context", std::string(name) + " recorded " + std::to_string(a) + ", replay " + std::to_string(b));
                    }
                };
                field("validate", recorded.mValidate, actual.mValidate);
                field("adapter", recorded.mAdapter, actual.mAdapter);
                field("vsync", recorded.mVSync, actual.mVSync);
                field("hwBasedInstancing", recorded.mHWBasedInstancing, actual.mHWBasedInstancing);
                field("supportsFloat16", recorded.mSupportsFloat16, actual.mSupportsFloat16);
                field("vertexShaderProfile", recorded.mVertexShaderProfile, actual.mVertexShaderProfile);
                field("pixelShaderProfile", recorded.mPixelShaderProfile, actual.mPixelShaderProfile);
                field("maxPrimitiveCount", recorded.mMaxPrimitiveCount, actual.mMaxPrimitiveCount);
                field("maxVertexCount", recorded.mMaxVertexCount, actual.mMaxVertexCount);
                field("heads", recorded.GetHeadCount(), actual.GetHeadCount());
                const int heads = std::min(recorded.GetHeadCount(), actual.GetHeadCount());
                for (int index = 0; index < heads; ++index) {
                    const gal::Head& a = recorded.GetHead(static_cast<std::uint32_t>(index));
                    const gal::Head& b = actual.GetHead(static_cast<std::uint32_t>(index));
                    field("head width", a.mWidth, b.mWidth);
                    field("head height", a.mHeight, b.mHeight);
                    field("head windowed", a.mWindowed, b.mWindowed);
                    field("head sample options", static_cast<long long>(a.mStrs.size()), static_cast<long long>(b.mStrs.size()));
                    field("head adapter modes", static_cast<long long>(a.adapterModes.size()), static_cast<long long>(b.adapterModes.size()));
                    field("head validFormats2", static_cast<long long>(a.validFormats2.size()), static_cast<long long>(b.validFormats2.size()));
                    field("head validFormats1", static_cast<long long>(a.validFormats1.size()), static_cast<long long>(b.validFormats1.size()));
                }
            }

            void ReadTextureContext(Cursor& c, gal::TextureContext& context, std::uint32_t* const dataBlob)
            {
                context.source_ = c.U32();
                context.location_ = ToString(c.Str());
                const std::uint32_t blob = c.Blob();
                if (dataBlob != nullptr) {
                    *dataBlob = blob;
                }
                context.type_ = c.U32();
                context.usage_ = c.U32();
                context.format_ = c.U32();
                context.mipmapLevels_ = c.U32();
                context.reserved0x44_ = c.U32();
                context.width_ = c.U32();
                context.height_ = c.U32();
                context.reserved0x50_ = c.U32();
            }

            void CompareTextureContext(const galtrace::Record& record, const gal::TextureContext& recorded, const gal::TextureContext* actual)
            {
                if (actual == nullptr) {
                    Mismatch(record, "texture context", "the replay texture has no context");
                    return;
                }
                if (recorded.type_ != actual->type_ || recorded.format_ != actual->format_ || recorded.width_ != actual->width_ ||
                    recorded.height_ != actual->height_ || recorded.mipmapLevels_ != actual->mipmapLevels_) {
                    char text[256];
                    std::snprintf(text, sizeof(text), "recorded type %u format %u %ux%u mips %u, replay type %u format %u %ux%u mips %u",
                                  recorded.type_, recorded.format_, recorded.width_, recorded.height_, recorded.mipmapLevels_, actual->type_,
                                  actual->format_, actual->width_, actual->height_, actual->mipmapLevels_);
                    Mismatch(record, "texture context", text);
                }
            }

            void ApplyFpu(const std::uint32_t x87, const std::uint32_t sse)
            {
#if defined(_M_IX86)
                if (options_.applyFpuState) {
                    unsigned int current = 0;
                    (void)__control87_2(x87, _MCW_EM | _MCW_RC | _MCW_PC, &current, nullptr);
                    (void)__control87_2(sse, _MCW_EM | _MCW_RC | _MCW_DN, nullptr, &current);
                }
#else
                (void)x87;
                (void)sse;
#endif
            }

            std::string OutPath(const std::string& sub, const std::string& path)
            {
                return options_.outDir + "/" + sub + "/" + BaseName(path);
            }

            /** A read-only lock: the replayed bytes against the recording (bytes or hashes) and the reference frame hash. */
            bool Readback(const galtrace::Record& record, TextureLockState& state, const std::uint32_t recordedBlob)
            {
                ReadbackResult result{};
                result.index = static_cast<unsigned>(report_.readbacks.size());
                result.name = result.index < readbackNames_.size() ? readbackNames_[result.index] : result.index + 1;
                result.presents = report_.presents;
                result.endScenes = report_.endScenes;
                result.format = state.format;
                std::vector<std::uint8_t> replayed(static_cast<std::size_t>(state.rowBytes) * state.rows);
                const auto* const bits = static_cast<const std::uint8_t*>(state.lock.bits);
                if (bits != nullptr && state.lock.pitch >= static_cast<int>(state.rowBytes)) {
                    for (std::uint32_t row = 0; row < state.rows; ++row) {
                        std::memcpy(replayed.data() + static_cast<std::size_t>(row) * state.rowBytes,
                                    bits + static_cast<std::size_t>(row) * static_cast<std::size_t>(state.lock.pitch), state.rowBytes);
                    }
                } else {
                    Mismatch(record, "readback", "the replay lock has no bits or a short pitch");
                }
                // The recording: its bytes (version 1, or an embedded version 2 payload) or its hashes.
                const galtrace::PayloadEntry* const entry = reader_.GetPayload(recordedBlob);
                std::vector<std::uint8_t> recorded;
                if (entry != nullptr && entry->kind == galtrace::PayloadKind::Digest) {
                    result.identical = galtrace::HashBlob(replayed.data(), replayed.size()) == entry->key;
                } else {
                    if (!NeedPayload(record, recordedBlob, &recorded, "the recorded readback")) {
                        return false;
                    }
                    result.recordedBytesKnown = true;
                    result.identical = replayed == recorded;
                    if (!result.identical) {
                        const std::size_t common = std::min(replayed.size(), recorded.size());
                        for (std::size_t index = 0; index < common; ++index) {
                            result.differingBytes += replayed[index] != recorded[index] ? 1 : 0;
                        }
                        result.differingBytes += std::max(replayed.size(), recorded.size()) - common;
                    }
                }
                const bool bgra = (state.format == 2 || state.format == 3) && state.rowBytes == state.width * 4u;
                if (bgra) {
                    result.width = state.width;
                    result.height = state.rows;
                    result.replayedRgb = galtrace::Hex64(RgbHash(replayed));
                    if (result.recordedBytesKnown) {
                        result.recordedRgb = galtrace::Hex64(RgbHash(recorded));
                    } else if (const auto found = recordedFrames_.find(result.name); found != recordedFrames_.end()) {
                        result.recordedRgb = found->second;
                    }
                    if (options_.writeBmps && !options_.outDir.empty()) {
                        char name[48];
                        std::snprintf(name, sizeof(name), "frame_%06u.bmp", result.name);
                        result.file = name;
                        if (!WriteBmp(options_.outDir + "/" + name, result.width, result.height, replayed)) {
                            Mismatch(record, "readback", std::string("could not write ") + name);
                        }
                    }
                }
                if (const auto found = referenceFrames_.find(result.name); found != referenceFrames_.end()) {
                    result.referenceRgb = found->second;
                    result.verdict = result.replayedRgb == result.referenceRgb ? "pass" : "differs";
                } else {
                    result.verdict = "no-reference";
                }
                report_.readbacks.push_back(result);
                if (hooks_.onReadback) {
                    hooks_.onReadback(report_.readbacks.back(), replayed.data(), state.rowBytes, state.rows);
                }
                return true;
            }

            void ReleaseAll()
            {
                for (auto& [id, lock] : textureLocks_) {
                    (void)id;
                    lock.locked = false;
                }
                bufferLocks_.clear();
                objects_.clear();
            }

            void TearDown()
            {
                ReleaseAll();
                if (device_ != nullptr && hooks_.destroyDevice) {
                    hooks_.destroyDevice(device_);
                }
                device_ = nullptr;
            }

            // ---- the ops -------------------------------------------------------------------------

            bool Play(const galtrace::Record& record)
            {
                Cursor c(record);
                switch (static_cast<Op>(record.op)) {
                    case Op::Note:
                        (void)c.Str();
                        return Finish(record, c);

                    case Op::FpuState: {
                        const std::uint32_t x87 = c.U32();
                        const std::uint32_t sse = c.U32();
                        ApplyFpu(x87, sse);
                        return Finish(record, c);
                    }

                    case Op::DeviceCreate: {
                        const std::uint32_t id = c.Id();
                        gal::DeviceContext requested;
                        ReadDeviceContext(c, requested);
                        gal::DeviceContext recordedActual;
                        ReadDeviceContext(c, recordedActual);
                        if (!c.Finished()) {
                            return Fatal(record, c.Ok() ? "unread fields" : c.Error());
                        }
                        if (device_ != nullptr) {
                            TearDown();
                        }
                        if (!hooks_.createDevice) {
                            return Fatal(record, "no device hook");
                        }
                        device_ = hooks_.createDevice(requested);
                        if (device_ == nullptr) {
                            return Fatal(record, "the replay device could not be created");
                        }
                        deviceId_ = id;
                        if (const gal::DeviceContext* const actual = device_->GetDeviceContext()) {
                            CompareDeviceContext(record, recordedActual, *actual);
                        }
                        ++report_.calls;
                        return true;
                    }

                    case Op::DeviceDestroy: {
                        const std::uint32_t id = c.Id();
                        if (!Finish(record, c)) {
                            return false;
                        }
                        if (id == deviceId_) {
                            TearDown();
                        }
                        return true;
                    }

                    case Op::Release: {
                        const std::uint32_t id = c.Id();
                        if (!Finish(record, c)) {
                            return false;
                        }
                        textureLocks_.erase(id);
                        bufferLocks_.erase(id);
                        if (objects_.erase(id) == 0) {
                            Mismatch(record, "unresolved id", "release of @" + std::to_string(id));
                        }
                        return true;
                    }

                    // -- Device --------------------------------------------------------------------

                    case Op::DevGetLog: {
                        gal::Device* const d = DeviceFor(record, c.Id());
                        if (!Finish(record, c)) return false;
                        if (d) (void)d->GetLog();
                        return true;
                    }
                    case Op::DevGetDeviceContext: {
                        gal::Device* const d = DeviceFor(record, c.Id());
                        if (!Finish(record, c)) return false;
                        if (d) (void)d->GetDeviceContext();
                        return true;
                    }
                    case Op::DevGetCurThreadId: {
                        gal::Device* const d = DeviceFor(record, c.Id());
                        if (!Finish(record, c)) return false;
                        if (d) (void)d->GetCurThreadId();
                        return true;
                    }
                    case Op::DevFunc1: {
                        gal::Device* const d = DeviceFor(record, c.Id());
                        if (!Finish(record, c)) return false;
                        if (d) d->Func1();
                        return true;
                    }
                    case Op::DevFunc7: {
                        gal::Device* const d = DeviceFor(record, c.Id());
                        if (!Finish(record, c)) return false;
                        if (d) {
                            boost::weak_ptr<void> handle;
                            (void)d->Func7(&handle, boost::shared_ptr<void>());
                        }
                        return true;
                    }
                    case Op::DevGetModesForAdapter: {
                        gal::Device* const d = DeviceFor(record, c.Id());
                        const int adapter = c.I32();
                        std::vector<gal::HeadAdapterMode> recorded(c.Count());
                        for (gal::HeadAdapterMode& mode : recorded) {
                            mode.width = c.U32();
                            mode.height = c.U32();
                            mode.refreshRate = c.U32();
                        }
                        if (!Finish(record, c)) return false;
                        if (d) {
                            msvc8::vector<gal::HeadAdapterMode> modes;
                            d->GetModesForAdapter(modes, adapter);
                            bool same = modes.size() == recorded.size();
                            for (std::size_t index = 0; same && index < recorded.size(); ++index) {
                                same = modes[index].width == recorded[index].width && modes[index].height == recorded[index].height &&
                                       modes[index].refreshRate == recorded[index].refreshRate;
                            }
                            if (!same) {
                                Mismatch(record, "adapter modes", "recorded " + std::to_string(recorded.size()) + " modes, replay " +
                                                                    std::to_string(modes.size()));
                            }
                        }
                        return true;
                    }
                    case Op::DevGetHeadOutputContext: {
                        gal::Device* const d = DeviceFor(record, c.Id());
                        const std::uint32_t head = c.U32();
                        const bool constOverload = c.Bool();
                        const std::uint32_t cubeId = c.Id();
                        const std::int32_t face = c.I32();
                        const std::uint32_t surfaceId = c.Id();
                        const std::uint32_t depthId = c.Id();
                        if (!Finish(record, c)) return false;
                        if (d) {
                            const gal::OutputContext* const output =
                                constOverload ? static_cast<const gal::Device*>(d)->GetHeadOutputContext(head) : d->GetHeadOutputContext(head);
                            if (output != nullptr) {
                                Bind(record, cubeId, ObjectType::CubeRenderTarget, output->cubeTarget);
                                Bind(record, surfaceId, ObjectType::RenderTarget, output->surface);
                                Bind(record, depthId, ObjectType::DepthStencilTarget, output->depthStencil);
                                if (output->face != face) {
                                    Mismatch(record, "head output", "face recorded " + std::to_string(face) + ", replay " +
                                                                        std::to_string(output->face));
                                }
                            }
                        }
                        return true;
                    }
                    case Op::DevGetPipelineState: {
                        gal::Device* const d = DeviceFor(record, c.Id());
                        const std::uint32_t id = c.Id();
                        if (!Finish(record, c)) return false;
                        if (d) Bind(record, id, ObjectType::PipelineState, d->GetPipelineState());
                        return true;
                    }
                    case Op::DevCreateEffect: {
                        gal::Device* const d = DeviceFor(record, c.Id());
                        gal::EffectContext context;
                        context.mSourceType = c.U32();
                        context.mUseCache = c.Bool();
                        context.mSourcePath = ToString(c.Str());
                        const std::string cachePath = c.Str();
                        const std::uint32_t sourceBlob = c.Blob();
                        const std::uint32_t macros = c.Count();
                        for (std::uint32_t index = 0; index < macros; ++index) {
                            const std::string name = c.Str();
                            const std::string value = c.Str();
                            context.mMacros.push_back(gal::EffectMacro(name.c_str(), value.c_str()));
                        }
                        const std::uint32_t cacheBlob = c.Blob();
                        const std::uint32_t id = c.Id();
                        if (!Finish(record, c)) return false;
                        std::vector<std::uint8_t> source;
                        if (!NeedPayload(record, sourceBlob, &source, "the effect source")) {
                            return false;
                        }
                        context.mSourceBuffer = OwnedBuffer(source);
                        std::vector<std::uint8_t> cacheFile;
                        if (!NeedPayload(record, cacheBlob, &cacheFile, "the compiled effect cache file")) {
                            return false;
                        }
                        // The recording's cache path is another run's directory: the replay reads
                        // and writes its own copy under <out>/cache.
                        if (!cachePath.empty()) {
                            const std::string mapped = OutPath("cache", cachePath);
                            context.mCachePath = ToString(mapped);
                            if (context.mUseCache) {
                                if (std::FILE* const file = std::fopen(mapped.c_str(), "wb")) {
                                    if (!cacheFile.empty()) {
                                        std::fwrite(cacheFile.data(), 1, cacheFile.size(), file);
                                    }
                                    std::fclose(file);
                                }
                            }
                        }
                        if (d) Bind(record, id, ObjectType::Effect, d->CreateEffect(context));
                        return true;
                    }
                    case Op::DevCreateTexture: {
                        gal::Device* const d = DeviceFor(record, c.Id());
                        gal::TextureContext context;
                        std::uint32_t dataBlob = 0;
                        ReadTextureContext(c, context, &dataBlob);
                        const std::uint32_t id = c.Id();
                        gal::TextureContext created;
                        ReadTextureContext(c, created, nullptr);
                        if (!Finish(record, c)) return false;
                        std::vector<std::uint8_t> data;
                        if (!NeedPayload(record, dataBlob, &data, "the texture's file image")) {
                            return false;
                        }
                        if (!data.empty()) {
                            const gpg::MemBuffer<char> owned = OwnedBuffer(data);
                            context.SetDataBuffer(gpg::MemBuffer<const char>(owned));
                        }
                        if (d) {
                            const boost::shared_ptr<gal::Texture> texture = d->CreateTexture(&context);
                            Bind(record, id, ObjectType::Texture, texture);
                            if (texture) {
                                CompareTextureContext(record, created, texture->GetContext());
                            }
                        }
                        return true;
                    }
                    case Op::DevCreateRenderTarget: {
                        gal::Device* const d = DeviceFor(record, c.Id());
                        const std::uint32_t width = c.U32();
                        const std::uint32_t height = c.U32();
                        const std::uint32_t format = c.U32();
                        const std::uint32_t id = c.Id();
                        if (!Finish(record, c)) return false;
                        const gal::RenderTargetContext context(width, height, format);
                        if (d) Bind(record, id, ObjectType::RenderTarget, d->CreateRenderTarget(&context));
                        return true;
                    }
                    case Op::DevCreateCubeRenderTarget: {
                        gal::Device* const d = DeviceFor(record, c.Id());
                        const std::uint32_t dimension = c.U32();
                        const std::uint32_t format = c.U32();
                        const std::uint32_t id = c.Id();
                        if (!Finish(record, c)) return false;
                        const gal::CubeRenderTargetContext context(dimension, format);
                        if (d) Bind(record, id, ObjectType::CubeRenderTarget, d->CreateCubeRenderTarget(&context));
                        return true;
                    }
                    case Op::DevCreateDepthStencilTarget: {
                        gal::Device* const d = DeviceFor(record, c.Id());
                        const std::uint32_t width = c.U32();
                        const std::uint32_t height = c.U32();
                        const std::uint32_t format = c.U32();
                        const bool flag = c.Bool();
                        const std::uint32_t id = c.Id();
                        if (!Finish(record, c)) return false;
                        const gal::DepthStencilTargetContext context(width, height, format, flag);
                        if (d) Bind(record, id, ObjectType::DepthStencilTarget, d->CreateDepthStencilTarget(&context));
                        return true;
                    }
                    case Op::DevCreateVertexFormat: {
                        gal::Device* const d = DeviceFor(record, c.Id());
                        const std::uint32_t code = c.U32();
                        const std::uint32_t id = c.Id();
                        const std::uint32_t createdCode = c.U32();
                        const std::vector<std::uint32_t> strides = c.U32Array();
                        if (!Finish(record, c)) return false;
                        if (d) {
                            const boost::shared_ptr<gal::VertexFormat> format = d->CreateVertexFormat(code);
                            Bind(record, id, ObjectType::VertexFormat, format);
                            if (format) {
                                bool same = format->formatCode_ == createdCode && format->streamStrides_.size() == strides.size();
                                for (std::size_t index = 0; same && index < strides.size(); ++index) {
                                    same = format->streamStrides_[index] == strides[index];
                                }
                                if (!same) {
                                    Mismatch(record, "vertex format", "code or strides differ");
                                }
                            }
                        }
                        return true;
                    }
                    case Op::DevCreateVertexBuffer: {
                        gal::Device* const d = DeviceFor(record, c.Id());
                        const std::uint32_t type = c.U32();
                        const std::uint32_t usage = c.U32();
                        const std::uint32_t count = c.U32();
                        const std::uint32_t stride = c.U32();
                        const std::uint32_t id = c.Id();
                        if (!Finish(record, c)) return false;
                        const gal::VertexBufferContext context(count, stride, type, usage);
                        if (d) Bind(record, id, ObjectType::VertexBuffer, d->CreateVertexBuffer(&context));
                        return true;
                    }
                    case Op::DevCreateIndexBuffer: {
                        gal::Device* const d = DeviceFor(record, c.Id());
                        const std::uint32_t format = c.U32();
                        const std::uint32_t size = c.U32();
                        const std::uint32_t type = c.U32();
                        const std::uint32_t id = c.Id();
                        if (!Finish(record, c)) return false;
                        const gal::IndexBufferContext context(size, format, type);
                        if (d) Bind(record, id, ObjectType::IndexBuffer, d->CreateIndexBuffer(&context));
                        return true;
                    }
                    case Op::DevGetRenderTargetData: {
                        gal::Device* const d = DeviceFor(record, c.Id());
                        const auto source = Get<gal::RenderTarget>(record, c.Id(), ObjectType::RenderTarget);
                        const auto destination = Get<gal::Texture>(record, c.Id(), ObjectType::Texture);
                        if (!Finish(record, c)) return false;
                        if (d) d->GetRenderTargetData(source, destination);
                        return true;
                    }
                    case Op::DevStretchRect:
                    case Op::DevUpdateSurface: {
                        const bool stretch = static_cast<Op>(record.op) == Op::DevStretchRect;
                        gal::Device* const d = DeviceFor(record, c.Id());
                        const std::uint32_t sourceId = c.Id();
                        const std::uint32_t destinationId = c.Id();
                        galtrace::Rect sourceRect{};
                        galtrace::Rect destinationRect{};
                        const bool hasSource = c.OptRect(&sourceRect);
                        const bool hasDestination = c.OptRect(&destinationRect);
                        if (!Finish(record, c)) return false;
                        const RECT sourceValue = ToRect(sourceRect);
                        const RECT destinationValue = ToRect(destinationRect);
                        if (d == nullptr) return true;
                        if (stretch) {
                            d->StretchRect(Get<gal::RenderTarget>(record, sourceId, ObjectType::RenderTarget),
                                           Get<gal::RenderTarget>(record, destinationId, ObjectType::RenderTarget),
                                           hasSource ? &sourceValue : nullptr, hasDestination ? &destinationValue : nullptr);
                        } else {
                            d->UpdateSurface(Get<gal::Texture>(record, sourceId, ObjectType::Texture),
                                             Get<gal::Texture>(record, destinationId, ObjectType::Texture), hasSource ? &sourceValue : nullptr,
                                             hasDestination ? &destinationValue : nullptr);
                        }
                        return true;
                    }
                    case Op::DevSaveCubeRenderTarget: {
                        gal::Device* const d = DeviceFor(record, c.Id());
                        const auto target = Get<gal::CubeRenderTarget>(record, c.Id(), ObjectType::CubeRenderTarget);
                        const std::string path = c.Str();
                        if (!Finish(record, c)) return false;
                        if (d) d->SaveCubeRenderTarget(target, ToString(OutPath("saved", path)));
                        return true;
                    }
                    case Op::DevSaveRenderTarget: {
                        gal::Device* const d = DeviceFor(record, c.Id());
                        const auto target = Get<gal::RenderTarget>(record, c.Id(), ObjectType::RenderTarget);
                        const std::string path = c.Str();
                        const int format = c.I32();
                        if (!Finish(record, c)) return false;
                        if (d) d->SaveRenderTarget(target, ToString(OutPath("saved", path)), format);
                        return true;
                    }
                    case Op::DevSaveTexture: {
                        gal::Device* const d = DeviceFor(record, c.Id());
                        const auto texture = Get<gal::Texture>(record, c.Id(), ObjectType::Texture);
                        const std::string path = c.Str();
                        const int format = c.I32();
                        const bool toBuffer = c.Bool();
                        (void)c.Blob();
                        if (!Finish(record, c)) return false;
                        gpg::MemBuffer<char> buffer;
                        if (d) d->SaveTexture(texture, ToString(OutPath("saved", path)), format, toBuffer ? &buffer : nullptr);
                        return true;
                    }
                    case Op::DevGetTexture2D: {
                        gal::Device* const d = DeviceFor(record, c.Id());
                        const std::uint32_t sourceBlob = c.Blob();
                        const std::uint32_t expectedBlob = c.Blob();
                        const std::uint32_t width = c.U32();
                        const std::int32_t height = c.I32();
                        if (!Finish(record, c)) return false;
                        std::vector<std::uint8_t> source;
                        if (!NeedPayload(record, sourceBlob, &source, "the GetTexture2D input")) {
                            return false;
                        }
                        // The recorded output: its bytes (version 1), or only its hashes (a version 2
                        // digest, which this replay's own output then stands in for).
                        const galtrace::PayloadEntry* const expectedEntry = reader_.GetPayload(expectedBlob);
                        const bool expectedIsDigest = expectedEntry != nullptr && expectedEntry->kind == galtrace::PayloadKind::Digest;
                        std::vector<std::uint8_t> expected;
                        if (!expectedIsDigest && !NeedPayload(record, expectedBlob, &expected, "the recorded GetTexture2D output")) {
                            return false;
                        }
                        if (d) {
                            gpg::MemBuffer<char> out;
                            std::uint32_t outWidth = 0;
                            int outHeight = 0;
                            d->GetTexture2D(source.empty() ? nullptr : source.data(), static_cast<std::uint32_t>(source.size()), &out, &outWidth,
                                            &outHeight);
                            const std::size_t outSize = (out.mBegin != nullptr && out.mEnd > out.mBegin) ? out.Size() : 0;
                            bool sameBytes = false;
                            std::size_t expectedSize = expected.size();
                            if (expectedIsDigest) {
                                expectedSize = expectedEntry->key.size;
                                sameBytes = reader_.ProvidePayload(expectedBlob, outSize != 0 ? out.mBegin : nullptr, outSize);
                                ++report_.providedPayloads;
                            } else {
                                sameBytes = outSize == expected.size() && (outSize == 0 || std::memcmp(out.mBegin, expected.data(), outSize) == 0);
                            }
                            const bool same = outWidth == width && outHeight == height && sameBytes;
                            if (!same) {
                                Mismatch(record, "GetTexture2D output", "recorded " + std::to_string(width) + "x" + std::to_string(height) + " " +
                                                                           std::to_string(expectedSize) + " bytes, replay " +
                                                                           std::to_string(outWidth) + "x" + std::to_string(outHeight) + " " +
                                                                           std::to_string(outSize) + " bytes" +
                                                                           (outSize == expectedSize ? std::string(", content differs") : std::string()));
                            }
                        }
                        return true;
                    }
                    case Op::DevResetWithContext: {
                        gal::Device* const d = DeviceFor(record, c.Id());
                        gal::DeviceContext context;
                        ReadDeviceContext(c, context);
                        if (!Finish(record, c)) return false;
                        if (hooks_.fillWindows) {
                            hooks_.fillWindows(context);
                        }
                        if (d) d->Reset(&context);
                        return true;
                    }
                    case Op::DevReset: {
                        gal::Device* const d = DeviceFor(record, c.Id());
                        if (!Finish(record, c)) return false;
                        if (d) d->Reset();
                        return true;
                    }
                    case Op::DevTestCooperativeLevel: {
                        gal::Device* const d = DeviceFor(record, c.Id());
                        const int recorded = c.I32();
                        if (!Finish(record, c)) return false;
                        if (d) {
                            const int result = d->TestCooperativeLevel();
                            if (result != recorded) {
                                Mismatch(record, "cooperative level", std::to_string(recorded) + " vs " + std::to_string(result));
                            }
                        }
                        return true;
                    }
                    case Op::DevBeginScene: {
                        gal::Device* const d = DeviceFor(record, c.Id());
                        if (!Finish(record, c)) return false;
                        if (d) d->BeginScene();
                        return true;
                    }
                    case Op::DevEndScene: {
                        gal::Device* const d = DeviceFor(record, c.Id());
                        if (!Finish(record, c)) return false;
                        if (d) d->EndScene();
                        ++report_.endScenes;
                        return true;
                    }
                    case Op::DevPresent: {
                        gal::Device* const d = DeviceFor(record, c.Id());
                        if (!Finish(record, c)) return false;
                        if (d) d->Present();
                        ++report_.presents;
                        if (hooks_.afterPresent && !hooks_.afterPresent(report_.presents)) {
                            report_.stopped = true;
                            report_.fatal = "stopped on request after " + std::to_string(report_.presents) + " presents";
                            return false;
                        }
                        return true;
                    }
                    case Op::DevSetCursor: {
                        gal::Device* const d = DeviceFor(record, c.Id());
                        gal::CursorContext context;
                        context.hotspotX_ = c.I32();
                        context.hotspotY_ = c.I32();
                        context.texture_ = Get<gal::Texture>(record, c.Id(), ObjectType::Texture);
                        if (!Finish(record, c)) return false;
                        if (d) d->SetCursor(&context);
                        return true;
                    }
                    case Op::DevInitCursor: {
                        gal::Device* const d = DeviceFor(record, c.Id());
                        if (!Finish(record, c)) return false;
                        if (d) d->InitCursor();
                        return true;
                    }
                    case Op::DevShowCursor: {
                        gal::Device* const d = DeviceFor(record, c.Id());
                        const bool show = c.Bool();
                        (void)c.I32();
                        if (!Finish(record, c)) return false;
                        if (d) (void)d->ShowCursor(show);
                        return true;
                    }
                    case Op::DevSetViewport:
                    case Op::DevGetViewport: {
                        gal::Device* const d = DeviceFor(record, c.Id());
                        D3DVIEWPORT9 viewport{};
                        viewport.X = c.U32();
                        viewport.Y = c.U32();
                        viewport.Width = c.U32();
                        viewport.Height = c.U32();
                        viewport.MinZ = c.F32();
                        viewport.MaxZ = c.F32();
                        if (!Finish(record, c)) return false;
                        if (d == nullptr) return true;
                        if (static_cast<Op>(record.op) == Op::DevSetViewport) {
                            d->SetViewport(&viewport);
                        } else {
                            D3DVIEWPORT9 current{};
                            d->GetViewport(&current);
                            if (std::memcmp(&current, &viewport, sizeof(viewport)) != 0) {
                                Mismatch(record, "viewport", "GetViewport differs");
                            }
                        }
                        return true;
                    }
                    case Op::DevClearTarget: {
                        gal::Device* const d = DeviceFor(record, c.Id());
                        gal::OutputContext context;
                        context.cubeTarget = Get<gal::CubeRenderTarget>(record, c.Id(), ObjectType::CubeRenderTarget);
                        context.face = c.I32();
                        context.surface = Get<gal::RenderTarget>(record, c.Id(), ObjectType::RenderTarget);
                        context.depthStencil = Get<gal::DepthStencilTarget>(record, c.Id(), ObjectType::DepthStencilTarget);
                        if (!Finish(record, c)) return false;
                        if (d) d->ClearTarget(&context);
                        return true;
                    }
                    case Op::DevGetContext: {
                        gal::Device* const d = DeviceFor(record, c.Id());
                        const std::uint32_t cubeId = c.Id();
                        (void)c.I32();
                        const std::uint32_t surfaceId = c.Id();
                        const std::uint32_t depthId = c.Id();
                        if (!Finish(record, c)) return false;
                        if (d) {
                            gal::OutputContext context;
                            d->GetContext(&context);
                            Bind(record, cubeId, ObjectType::CubeRenderTarget, context.cubeTarget);
                            Bind(record, surfaceId, ObjectType::RenderTarget, context.surface);
                            Bind(record, depthId, ObjectType::DepthStencilTarget, context.depthStencil);
                        }
                        return true;
                    }
                    case Op::DevClear: {
                        gal::Device* const d = DeviceFor(record, c.Id());
                        const bool target = c.Bool();
                        const bool zbuffer = c.Bool();
                        const bool stencil = c.Bool();
                        const std::uint32_t color = c.U32();
                        const float depth = c.F32();
                        const int stencilValue = c.I32();
                        if (!Finish(record, c)) return false;
                        if (d) d->Clear(target, zbuffer, stencil, color, depth, stencilValue);
                        return true;
                    }
                    case Op::DevClearTextures: {
                        gal::Device* const d = DeviceFor(record, c.Id());
                        if (!Finish(record, c)) return false;
                        if (d) d->ClearTextures();
                        return true;
                    }
                    case Op::DevSetVertexDeclaration: {
                        gal::Device* const d = DeviceFor(record, c.Id());
                        const auto format = Get<gal::VertexFormat>(record, c.Id(), ObjectType::VertexFormat);
                        if (!Finish(record, c)) return false;
                        if (d) d->SetVertexDeclaration(format);
                        return true;
                    }
                    case Op::DevSetVertexBuffer: {
                        gal::Device* const d = DeviceFor(record, c.Id());
                        const std::uint32_t stream = c.U32();
                        const auto buffer = Get<gal::VertexBuffer>(record, c.Id(), ObjectType::VertexBuffer);
                        const int frequency = c.I32();
                        const int start = c.I32();
                        if (!Finish(record, c)) return false;
                        if (d) d->SetVertexBuffer(stream, buffer, frequency, start);
                        return true;
                    }
                    case Op::DevSetBufferIndices: {
                        gal::Device* const d = DeviceFor(record, c.Id());
                        const auto buffer = Get<gal::IndexBuffer>(record, c.Id(), ObjectType::IndexBuffer);
                        if (!Finish(record, c)) return false;
                        if (d) d->SetBufferIndices(buffer);
                        return true;
                    }
                    case Op::DevSetFogState: {
                        gal::Device* const d = DeviceFor(record, c.Id());
                        const bool enable = c.Bool();
                        float projection[16];
                        const bool hasProjection = c.OptMatrix(projection);
                        const float start = c.F32();
                        const float end = c.F32();
                        const int color = c.I32();
                        if (!Finish(record, c)) return false;
                        const gal::Matrix matrix = ToMatrix(projection);
                        if (d) d->SetFogState(enable, hasProjection ? &matrix : nullptr, start, end, color);
                        return true;
                    }
                    case Op::DevSetWireframeState: {
                        gal::Device* const d = DeviceFor(record, c.Id());
                        const bool enabled = c.Bool();
                        if (!Finish(record, c)) return false;
                        if (d) d->SetWireframeState(enabled);
                        return true;
                    }
                    case Op::DevSetColorWriteState: {
                        gal::Device* const d = DeviceFor(record, c.Id());
                        const bool color = c.Bool();
                        const bool alpha = c.Bool();
                        if (!Finish(record, c)) return false;
                        if (d) d->SetColorWriteState(color, alpha);
                        return true;
                    }
                    case Op::DevDrawIndexedPrimitive: {
                        gal::Device* const d = DeviceFor(record, c.Id());
                        const auto topology = static_cast<gal::DrawContext::TOPOLOGY>(c.U32());
                        const std::uint32_t minVertex = c.U32();
                        const std::uint32_t vertexCount = c.U32();
                        const std::uint32_t indexCount = c.U32();
                        const std::uint32_t startIndex = c.U32();
                        const std::int32_t baseVertex = c.I32();
                        if (!Finish(record, c)) return false;
                        const gal::DrawIndexedContext context(topology, minVertex, vertexCount, indexCount, startIndex, baseVertex);
                        if (d) d->DrawIndexedPrimitive(&context);
                        ++report_.draws;
                        return true;
                    }
                    case Op::DevDrawPrimitive: {
                        gal::Device* const d = DeviceFor(record, c.Id());
                        const auto topology = static_cast<gal::DrawContext::TOPOLOGY>(c.U32());
                        const std::uint32_t vertexCount = c.U32();
                        const std::uint32_t startVertex = c.U32();
                        if (!Finish(record, c)) return false;
                        const gal::DrawContext context(topology, vertexCount, startVertex);
                        if (d) d->DrawPrimitive(&context);
                        ++report_.draws;
                        return true;
                    }
                    case Op::DevBeginTechnique: {
                        gal::Device* const d = DeviceFor(record, c.Id());
                        if (!Finish(record, c)) return false;
                        if (d) d->BeginTechnique();
                        return true;
                    }
                    case Op::DevEndTechnique: {
                        gal::Device* const d = DeviceFor(record, c.Id());
                        if (!Finish(record, c)) return false;
                        if (d) d->EndTechnique();
                        return true;
                    }

                    // -- Texture -------------------------------------------------------------------

                    case Op::TexGetContext: {
                        const std::uint32_t id = c.Id();
                        const auto texture = Get<gal::Texture>(record, id, ObjectType::Texture);
                        gal::TextureContext recorded;
                        ReadTextureContext(c, recorded, nullptr);
                        if (!Finish(record, c)) return false;
                        if (texture) CompareTextureContext(record, recorded, texture->GetContext());
                        return true;
                    }
                    case Op::TexLock: {
                        const std::uint32_t id = c.Id();
                        const auto texture = Get<gal::Texture>(record, id, ObjectType::Texture);
                        const int level = c.I32();
                        const RECT rect = ToRect(c.RectValue());
                        const int flags = c.I32();
                        const std::uint32_t rowBytes = c.U32();
                        const std::uint32_t rows = c.U32();
                        if (!Finish(record, c)) return false;
                        if (!texture) return true;
                        TextureLockState& state = textureLocks_[id];
                        state.lock = texture->Lock(level, rect, flags);
                        state.locked = true;
                        state.rowBytes = rowBytes;
                        state.rows = rows;
                        state.width = 0;
                        state.height = 0;
                        state.format = 0;
                        if (const gal::TextureContext* const context = texture->GetContext()) {
                            state.width = (context->width_ >> level) != 0 ? (context->width_ >> level) : 1u;
                            state.height = (context->height_ >> level) != 0 ? (context->height_ >> level) : 1u;
                            state.format = context->format_;
                            if (rect.left != rect.right) {
                                state.width = static_cast<std::uint32_t>(rect.right - rect.left);
                                state.height = static_cast<std::uint32_t>(rect.bottom - rect.top);
                            }
                            const galtrace::LockRegion region = galtrace::TextureLockRegion(
                                context->format_, (context->width_ >> level) != 0 ? (context->width_ >> level) : 1u,
                                (context->height_ >> level) != 0 ? (context->height_ >> level) : 1u, rect.left, rect.top, rect.right, rect.bottom
                            );
                            if (region.known && (region.rowBytes != rowBytes || region.rows != rows)) {
                                Mismatch(record, "lock region", "recorded " + std::to_string(rowBytes) + "x" + std::to_string(rows) + ", replay " +
                                                                  std::to_string(region.rowBytes) + "x" + std::to_string(region.rows));
                            }
                        }
                        if (state.lock.bits == nullptr || state.lock.pitch < static_cast<int>(rowBytes)) {
                            Mismatch(record, "lock region", "the replay lock is smaller than the recorded region");
                        }
                        return true;
                    }
                    case Op::TexUnlockRect:
                    case Op::TexUnlockLevel: {
                        const bool byRect = static_cast<Op>(record.op) == Op::TexUnlockRect;
                        const std::uint32_t id = c.Id();
                        const auto texture = Get<gal::Texture>(record, id, ObjectType::Texture);
                        int flags = 0;
                        if (byRect) {
                            flags = c.I32();
                        }
                        const int level = c.I32();
                        const auto content = static_cast<galtrace::LockContent>(c.U8());
                        const std::uint32_t contentBlob = c.Blob();
                        if (!Finish(record, c)) return false;
                        // What was written is needed to replay the unlock; what was read back is only
                        // compared (a version 2 trace may hold just its hashes).
                        std::vector<std::uint8_t> bytes;
                        if (content != galtrace::LockContent::ReadBack && !NeedPayload(record, contentBlob, &bytes, "the texture lock's content")) {
                            return false;
                        }
                        if (!texture) return true;
                        TextureLockState& state = textureLocks_[id];
                        if (!state.locked) {
                            Mismatch(record, "lock", "unlock without a replayed lock");
                        } else if (content == galtrace::LockContent::Written) {
                            if (bytes.size() != static_cast<std::size_t>(state.rowBytes) * state.rows) {
                                Mismatch(record, "lock content", "content size does not match the region");
                            } else if (state.lock.bits != nullptr && state.lock.pitch >= static_cast<int>(state.rowBytes)) {
                                auto* const bits = static_cast<std::uint8_t*>(state.lock.bits);
                                for (std::uint32_t row = 0; row < state.rows; ++row) {
                                    std::memcpy(bits + static_cast<std::size_t>(row) * static_cast<std::size_t>(state.lock.pitch),
                                                bytes.data() + static_cast<std::size_t>(row) * state.rowBytes, state.rowBytes);
                                }
                            }
                        } else if (content == galtrace::LockContent::ReadBack) {
                            if (!Readback(record, state, contentBlob)) {
                                return false;
                            }
                        } else {
                            Mismatch(record, "lock content", "the recording could not size this lock");
                        }
                        state.locked = false;
                        if (byRect) {
                            gal::TextureLockRect lock = state.lock;
                            lock.flags = flags;
                            lock.level = level;
                            (void)texture->Unlock(lock);
                        } else {
                            (void)texture->Unlock(level);
                        }
                        return true;
                    }
                    case Op::TexSaveToBuffer: {
                        const auto texture = Get<gal::Texture>(record, c.Id(), ObjectType::Texture);
                        (void)c.Blob();
                        if (!Finish(record, c)) return false;
                        gpg::MemBuffer<char> buffer;
                        if (texture) texture->SaveToBuffer(&buffer);
                        return true;
                    }

                    // -- buffers -------------------------------------------------------------------

                    case Op::VbGetContext: {
                        const auto buffer = Get<gal::VertexBuffer>(record, c.Id(), ObjectType::VertexBuffer);
                        const std::uint32_t type = c.U32();
                        const std::uint32_t usage = c.U32();
                        const std::uint32_t count = c.U32();
                        const std::uint32_t stride = c.U32();
                        if (!Finish(record, c)) return false;
                        if (buffer) {
                            const gal::VertexBufferContext* const context = buffer->GetContext();
                            if (context == nullptr || context->type_ != type || context->usage_ != usage || context->vertexCount_ != count ||
                                context->stride_ != stride) {
                                Mismatch(record, "buffer context", "vertex buffer context differs");
                            }
                        }
                        return true;
                    }
                    case Op::IbGetContext: {
                        const auto buffer = Get<gal::IndexBuffer>(record, c.Id(), ObjectType::IndexBuffer);
                        const std::uint32_t format = c.U32();
                        const std::uint32_t size = c.U32();
                        const std::uint32_t type = c.U32();
                        if (!Finish(record, c)) return false;
                        if (buffer) {
                            const gal::IndexBufferContext* const context = buffer->GetContext();
                            if (context == nullptr || context->format_ != format || context->size_ != size || context->type_ != type) {
                                Mismatch(record, "buffer context", "index buffer context differs");
                            }
                        }
                        return true;
                    }
                    case Op::VbLock:
                    case Op::IbLock: {
                        const bool vertex = static_cast<Op>(record.op) == Op::VbLock;
                        const std::uint32_t id = c.Id();
                        const std::uint32_t offset = c.U32();
                        const std::uint32_t size = c.U32();
                        const auto flags = static_cast<gal::MohoD3DLockFlags>(c.U32());
                        const std::uint32_t bytes = c.U32();
                        if (!Finish(record, c)) return false;
                        BufferLockState& state = bufferLocks_[id];
                        state.bytes = bytes;
                        state.data = nullptr;
                        if (vertex) {
                            if (const auto buffer = Get<gal::VertexBuffer>(record, id, ObjectType::VertexBuffer)) {
                                state.data = static_cast<std::uint8_t*>(buffer->Lock(offset, size, flags));
                            }
                        } else {
                            if (const auto buffer = Get<gal::IndexBuffer>(record, id, ObjectType::IndexBuffer)) {
                                state.data = reinterpret_cast<std::uint8_t*>(buffer->Lock(offset, size, flags));
                            }
                        }
                        return true;
                    }
                    case Op::VbUnlock:
                    case Op::IbUnlock: {
                        const bool vertex = static_cast<Op>(record.op) == Op::VbUnlock;
                        const std::uint32_t id = c.Id();
                        const std::uint32_t contentBlob = c.Blob();
                        if (!Finish(record, c)) return false;
                        std::vector<std::uint8_t> bytes;
                        if (!NeedPayload(record, contentBlob, &bytes, "the buffer lock's content")) {
                            return false;
                        }
                        BufferLockState& state = bufferLocks_[id];
                        if (bytes.size() != state.bytes) {
                            Mismatch(record, "lock content", "buffer content size differs from the locked size");
                        }
                        if (state.data != nullptr && !bytes.empty()) {
                            std::memcpy(state.data, bytes.data(), std::min<std::size_t>(bytes.size(), state.bytes));
                        }
                        state.data = nullptr;
                        if (vertex) {
                            if (const auto buffer = Get<gal::VertexBuffer>(record, id, ObjectType::VertexBuffer)) buffer->Unlock();
                        } else {
                            if (const auto buffer = Get<gal::IndexBuffer>(record, id, ObjectType::IndexBuffer)) buffer->Unlock();
                        }
                        return true;
                    }

                    // -- targets -------------------------------------------------------------------

                    case Op::RtGetContext: {
                        const auto target = Get<gal::RenderTarget>(record, c.Id(), ObjectType::RenderTarget);
                        const std::uint32_t width = c.U32();
                        const std::uint32_t height = c.U32();
                        const std::uint32_t format = c.U32();
                        if (!Finish(record, c)) return false;
                        if (target) {
                            const gal::RenderTargetContext* const context = target->GetContext();
                            if (context == nullptr || context->width_ != width || context->height_ != height || context->format_ != format) {
                                Mismatch(record, "target context",
                                         "render target recorded " + std::to_string(width) + "x" + std::to_string(height) + " format " +
                                             std::to_string(format) + ", replay " +
                                             (context == nullptr ? std::string("none")
                                                                 : std::to_string(context->width_) + "x" + std::to_string(context->height_) +
                                                                       " format " + std::to_string(context->format_)));
                            }
                        }
                        return true;
                    }
                    case Op::RtGetDC: {
                        (void)Get<gal::RenderTarget>(record, c.Id(), ObjectType::RenderTarget);
                        if (!Finish(record, c)) return false;
                        // GDI drawing through the DC is invisible to the trace: not replayed.
                        Mismatch(record, "untraceable", "RenderTarget::GetDC");
                        return true;
                    }
                    case Op::CubeGetContext: {
                        const auto target = Get<gal::CubeRenderTarget>(record, c.Id(), ObjectType::CubeRenderTarget);
                        const std::uint32_t dimension = c.U32();
                        const std::uint32_t format = c.U32();
                        if (!Finish(record, c)) return false;
                        if (target) {
                            const gal::CubeRenderTargetContext* const context = target->GetContext();
                            if (context == nullptr || context->dimension_ != dimension || context->format_ != format) {
                                Mismatch(record, "target context", "cube target context differs");
                            }
                        }
                        return true;
                    }
                    case Op::DsGetContext: {
                        const auto target = Get<gal::DepthStencilTarget>(record, c.Id(), ObjectType::DepthStencilTarget);
                        const std::uint32_t width = c.U32();
                        const std::uint32_t height = c.U32();
                        const std::uint32_t format = c.U32();
                        const bool flag = c.Bool();
                        if (!Finish(record, c)) return false;
                        if (target) {
                            const gal::DepthStencilTargetContext* const context = target->GetContext();
                            if (context == nullptr || context->width_ != width || context->height_ != height || context->format_ != format ||
                                context->field0x10_ != flag) {
                                Mismatch(record, "target context", "depth target context differs");
                            }
                        }
                        return true;
                    }

                    // -- Effect --------------------------------------------------------------------

                    case Op::EffGetContext:
                    case Op::EffOnReset:
                    case Op::EffOnLost: {
                        const auto effect = Get<gal::Effect>(record, c.Id(), ObjectType::Effect);
                        if (!Finish(record, c)) return false;
                        if (!effect) return true;
                        if (static_cast<Op>(record.op) == Op::EffGetContext) {
                            (void)effect->GetContext();
                        } else if (static_cast<Op>(record.op) == Op::EffOnReset) {
                            effect->OnReset();
                        } else {
                            effect->OnLost();
                        }
                        return true;
                    }
                    case Op::EffGetTechniques: {
                        const auto effect = Get<gal::Effect>(record, c.Id(), ObjectType::Effect);
                        std::vector<std::pair<std::uint32_t, std::string>> recorded(c.Count());
                        for (auto& [id, name] : recorded) {
                            id = c.Id();
                            name = c.Str();
                        }
                        if (!Finish(record, c)) return false;
                        if (!effect) return true;
                        msvc8::vector<boost::shared_ptr<gal::EffectTechnique>> techniques;
                        effect->GetTechniques(techniques);
                        if (techniques.size() != recorded.size()) {
                            Mismatch(record, "technique list", "recorded " + std::to_string(recorded.size()) + " techniques, replay " +
                                                                 std::to_string(techniques.size()));
                        }
                        for (std::size_t index = 0; index < recorded.size() && index < techniques.size(); ++index) {
                            Bind(record, recorded[index].first, ObjectType::EffectTechnique, techniques[index]);
                            const msvc8::string* const name = techniques[index] ? techniques[index]->GetName() : nullptr;
                            if (name == nullptr || recorded[index].second != name->c_str()) {
                                Mismatch(record, "technique list", "technique " + std::to_string(index) + " is not " + recorded[index].second);
                            }
                        }
                        return true;
                    }
                    case Op::EffGetVariable:
                    case Op::EffGetTechnique: {
                        const auto effect = Get<gal::Effect>(record, c.Id(), ObjectType::Effect);
                        const std::string name = c.Str();
                        const std::uint32_t id = c.Id();
                        if (!Finish(record, c)) return false;
                        if (!effect) return true;
                        if (static_cast<Op>(record.op) == Op::EffGetVariable) {
                            Bind(record, id, ObjectType::EffectVariable, effect->GetVariable(name.c_str()));
                        } else {
                            Bind(record, id, ObjectType::EffectTechnique, effect->GetTechnique(name.c_str()));
                        }
                        return true;
                    }

                    // -- EffectTechnique ---------------------------------------------------------------

                    case Op::TechGetName: {
                        const auto technique = Get<gal::EffectTechnique>(record, c.Id(), ObjectType::EffectTechnique);
                        const std::string name = c.Str();
                        if (!Finish(record, c)) return false;
                        if (technique) {
                            const msvc8::string* const result = technique->GetName();
                            if (result == nullptr || name != result->c_str()) {
                                Mismatch(record, "technique name", name);
                            }
                        }
                        return true;
                    }
                    case Op::TechBegin: {
                        const auto technique = Get<gal::EffectTechnique>(record, c.Id(), ObjectType::EffectTechnique);
                        const int passes = c.I32();
                        if (!Finish(record, c)) return false;
                        if (technique) {
                            const int result = technique->BeginTechnique();
                            if (result != passes) {
                                Mismatch(record, "pass count", std::to_string(passes) + " vs " + std::to_string(result));
                            }
                        }
                        return true;
                    }
                    case Op::TechEnd:
                    case Op::TechEndPass: {
                        const auto technique = Get<gal::EffectTechnique>(record, c.Id(), ObjectType::EffectTechnique);
                        if (!Finish(record, c)) return false;
                        if (technique) {
                            if (static_cast<Op>(record.op) == Op::TechEnd) {
                                technique->EndTechnique();
                            } else {
                                technique->EndPass();
                            }
                        }
                        return true;
                    }
                    case Op::TechBeginPass: {
                        const auto technique = Get<gal::EffectTechnique>(record, c.Id(), ObjectType::EffectTechnique);
                        const int pass = c.I32();
                        if (!Finish(record, c)) return false;
                        if (technique) technique->BeginPass(pass);
                        return true;
                    }
                    case Op::TechGetAnnotationBool:
                    case Op::TechGetAnnotationInt:
                    case Op::TechGetAnnotationFloat:
                    case Op::TechGetAnnotationString:
                    case Op::VarGetAnnotationBool:
                    case Op::VarGetAnnotationInt:
                    case Op::VarGetAnnotationFloat:
                    case Op::VarGetAnnotationString:
                        return Annotation(record, c);

                    // -- EffectVariable ----------------------------------------------------------------

                    case Op::VarGetName: {
                        const auto variable = Get<gal::EffectVariable>(record, c.Id(), ObjectType::EffectVariable);
                        const std::string name = c.Str();
                        if (!Finish(record, c)) return false;
                        if (variable) {
                            const msvc8::string* const result = variable->GetName();
                            if (result == nullptr || name != result->c_str()) {
                                Mismatch(record, "variable name", name);
                            }
                        }
                        return true;
                    }
                    case Op::VarSetCubeRenderTarget: {
                        const auto variable = Get<gal::EffectVariable>(record, c.Id(), ObjectType::EffectVariable);
                        const auto target = Get<gal::CubeRenderTarget>(record, c.Id(), ObjectType::CubeRenderTarget);
                        if (!Finish(record, c)) return false;
                        if (variable) variable->SetCubeRenderTarget(target);
                        return true;
                    }
                    case Op::VarSetRenderTarget: {
                        const auto variable = Get<gal::EffectVariable>(record, c.Id(), ObjectType::EffectVariable);
                        const auto target = Get<gal::RenderTarget>(record, c.Id(), ObjectType::RenderTarget);
                        if (!Finish(record, c)) return false;
                        if (variable) variable->SetRenderTarget(target);
                        return true;
                    }
                    case Op::VarSetTexture: {
                        const auto variable = Get<gal::EffectVariable>(record, c.Id(), ObjectType::EffectVariable);
                        const auto texture = Get<gal::Texture>(record, c.Id(), ObjectType::Texture);
                        if (!Finish(record, c)) return false;
                        if (variable) variable->SetTexture(texture);
                        return true;
                    }
                    case Op::VarSetMatrix4x4: {
                        const auto variable = Get<gal::EffectVariable>(record, c.Id(), ObjectType::EffectVariable);
                        float values[16];
                        const bool present = c.OptMatrix(values);
                        if (!Finish(record, c)) return false;
                        const gal::Matrix matrix = ToMatrix(values);
                        if (variable) variable->SetMatrix4x4(present ? &matrix : nullptr);
                        return true;
                    }
                    case Op::VarSetFloatArray: {
                        const auto variable = Get<gal::EffectVariable>(record, c.Id(), ObjectType::EffectVariable);
                        const std::vector<float> values = c.F32Array();
                        if (!Finish(record, c)) return false;
                        if (variable) variable->SetFloatArray(static_cast<std::uint32_t>(values.size()), values.empty() ? nullptr : values.data());
                        return true;
                    }
                    case Op::VarSetVector: {
                        const auto variable = Get<gal::EffectVariable>(record, c.Id(), ObjectType::EffectVariable);
                        float vector[4];
                        c.Vec4(vector);
                        if (!Finish(record, c)) return false;
                        if (variable) variable->SetVector(vector);
                        return true;
                    }
                    case Op::VarSetValue: {
                        const auto variable = Get<gal::EffectVariable>(record, c.Id(), ObjectType::EffectVariable);
                        const std::vector<std::uint8_t> data = c.Bytes();
                        if (!Finish(record, c)) return false;
                        if (variable) variable->SetValue(data.empty() ? nullptr : data.data(), static_cast<std::uint32_t>(data.size()));
                        return true;
                    }
                    case Op::VarSetFloat: {
                        const auto variable = Get<gal::EffectVariable>(record, c.Id(), ObjectType::EffectVariable);
                        const float value = c.F32();
                        if (!Finish(record, c)) return false;
                        if (variable) variable->SetFloat(value);
                        return true;
                    }
                    case Op::VarSetInt: {
                        const auto variable = Get<gal::EffectVariable>(record, c.Id(), ObjectType::EffectVariable);
                        const int value = c.I32();
                        if (!Finish(record, c)) return false;
                        if (variable) variable->SetInt(value);
                        return true;
                    }
                    case Op::VarSetBool: {
                        const auto variable = Get<gal::EffectVariable>(record, c.Id(), ObjectType::EffectVariable);
                        const bool value = c.Bool();
                        if (!Finish(record, c)) return false;
                        if (variable) variable->SetBool(value);
                        return true;
                    }
                    case Op::VarSetMatrixArray: {
                        const auto variable = Get<gal::EffectVariable>(record, c.Id(), ObjectType::EffectVariable);
                        const std::uint32_t count = c.U32();
                        const std::vector<float> values = c.F32Array();
                        if (!Finish(record, c)) return false;
                        std::vector<gal::Matrix> matrices(values.size() / 16);
                        if (!matrices.empty()) {
                            std::memcpy(matrices.data(), values.data(), matrices.size() * sizeof(gal::Matrix));
                        }
                        if (variable) variable->SetMatrixArray(count, matrices.empty() ? nullptr : matrices.data());
                        return true;
                    }
                    case Op::VarSetVectorArray: {
                        const auto variable = Get<gal::EffectVariable>(record, c.Id(), ObjectType::EffectVariable);
                        const std::uint32_t count = c.U32();
                        const std::vector<float> values = c.F32Array();
                        if (!Finish(record, c)) return false;
                        if (variable) variable->SetVectorArray(count, values.empty() ? nullptr : values.data());
                        return true;
                    }

                    case Op::Blob:
                    case Op::PayloadRef:
                    case Op::PayloadDigest:
                    case Op::PayloadCompose:
                    case Op::End:
                        return true;
                }
                return Fatal(record, "the player does not know this op");
            }

            bool Annotation(const galtrace::Record& record, Cursor& c)
            {
                const Op op = static_cast<Op>(record.op);
                const bool technique = op == Op::TechGetAnnotationBool || op == Op::TechGetAnnotationInt || op == Op::TechGetAnnotationFloat ||
                                       op == Op::TechGetAnnotationString;
                const std::uint32_t id = c.Id();
                const msvc8::string name = ToString(c.Str());
                const bool found = c.Bool();
                bool boolValue = false;
                int intValue = 0;
                float floatValue = 0.0f;
                std::string stringValue;
                switch (op) {
                    case Op::TechGetAnnotationBool:
                    case Op::VarGetAnnotationBool: boolValue = c.Bool(); break;
                    case Op::TechGetAnnotationInt:
                    case Op::VarGetAnnotationInt: intValue = c.I32(); break;
                    case Op::TechGetAnnotationFloat:
                    case Op::VarGetAnnotationFloat: floatValue = c.F32(); break;
                    default: stringValue = c.Str(); break;
                }
                if (!Finish(record, c)) {
                    return false;
                }
                boost::shared_ptr<gal::EffectTechnique> techniqueObject;
                boost::shared_ptr<gal::EffectVariable> variableObject;
                if (technique) {
                    techniqueObject = Get<gal::EffectTechnique>(record, id, ObjectType::EffectTechnique);
                    if (!techniqueObject) return true;
                } else {
                    variableObject = Get<gal::EffectVariable>(record, id, ObjectType::EffectVariable);
                    if (!variableObject) return true;
                }
                bool resultFound = false;
                bool same = true;
                switch (op) {
                    case Op::TechGetAnnotationBool:
                    case Op::VarGetAnnotationBool: {
                        bool value = false;
                        resultFound = technique ? techniqueObject->GetAnnotationBool(&value, name) : variableObject->GetAnnotationBool(&value, name);
                        same = !found || value == boolValue;
                        break;
                    }
                    case Op::TechGetAnnotationInt:
                    case Op::VarGetAnnotationInt: {
                        int value = 0;
                        resultFound = technique ? techniqueObject->GetAnnotationInt(&value, name) : variableObject->GetAnnotationInt(&value, name);
                        same = !found || value == intValue;
                        break;
                    }
                    case Op::TechGetAnnotationFloat:
                    case Op::VarGetAnnotationFloat: {
                        float value = 0.0f;
                        resultFound = technique ? techniqueObject->GetAnnotationFloat(&value, name) : variableObject->GetAnnotationFloat(&value, name);
                        same = !found || std::memcmp(&value, &floatValue, sizeof(value)) == 0;
                        break;
                    }
                    default: {
                        msvc8::string value;
                        resultFound =
                            technique ? techniqueObject->GetAnnotationString(&value, name) : variableObject->GetAnnotationString(&value, name);
                        same = !found || stringValue == value.c_str();
                        break;
                    }
                }
                if (resultFound != found || !same) {
                    Mismatch(record, "annotation", std::string(name.c_str()));
                }
                return true;
            }

            const PlayOptions& options_;
            const PlayHooks& hooks_;
            PlayReport& report_;
            galtrace::Reader reader_;
            std::vector<unsigned> readbackNames_;
            std::map<unsigned, std::string> referenceFrames_; // reference_frames.<options_.referenceBackend>
            std::map<unsigned, std::string> recordedFrames_;  // reference_frames.<the recording's backend>
            gal::Device* device_ = nullptr;
            std::uint32_t deviceId_ = 0;
            bool ended_ = false;
            std::unordered_map<std::uint32_t, Slot> objects_;
            std::unordered_map<std::uint32_t, TextureLockState> textureLocks_;
            std::unordered_map<std::uint32_t, BufferLockState> bufferLocks_;
        };

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
    } // namespace

    bool PlayTrace(const PlayOptions& options, const PlayHooks& hooks, PlayReport* const report)
    {
        const auto start = std::chrono::steady_clock::now();
        Player player(options, hooks, *report);
        const bool ok = player.Run();
        report->seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        return ok;
    }

    std::string PlayReportJson(const PlayOptions& options, const PlayReport& report, const std::string& extraJsonFields)
    {
        std::string json = "{\n";
        json += "  \"trace\": \"" + JsonEscape(options.tracePath) + "\",\n";
        json += extraJsonFields;
        json += "  \"completed\": " + std::string(report.completed ? "true" : "false") + ",\n";
        json += "  \"stopped\": " + std::string(report.stopped ? "true" : "false") + ",\n";
        json += "  \"fatal\": \"" + JsonEscape(report.fatal) + "\",\n";
        json += "  \"missing_data\": " + std::string(report.missingData ? "true" : "false") + ",\n";
        json += "  \"data_problems\": [";
        for (std::size_t index = 0; index < report.dataProblems.size(); ++index) {
            json += std::string(index == 0 ? "" : ", ") + "\"" + JsonEscape(report.dataProblems[index]) + "\"";
        }
        json += "],\n";
        json += "  \"format_version\": " + std::to_string(report.formatVersion) + ",\n";
        json += "  \"reference_backend\": \"" + JsonEscape(options.referenceBackend) + "\",\n";
        json += "  \"references_read\": " + std::to_string(report.references) + ",\n";
        json += "  \"provided_payloads\": " + std::to_string(report.providedPayloads) + ",\n";
        char seconds[32];
        std::snprintf(seconds, sizeof(seconds), "%.3f", report.seconds);
        json += "  \"seconds\": " + std::string(seconds) + ",\n";
        std::snprintf(seconds, sizeof(seconds), "%.3f", report.referenceCheckSeconds);
        json += "  \"reference_check_seconds\": " + std::string(seconds) + ",\n";
        json += "  \"records\": " + std::to_string(report.records) + ",\n";
        json += "  \"calls\": " + std::to_string(report.calls) + ",\n";
        json += "  \"presents\": " + std::to_string(report.presents) + ",\n";
        json += "  \"end_scenes\": " + std::to_string(report.endScenes) + ",\n";
        json += "  \"draws\": " + std::to_string(report.draws) + ",\n";
        json += "  \"gal_errors\": " + std::to_string(report.galErrors) + ",\n";
        json += "  \"skip_op\": \"" + JsonEscape(options.skipOp) + "\",\n";
        json += "  \"skipped\": " + std::to_string(report.skipped) + ",\n";
        json += "  \"readbacks\": [";
        bool first = true;
        unsigned identical = 0;
        unsigned passed = 0;
        unsigned withReference = 0;
        for (const ReadbackResult& readback : report.readbacks) {
            identical += readback.identical ? 1u : 0u;
            passed += readback.verdict == "pass" ? 1u : 0u;
            withReference += readback.referenceRgb.empty() ? 0u : 1u;
            json += std::string(first ? "\n" : ",\n") + "    {\"index\": " + std::to_string(readback.index) + ", \"frame\": " +
                    std::to_string(readback.name) + ", \"presents\": " + std::to_string(readback.presents) + ", \"end_scenes\": " +
                    std::to_string(readback.endScenes) + ", \"width\": " + std::to_string(readback.width) + ", \"height\": " +
                    std::to_string(readback.height) + ", \"identical_to_recording\": " + (readback.identical ? "true" : "false") +
                    ", \"recorded_bytes_known\": " + (readback.recordedBytesKnown ? "true" : "false") +
                    ", \"differing_bytes\": " + std::to_string(readback.differingBytes) + ", \"recorded_rgb_fnv1a64\": \"" +
                    readback.recordedRgb + "\", \"replayed_rgb_fnv1a64\": \"" + readback.replayedRgb + "\", \"reference_rgb_fnv1a64\": \"" +
                    readback.referenceRgb + "\", \"verdict\": \"" + readback.verdict + "\", \"file\": \"" + JsonEscape(readback.file) + "\"}";
            first = false;
        }
        json += std::string(first ? "" : "\n  ") + "],\n";
        json += "  \"readbacks_identical\": " + std::to_string(identical) + ",\n";
        json += "  \"readbacks_with_reference\": " + std::to_string(withReference) + ",\n";
        json += "  \"readbacks_pass\": " + std::to_string(passed) + ",\n";
        json += "  \"mismatches\": {";
        first = true;
        for (const auto& [category, count] : report.mismatchCounts) {
            json += std::string(first ? "" : ", ") + "\"" + JsonEscape(category) + "\": " + std::to_string(count);
            first = false;
        }
        json += "},\n  \"mismatch_examples\": [";
        first = true;
        for (const std::string& example : report.mismatchExamples) {
            json += std::string(first ? "\n    \"" : ",\n    \"") + JsonEscape(example) + "\"";
            first = false;
        }
        json += std::string(first ? "" : "\n  ") + "],\n  \"error_examples\": [";
        first = true;
        for (const std::string& example : report.errorExamples) {
            json += std::string(first ? "\n    \"" : ",\n    \"") + JsonEscape(example) + "\"";
            first = false;
        }
        json += std::string(first ? "" : "\n  ") + "],\n  \"ops\": {";
        first = true;
        for (const auto& [name, count] : report.opCounts) {
            json += std::string(first ? "" : ", ") + "\"" + name + "\": " + std::to_string(count);
            first = false;
        }
        json += "}\n}\n";
        return json;
    }
} // namespace port::graphics::trace
