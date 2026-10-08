// The galtrace v1 schema: one field list per op (GalTraceFormat.h). The recorder writes these
// fields in this order, galplay reads them in this order through a schema-checked cursor, and the
// generic decoder walks this table, so the three cannot drift apart silently.

#include "GalTraceFormat.h"

#include <algorithm>
#include <cstdio>
#include <iterator>

namespace galtrace
{
  namespace
  {
    using T = FieldType;
    using O = ObjectType;

    constexpr FieldDesc F(const char* name, const FieldType type)
    {
      return FieldDesc{name, type};
    }
    constexpr FieldDesc R(const char* name, const FieldType type) // a result field
    {
      return FieldDesc{name, type, ObjectType::None, nullptr, 0, true};
    }
    constexpr FieldDesc Ref(const char* name, const ObjectType type)
    {
      return FieldDesc{name, FieldType::IdRef, type};
    }
    constexpr FieldDesc Def(const char* name, const ObjectType type) // the object a call returned
    {
      return FieldDesc{name, FieldType::IdDef, type, nullptr, 0, true};
    }
    template <std::size_t N>
    constexpr FieldDesc S(const char* name, const FieldDesc (&sub)[N], const bool result = false)
    {
      return FieldDesc{name, FieldType::Struct, ObjectType::None, sub, static_cast<std::uint8_t>(N), result};
    }
    template <std::size_t N>
    constexpr FieldDesc L(const char* name, const FieldDesc (&sub)[N], const bool result = false)
    {
      return FieldDesc{name, FieldType::List, ObjectType::None, sub, static_cast<std::uint8_t>(N), result};
    }

    // ---- shared structs -------------------------------------------------------------------------

    // gpg::gal::HeadSampleOption
    constexpr FieldDesc kSampleOption[] = {F("sampleType", T::U32), F("sampleQuality", T::U32), F("label", T::Str)};
    // gpg::gal::HeadAdapterMode
    constexpr FieldDesc kAdapterMode[] = {F("width", T::U32), F("height", T::U32), F("refreshRate", T::U32)};
    // gpg::gal::Head. The window handles are process-local: only whether one was set is kept.
    constexpr FieldDesc kHead[] = {
      F("hasHandle", T::Bool),
      F("hasWindow", T::Bool),
      F("windowed", T::Bool), // Head::mWindowed as the engine sets it (true = full screen, CScApp.cpp)
      F("width", T::U32),
      F("height", T::U32),
      F("framesPerSecond", T::U32),
      F("antialiasingHigh", T::U32),
      F("antialiasingLow", T::U32),
      F("name", T::Str),
      L("sampleOptions", kSampleOption),
      L("adapterModes", kAdapterMode),
      F("validFormats2", T::U32Array),
      F("validFormats1", T::U32Array),
    };
    // gpg::gal::DeviceContext
    constexpr FieldDesc kDeviceContext[] = {
      F("deviceType", T::I32),
      F("validate", T::Bool),
      F("adapter", T::I32),
      F("vsync", T::Bool),
      F("hwBasedInstancing", T::Bool),
      F("supportsFloat16", T::Bool),
      F("vertexShaderProfile", T::I32),
      F("pixelShaderProfile", T::I32),
      F("maxPrimitiveCount", T::U32),
      F("maxVertexCount", T::U32),
      L("heads", kHead),
    };
    // gpg::gal::TextureContext. `data` is the file image (source 1) the engine handed over.
    constexpr FieldDesc kTextureContext[] = {
      F("source", T::U32),
      F("location", T::Str),
      F("data", T::Blob),
      F("type", T::U32),
      F("usage", T::U32),
      F("format", T::U32),
      F("mipmapLevels", T::U32),
      F("reserved44", T::U32),
      F("width", T::U32),
      F("height", T::U32),
      F("reserved50", T::U32),
    };
    // gpg::gal::EffectMacro
    constexpr FieldDesc kMacro[] = {F("name", T::Str), F("value", T::Str)};
    // gpg::gal::EffectContext. `cachePath` is the file name only (its directory is the recording
    // run's /cachedir); `cacheFile` is the compiled effect the backend reads from that path when
    // useCache is set (an input the call does not carry), so a replay does not need the file.
    constexpr FieldDesc kEffectContext[] = {
      F("sourceType", T::U32),
      F("useCache", T::Bool),
      F("sourcePath", T::Str),
      F("cachePath", T::Str),
      F("source", T::Blob),
      L("macros", kMacro),
      F("cacheFile", T::Blob),
    };
    // gpg::gal::OutputContext, passed in
    constexpr FieldDesc kOutputIn[] = {
      Ref("cubeTarget", O::CubeRenderTarget),
      F("face", T::I32),
      Ref("surface", O::RenderTarget),
      Ref("depthStencil", O::DepthStencilTarget),
    };
    // gpg::gal::OutputContext, handed back by the backend
    constexpr FieldDesc kOutputOut[] = {
      Def("cubeTarget", O::CubeRenderTarget),
      R("face", T::I32),
      Def("surface", O::RenderTarget),
      Def("depthStencil", O::DepthStencilTarget),
    };
    // D3DVIEWPORT9
    constexpr FieldDesc kViewport[] = {
      F("x", T::U32), F("y", T::U32), F("width", T::U32), F("height", T::U32), F("minZ", T::F32), F("maxZ", T::F32),
    };
    constexpr FieldDesc kTechniqueEntry[] = {Def("technique", O::EffectTechnique), R("name", T::Str)};

    // ---- stream ---------------------------------------------------------------------------------

    constexpr FieldDesc kBlob[] = {F("blob", T::U32), F("hashA", T::U64), F("hashB", T::U64), F("data", T::Bytes)};
    constexpr FieldDesc kEnd[] = {
      F("records", T::U64), F("blobs", T::U32), F("blobBytes", T::U64), F("presents", T::U32), F("objects", T::U32),
    };
    constexpr FieldDesc kFpuState[] = {F("x87Control", T::U32), F("mxcsr", T::U32)};
    constexpr FieldDesc kNote[] = {F("text", T::Str)};
    // The device the engine created: what it asked for (`requested`, Device::Create's argument)
    // and what the backend reports after its setup (`actual`, GetDeviceContext()).
    constexpr FieldDesc kDeviceCreate[] = {
      FieldDesc{"device", T::IdDef, O::Device}, // numbered by the recorder, not a backend result
      S("requested", kDeviceContext),
      S("actual", kDeviceContext, true),
    };
    constexpr FieldDesc kDeviceDestroy[] = {Ref("device", O::Device)};
    constexpr FieldDesc kRelease[] = {Ref("object", O::Any)};

    // ---- Device ---------------------------------------------------------------------------------

    constexpr FieldDesc kDev[] = {Ref("device", O::Device)};
    constexpr FieldDesc kDevGetModes[] = {Ref("device", O::Device), F("adapter", T::I32), L("modes", kAdapterMode, true)};
    constexpr FieldDesc kDevGetHeadOutput[] = {
      Ref("device", O::Device), F("head", T::U32), F("constOverload", T::Bool), S("output", kOutputOut, true),
    };
    constexpr FieldDesc kDevGetPipelineState[] = {Ref("device", O::Device), Def("pipelineState", O::PipelineState)};
    constexpr FieldDesc kDevCreateEffect[] = {Ref("device", O::Device), S("context", kEffectContext), Def("effect", O::Effect)};
    constexpr FieldDesc kDevCreateTexture[] = {
      Ref("device", O::Device), S("context", kTextureContext), Def("texture", O::Texture), S("created", kTextureContext, true),
    };
    constexpr FieldDesc kDevCreateRenderTarget[] = {
      Ref("device", O::Device), F("width", T::U32), F("height", T::U32), F("format", T::U32), Def("target", O::RenderTarget),
    };
    constexpr FieldDesc kDevCreateCube[] = {
      Ref("device", O::Device), F("dimension", T::U32), F("format", T::U32), Def("target", O::CubeRenderTarget),
    };
    constexpr FieldDesc kDevCreateDepth[] = {
      Ref("device", O::Device), F("width", T::U32), F("height", T::U32), F("format", T::U32), F("field0x10", T::Bool),
      Def("target", O::DepthStencilTarget),
    };
    constexpr FieldDesc kDevCreateVertexFormat[] = {
      Ref("device", O::Device), F("formatCode", T::U32), Def("format", O::VertexFormat), R("createdCode", T::U32),
      R("streamStrides", T::U32Array),
    };
    constexpr FieldDesc kDevCreateVertexBuffer[] = {
      Ref("device", O::Device), F("type", T::U32), F("usage", T::U32), F("vertexCount", T::U32), F("stride", T::U32),
      Def("buffer", O::VertexBuffer),
    };
    constexpr FieldDesc kDevCreateIndexBuffer[] = {
      Ref("device", O::Device), F("format", T::U32), F("size", T::U32), F("type", T::U32), Def("buffer", O::IndexBuffer),
    };
    constexpr FieldDesc kDevGetRenderTargetData[] = {
      Ref("device", O::Device), Ref("source", O::RenderTarget), Ref("destination", O::Texture),
    };
    constexpr FieldDesc kDevStretchRect[] = {
      Ref("device", O::Device), Ref("source", O::RenderTarget), Ref("destination", O::RenderTarget),
      F("sourceRect", T::OptRect), F("destinationRect", T::OptRect),
    };
    constexpr FieldDesc kDevUpdateSurface[] = {
      Ref("device", O::Device), Ref("source", O::Texture), Ref("destination", O::Texture),
      F("sourceRect", T::OptRect), F("destinationRect", T::OptRect),
    };
    constexpr FieldDesc kDevSaveCube[] = {Ref("device", O::Device), Ref("target", O::CubeRenderTarget), F("path", T::Str)};
    constexpr FieldDesc kDevSaveRenderTarget[] = {
      Ref("device", O::Device), Ref("target", O::RenderTarget), F("path", T::Str), F("fileFormat", T::I32),
    };
    constexpr FieldDesc kDevSaveTexture[] = {
      Ref("device", O::Device), Ref("texture", O::Texture), F("path", T::Str), F("fileFormat", T::I32),
      F("toBuffer", T::Bool), R("buffer", T::Blob),
    };
    constexpr FieldDesc kDevGetTexture2D[] = {
      Ref("device", O::Device), F("source", T::Blob), R("data", T::Blob), R("width", T::U32), R("height", T::I32),
    };
    constexpr FieldDesc kDevResetWithContext[] = {Ref("device", O::Device), S("context", kDeviceContext)};
    constexpr FieldDesc kDevIntResult[] = {Ref("device", O::Device), R("result", T::I32)};
    constexpr FieldDesc kDevSetCursor[] = {
      Ref("device", O::Device), F("hotspotX", T::I32), F("hotspotY", T::I32), Ref("texture", O::Texture),
    };
    constexpr FieldDesc kDevShowCursor[] = {Ref("device", O::Device), F("show", T::Bool), R("result", T::I32)};
    constexpr FieldDesc kDevSetViewport[] = {Ref("device", O::Device), S("viewport", kViewport)};
    constexpr FieldDesc kDevGetViewport[] = {Ref("device", O::Device), S("viewport", kViewport, true)};
    constexpr FieldDesc kDevClearTarget[] = {Ref("device", O::Device), S("output", kOutputIn)};
    constexpr FieldDesc kDevGetContext[] = {Ref("device", O::Device), S("output", kOutputOut, true)};
    constexpr FieldDesc kDevClear[] = {
      Ref("device", O::Device), F("clearTarget", T::Bool), F("clearZbuffer", T::Bool), F("clearStencil", T::Bool),
      F("color", T::U32), F("depth", T::F32), F("stencil", T::I32),
    };
    constexpr FieldDesc kDevSetVertexDeclaration[] = {Ref("device", O::Device), Ref("format", O::VertexFormat)};
    constexpr FieldDesc kDevSetVertexBuffer[] = {
      Ref("device", O::Device), F("stream", T::U32), Ref("buffer", O::VertexBuffer), F("frequency", T::I32),
      F("startVertex", T::I32),
    };
    constexpr FieldDesc kDevSetBufferIndices[] = {Ref("device", O::Device), Ref("buffer", O::IndexBuffer)};
    constexpr FieldDesc kDevSetFogState[] = {
      Ref("device", O::Device), F("enable", T::Bool), F("projection", T::OptMatrix), F("fogStart", T::F32),
      F("fogEnd", T::F32), F("fogColor", T::I32),
    };
    constexpr FieldDesc kDevSetWireframe[] = {Ref("device", O::Device), F("enabled", T::Bool)};
    constexpr FieldDesc kDevSetColorWrite[] = {Ref("device", O::Device), F("writeColor", T::Bool), F("writeAlpha", T::Bool)};
    constexpr FieldDesc kDevDrawIndexed[] = {
      Ref("device", O::Device), F("topology", T::U32), F("minVertexIndex", T::U32), F("vertexCount", T::U32),
      F("indexCount", T::U32), F("startIndex", T::U32), F("baseVertexIndex", T::I32),
    };
    constexpr FieldDesc kDevDraw[] = {
      Ref("device", O::Device), F("topology", T::U32), F("vertexCount", T::U32), F("startVertex", T::U32),
    };

    // ---- resources ------------------------------------------------------------------------------

    constexpr FieldDesc kTexGetContext[] = {Ref("texture", O::Texture), S("context", kTextureContext, true)};
    // The packed region (rowBytes x rows) is the recorder's measure of the locked rectangle; the
    // backend's pitch is not kept (it differs between backends).
    constexpr FieldDesc kTexLock[] = {
      Ref("texture", O::Texture), F("level", T::I32), F("rect", T::Rect), F("flags", T::I32),
      R("rowBytes", T::U32), R("rows", T::U32),
    };
    constexpr FieldDesc kTexUnlockRect[] = {
      Ref("texture", O::Texture), F("flags", T::I32), F("level", T::I32), F("content", T::U8), F("data", T::Blob),
    };
    constexpr FieldDesc kTexUnlockLevel[] = {
      Ref("texture", O::Texture), F("level", T::I32), F("content", T::U8), F("data", T::Blob),
    };
    constexpr FieldDesc kTexSaveToBuffer[] = {Ref("texture", O::Texture), R("buffer", T::Blob)};

    constexpr FieldDesc kVbGetContext[] = {
      Ref("buffer", O::VertexBuffer), R("type", T::U32), R("usage", T::U32), R("vertexCount", T::U32), R("stride", T::U32),
    };
    constexpr FieldDesc kVbLock[] = {
      Ref("buffer", O::VertexBuffer), F("offset", T::U32), F("size", T::U32), F("flags", T::U32), R("bytes", T::U32),
    };
    constexpr FieldDesc kVbUnlock[] = {Ref("buffer", O::VertexBuffer), F("data", T::Blob)};
    constexpr FieldDesc kIbGetContext[] = {
      Ref("buffer", O::IndexBuffer), R("format", T::U32), R("size", T::U32), R("type", T::U32),
    };
    constexpr FieldDesc kIbLock[] = {
      Ref("buffer", O::IndexBuffer), F("offset", T::U32), F("size", T::U32), F("flags", T::U32), R("bytes", T::U32),
    };
    constexpr FieldDesc kIbUnlock[] = {Ref("buffer", O::IndexBuffer), F("data", T::Blob)};

    constexpr FieldDesc kRtGetContext[] = {
      Ref("target", O::RenderTarget), R("width", T::U32), R("height", T::U32), R("format", T::U32),
    };
    constexpr FieldDesc kRtGetDC[] = {Ref("target", O::RenderTarget)};
    constexpr FieldDesc kCubeGetContext[] = {Ref("target", O::CubeRenderTarget), R("dimension", T::U32), R("format", T::U32)};
    constexpr FieldDesc kDsGetContext[] = {
      Ref("target", O::DepthStencilTarget), R("width", T::U32), R("height", T::U32), R("format", T::U32),
      R("field0x10", T::Bool),
    };

    constexpr FieldDesc kEffSelf[] = {Ref("effect", O::Effect)};
    constexpr FieldDesc kEffGetTechniques[] = {Ref("effect", O::Effect), L("techniques", kTechniqueEntry, true)};
    constexpr FieldDesc kEffGetVariable[] = {Ref("effect", O::Effect), F("name", T::Str), Def("variable", O::EffectVariable)};
    constexpr FieldDesc kEffGetTechnique[] = {Ref("effect", O::Effect), F("name", T::Str), Def("technique", O::EffectTechnique)};

    constexpr FieldDesc kTechSelf[] = {Ref("technique", O::EffectTechnique)};
    constexpr FieldDesc kTechGetName[] = {Ref("technique", O::EffectTechnique), R("name", T::Str)};
    constexpr FieldDesc kTechBegin[] = {Ref("technique", O::EffectTechnique), R("passes", T::I32)};
    constexpr FieldDesc kTechBeginPass[] = {Ref("technique", O::EffectTechnique), F("pass", T::I32)};
    constexpr FieldDesc kTechAnnBool[] = {
      Ref("technique", O::EffectTechnique), F("name", T::Str), R("found", T::Bool), R("value", T::Bool),
    };
    constexpr FieldDesc kTechAnnInt[] = {
      Ref("technique", O::EffectTechnique), F("name", T::Str), R("found", T::Bool), R("value", T::I32),
    };
    constexpr FieldDesc kTechAnnFloat[] = {
      Ref("technique", O::EffectTechnique), F("name", T::Str), R("found", T::Bool), R("value", T::F32),
    };
    constexpr FieldDesc kTechAnnString[] = {
      Ref("technique", O::EffectTechnique), F("name", T::Str), R("found", T::Bool), R("value", T::Str),
    };

    constexpr FieldDesc kVarGetName[] = {Ref("variable", O::EffectVariable), R("name", T::Str)};
    constexpr FieldDesc kVarSetCube[] = {Ref("variable", O::EffectVariable), Ref("target", O::CubeRenderTarget)};
    constexpr FieldDesc kVarSetRt[] = {Ref("variable", O::EffectVariable), Ref("target", O::RenderTarget)};
    constexpr FieldDesc kVarSetTexture[] = {Ref("variable", O::EffectVariable), Ref("texture", O::Texture)};
    constexpr FieldDesc kVarSetMatrix[] = {Ref("variable", O::EffectVariable), F("matrix", T::OptMatrix)};
    constexpr FieldDesc kVarSetFloatArray[] = {Ref("variable", O::EffectVariable), F("values", T::F32Array)};
    constexpr FieldDesc kVarSetVector[] = {Ref("variable", O::EffectVariable), F("vector", T::Vec4)};
    constexpr FieldDesc kVarSetValue[] = {Ref("variable", O::EffectVariable), F("data", T::Bytes)};
    constexpr FieldDesc kVarSetFloat[] = {Ref("variable", O::EffectVariable), F("value", T::F32)};
    constexpr FieldDesc kVarSetInt[] = {Ref("variable", O::EffectVariable), F("value", T::I32)};
    constexpr FieldDesc kVarSetBool[] = {Ref("variable", O::EffectVariable), F("value", T::Bool)};
    constexpr FieldDesc kVarSetMatrixArray[] = {Ref("variable", O::EffectVariable), F("count", T::U32), F("values", T::F32Array)};
    constexpr FieldDesc kVarSetVectorArray[] = {Ref("variable", O::EffectVariable), F("count", T::U32), F("values", T::F32Array)};
    constexpr FieldDesc kVarAnnBool[] = {
      Ref("variable", O::EffectVariable), F("name", T::Str), R("found", T::Bool), R("value", T::Bool),
    };
    constexpr FieldDesc kVarAnnInt[] = {
      Ref("variable", O::EffectVariable), F("name", T::Str), R("found", T::Bool), R("value", T::I32),
    };
    constexpr FieldDesc kVarAnnFloat[] = {
      Ref("variable", O::EffectVariable), F("name", T::Str), R("found", T::Bool), R("value", T::F32),
    };
    constexpr FieldDesc kVarAnnString[] = {
      Ref("variable", O::EffectVariable), F("name", T::Str), R("found", T::Bool), R("value", T::Str),
    };

#define GALTRACE_OP(op, fields) OpDesc{Op::op, #op, fields, static_cast<std::uint8_t>(std::size(fields))}

    const OpDesc kOps[] = {
      GALTRACE_OP(Blob, kBlob),
      GALTRACE_OP(End, kEnd),
      GALTRACE_OP(FpuState, kFpuState),
      GALTRACE_OP(Note, kNote),
      GALTRACE_OP(DeviceCreate, kDeviceCreate),
      GALTRACE_OP(DeviceDestroy, kDeviceDestroy),
      GALTRACE_OP(Release, kRelease),

      GALTRACE_OP(DevGetLog, kDev),
      GALTRACE_OP(DevGetDeviceContext, kDev),
      GALTRACE_OP(DevGetCurThreadId, kDev),
      GALTRACE_OP(DevFunc1, kDev),
      GALTRACE_OP(DevGetModesForAdapter, kDevGetModes),
      GALTRACE_OP(DevGetHeadOutputContext, kDevGetHeadOutput),
      GALTRACE_OP(DevGetPipelineState, kDevGetPipelineState),
      GALTRACE_OP(DevCreateEffect, kDevCreateEffect),
      GALTRACE_OP(DevCreateTexture, kDevCreateTexture),
      GALTRACE_OP(DevCreateRenderTarget, kDevCreateRenderTarget),
      GALTRACE_OP(DevCreateCubeRenderTarget, kDevCreateCube),
      GALTRACE_OP(DevCreateDepthStencilTarget, kDevCreateDepth),
      GALTRACE_OP(DevCreateVertexFormat, kDevCreateVertexFormat),
      GALTRACE_OP(DevCreateVertexBuffer, kDevCreateVertexBuffer),
      GALTRACE_OP(DevCreateIndexBuffer, kDevCreateIndexBuffer),
      GALTRACE_OP(DevGetRenderTargetData, kDevGetRenderTargetData),
      GALTRACE_OP(DevStretchRect, kDevStretchRect),
      GALTRACE_OP(DevUpdateSurface, kDevUpdateSurface),
      GALTRACE_OP(DevSaveCubeRenderTarget, kDevSaveCube),
      GALTRACE_OP(DevSaveRenderTarget, kDevSaveRenderTarget),
      GALTRACE_OP(DevSaveTexture, kDevSaveTexture),
      GALTRACE_OP(DevGetTexture2D, kDevGetTexture2D),
      GALTRACE_OP(DevFunc7, kDev),
      GALTRACE_OP(DevResetWithContext, kDevResetWithContext),
      GALTRACE_OP(DevReset, kDev),
      GALTRACE_OP(DevTestCooperativeLevel, kDevIntResult),
      GALTRACE_OP(DevBeginScene, kDev),
      GALTRACE_OP(DevEndScene, kDev),
      GALTRACE_OP(DevPresent, kDev),
      GALTRACE_OP(DevSetCursor, kDevSetCursor),
      GALTRACE_OP(DevInitCursor, kDev),
      GALTRACE_OP(DevShowCursor, kDevShowCursor),
      GALTRACE_OP(DevSetViewport, kDevSetViewport),
      GALTRACE_OP(DevGetViewport, kDevGetViewport),
      GALTRACE_OP(DevClearTarget, kDevClearTarget),
      GALTRACE_OP(DevGetContext, kDevGetContext),
      GALTRACE_OP(DevClear, kDevClear),
      GALTRACE_OP(DevClearTextures, kDev),
      GALTRACE_OP(DevSetVertexDeclaration, kDevSetVertexDeclaration),
      GALTRACE_OP(DevSetVertexBuffer, kDevSetVertexBuffer),
      GALTRACE_OP(DevSetBufferIndices, kDevSetBufferIndices),
      GALTRACE_OP(DevSetFogState, kDevSetFogState),
      GALTRACE_OP(DevSetWireframeState, kDevSetWireframe),
      GALTRACE_OP(DevSetColorWriteState, kDevSetColorWrite),
      GALTRACE_OP(DevDrawIndexedPrimitive, kDevDrawIndexed),
      GALTRACE_OP(DevDrawPrimitive, kDevDraw),
      GALTRACE_OP(DevBeginTechnique, kDev),
      GALTRACE_OP(DevEndTechnique, kDev),

      GALTRACE_OP(TexGetContext, kTexGetContext),
      GALTRACE_OP(TexLock, kTexLock),
      GALTRACE_OP(TexUnlockRect, kTexUnlockRect),
      GALTRACE_OP(TexUnlockLevel, kTexUnlockLevel),
      GALTRACE_OP(TexSaveToBuffer, kTexSaveToBuffer),

      GALTRACE_OP(VbGetContext, kVbGetContext),
      GALTRACE_OP(VbLock, kVbLock),
      GALTRACE_OP(VbUnlock, kVbUnlock),
      GALTRACE_OP(IbGetContext, kIbGetContext),
      GALTRACE_OP(IbLock, kIbLock),
      GALTRACE_OP(IbUnlock, kIbUnlock),

      GALTRACE_OP(RtGetContext, kRtGetContext),
      GALTRACE_OP(RtGetDC, kRtGetDC),
      GALTRACE_OP(CubeGetContext, kCubeGetContext),
      GALTRACE_OP(DsGetContext, kDsGetContext),

      GALTRACE_OP(EffGetContext, kEffSelf),
      GALTRACE_OP(EffGetTechniques, kEffGetTechniques),
      GALTRACE_OP(EffGetVariable, kEffGetVariable),
      GALTRACE_OP(EffGetTechnique, kEffGetTechnique),
      GALTRACE_OP(EffOnReset, kEffSelf),
      GALTRACE_OP(EffOnLost, kEffSelf),

      GALTRACE_OP(TechGetName, kTechGetName),
      GALTRACE_OP(TechBegin, kTechBegin),
      GALTRACE_OP(TechEnd, kTechSelf),
      GALTRACE_OP(TechBeginPass, kTechBeginPass),
      GALTRACE_OP(TechEndPass, kTechSelf),
      GALTRACE_OP(TechGetAnnotationBool, kTechAnnBool),
      GALTRACE_OP(TechGetAnnotationInt, kTechAnnInt),
      GALTRACE_OP(TechGetAnnotationFloat, kTechAnnFloat),
      GALTRACE_OP(TechGetAnnotationString, kTechAnnString),

      GALTRACE_OP(VarGetName, kVarGetName),
      GALTRACE_OP(VarSetCubeRenderTarget, kVarSetCube),
      GALTRACE_OP(VarSetRenderTarget, kVarSetRt),
      GALTRACE_OP(VarSetTexture, kVarSetTexture),
      GALTRACE_OP(VarSetMatrix4x4, kVarSetMatrix),
      GALTRACE_OP(VarSetFloatArray, kVarSetFloatArray),
      GALTRACE_OP(VarSetVector, kVarSetVector),
      GALTRACE_OP(VarSetValue, kVarSetValue),
      GALTRACE_OP(VarSetFloat, kVarSetFloat),
      GALTRACE_OP(VarSetInt, kVarSetInt),
      GALTRACE_OP(VarSetBool, kVarSetBool),
      GALTRACE_OP(VarSetMatrixArray, kVarSetMatrixArray),
      GALTRACE_OP(VarSetVectorArray, kVarSetVectorArray),
      GALTRACE_OP(VarGetAnnotationBool, kVarAnnBool),
      GALTRACE_OP(VarGetAnnotationInt, kVarAnnInt),
      GALTRACE_OP(VarGetAnnotationFloat, kVarAnnFloat),
      GALTRACE_OP(VarGetAnnotationString, kVarAnnString),
    };

#undef GALTRACE_OP
  } // namespace

  const char* ObjectTypeName(const ObjectType type)
  {
    switch (type) {
      case ObjectType::None: return "none";
      case ObjectType::Device: return "Device";
      case ObjectType::Texture: return "Texture";
      case ObjectType::RenderTarget: return "RenderTarget";
      case ObjectType::CubeRenderTarget: return "CubeRenderTarget";
      case ObjectType::DepthStencilTarget: return "DepthStencilTarget";
      case ObjectType::VertexFormat: return "VertexFormat";
      case ObjectType::VertexBuffer: return "VertexBuffer";
      case ObjectType::IndexBuffer: return "IndexBuffer";
      case ObjectType::Effect: return "Effect";
      case ObjectType::EffectTechnique: return "EffectTechnique";
      case ObjectType::EffectVariable: return "EffectVariable";
      case ObjectType::PipelineState: return "PipelineState";
      case ObjectType::Any: return "any";
    }
    return "?";
  }

  const std::vector<const OpDesc*>& AllOps()
  {
    static const std::vector<const OpDesc*> ops = [] {
      std::vector<const OpDesc*> list;
      for (const OpDesc& desc : kOps) {
        list.push_back(&desc);
      }
      std::sort(list.begin(), list.end(), [](const OpDesc* a, const OpDesc* b) {
        return static_cast<std::uint16_t>(a->op) < static_cast<std::uint16_t>(b->op);
      });
      return list;
    }();
    return ops;
  }

  const OpDesc* FindOp(const std::uint16_t op)
  {
    const std::vector<const OpDesc*>& ops = AllOps();
    const auto it = std::lower_bound(ops.begin(), ops.end(), op, [](const OpDesc* desc, const std::uint16_t value) {
      return static_cast<std::uint16_t>(desc->op) < value;
    });
    return (it != ops.end() && static_cast<std::uint16_t>((*it)->op) == op) ? *it : nullptr;
  }

  const OpDesc* FindOp(const Op op)
  {
    return FindOp(static_cast<std::uint16_t>(op));
  }

  bool GalTextureFormatLayout(const std::uint32_t galFormat, TexelLayout* const out)
  {
    TexelLayout layout{};
    switch (galFormat) {
      case 1: layout.bytesPerBlock = 3; break;  // R8G8B8
      case 2: layout.bytesPerBlock = 4; break;  // A8R8G8B8
      case 3: layout.bytesPerBlock = 4; break;  // X8R8G8B8
      case 4: layout.bytesPerBlock = 2; break;  // R5G6B5
      case 5: layout.bytesPerBlock = 1; break;  // A8
      case 6: layout.bytesPerBlock = 1; break;  // L8
      case 7: layout.bytesPerBlock = 2; break;  // A8L8
      case 8:                                   // DXT1
        layout = TexelLayout{4, 4, 8};
        break;
      case 9:  // DXT2
      case 10: // DXT3
      case 11: // DXT4
      case 12: // DXT5
        layout = TexelLayout{4, 4, 16};
        break;
      case 13: layout.bytesPerBlock = 2; break;  // R16F
      case 14: layout.bytesPerBlock = 4; break;  // G16R16F
      case 15: layout.bytesPerBlock = 8; break;  // A16B16G16R16F
      case 16: layout.bytesPerBlock = 4; break;  // R32F
      case 17: layout.bytesPerBlock = 8; break;  // G32R32F
      case 18: layout.bytesPerBlock = 16; break; // A32B32G32R32F
      default: return false;                     // 0, 20 ("other": X1R5G5B5, A4R4G4B4, ...) and unknown
    }
    if (out != nullptr) {
      *out = layout;
    }
    return true;
  }

  LockRegion TextureLockRegion(
    const std::uint32_t galFormat, const std::uint32_t levelWidth, const std::uint32_t levelHeight, const std::int32_t left,
    const std::int32_t top, const std::int32_t right, const std::int32_t bottom
  )
  {
    LockRegion region{};
    TexelLayout layout{};
    if (!GalTextureFormatLayout(galFormat, &layout)) {
      return region;
    }
    std::uint32_t width = levelWidth;
    std::uint32_t height = levelHeight;
    if (left != right) { // TextureD3D9::Lock: an empty rect (left == right) is the whole level
      if (right < left || bottom < top) {
        return region;
      }
      width = static_cast<std::uint32_t>(right - left);
      height = static_cast<std::uint32_t>(bottom - top);
    }
    const std::uint32_t blocksWide = (width + layout.blockWidth - 1) / layout.blockWidth;
    const std::uint32_t blocksHigh = (height + layout.blockHeight - 1) / layout.blockHeight;
    region.rowBytes = blocksWide * layout.bytesPerBlock;
    region.rows = blocksHigh;
    region.known = true;
    return region;
  }

  std::uint64_t Fnv1a64(const void* const data, const std::size_t size, std::uint64_t hash)
  {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t index = 0; index < size; ++index) {
      hash = (hash ^ bytes[index]) * 0x100000001B3ULL;
    }
    return hash;
  }

  BlobKey HashBlob(const void* const data, const std::size_t size)
  {
    BlobKey key{};
    key.a = Fnv1a64(data, size);
    // A second, independent hash: 8-byte words through a multiply-xorshift mix (SplitMix64's
    // finaliser), so equal FNV values with different content are not taken as one blob.
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::uint64_t hash = 0x9E3779B97F4A7C15ULL ^ static_cast<std::uint64_t>(size);
    std::size_t index = 0;
    for (; index + 8 <= size; index += 8) {
      std::uint64_t word = 0;
      for (int byte = 7; byte >= 0; --byte) {
        word = (word << 8) | bytes[index + static_cast<std::size_t>(byte)];
      }
      hash ^= word;
      hash *= 0xBF58476D1CE4E5B9ULL;
      hash ^= hash >> 31;
    }
    std::uint64_t tail = 0;
    for (std::size_t byte = 0; index + byte < size; ++byte) {
      tail |= static_cast<std::uint64_t>(bytes[index + byte]) << (8 * byte);
    }
    hash ^= tail;
    hash *= 0x94D049BB133111EBULL;
    hash ^= hash >> 29;
    key.b = hash;
    key.size = static_cast<std::uint32_t>(size);
    return key;
  }

  std::string Hex64(const std::uint64_t value)
  {
    char text[17];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(value));
    return text;
  }
} // namespace galtrace
