#include "EffectsDiligent.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

#include "D3D9Oracle.h"
#include "gpg/core/utils/BoostWrappers.h"
#include "gpg/gal/Device.hpp"
#include "gpg/gal/EffectMacro.hpp"
#include "gpg/gal/Matrix.h"

namespace gpg::gal::diligent
{
    namespace
    {
        std::mutex gRecordLock;
        std::vector<EffectRecord> gRecords;

        std::string ReadErrors(ID3DXBuffer* const errors)
        {
            if (errors == nullptr || errors->GetBufferPointer() == nullptr) {
                return "unknown error";
            }
            return static_cast<const char*>(errors->GetBufferPointer());
        }

        template <class T>
        void SafeRelease(T*& object)
        {
            if (object != nullptr) {
                object->Release();
                object = nullptr;
            }
        }

        [[noreturn]] void ThrowEffectError(const char* const prefix, const EffectContext& context, const std::string& reason, const int line)
        {
            // The message of BuildEffectCreationMessage, D3D9Interfaces.cpp:842-853.
            const std::string message = std::string(prefix) + context.mSourcePath.c_str() + " reason: " + reason;
            ThrowGalError("DeviceDiligent.cpp", line, message.c_str());
        }

        bool DefinesMacro(const msvc8::vector<EffectMacro>& macros, const char* const name)
        {
            for (const EffectMacro& macro : macros) {
                if (std::strcmp(macro.keyText_.c_str(), name) == 0) {
                    return true;
                }
            }
            return false;
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

        boost::shared_ptr<EffectDiligent> LockEffect(const boost::weak_ptr<EffectDiligent>& effect, const int line)
        {
            boost::shared_ptr<EffectDiligent> locked = effect.lock();
            if (!locked) {
                ThrowGalError("EffectDiligent.cpp", line, "invalid effect specified");
            }
            return locked;
        }

        template <class Out, class Get>
        bool ReadAnnotation(ID3DXEffect* const effect, const D3DXHANDLE owner, const msvc8::string& name, Out* const out, Get&& get, const int line)
        {
            const D3DXHANDLE annotation = effect->GetAnnotationByName(owner, name.c_str());
            if (annotation == nullptr) {
                return false;
            }
            const HRESULT result = get(annotation, out);
            if (FAILED(result)) {
                ThrowGalErrorFromHresult("EffectDiligent.cpp", line, result);
            }
            return true;
        }

        void CheckSet(const HRESULT result, const int line)
        {
            if (FAILED(result)) {
                ThrowGalErrorFromHresult("EffectVariableDiligent.cpp", line, result);
            }
        }
    } // namespace

    std::vector<EffectRecord> GetEffectRecords()
    {
        std::lock_guard<std::mutex> lock(gRecordLock);
        return gRecords;
    }

    boost::shared_ptr<Effect> CreateEffectFromContext(const D3D9Oracle& oracle, const EffectContext& context, const std::string& dumpDir)
    {
        if (context.mSourceType != 2U) {
            ThrowGalError("DeviceDiligent.cpp", __LINE__, "");
        }

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

        // BuildD3DXMacroDefines, D3D9Interfaces.cpp:861-882.
        std::vector<D3DXMACRO> defines;
        for (const EffectMacro& macro : context.mMacros) {
            defines.push_back(D3DXMACRO{macro.keyText_.c_str(), macro.valueText_.c_str()});
        }
        if (!defines.empty()) {
            defines.push_back(D3DXMACRO{nullptr, nullptr});
        }
        const D3DXMACRO* const defineArray = defines.empty() ? nullptr : defines.data();

        ID3DXEffectCompiler* compiler = nullptr;
        ID3DXBuffer* compiled = nullptr;
        ID3DXBuffer* errors = nullptr;
        ID3DXEffect* effect = nullptr;
        try {
            if (context.mUseCache) {
                // DeviceD3D9::CreateEffectFromCachedBinary, D3D9Interfaces.cpp:1628-1690.
                std::ifstream cache(context.mCachePath.c_str(), std::ios::binary);
                if (!cache.is_open()) {
                    ThrowGalError("DeviceDiligent.cpp", __LINE__, "");
                }
                const std::vector<char> bytes{std::istreambuf_iterator<char>(cache), std::istreambuf_iterator<char>()};
                const HRESULT result = D3DXCreateEffect(
                    oracle.GetDevice(), bytes.data(), static_cast<UINT>(bytes.size()), nullptr, nullptr, 0U, nullptr, &effect, &errors
                );
                if (FAILED(result)) {
                    ThrowEffectError("unable to create effect: ", context, ReadErrors(errors), __LINE__);
                }
                SafeRelease(errors);
            } else {
                // DeviceD3D9::CreateEffectFromSourceBuffer, D3D9Interfaces.cpp:1529-1626.
                const DWORD flowControlFlags = DefinesMacro(context.mMacros, "FAF_BONE_TEXTURE") ? D3DXSHADER_AVOID_FLOW_CONTROL : 0U;
                const char* const sourceData = context.mSourceBuffer.mBegin;
                const unsigned int sourceBytes = static_cast<unsigned int>(context.mSourceBuffer.mEnd - context.mSourceBuffer.mBegin);
                HRESULT result = D3DXCreateEffectCompiler(
                    sourceData, sourceBytes, defineArray, nullptr,
                    D3DXSHADER_DEBUG | D3DXSHADER_USE_LEGACY_D3DX9_31_DLL | flowControlFlags, &compiler, &errors
                );
                if (FAILED(result)) {
                    ThrowEffectError("unable to compile effect: ", context, ReadErrors(errors), __LINE__);
                }
                SafeRelease(errors);
                result = compiler->CompileEffect(D3DXSHADER_DEBUG | flowControlFlags, &compiled, &errors);
                if (FAILED(result)) {
                    ThrowEffectError("unable to compile effect: ", context, ReadErrors(errors), __LINE__);
                }
                SafeRelease(errors);
                SafeRelease(compiler);
                result = D3DXCreateEffect(
                    oracle.GetDevice(), compiled->GetBufferPointer(), compiled->GetBufferSize(), defineArray, nullptr,
                    D3DXSHADER_DEBUG, nullptr, &effect, &errors
                );
                if (FAILED(result)) {
                    ThrowEffectError("unable to create effect: ", context, ReadErrors(errors), __LINE__);
                }
                SafeRelease(errors);
                // The compiled effect goes to the cache path as on D3D9; CD3DEffect decides when to
                // read it back (mUseCache).
                std::ofstream cache(context.mCachePath.c_str(), std::ios::binary);
                if (cache.is_open()) {
                    cache.write(static_cast<const char*>(compiled->GetBufferPointer()), compiled->GetBufferSize());
                }
                SafeRelease(compiled);
            }
        } catch (...) {
            SafeRelease(compiler);
            SafeRelease(compiled);
            SafeRelease(errors);
            SafeRelease(effect);
            throw;
        }

        D3DXEFFECT_DESC desc{};
        if (SUCCEEDED(effect->GetDesc(&desc))) {
            std::lock_guard<std::mutex> lock(gRecordLock);
            gRecords[recordIndex].techniqueCount = desc.Techniques;
            gRecords[recordIndex].parameterCount = desc.Parameters;
        }
        const D3DCAPS9& caps = oracle.GetHalCaps();
        return boost::shared_ptr<Effect>(
            new EffectDiligent(context, effect, recordIndex, caps.VertexShaderVersion, caps.PixelShaderVersion)
        );
    }

    // ---------------------------------------------------------------------------------------------
    // Effect

    EffectDiligent::EffectDiligent(
        const EffectContext& context,
        ID3DXEffect* const effect,
        const std::size_t recordIndex,
        const DWORD halVertexShaderVersion,
        const DWORD halPixelShaderVersion
    )
        : dxEffect_(effect),
          recordIndex_(recordIndex),
          halVertexShaderVersion_(halVertexShaderVersion),
          halPixelShaderVersion_(halPixelShaderVersion)
    {
        // EffectD3D9::SetEffect (D3D9Interfaces.cpp:4182-4188) keeps the settings, not the bytes.
        context_ = context;
        context_.mSourceBuffer.Reset();
    }

    EffectDiligent::~EffectDiligent()
    {
        SafeRelease(dxEffect_);
    }

    EffectContext* EffectDiligent::GetContext()
    {
        return &context_;
    }

    ID3DXEffect* EffectDiligent::GetDxEffect()
    {
        if (dxEffect_ == nullptr) {
            ThrowGalError("EffectDiligent.cpp", __LINE__, "attempt to retrieve invalid effect");
        }
        return dxEffect_;
    }

    /**
     * What FindNextValidTechnique decides on the D3D9 device, for the part that depends on the
     * hardware: every pass's vertex and pixel shader version (the first token of the bytecode,
     * D3DXPASS_DESC) within the HAL's caps. A null shader (FIXED_FUNC_VS, PixelShader = null) needs
     * nothing. ValidateTechnique also checks render and sampler states against the caps; the
     * shipped effects use none a shader model 2.0 part lacks (m6u-FX.txt section 4), and gate 1
     * compares the result with a HAL device's lists.
     */
    bool EffectDiligent::IsTechniqueValid(const D3DXHANDLE technique)
    {
        ID3DXEffect* const effect = GetDxEffect();
        D3DXTECHNIQUE_DESC description{};
        if (FAILED(effect->GetTechniqueDesc(technique, &description))) {
            return false;
        }
        for (UINT pass = 0; pass < description.Passes; ++pass) {
            D3DXPASS_DESC passDesc{};
            if (FAILED(effect->GetPassDesc(effect->GetPass(technique, pass), &passDesc))) {
                return false;
            }
            if (passDesc.pVertexShaderFunction != nullptr && *passDesc.pVertexShaderFunction > halVertexShaderVersion_) {
                return false;
            }
            if (passDesc.pPixelShaderFunction != nullptr && *passDesc.pPixelShaderFunction > halPixelShaderVersion_) {
                return false;
            }
        }
        return true;
    }

    // EffectD3D9::GetTechniques (D3D9Interfaces.cpp:4226-4250): the valid techniques in
    // declaration order, which is the order FindNextValidTechnique walks them in.
    void EffectDiligent::GetTechniques(msvc8::vector<boost::shared_ptr<EffectTechnique>>& outTechniques)
    {
        ID3DXEffect* const effect = GetDxEffect();
        D3DXEFFECT_DESC effectDesc{};
        const HRESULT result = effect->GetDesc(&effectDesc);
        if (FAILED(result)) {
            ThrowGalErrorFromHresult("EffectDiligent.cpp", __LINE__, result);
        }
        std::vector<std::string> names;
        for (UINT index = 0; index < effectDesc.Techniques; ++index) {
            const D3DXHANDLE technique = effect->GetTechnique(index);
            if (technique == nullptr || !IsTechniqueValid(technique)) {
                continue;
            }
            D3DXTECHNIQUE_DESC description{};
            effect->GetTechniqueDesc(technique, &description);
            names.emplace_back(description.Name);
            outTechniques.push_back(boost::shared_ptr<EffectTechnique>(
                new EffectTechniqueDiligent(description.Name, boost::SharedFromThis(*this), technique)
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
        const D3DXHANDLE handle = GetDxEffect()->GetParameterByName(nullptr, name);
        if (handle == nullptr) {
            char message[512] = {};
            std::snprintf(message, sizeof(message), "invalid effect variable requested: %s", name != nullptr ? name : "");
            ThrowGalError("EffectDiligent.cpp", __LINE__, message);
        }
        return boost::shared_ptr<EffectVariable>(new EffectVariableDiligent(name, boost::SharedFromThis(*this), handle));
    }

    boost::shared_ptr<EffectTechnique> EffectDiligent::GetTechnique(const char* const name)
    {
        const D3DXHANDLE handle = GetDxEffect()->GetTechniqueByName(name);
        if (handle == nullptr) {
            char message[512] = {};
            std::snprintf(message, sizeof(message), "invalid effect technique requested: %s", name != nullptr ? name : "");
            ThrowGalError("EffectDiligent.cpp", __LINE__, message);
        }
        return boost::shared_ptr<EffectTechnique>(new EffectTechniqueDiligent(name, boost::SharedFromThis(*this), handle));
    }

    // A NULLREF device is never lost; there is nothing to reset.
    void EffectDiligent::OnReset()
    {
        static_cast<void>(GetDxEffect());
    }

    void EffectDiligent::OnLost()
    {
        static_cast<void>(GetDxEffect());
    }

    // ---------------------------------------------------------------------------------------------
    // Technique

    EffectTechniqueDiligent::EffectTechniqueDiligent(
        const char* const name,
        const boost::shared_ptr<EffectDiligent>& effect,
        const D3DXHANDLE handle
    )
        : name_(name),
          effect_(effect),
          handle_(handle)
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

    // EffectTechniqueD3D9::BeginTechnique (D3D9Interfaces.cpp:4084-4110) minus the D3DX
    // SetTechnique/Begin, which would apply states to the NULLREF device: Device slot 48 is still
    // called, and the pass count comes from the technique's description.
    int EffectTechniqueDiligent::BeginTechnique()
    {
        if (beginEndActive_) {
            ThrowGalError("EffectTechniqueDiligent.cpp", __LINE__, "effect technique begin/end mismatch");
        }
        const boost::shared_ptr<EffectDiligent> effect = LockEffect(effect_, __LINE__);
        D3DXTECHNIQUE_DESC description{};
        const HRESULT result = effect->GetDxEffect()->GetTechniqueDesc(handle_, &description);
        if (FAILED(result)) {
            ThrowGalErrorFromHresult("EffectTechniqueDiligent.cpp", __LINE__, result);
        }
        Device::GetInstance()->BeginTechnique();
        beginEndActive_ = true;
        passCount_ = static_cast<int>(description.Passes);
        return passCount_;
    }

    void EffectTechniqueDiligent::EndTechnique()
    {
        if (!beginEndActive_) {
            ThrowGalError("EffectTechniqueDiligent.cpp", __LINE__, "effect technique begin/end mismatch");
        }
        Device::GetInstance()->EndTechnique();
        beginEndActive_ = false;
    }

    void EffectTechniqueDiligent::BeginPass(const int pass)
    {
        if (!beginEndActive_ || pass < 0 || pass >= passCount_) {
            ThrowGalError("EffectTechniqueDiligent.cpp", __LINE__, "invalid pass");
        }
    }

    void EffectTechniqueDiligent::EndPass()
    {}

    bool EffectTechniqueDiligent::GetAnnotationBool(bool* const outValue, const msvc8::string& annotationName)
    {
        const boost::shared_ptr<EffectDiligent> effect = LockEffect(effect_, __LINE__);
        ID3DXEffect* const dx = effect->GetDxEffect();
        BOOL raw = FALSE;
        if (!ReadAnnotation(dx, handle_, annotationName, &raw, [dx](D3DXHANDLE h, BOOL* v) { return dx->GetBool(h, v); }, __LINE__)) {
            return false;
        }
        *outValue = (raw == 1);
        return true;
    }

    bool EffectTechniqueDiligent::GetAnnotationInt(int* const outValue, const msvc8::string& annotationName)
    {
        const boost::shared_ptr<EffectDiligent> effect = LockEffect(effect_, __LINE__);
        ID3DXEffect* const dx = effect->GetDxEffect();
        return ReadAnnotation(dx, handle_, annotationName, outValue, [dx](D3DXHANDLE h, int* v) { return dx->GetInt(h, v); }, __LINE__);
    }

    bool EffectTechniqueDiligent::GetAnnotationFloat(float* const outValue, const msvc8::string& annotationName)
    {
        const boost::shared_ptr<EffectDiligent> effect = LockEffect(effect_, __LINE__);
        ID3DXEffect* const dx = effect->GetDxEffect();
        return ReadAnnotation(dx, handle_, annotationName, outValue, [dx](D3DXHANDLE h, float* v) { return dx->GetFloat(h, v); }, __LINE__);
    }

    bool EffectTechniqueDiligent::GetAnnotationString(msvc8::string* const outValue, const msvc8::string& annotationName)
    {
        const boost::shared_ptr<EffectDiligent> effect = LockEffect(effect_, __LINE__);
        ID3DXEffect* const dx = effect->GetDxEffect();
        const char* text = nullptr;
        if (!ReadAnnotation(dx, handle_, annotationName, &text, [dx](D3DXHANDLE h, const char** v) { return dx->GetString(h, v); }, __LINE__)) {
            return false;
        }
        outValue->assign_owned(text != nullptr ? text : "");
        return true;
    }

    // ---------------------------------------------------------------------------------------------
    // Variable

    EffectVariableDiligent::EffectVariableDiligent(
        const char* const name,
        const boost::shared_ptr<EffectDiligent>& effect,
        const D3DXHANDLE handle
    )
        : name_(name),
          effect_(effect),
          handle_(handle)
    {
        if (effect_.expired()) {
            ThrowGalError("EffectVariableDiligent.cpp", __LINE__, "invalid effect specified");
        }
    }

    EffectVariableDiligent::~EffectVariableDiligent() = default;

    ID3DXEffect* EffectVariableDiligent::Effect()
    {
        return LockEffect(effect_, __LINE__)->GetDxEffect();
    }

    msvc8::string* EffectVariableDiligent::GetName()
    {
        return &name_;
    }

    // Nothing is drawn in this step, so there is nothing to bind a texture to.
    void EffectVariableDiligent::SetCubeRenderTarget(const boost::shared_ptr<CubeRenderTarget> cubeTarget)
    {
        static_cast<void>(cubeTarget);
        static_cast<void>(Effect());
    }

    void EffectVariableDiligent::SetRenderTarget(const boost::shared_ptr<RenderTarget> renderTarget)
    {
        static_cast<void>(renderTarget);
        static_cast<void>(Effect());
    }

    void EffectVariableDiligent::SetTexture(const boost::shared_ptr<Texture> texture)
    {
        static_cast<void>(texture);
        static_cast<void>(Effect());
    }

    // The numeric setters forward to D3DX as EffectVariableD3D9 does (D3D9Interfaces.cpp:4403-4550).
    void EffectVariableDiligent::SetMatrix4x4(const Matrix* const matrix)
    {
        CheckSet(Effect()->SetMatrix(handle_, reinterpret_cast<const D3DXMATRIX*>(matrix)), __LINE__);
    }

    void EffectVariableDiligent::SetFloatArray(const std::uint32_t count, const float* const values)
    {
        CheckSet(Effect()->SetFloatArray(handle_, values, count), __LINE__);
    }

    void EffectVariableDiligent::SetVector(const float* const vector4)
    {
        CheckSet(Effect()->SetVector(handle_, reinterpret_cast<const D3DXVECTOR4*>(vector4)), __LINE__);
    }

    void EffectVariableDiligent::SetValue(const void* const data, const std::uint32_t byteCount)
    {
        CheckSet(Effect()->SetValue(handle_, data, byteCount), __LINE__);
    }

    void EffectVariableDiligent::SetFloat(const float value)
    {
        CheckSet(Effect()->SetFloat(handle_, value), __LINE__);
    }

    void EffectVariableDiligent::SetInt(const int value)
    {
        CheckSet(Effect()->SetInt(handle_, value), __LINE__);
    }

    void EffectVariableDiligent::SetBool(const bool value)
    {
        CheckSet(Effect()->SetBool(handle_, value ? TRUE : FALSE), __LINE__);
    }

    void EffectVariableDiligent::SetMatrixArray(const std::uint32_t count, const Matrix* const matrices)
    {
        CheckSet(Effect()->SetMatrixArray(handle_, reinterpret_cast<const D3DXMATRIX*>(matrices), count), __LINE__);
    }

    // The vectors themselves. EffectVariableD3D9::SetVectorArray (D3D9Interfaces.cpp:4445-4458)
    // keeps the binary's slip of passing the address of its `vectors4` argument (0x00943990);
    // nothing in the binary calls the slot, and a new backend has no reason to copy it.
    void EffectVariableDiligent::SetVectorArray(const std::uint32_t count, const float* const vectors4)
    {
        CheckSet(Effect()->SetVectorArray(handle_, reinterpret_cast<const D3DXVECTOR4*>(vectors4), count), __LINE__);
    }

    bool EffectVariableDiligent::GetAnnotationBool(bool* const outValue, const msvc8::string& annotationName)
    {
        ID3DXEffect* const dx = Effect();
        BOOL raw = FALSE;
        if (!ReadAnnotation(dx, handle_, annotationName, &raw, [dx](D3DXHANDLE h, BOOL* v) { return dx->GetBool(h, v); }, __LINE__)) {
            return false;
        }
        *outValue = (raw == 1);
        return true;
    }

    bool EffectVariableDiligent::GetAnnotationInt(int* const outValue, const msvc8::string& annotationName)
    {
        ID3DXEffect* const dx = Effect();
        return ReadAnnotation(dx, handle_, annotationName, outValue, [dx](D3DXHANDLE h, int* v) { return dx->GetInt(h, v); }, __LINE__);
    }

    bool EffectVariableDiligent::GetAnnotationFloat(float* const outValue, const msvc8::string& annotationName)
    {
        ID3DXEffect* const dx = Effect();
        return ReadAnnotation(dx, handle_, annotationName, outValue, [dx](D3DXHANDLE h, float* v) { return dx->GetFloat(h, v); }, __LINE__);
    }

    bool EffectVariableDiligent::GetAnnotationString(msvc8::string* const outValue, const msvc8::string& annotationName)
    {
        ID3DXEffect* const dx = Effect();
        const char* text = nullptr;
        if (!ReadAnnotation(dx, handle_, annotationName, &text, [dx](D3DXHANDLE h, const char** v) { return dx->GetString(h, v); }, __LINE__)) {
            return false;
        }
        outValue->assign_owned(text != nullptr ? text : "");
        return true;
    }
} // namespace gpg::gal::diligent
