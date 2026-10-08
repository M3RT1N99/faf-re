#include "EffectsDiligent.h"

#if defined(_WIN32)
#include <d3d9.h>
#else
#include "D3D9Portable.h"
#endif

#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>

#include "D3D9Oracle.h"
#include "EffectsDiligentGpu.h"
#include "PassBinding.h"
#include "gpg/core/utils/BoostWrappers.h"
#include "gpg/core/utils/Logging.h"
#include "gpg/gal/Device.hpp"
#include "gpg/gal/EffectMacro.hpp"
#include "gpg/gal/Matrix.h"

namespace gpg::gal::diligent
{
    namespace
    {
        std::mutex gRecordLock;
        std::vector<EffectRecord> gRecords;

        void LogToEngine(const char* const message)
        {
            gpg::Warnf("%s", message);
        }

        [[noreturn]] void ThrowEffectError(const char* const prefix, const EffectContext& context, const std::string& reason, const int line)
        {
            // The message of BuildEffectCreationMessage, D3D9Interfaces.cpp:842-853.
            const std::string message = std::string(prefix) + context.mSourcePath.c_str() + " reason: " + reason;
            ThrowGalError("DeviceDiligent.cpp", line, message.c_str());
        }

        std::string BaseName(const char* const path)
        {
            const char* name = path;
            for (const char* cursor = path; *cursor != '\0'; ++cursor) {
                if (*cursor == '/' || *cursor == '\\') {
                    name = cursor + 1;
                }
            }
            return name;
        }

        std::string StemOf(const char* const path)
        {
            std::string name = BaseName(path);
            const std::size_t dot = name.rfind('.');
            return dot == std::string::npos ? name : name.substr(0, dot);
        }

        std::string JsonEscape(const std::string& text)
        {
            std::string out;
            for (const char c : text) {
                if (c == '"' || c == '\\') {
                    out += '\\';
                    out += c;
                } else if (static_cast<unsigned char>(c) < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                    out += buffer;
                } else {
                    out += c;
                }
            }
            return out;
        }

        /** The exact bytes and macros the engine handed to CreateEffect, for tools/fxtechlist.cpp. */
        void DumpSource(const std::string& dumpDir, const EffectContext& context, const std::size_t index)
        {
            const char* const begin = context.mSourceBuffer.mBegin;
            const char* const end = context.mSourceBuffer.mEnd;
            if (dumpDir.empty() || begin == nullptr || end <= begin) {
                return;
            }
            char prefix[16];
            std::snprintf(prefix, sizeof(prefix), "%02u_", static_cast<unsigned>(index));
            const std::string stem = dumpDir + "\\" + prefix + BaseName(context.mSourcePath.c_str());
            std::ofstream source(stem + ".src", std::ios::binary);
            source.write(begin, end - begin);
            std::ofstream meta(stem + ".json", std::ios::binary);
            meta << "{\"sourcePath\": \"" << JsonEscape(context.mSourcePath.c_str()) << "\", \"useCache\": "
                 << (context.mUseCache ? "true" : "false") << ", \"bytes\": " << (end - begin) << ", \"macros\": [";
            bool first = true;
            for (const EffectMacro& macro : context.mMacros) {
                meta << (first ? "" : ", ") << "[\"" << JsonEscape(macro.keyText_.c_str()) << "\", \""
                     << JsonEscape(macro.valueText_.c_str()) << "\"]";
                first = false;
            }
            meta << "]}\n";
        }

        boost::shared_ptr<EffectDiligent> LockEffect(const boost::weak_ptr<EffectDiligent>& effect, const char* const file, const int line)
        {
            boost::shared_ptr<EffectDiligent> locked = effect.lock();
            if (!locked) {
                ThrowGalError(file, line, "invalid effect specified");
            }
            return locked;
        }

        // ID3DXEffect::GetAnnotationByName + GetBool/GetInt/GetFloat/GetString on the annotation.
        // A missing annotation returns false (the D3D9 backend's null-handle path); a type D3DX cannot
        // convert throws, as the D3D9 backend turns D3DX's failure into gal::Error.
        const fx::AnnotationInfo* FindAnnotation(const std::vector<fx::AnnotationInfo>& annotations, const msvc8::string& name)
        {
            for (const fx::AnnotationInfo& annotation : annotations) {
                if (annotation.desc.name == name.c_str()) {
                    return &annotation;
                }
            }
            return nullptr;
        }

        bool IsNumeric(const fx::AnnotationInfo& annotation)
        {
            return (annotation.desc.type == fx::ParameterType::Bool || annotation.desc.type == fx::ParameterType::Int ||
                    annotation.desc.type == fx::ParameterType::Float) &&
                   !annotation.value.empty();
        }

        float AnnotationFloat(const fx::AnnotationInfo& annotation)
        {
            const std::uint32_t word = annotation.value[0];
            if (annotation.desc.type == fx::ParameterType::Float) {
                float value = 0.0F;
                std::memcpy(&value, &word, sizeof(value));
                return value;
            }
            return static_cast<float>(static_cast<std::int32_t>(word));
        }

        int AnnotationInt(const fx::AnnotationInfo& annotation)
        {
            if (annotation.desc.type == fx::ParameterType::Float) {
                return static_cast<int>(AnnotationFloat(annotation));
            }
            return static_cast<int>(annotation.value[0]);
        }

        template <class Out, class Read>
        bool ReadAnnotation(const std::vector<fx::AnnotationInfo>& annotations, const msvc8::string& name, Out* const out,
                            Read&& read, const char* const file, const int line)
        {
            const fx::AnnotationInfo* const annotation = FindAnnotation(annotations, name);
            if (annotation == nullptr) {
                return false;
            }
            if (!read(*annotation, out)) {
                ThrowGalErrorFromHresult(file, line, D3DERR_INVALIDCALL);
            }
            return true;
        }

        bool ReadBool(const std::vector<fx::AnnotationInfo>& annotations, bool* const out, const msvc8::string& name, const char* file, int line)
        {
            return ReadAnnotation(annotations, name, out, [](const fx::AnnotationInfo& a, bool* v) {
                if (!IsNumeric(a)) {
                    return false;
                }
                *v = AnnotationInt(a) == 1; // EffectVariableD3D9::GetAnnotationBool: `raw == 1`
                return true;
            }, file, line);
        }

        bool ReadInt(const std::vector<fx::AnnotationInfo>& annotations, int* const out, const msvc8::string& name, const char* file, int line)
        {
            return ReadAnnotation(annotations, name, out, [](const fx::AnnotationInfo& a, int* v) {
                if (!IsNumeric(a)) {
                    return false;
                }
                *v = AnnotationInt(a);
                return true;
            }, file, line);
        }

        bool ReadFloat(const std::vector<fx::AnnotationInfo>& annotations, float* const out, const msvc8::string& name, const char* file, int line)
        {
            return ReadAnnotation(annotations, name, out, [](const fx::AnnotationInfo& a, float* v) {
                if (!IsNumeric(a)) {
                    return false;
                }
                *v = AnnotationFloat(a);
                return true;
            }, file, line);
        }

        bool ReadString(const std::vector<fx::AnnotationInfo>& annotations, msvc8::string* const out, const msvc8::string& name, const char* file, int line)
        {
            return ReadAnnotation(annotations, name, out, [](const fx::AnnotationInfo& a, msvc8::string* v) {
                if (a.desc.type != fx::ParameterType::String) {
                    return false;
                }
                v->assign_owned(a.string.c_str());
                return true;
            }, file, line);
        }
    } // namespace

    std::vector<EffectRecord> GetEffectRecords()
    {
        std::lock_guard<std::mutex> lock(gRecordLock);
        return gRecords;
    }

    EffectLayerCounts GetEffectLayerCounts()
    {
        const EffectGpuStats stats = GetEffectGpuStats();
        EffectLayerCounts counts;
        counts.effects = stats.effects;
        counts.programsCompiled = stats.programsCompiled;
        counts.shadersCompiled = stats.shadersCompiled;
        counts.generationFailures = stats.generationFailures;
        counts.constantUploads = stats.constantUploads;
        counts.drawConstantUploads = stats.drawConstantUploads;
        counts.commits = stats.commits;
        counts.nullTextureBinds = stats.nullTextureBinds;
        return counts;
    }

    boost::shared_ptr<Effect> CreateEffectFromContext(const D3D9Oracle& oracle, const EffectContext& context, const std::string& dumpDir)
    {
        // DeviceD3D9::CreateEffect accepts only the payload form (mSourceType 2, D3D9Interfaces.cpp:1696).
        if (context.mSourceType != 2U) {
            ThrowGalError("DeviceDiligent.cpp", __LINE__, "");
        }
        SetEffectLog(&LogToEngine);

        std::size_t recordIndex = 0;
        {
            std::lock_guard<std::mutex> lock(gRecordLock);
            recordIndex = gRecords.size();
            EffectRecord record;
            record.sourcePath = context.mSourcePath.c_str();
            record.fromCache = context.mUseCache;
            gRecords.push_back(record);
        }
        DumpSource(dumpDir, context, recordIndex);

        const char* const begin = context.mSourceBuffer.mBegin;
        const char* const end = context.mSourceBuffer.mEnd;
        if (begin == nullptr || end <= begin) {
            ThrowEffectError("unable to compile effect: ", context, "empty source buffer", __LINE__);
        }
        // One part: the engine has already merged the compat header and the .fx (diagnostics count
        // lines across both, as D3DX's do).
        fx::EffectInput input;
        input.parts.emplace_back(BaseName(context.mSourcePath.c_str()), std::string(begin, end));
        for (const EffectMacro& macro : context.mMacros) {
            input.macros.push_back({macro.keyText_.c_str(), macro.valueText_.c_str()});
        }
        std::string errors;
        std::shared_ptr<EffectGpu> gpu = EffectGpu::Create(StemOf(context.mSourcePath.c_str()), input, &errors);
        if (!gpu) {
            ThrowEffectError("unable to compile effect: ", context, errors, __LINE__);
        }
        {
            std::lock_guard<std::mutex> lock(gRecordLock);
            gRecords[recordIndex].techniqueCount = static_cast<std::uint32_t>(gpu->GetMetadata().techniques.size());
            gRecords[recordIndex].parameterCount = static_cast<std::uint32_t>(gpu->GetMetadata().parameters.size());
        }
        // FindNextValidTechnique accepts a technique when the device creates every shader of its
        // passes (port/graphics/fx/README.md "The oracle"); on a HAL device that is the shader
        // version the caps report.
        const D3DCAPS9& caps = oracle.GetHalCaps();
        return boost::shared_ptr<Effect>(new EffectDiligent(
            context, std::move(gpu), recordIndex, caps.VertexShaderVersion & 0xFFFFU, caps.PixelShaderVersion & 0xFFFFU
        ));
    }

    // ---------------------------------------------------------------------------------------------
    // Effect

    EffectDiligent::EffectDiligent(
        const EffectContext& context,
        std::shared_ptr<EffectGpu> gpu,
        const std::size_t recordIndex,
        const std::uint32_t maxVertexShaderVersion,
        const std::uint32_t maxPixelShaderVersion
    )
        : gpu_(std::move(gpu)),
          recordIndex_(recordIndex),
          maxVertexShaderVersion_(maxVertexShaderVersion),
          maxPixelShaderVersion_(maxPixelShaderVersion)
    {
        // EffectD3D9::SetEffect (D3D9Interfaces.cpp:4182-4188) keeps the settings, not the bytes.
        context_ = context;
        context_.mSourceBuffer.Reset();
        textureHolds_.resize(gpu_->GetMetadata().parameters.size());
    }

    EffectDiligent::~EffectDiligent() = default;

    EffectContext* EffectDiligent::GetContext()
    {
        return &context_;
    }

    EffectGpu& EffectDiligent::Gpu()
    {
        return *gpu_;
    }

    // EffectD3D9::GetTechniques (D3D9Interfaces.cpp:4226-4250): the valid techniques in declaration
    // order, which is the order FindNextValidTechnique walks them in.
    void EffectDiligent::GetTechniques(msvc8::vector<boost::shared_ptr<EffectTechnique>>& outTechniques)
    {
        const std::vector<fx::TechniqueInfo>& techniques = gpu_->GetMetadata().techniques;
        const fx::DeviceProfile profile{"hal", maxVertexShaderVersion_, maxPixelShaderVersion_};
        std::vector<std::string> names;
        for (std::size_t index = 0; index < techniques.size(); ++index) {
            if (!fx::IsTechniqueValid(techniques[index], profile)) {
                continue;
            }
            names.push_back(techniques[index].name);
            outTechniques.push_back(boost::shared_ptr<EffectTechnique>(
                new EffectTechniqueDiligent(techniques[index].name.c_str(), boost::SharedFromThis(*this), static_cast<int>(index))
            ));
        }
        if (!recorded_) {
            std::lock_guard<std::mutex> lock(gRecordLock);
            gRecords[recordIndex_].validTechniques = names;
            recorded_ = true;
        }
    }

    boost::shared_ptr<EffectVariable> EffectDiligent::GetVariable(const char* const name)
    {
        const int parameter = gpu_->FindParameter(name);
        if (parameter < 0) {
            char message[512] = {};
            std::snprintf(message, sizeof(message), "invalid effect variable requested: %s", name != nullptr ? name : "");
            ThrowGalError("EffectDiligent.cpp", __LINE__, message);
        }
        return boost::shared_ptr<EffectVariable>(new EffectVariableDiligent(name, boost::SharedFromThis(*this), parameter));
    }

    boost::shared_ptr<EffectTechnique> EffectDiligent::GetTechnique(const char* const name)
    {
        const int technique = gpu_->FindTechnique(name);
        if (technique < 0) {
            char message[512] = {};
            std::snprintf(message, sizeof(message), "invalid effect technique requested: %s", name != nullptr ? name : "");
            ThrowGalError("EffectDiligent.cpp", __LINE__, message);
        }
        return boost::shared_ptr<EffectTechnique>(new EffectTechniqueDiligent(name, boost::SharedFromThis(*this), technique));
    }

    // Nothing device-dependent lives in the effect object: SetEffectRenderDevice releases and remakes
    // the GPU side.
    void EffectDiligent::OnReset() {}

    void EffectDiligent::OnLost() {}

    // ---------------------------------------------------------------------------------------------
    // Technique

    EffectTechniqueDiligent::EffectTechniqueDiligent(
        const char* const name,
        const boost::shared_ptr<EffectDiligent>& effect,
        const int technique
    )
        : name_(name),
          effect_(effect),
          technique_(technique)
    {
        if (effect_.expired()) {
            ThrowGalError("EffectTechniqueDiligent.cpp", __LINE__, "invalid effect specified");
        }
    }

    EffectTechniqueDiligent::~EffectTechniqueDiligent() = default;

    msvc8::string* EffectTechniqueDiligent::GetName()
    {
        return &name_;
    }

    // EffectTechniqueD3D9::BeginTechnique (D3D9Interfaces.cpp:4084-4110): Device slot 48, then
    // ID3DXEffect::Begin(DONOTSAVESTATE), which returns the pass count.
    int EffectTechniqueDiligent::BeginTechnique()
    {
        if (beginEndActive_) {
            ThrowGalError("EffectTechniqueDiligent.cpp", __LINE__, "effect technique begin/end mismatch");
        }
        const boost::shared_ptr<EffectDiligent> effect = LockEffect(effect_, "EffectTechniqueDiligent.cpp", __LINE__);
        Device::GetInstance()->BeginTechnique();
        beginEndActive_ = true;
        passCount_ = static_cast<int>(effect->Gpu().GetMetadata().techniques[static_cast<std::size_t>(technique_)].passes.size());
        return passCount_;
    }

    void EffectTechniqueDiligent::EndTechnique()
    {
        if (!beginEndActive_) {
            ThrowGalError("EffectTechniqueDiligent.cpp", __LINE__, "effect technique begin/end mismatch");
        }
        static_cast<void>(LockEffect(effect_, "EffectTechniqueDiligent.cpp", __LINE__));
        if (activePass_ != nullptr) {
            // ID3DXEffect::End without EndPass: the pass ends with the technique.
            if (PassSink* const sink = GetPassSink()) {
                sink->OnEndPass(*activePass_);
            }
            activePass_ = nullptr;
            passHolds_.clear();
        }
        Device::GetInstance()->EndTechnique();
        beginEndActive_ = false;
    }

    void EffectTechniqueDiligent::BeginPass(const int pass)
    {
        if (!beginEndActive_) {
            ThrowGalError("EffectTechniqueDiligent.cpp", __LINE__, "effect technique begin/end mismatch");
        }
        const boost::shared_ptr<EffectDiligent> effect = LockEffect(effect_, "EffectTechniqueDiligent.cpp", __LINE__);
        PassBinding* const binding = effect->Gpu().BeginPass(technique_, pass);
        if (binding == nullptr) {
            ThrowGalErrorFromHresult("EffectTechniqueDiligent.cpp", __LINE__, D3DERR_INVALIDCALL);
        }
        if (activePass_ != nullptr && activePass_ != binding) {
            if (PassSink* const sink = GetPassSink()) {
                sink->OnEndPass(*activePass_);
            }
        }
        // The snapshot points at the textures bound now; keep them alive with the pass.
        passHolds_ = effect->TextureHolds();
        activePass_ = binding;
        if (PassSink* const sink = GetPassSink()) {
            sink->OnBeginPass(*binding);
        }
    }

    void EffectTechniqueDiligent::EndPass()
    {
        if (!beginEndActive_) {
            ThrowGalError("EffectTechniqueDiligent.cpp", __LINE__, "effect technique begin/end mismatch");
        }
        static_cast<void>(LockEffect(effect_, "EffectTechniqueDiligent.cpp", __LINE__));
        if (activePass_ != nullptr) {
            if (PassSink* const sink = GetPassSink()) {
                sink->OnEndPass(*activePass_);
            }
            activePass_ = nullptr;
        }
        passHolds_.clear();
    }

    void EffectTechniqueDiligent::CommitChanges()
    {
        if (!beginEndActive_) {
            ThrowGalError("EffectTechniqueDiligent.cpp", 0, "effect technique begin/end mismatch");
        }
        const boost::shared_ptr<EffectDiligent> effect = LockEffect(effect_, "EffectTechniqueDiligent.cpp", 0);
        if (activePass_ != nullptr) {
            passHolds_ = effect->TextureHolds();
            effect->Gpu().CommitChanges(activePass_);
        }
    }

    bool EffectTechniqueDiligent::GetAnnotationBool(bool* const outValue, const msvc8::string& annotationName)
    {
        const boost::shared_ptr<EffectDiligent> effect = LockEffect(effect_, "EffectTechniqueDiligent.cpp", __LINE__);
        return ReadBool(effect->Gpu().GetMetadata().techniques[static_cast<std::size_t>(technique_)].annotations, outValue, annotationName,
                        "EffectTechniqueDiligent.cpp", __LINE__);
    }

    bool EffectTechniqueDiligent::GetAnnotationInt(int* const outValue, const msvc8::string& annotationName)
    {
        const boost::shared_ptr<EffectDiligent> effect = LockEffect(effect_, "EffectTechniqueDiligent.cpp", __LINE__);
        return ReadInt(effect->Gpu().GetMetadata().techniques[static_cast<std::size_t>(technique_)].annotations, outValue, annotationName,
                       "EffectTechniqueDiligent.cpp", __LINE__);
    }

    bool EffectTechniqueDiligent::GetAnnotationFloat(float* const outValue, const msvc8::string& annotationName)
    {
        const boost::shared_ptr<EffectDiligent> effect = LockEffect(effect_, "EffectTechniqueDiligent.cpp", __LINE__);
        return ReadFloat(effect->Gpu().GetMetadata().techniques[static_cast<std::size_t>(technique_)].annotations, outValue, annotationName,
                         "EffectTechniqueDiligent.cpp", __LINE__);
    }

    bool EffectTechniqueDiligent::GetAnnotationString(msvc8::string* const outValue, const msvc8::string& annotationName)
    {
        const boost::shared_ptr<EffectDiligent> effect = LockEffect(effect_, "EffectTechniqueDiligent.cpp", __LINE__);
        return ReadString(effect->Gpu().GetMetadata().techniques[static_cast<std::size_t>(technique_)].annotations, outValue, annotationName,
                          "EffectTechniqueDiligent.cpp", __LINE__);
    }

    // ---------------------------------------------------------------------------------------------
    // Variable

    EffectVariableDiligent::EffectVariableDiligent(
        const char* const name,
        const boost::shared_ptr<EffectDiligent>& effect,
        const int parameter
    )
        : name_(name),
          effect_(effect),
          parameter_(parameter)
    {
        if (effect_.expired()) {
            ThrowGalError("EffectVariableDiligent.cpp", __LINE__, "invalid effect specified");
        }
    }

    EffectVariableDiligent::~EffectVariableDiligent() = default;

    boost::shared_ptr<EffectDiligent> EffectVariableDiligent::Effect(const int line)
    {
        return LockEffect(effect_, "EffectVariableDiligent.cpp", line);
    }

    msvc8::string* EffectVariableDiligent::GetName()
    {
        return &name_;
    }

    // ID3DXEffect::SetTexture: a null object unbinds; otherwise the draw path's object exposes its
    // shader resource view through ShaderResourceSource (PassBinding.h).
    void EffectVariableDiligent::BindTexture(EffectDiligent::TextureHold hold, void* const source, const int line)
    {
        const boost::shared_ptr<EffectDiligent> effect = Effect(line);
        auto* const resource = static_cast<ShaderResourceSource*>(source);
        if (!effect->Gpu().SetTexture(parameter_, resource)) {
            ThrowGalErrorFromHresult("EffectVariableDiligent.cpp", line, D3DERR_INVALIDCALL);
        }
        effect->TextureHolds()[static_cast<std::size_t>(parameter_)] = std::move(hold);
    }

    void EffectVariableDiligent::SetCubeRenderTarget(const boost::shared_ptr<CubeRenderTarget> cubeTarget)
    {
        EffectDiligent::TextureHold hold;
        hold.cubeTarget = cubeTarget;
        BindTexture(std::move(hold), dynamic_cast<ShaderResourceSource*>(cubeTarget.get()), __LINE__);
    }

    void EffectVariableDiligent::SetRenderTarget(const boost::shared_ptr<RenderTarget> renderTarget)
    {
        EffectDiligent::TextureHold hold;
        hold.renderTarget = renderTarget;
        BindTexture(std::move(hold), dynamic_cast<ShaderResourceSource*>(renderTarget.get()), __LINE__);
    }

    void EffectVariableDiligent::SetTexture(const boost::shared_ptr<Texture> texture)
    {
        EffectDiligent::TextureHold hold;
        hold.texture = texture;
        BindTexture(std::move(hold), dynamic_cast<ShaderResourceSource*>(texture.get()), __LINE__);
    }

    // The numeric setters: ID3DXEffect's semantics (EffectsDiligentGpu.h), the D3D9 backend's errors
    // (EffectVariableD3D9, D3D9Interfaces.cpp:4403-4550: a failed D3DX call throws gal::Error).
    void EffectVariableDiligent::SetMatrix4x4(const Matrix* const matrix)
    {
        if (!Effect(__LINE__)->Gpu().SetMatrices(parameter_, reinterpret_cast<const float*>(matrix), 1U)) {
            ThrowGalErrorFromHresult("EffectVariableDiligent.cpp", __LINE__, D3DERR_INVALIDCALL);
        }
    }

    void EffectVariableDiligent::SetFloatArray(const std::uint32_t count, const float* const values)
    {
        if (!Effect(__LINE__)->Gpu().SetFloats(parameter_, values, count)) {
            ThrowGalErrorFromHresult("EffectVariableDiligent.cpp", __LINE__, D3DERR_INVALIDCALL);
        }
    }

    void EffectVariableDiligent::SetVector(const float* const vector4)
    {
        if (!Effect(__LINE__)->Gpu().SetVectors(parameter_, vector4, 1U)) {
            ThrowGalErrorFromHresult("EffectVariableDiligent.cpp", __LINE__, D3DERR_INVALIDCALL);
        }
    }

    void EffectVariableDiligent::SetValue(const void* const data, const std::uint32_t byteCount)
    {
        if (!Effect(__LINE__)->Gpu().SetRaw(parameter_, data, byteCount)) {
            ThrowGalErrorFromHresult("EffectVariableDiligent.cpp", __LINE__, D3DERR_INVALIDCALL);
        }
    }

    void EffectVariableDiligent::SetFloat(const float value)
    {
        if (!Effect(__LINE__)->Gpu().SetFloats(parameter_, &value, 1U)) {
            ThrowGalErrorFromHresult("EffectVariableDiligent.cpp", __LINE__, D3DERR_INVALIDCALL);
        }
    }

    void EffectVariableDiligent::SetInt(const int value)
    {
        if (!Effect(__LINE__)->Gpu().SetInts(parameter_, &value, 1U)) {
            ThrowGalErrorFromHresult("EffectVariableDiligent.cpp", __LINE__, D3DERR_INVALIDCALL);
        }
    }

    void EffectVariableDiligent::SetBool(const bool value)
    {
        const int raw = value ? 1 : 0;
        if (!Effect(__LINE__)->Gpu().SetBools(parameter_, &raw, 1U)) {
            ThrowGalErrorFromHresult("EffectVariableDiligent.cpp", __LINE__, D3DERR_INVALIDCALL);
        }
    }

    void EffectVariableDiligent::SetMatrixArray(const std::uint32_t count, const Matrix* const matrices)
    {
        if (!Effect(__LINE__)->Gpu().SetMatrices(parameter_, reinterpret_cast<const float*>(matrices), count)) {
            ThrowGalErrorFromHresult("EffectVariableDiligent.cpp", __LINE__, D3DERR_INVALIDCALL);
        }
    }

    // The vectors themselves. EffectVariableD3D9::SetVectorArray (D3D9Interfaces.cpp:4445-4458)
    // keeps the binary's slip of passing the address of its `vectors4` argument (0x00943990);
    // nothing in the binary calls the slot, and a new backend has no reason to copy it.
    void EffectVariableDiligent::SetVectorArray(const std::uint32_t count, const float* const vectors4)
    {
        if (!Effect(__LINE__)->Gpu().SetVectors(parameter_, vectors4, count)) {
            ThrowGalErrorFromHresult("EffectVariableDiligent.cpp", __LINE__, D3DERR_INVALIDCALL);
        }
    }

    bool EffectVariableDiligent::GetAnnotationBool(bool* const outValue, const msvc8::string& annotationName)
    {
        const boost::shared_ptr<EffectDiligent> effect = Effect(__LINE__);
        return ReadBool(effect->Gpu().GetMetadata().parameters[static_cast<std::size_t>(parameter_)].annotations, outValue,
                        annotationName, "EffectVariableDiligent.cpp", __LINE__);
    }

    bool EffectVariableDiligent::GetAnnotationInt(int* const outValue, const msvc8::string& annotationName)
    {
        const boost::shared_ptr<EffectDiligent> effect = Effect(__LINE__);
        return ReadInt(effect->Gpu().GetMetadata().parameters[static_cast<std::size_t>(parameter_)].annotations, outValue,
                       annotationName, "EffectVariableDiligent.cpp", __LINE__);
    }

    bool EffectVariableDiligent::GetAnnotationFloat(float* const outValue, const msvc8::string& annotationName)
    {
        const boost::shared_ptr<EffectDiligent> effect = Effect(__LINE__);
        return ReadFloat(effect->Gpu().GetMetadata().parameters[static_cast<std::size_t>(parameter_)].annotations, outValue,
                         annotationName, "EffectVariableDiligent.cpp", __LINE__);
    }

    bool EffectVariableDiligent::GetAnnotationString(msvc8::string* const outValue, const msvc8::string& annotationName)
    {
        const boost::shared_ptr<EffectDiligent> effect = Effect(__LINE__);
        return ReadString(effect->Gpu().GetMetadata().parameters[static_cast<std::size_t>(parameter_)].annotations, outValue,
                          annotationName, "EffectVariableDiligent.cpp", __LINE__);
    }
} // namespace gpg::gal::diligent
