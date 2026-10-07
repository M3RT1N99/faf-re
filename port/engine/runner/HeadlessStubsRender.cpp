// The renderer (gpg/gal/Device.cpp, moho/mesh/Mesh.cpp, moho/render/d3d/*, the render-side console
// variables) for the Android headless runner; see HeadlessStubs.h.
//
// The runner state these follow (moho/app/HeadlessReplay.cpp, Run): CreateDevice and REN_Init never
// run, so `gpg::gal::Device`'s instance (Device.cpp:129, `sDeviceD3D`) stays empty, and no
// MeshRenderer, MeshInstance, camera, viewport or frame is ever created; no console command runs.
//
// What the runner does use of the renderer is not stood in for here (M3b integration): the spatial
// database is the engine's own code (MeshSpatialDb.cpp); the decal manager, decals and decal groups
// (CWldSplat.cpp, CWldTerrainDecal.cpp, CDecalGroup.cpp), which read the .scmap decal section, are
// linked for real; gpg::gal::Math::mul (Matrix.cpp, sim-side effects) is ported. The texture
// resource class RD3DTextureResource has its own file (HeadlessStubsTexture.cpp, M3c).
//
// D3D_GetDevice (CD3DDevice.cpp:1935) returns the device singleton, so it cannot be stood in for.
// The Windows runner reaches it only to create texture resources while it loads the map
// (RWldMapPreviewChunk::Load, CWldTerrainRes::Load/SetBackground/SetSkycube/AddEnvLookup,
// CAnimTexture::LoadFramesFromBaseName, recorded with an instrumented Debug|Win32 build on T1-T3),
// in UnitWeapon's DoInstaHit binder, and for the viewport in Sim_Create_exxt (null there; that
// block is Windows-only, StartupHelpers.cpp). Those call sites are port seams (no D3D9 device off
// Windows, the textures stay empty). Every other reference is renderer code the Windows runner
// never runs, so the definition below is a trap that ends the run, not a stand-in.

#include "HeadlessStubs.h"

#include "gpg/gal/Device.hpp"
#include "gpg/gal/DeviceContext.hpp"
#include "gpg/gal/Error.hpp"
#include "gpg/gal/Head.hpp"
#include "gpg/gal/MeshFormatter.h"
#include "moho/mesh/Mesh.h"
#include "moho/mesh/SpatialDb.h"
#include "moho/render/MapImager.h"
#include "moho/render/SelectionBracketRenderer.h"
#include "moho/render/d3d/CD3DFont.h"
#include "moho/render/d3d/CD3DPrimBatcher.h"
#include "moho/render/d3d/ShaderVar.h"
#include "moho/terrain/water/Shoreline.h"

namespace moho
{
  class CD3DDevice;
  CD3DDevice* D3D_GetDevice();

  // CD3DDevice.cpp:1935; see the note at the top of this file.
  CD3DDevice* D3D_GetDevice()
  {
    FAF_RUNNER_TRAP("D3D_GetDevice (the D3D9 device; renderer code the Windows runner never runs)");
  }
} // namespace moho

namespace gpg::gal
{
  // Device.cpp:127: `sDeviceD3D.get()`, empty without CreateDevice.
  Device* Device::GetInstance()
  {
    FAF_RUNNER_STUB("gpg::gal::Device::GetInstance");
    return nullptr;
  }

  // Device.cpp:138: `sDeviceD3D.get() != nullptr`, false without CreateDevice. Callers: the batch
  // texture factory (SBatchTextureDataFactory.cpp, which then loads nothing) and RD3DTextureResource's
  // lazy texture creation (through CD3DDevice::GetGalDevice on Windows).
  bool Device::IsReady()
  {
    FAF_RUNNER_STUB("gpg::gal::Device::IsReady");
    return false;
  }

  // Device.cpp:198: false when Device::GetInstance() is null.
  bool SupportsVertexTextureFormat(const std::uint32_t textureFormat)
  {
    (void)textureFormat;
    FAF_RUNNER_STUB("gpg::gal::SupportsVertexTextureFormat");
    return false;
  }

  // Device.cpp:58, the same body (a member-wise copy).
  HeadSampleOption::HeadSampleOption(const HeadSampleOption& other)
    : sampleType(other.sampleType),
      sampleQuality(other.sampleQuality),
      label(other.label)
  {
    FAF_RUNNER_STUB("gpg::gal::HeadSampleOption::HeadSampleOption(copy)");
  }

  // Device.cpp:275, :286, :305, the same bodies (with ThrowDeviceContextError, Device.cpp:23, and
  // CountHeadVectorEntries, Device.cpp:38, inlined): plain accessors of a device context's head list.
  // A context exists only with a device; their callers in StartupHelpers.cpp (the graphics options)
  // first ask Device::GetInstance(), which is null in the runner.
  int DeviceContext::GetHeadCount() const
  {
    FAF_RUNNER_STUB("gpg::gal::DeviceContext::GetHeadCount");
    const Head* const start = mHeads.begin();
    if (start == nullptr) {
      return 0;
    }
    return static_cast<int>(mHeads.end() - start);
  }

  const Head& DeviceContext::GetHead(const std::uint32_t index) const
  {
    FAF_RUNNER_STUB("gpg::gal::DeviceContext::GetHead const");
    const Head* const start = mHeads.begin();
    const Head* const finish = mHeads.end();
    const std::uint32_t count = (start == nullptr) ? 0U : static_cast<std::uint32_t>(finish - start);
    if ((start == nullptr) || (index >= count)) {
      throw Error(msvc8::string("c:\\work\\rts\\main\\code\\src\\libs\\gpggal\\Device.cpp"), 91, msvc8::string("invalid head index"));
    }
    return start[index];
  }

  Head& DeviceContext::GetHead(const std::uint32_t index)
  {
    FAF_RUNNER_STUB("gpg::gal::DeviceContext::GetHead");
    Head* const start = mHeads.begin();
    const Head* const finish = mHeads.end();
    const std::uint32_t count = (start == nullptr) ? 0U : static_cast<std::uint32_t>(finish - start);
    if ((start == nullptr) || (index >= count)) {
      throw Error(msvc8::string("c:\\work\\rts\\main\\code\\src\\libs\\gpggal\\Device.cpp"), 97, msvc8::string("invalid head index"));
    }
    return start[index];
  }

  // MeshVertex.cpp:69: clears the renderer's cached vertex formatter. Its caller is a console
  // command (CON_mesh_Rebatch, CConCommand.cpp); with no device there is nothing cached either.
  void ResetHardwareVertexFormatter()
  {
    FAF_RUNNER_STUB("gpg::gal::ResetHardwareVertexFormatter");
  }
} // namespace gpg::gal

namespace moho
{
  // ---------------------------------------------------------------------------------------------
  // Mesh.cpp. The mesh renderer and its instances exist only for drawing: their callers are
  // UserEntity, Clutter, CUIWorldMesh, CameraImpl and IdleUnitSelector (all user side, none of which
  // the runner creates) and console commands.

  // Mesh.cpp:3515: creates the renderer (a function-local static) on first use and returns it, which
  // no stand-in can reproduce, so this is a trap. Its callers draw or create mesh instances for
  // UserEntity/UserUnit, Clutter, CUIWorldMesh, CameraImpl and IdleUnitSelector, or are console
  // commands; the Windows runner reaches none of them.
  MeshRenderer* MeshRenderer::GetInstance()
  {
    FAF_RUNNER_TRAP("MeshRenderer::GetInstance");
  }

  // Mesh.cpp: MeshRenderer::Reset; its caller is a console command (CON_mesh_Rebatch, CConCommand.cpp).
  void MeshRenderer::Reset()
  {
    FAF_RUNNER_STUB("MeshRenderer::Reset");
  }

  MeshInstance* MeshRenderer::CreateMeshInstance(
    const std::int32_t gameTick,
    const std::int32_t color,
    const RMeshBlueprint* const blueprint,
    const Wm3::Vec3f& scale,
    const bool isStaticPose,
    boost::shared_ptr<MeshMaterial> material
  )
  {
    (void)gameTick;
    (void)color;
    (void)blueprint;
    (void)scale;
    (void)isStaticPose;
    (void)material;
    FAF_RUNNER_STUB("MeshRenderer::CreateMeshInstance(blueprint)");
    return nullptr;
  }

  MeshInstance* MeshRenderer::CreateMeshInstance(
    const std::int32_t gameTick,
    const std::int32_t color,
    boost::shared_ptr<RScmResource> resource,
    const Wm3::Vec3f& scale,
    const bool isStaticPose,
    boost::shared_ptr<MeshMaterial> material,
    const float lodCutoff
  )
  {
    (void)gameTick;
    (void)color;
    (void)resource;
    (void)scale;
    (void)isStaticPose;
    (void)material;
    (void)lodCutoff;
    FAF_RUNNER_STUB("MeshRenderer::CreateMeshInstance(resource)");
    return nullptr;
  }

  boost::shared_ptr<MeshMaterial> MeshMaterial::Create(
    const msvc8::string& shaderName,
    const msvc8::string& albedoName,
    const msvc8::string& normalsName,
    const msvc8::string& specularName,
    const msvc8::string& lookupName,
    const msvc8::string& secondaryName,
    CResourceWatcher* const resourceWatcher
  )
  {
    (void)shaderName;
    (void)albedoName;
    (void)normalsName;
    (void)specularName;
    (void)lookupName;
    (void)secondaryName;
    (void)resourceWatcher;
    FAF_RUNNER_STUB("MeshMaterial::Create");
    return {};
  }

  boost::shared_ptr<RScmResource> Mesh::GetResource(const std::int32_t lodIndex) const
  {
    (void)lodIndex;
    FAF_RUNNER_STUB("Mesh::GetResource");
    return {};
  }

  boost::shared_ptr<Mesh> MeshInstance::GetMesh() const
  {
    FAF_RUNNER_STUB("MeshInstance::GetMesh");
    return {};
  }

  void MeshInstance::SetStance(const VTransform& startTransform, const VTransform& endTransform)
  {
    (void)startTransform;
    (void)endTransform;
    FAF_RUNNER_STUB("MeshInstance::SetStance");
  }

  void MeshInstance::SetStance(
    const VTransform& startTransform,
    const VTransform& endTransform,
    const bool forceRefresh,
    boost::shared_ptr<CAniPose> startPoseArg,
    boost::shared_ptr<CAniPose> endPoseArg
  )
  {
    (void)startTransform;
    (void)endTransform;
    (void)forceRefresh;
    (void)startPoseArg;
    (void)endPoseArg;
    FAF_RUNNER_STUB("MeshInstance::SetStance(pose)");
  }

  void MeshInstance::UpdateInterpolatedFields()
  {
    FAF_RUNNER_STUB("MeshInstance::UpdateInterpolatedFields");
  }

  // Mesh.cpp:3121: freezes or releases an instance's pose. Its caller is UserUnit's visibility update
  // (UserUnit.cpp:2135), which needs a UserUnit with a mesh instance; neither exists in the runner (no
  // session, and the stand-ins above create no instance). A trap.
  void MeshInstance::LockPose(const bool lockPose)
  {
    (void)lockPose;
    FAF_RUNNER_TRAP("MeshInstance::LockPose");
  }

  Wm3::AxisAlignedBox3f MeshInstance::GetSweptAlignedBox() const
  {
    FAF_RUNNER_STUB("MeshInstance::GetSweptAlignedBox");
    return Wm3::AxisAlignedBox3f{};
  }

  // The spatial database itself (SpatialDB, SpatialDBEntry, all of Mesh.cpp's explicit
  // instantiations) is the engine's own code, compiled by MeshSpatialDb.cpp.

  // Mesh.cpp:97-109: the mesh renderer's console variables with their initial values (registered by
  // CConCommand.cpp; only the renderer reads them).
  float ren_MeshDissolve = 0.0f;
  float ren_MeshDissolveCutoff = 0.0f;
  bool ren_MeshSkinned = true;
  bool ren_MeshStatic = true;
  float ren_ShadowBias = 0.005f;

  // ---------------------------------------------------------------------------------------------
  // CD3DPrimBatcher.cpp and CD3DFont.cpp: immediate-mode drawing. Callers are TimeBar.cpp,
  // ProjectileArcRenderer.cpp and the debug overlays of Sim.cpp and SimDriver.cpp, all drawn from the
  // frame's render pass, which the runner never runs.

  CD3DPrimBatcher* CD3DPrimBatcher::Setup(const char* const techniqueName)
  {
    (void)techniqueName;
    FAF_RUNNER_STUB("CD3DPrimBatcher::Setup");
    return this;
  }

  void CD3DPrimBatcher::SetProjectionMatrix(const VMatrix4& matrix)
  {
    (void)matrix;
    FAF_RUNNER_STUB("CD3DPrimBatcher::SetProjectionMatrix");
  }

  void CD3DPrimBatcher::SetViewMatrix(const VMatrix4& matrix)
  {
    (void)matrix;
    FAF_RUNNER_STUB("CD3DPrimBatcher::SetViewMatrix");
  }

  void CD3DPrimBatcher::SetTexture(const boost::shared_ptr<CD3DBatchTexture>& texture)
  {
    (void)texture;
    FAF_RUNNER_STUB("CD3DPrimBatcher::SetTexture");
  }

  void CD3DPrimBatcher::DrawQuad(
    const Vertex& topLeft, const Vertex& topRight, const Vertex& bottomRight, const Vertex& bottomLeft
  )
  {
    (void)topLeft;
    (void)topRight;
    (void)bottomRight;
    (void)bottomLeft;
    FAF_RUNNER_STUB("CD3DPrimBatcher::DrawQuad");
  }

  void CD3DPrimBatcher::DrawLine(const Vertex& start, const Vertex& end)
  {
    (void)start;
    (void)end;
    FAF_RUNNER_STUB("CD3DPrimBatcher::DrawLine");
  }

  void CD3DPrimBatcher::Flush()
  {
    FAF_RUNNER_STUB("CD3DPrimBatcher::Flush");
  }

  boost::SharedPtrRaw<CD3DFont> CD3DFont::Create(const std::int32_t pointSize, const gpg::StrArg faceName)
  {
    (void)pointSize;
    (void)faceName;
    FAF_RUNNER_STUB("CD3DFont::Create");
    return {};
  }

  float CD3DFont::GetAdvance(const gpg::StrArg text, const std::int32_t flags)
  {
    (void)text;
    (void)flags;
    FAF_RUNNER_STUB("CD3DFont::GetAdvance");
    return 0.0f;
  }

  Vector3f CD3DFont::Render(
    const gpg::StrArg text,
    CD3DPrimBatcher* const primBatcher,
    const Vector3f& origin,
    const Vector3f& xAxis,
    const Vector3f& yAxis,
    const std::uint32_t color,
    const float glyphScale,
    const float maxAdvance
  )
  {
    (void)text;
    (void)primBatcher;
    (void)xAxis;
    (void)yAxis;
    (void)color;
    (void)glyphScale;
    (void)maxAdvance;
    FAF_RUNNER_STUB("CD3DFont::Render");
    return origin;
  }

  // ---------------------------------------------------------------------------------------------
  // ShaderVar.cpp. The particle and beam renderers declare static shader variables and register them
  // in static initialisers (CWorldParticles.cpp:227-267, BeamRenderHelpers.cpp:148-196), so
  // RegisterShaderVar and ~ShaderVar run in every process; both keep the real bodies. A variable is
  // linked to an effect only by Exists() (ShaderVar.cpp:234), which only drawing calls, so in the
  // runner no variable is ever linked and RelinkShaderVarEffect(*this, nullptr) in the destructor has
  // nothing to do.

  // ShaderVar.cpp:148, the same body.
  ShaderVar* RegisterShaderVar(const char* const variableName, ShaderVar* const shaderVar, const char* const effectFileName)
  {
    FAF_RUNNER_STUB("RegisterShaderVar");
    if (shaderVar == nullptr) {
      return nullptr;
    }

    const char* const safeVariableName = (variableName != nullptr) ? variableName : "";
    const char* const safeEffectFileName = (effectFileName != nullptr) ? effectFileName : "";

    shaderVar->mVariableName.tidy(true, 0U);
    shaderVar->mVariableName.assign_owned(safeVariableName);

    shaderVar->mEffectFileName.tidy(true, 0U);
    shaderVar->mEffectFileName.assign_owned(safeEffectFileName);

    shaderVar->mEffectLink.mLinkLane = nullptr;
    shaderVar->mEffectLink.mNext = nullptr;
    shaderVar->mEffectVariable.reset();
    return shaderVar;
  }

  // ShaderVar.cpp:349 without the effect unlink (see above).
  ShaderVar::~ShaderVar()
  {
    FAF_RUNNER_STUB("ShaderVar::~ShaderVar");
    mEffectVariable.reset();
    mEffectFileName.tidy(true, 0U);
    mVariableName.tidy(true, 0U);
  }

  // ShaderVar.cpp:234, :269, :299: drawing-time lookups through the device's effects.
  bool ShaderVar::Exists()
  {
    FAF_RUNNER_STUB("ShaderVar::Exists");
    return false;
  }

  ShaderVar* ShaderVar::GetTexture(const boost::shared_ptr<ID3DTextureSheet>& textureSheet)
  {
    (void)textureSheet;
    FAF_RUNNER_STUB("ShaderVar::GetTexture");
    return this;
  }

  ShaderVar* ShaderVar::SetRenderTargetTexture(const boost::shared_ptr<ID3DRenderTarget>& renderTarget)
  {
    (void)renderTarget;
    FAF_RUNNER_STUB("ShaderVar::SetRenderTargetTexture");
    return this;
  }

  // ---------------------------------------------------------------------------------------------
  // Console commands and console variables of the remaining render TUs (CConCommand.cpp registers
  // them; the runner executes no console command, and only drawing reads the variables).

  // WxRuntimeTypes.cpp:1852 (toggles skeleton display and tells WLD_GetDriver(), null in the runner).
  void REN_ShowSkeletons()
  {
    FAF_RUNNER_STUB("REN_ShowSkeletons");
  }

  // MapImager.cpp: map-border markers for the map imager window.
  void REN_MapBorderAdd(const msvc8::vector<msvc8::string>& args)
  {
    (void)args;
    FAF_RUNNER_STUB("REN_MapBorderAdd");
  }

  void REN_MapBorderClear(const msvc8::vector<msvc8::string>& args)
  {
    (void)args;
    FAF_RUNNER_STUB("REN_MapBorderClear");
  }

  // (SelectionBracketParams.cpp, which defines the ren_Select* bracket variables, is linked for real
  // since M3c.)

  // SelectionBracketRenderer.cpp:40.
  bool ren_SelectBoxes = true;

  // CRenFrame.cpp:35-36.
  float ren_BloomBlurKernelScale = 1.5f;
  float ren_BloomGlowCopyScale = 2.0f;

  // Shoreline.cpp:735-736 (Shoreline.cpp:872 sets ren_Shoreline only when a shoreline is built for
  // drawing).
  bool ren_Shoreline = false;
  float ren_ShorelineCutoff = 0.0f;
} // namespace moho
