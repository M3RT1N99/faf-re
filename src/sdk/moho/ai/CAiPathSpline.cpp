#include "moho/ai/CAiPathSpline.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <new>
#include <typeinfo>

#include "gpg/core/containers/ReadArchive.h"
#include "gpg/core/containers/String.h"
#include "gpg/core/containers/WriteArchive.h"
#include "moho/ai/CAiFormationInstance.h"
#include "moho/entity/Entity.h"
#include "moho/math/MathReflection.h"
#include "moho/misc/Stats.h"
#include "moho/sim/CArmyImpl.h"
#include "moho/misc/WeakPtr.h"
#include "moho/render/camera/VTransform.h"
#include "moho/resource/blueprints/RUnitBlueprint.h"
#include "moho/sim/COGrid.h"
#include "moho/sim/SFootprint.h"
#include "moho/sim/SOCellPos.h"
#include "moho/sim/STIMap.h"
#include "moho/sim/Sim.h"
#include "moho/unit/core/IUnit.h"
#include "moho/unit/core/Unit.h"
#include "moho/unit/core/UnitAttributes.h"

using namespace moho;

namespace
{
  using CollisionLinkWeakUnit = WeakPtr<IUnit>;

  static_assert(sizeof(CollisionLinkWeakUnit) == sizeof(SCollisionLink), "SCollisionLink/WeakPtr<IUnit> layout mismatch");
  static_assert(
    offsetof(CollisionLinkWeakUnit, ownerLinkSlot) == offsetof(SCollisionLink, mUnitIntrusiveSlot),
    "SCollisionLink::mUnitIntrusiveSlot owner slot mismatch"
  );
  static_assert(
    offsetof(CollisionLinkWeakUnit, nextInOwner) == offsetof(SCollisionLink, mNextInUnitChain),
    "SCollisionLink::mNextInUnitChain next slot mismatch"
  );

  [[nodiscard]] CollisionLinkWeakUnit& AsCollisionWeakLink(SCollisionLink& link) noexcept
  {
    return *reinterpret_cast<CollisionLinkWeakUnit*>(&link);
  }

  [[nodiscard]] const CollisionLinkWeakUnit& AsCollisionWeakLink(const SCollisionLink& link) noexcept
  {
    return *reinterpret_cast<const CollisionLinkWeakUnit*>(&link);
  }
} // namespace

Unit* SCollisionLink::ResolveUnitFromIntrusiveSlot() const noexcept
{
  if (IUnit* const iunit = AsCollisionWeakLink(*this).GetObjectPtr()) {
    return iunit->IsUnit();
  }
  return nullptr;
}

void** SCollisionLink::GetIntrusiveSlotAddress() const noexcept
{
  return reinterpret_cast<void**>(AsCollisionWeakLink(*this).ownerLinkSlot);
}

bool SCollisionLink::HasLinkedUnit() const noexcept
{
  return AsCollisionWeakLink(*this).HasValue();
}

void SCollisionLink::AssignUnit(Unit* const unit) noexcept
{
  AsCollisionWeakLink(*this).ResetFromObject(static_cast<IUnit*>(unit));
}

void SCollisionLink::ClearLink() noexcept
{
  AsCollisionWeakLink(*this).ResetFromOwnerLinkSlot(nullptr);
}

namespace
{
  void RemoveFromIntrusiveCollisionChain(SCollisionInfo& info)
  {
    auto& weakLink = AsCollisionWeakLink(info.mUnit);
    weakLink.UnlinkFromOwnerChain();
    weakLink.ClearLinkState();
  }

  [[nodiscard]] Wm3::Vector3f UnitForwardXZ(const Unit* unit) noexcept
  {
    if (!unit) {
      return {0.0f, 0.0f, 1.0f};
    }

    const Wm3::Vector3f forward = unit->GetTransform().orient_.Rotate(Wm3::Vector3f{0.0f, 0.0f, 1.0f});
    Wm3::Vector3f flat{forward.x, 0.0f, forward.z};
    const Wm3::Vector3f normalized = Wm3::Vector3f::NormalizeOrZero(flat);
    if (Wm3::Vector3f::LengthSq(normalized) <= 1.0e-6f) {
      return {0.0f, 0.0f, 1.0f};
    }
    return normalized;
  }

  void PushPathPoint(
    gpg::core::FastVectorN<CPathPoint, 20>& nodes,
    const Wm3::Vector3f& pos,
    const Wm3::Vector3f& forward,
    const EPathPointState state
  )
  {
    CPathPoint point{};
    point.mPosition = pos;
    point.mDirection = forward;
    point.mState = state;
    nodes.PushBack(point);
  }

  // Address 0x00F59978 in FA binary; runtime value = 5.
  // Node-count cap applied when a formation unit is path-generating.
  static constexpr std::uint32_t formation_path_value = 5u;

  /**
   * Address: 0x005B1C90 (FUN_005B1C90, func_VecSetLengthS)
   *
   * What it does:
   * Scales `v` so its magnitude equals `targetLen`. Returns false and leaves
   * `v` unchanged when `v` is the zero vector.
   */
  bool SetVecLength(Wm3::Vector3f& v, const float targetLen) noexcept
  {
    const float x = v.x;
    const float mag = x * x + v.y * v.y + v.z * v.z;
    if (mag <= 0.0f) {
      return false;
    }
    const float s = targetLen / std::sqrt(mag);
    v.x = x * s;
    v.y *= s;
    v.z *= s;
    return true;
  }

  /**
   * Address: 0x006992C0 (FUN_006992C0, sub_6992C0)
   *
   * What it does:
   * Rotates the 2D source direction `(srcX, srcY)` toward `(dstX, dstY)` by at
   * most `maxTurnRad` radians. Returns result through `*out`. When the angle
   * from src to dst is less than `maxTurnRad`, snaps `out` to the dst direction
   * at the same magnitude as src. When either vector is zero, copies src to
   * `out` unchanged. 2D convention: `y` axis = -3D z.
   */
  Wm3::Vector2f* SteerTurnVec(
    Wm3::Vector2f* const out,
    float maxTurnRad,
    const float srcX,
    const float srcY,
    const float dstX,
    const float dstY
  ) noexcept
  {
    if (maxTurnRad >= 3.1415927f) {
      maxTurnRad = 3.1415927f;
    }

    const float srcLen = std::sqrt(srcX * srcX + srcY * srcY);
    const float dstLen = std::sqrt(dstX * dstX + dstY * dstY);
    const float lenProd = dstLen * srcLen;

    if (lenProd == 0.0f) {
      out->x = srcX;
      out->y = srcY;
      return out;
    }

    const float cosMax = std::cos(maxTurnRad);
    const float dot = dstY * srcY + dstX * srcX;

    if (dot < cosMax * lenProd) {
      const float a2sq = maxTurnRad * maxTurnRad;
      const float sinApprox =
        ((a2sq * 0.0076100002f - 0.16605f) * a2sq + 1.0f) * maxTurnRad;
      const float cross = dstY * srcX - srcY * dstX;
      const float sinV = (cross < 0.0f) ? -sinApprox : sinApprox;

      Wm3::Vector2f rot{cosMax, sinV};
      if (std::fabs(sinV * sinV + cosMax * cosMax - 1.0f) > 0.001f) {
        Wm3::Vector2f::Normalize(rot);
      }
      out->x = rot.x * srcX - rot.y * srcY;
      out->y = rot.x * srcY + rot.y * srcX;
    } else {
      const float scale = srcLen / dstLen;
      out->x = dstX * scale;
      out->y = dstY * scale;
    }
    return out;
  }

  /**
   * Address: 0x00699760 (FUN_00699760, sub_699760)
   *
   * What it does:
   * Computes the target wanted-speed given steering parameters and current
   * normalized speed ratio (curSpeed / MaxSpeed * 10). Honors rotate-on-spot
   * alignment gating and computes a turning-arc speed cap from 2D geometry.
   */
  [[nodiscard]] float WantedSpeed(const SSteeringParams& p, const float curSpeedRatio) noexcept
  {
    if (p.mRotateOnSpot != 0 && p.mRotateOnSpotThreshold > curSpeedRatio) {
      const float destMagSq = p.mDX * p.mDX + p.mDZ * p.mDZ;
      if (destMagSq > 0.0f) {
        const float invMag = 1.0f / std::sqrt(destMagSq);
        const float align = p.mVec1.y * (p.mDZ * invMag) + p.mVec1.x * (p.mDX * invMag);
        if (align < 0.98000002f) {
          return 0.0f;
        }
      }
      return p.mMaxSpeed;
    }

    const float cross = p.mDZ * p.mVec1.x - p.mVec1.y * p.mDX;
    const float turnR = (cross == 0.0f) ? 0.0f : (p.mDistSq * 0.5f / cross);
    const float absR = std::fabs(turnR);

    if (absR < p.mInvTurnRadius) {
      if (absR == 0.0f) {
        return p.mMaxSpeed;
      }
      return p.mTurnRate * absR * 0.5f;
    }
    if (p.mInvTurnRadius < 0.0f) {
      return 0.0f;
    }
    return p.mInvTurnRadius;
  }

  /**
   * Address: 0x0062AA90 (FUN_0062AA90, func_UnitWontFitAt)
   *
   * What it does:
   * Returns true when the unit's footprint does NOT fit at the world position
   * `pos` — either out of map bounds or the destination cell has blocked
   * occupancy. Water-layer units mask off the water-restriction bit before
   * testing the occupancy grid.
   */
  [[nodiscard]] bool UnitWontFitAt(const Wm3::Vector3f& pos, Unit* const unit) noexcept
  {
    const SFootprint& footprint = unit->GetFootprint();
    Sim* const sim = unit->SimulationRef;
    STIMap* const map = sim ? sim->mMapData : nullptr;
    COGrid* const grid = sim ? sim->mOGrid : nullptr;
    if (map == nullptr || grid == nullptr) {
      return true;
    }

    const bool useWholeMap = (unit->ArmyRef != nullptr) && unit->ArmyRef->UseWholeMap();
    if (!map->IsWithin(pos, 0.0f, useWholeMap)) {
      return true;
    }

    const float cellZ = pos.z - static_cast<float>(footprint.mSizeZ) * 0.5f;
    const float cellX = pos.x - static_cast<float>(footprint.mSizeX) * 0.5f;
    SOCellPos cellPos{};
    cellPos.x = static_cast<std::int16_t>(static_cast<std::int32_t>(cellX));
    cellPos.z = static_cast<std::int16_t>(static_cast<std::int32_t>(cellZ));

    EOccupancyCaps caps = OCCUPY_MobileCheck(footprint, *map, cellPos);
    if (unit->mCurrentLayer == LAYER_Water) {
      caps = static_cast<EOccupancyCaps>(static_cast<std::uint32_t>(caps) & ~static_cast<std::uint32_t>(4u));
    }
    return OCCUPY_FootprintFits(*grid, cellPos, footprint, caps) == static_cast<EOccupancyCaps>(0);
  }

  /**
   * Address: 0x0062ABA0 (FUN_0062ABA0, sub_62ABA0)
   *
   * What it does:
   * Returns true when `COGrid::UnitIsBlocked` reports the unit's footprint is
   * blocked at cell position of `pos` under the given `flags`.
   */
  [[nodiscard]] bool IsUnitBlockedAt(const Wm3::Vector3f& pos, Unit* const unit, const int flags) noexcept
  {
    const SFootprint& footprint = unit->GetFootprint();
    const float cellZ = pos.z - static_cast<float>(footprint.mSizeZ) * 0.5f;
    const float cellX = pos.x - static_cast<float>(footprint.mSizeX) * 0.5f;
    SOCellPos cellPos{};
    cellPos.x = static_cast<std::int16_t>(static_cast<std::int32_t>(cellX));
    cellPos.z = static_cast<std::int16_t>(static_cast<std::int32_t>(cellZ));
    Sim* const sim = unit->SimulationRef;
    COGrid* const grid = sim ? sim->mOGrid : nullptr;
    return COGrid::UnitIsBlocked(&cellPos, grid, unit, flags);
  }

} // namespace

// ── SSteeringParams ctor ───────────────────────────────────────────────────

namespace moho
{
  /**
   * Address: 0x006990E0 (FUN_006990E0, struct_SteeringParams::struct_SteeringParams)
   *
   * IDA signature:
   * void __userpurge struct_SteeringParams::struct_SteeringParams(
   *   Moho::Unit *unit@<ebx>, struct_SteeringParams *params@<esi>,
   *   Wm3::Vector3f *curPos, Wm3::Vector3f *dest, Wm3::Vector3f *forward,
   *   float speed, bool skipPhysics)
   */
  SSteeringParams::SSteeringParams(
    Unit* const unit,
    const Wm3::Vector3f& curPos,
    const Wm3::Vector3f& dest,
    const Wm3::Vector3f& forward,
    const float speed,
    const bool skipPhysics
  )
  {
    const RUnitBlueprintPhysics& phys = unit->GetBlueprint()->Physics;
    const UnitAttributes& attrs = unit->GetAttributes();
    const float turnMult = attrs.turnMult;

    mTurnRate = phys.TurnRate * turnMult * 0.0017453292f;
    mTurnFacingRate = phys.TurnFacingRate * turnMult * 0.0017453292f;

    const float dx = dest.x - curPos.x;
    const float dz = dest.z - curPos.z;
    mDX = dx;
    mDZ = -dz;
    mDistSq = dx * dx + (-dz) * (-dz);
    mDist = std::sqrt(mDistSq);

    mVec1.x = forward.x;
    mVec1.y = -forward.z;
    Wm3::Vector2f::Normalize(mVec1);

    if (skipPhysics) {
      return;
    }

    const float speedMult = attrs.moveSpeedMult;
    const float accMult = attrs.accelerationMult;

    const float cappedMaxSpeed = (speed <= phys.MaxSpeed) ? speed : phys.MaxSpeed;
    const float cappedMaxRev = (speed <= phys.MaxSpeedReverse) ? speed : phys.MaxSpeedReverse;
    mMaxSpeed = cappedMaxSpeed * speedMult * 0.1f;
    mMaxReserveSpeed = cappedMaxRev * speedMult * 0.1f;

    const float baseAcc = phys.MaxAcceleration * accMult * 0.0099999998f;
    mMaxAcceleration = baseAcc;
    mMaxBrake = (phys.MaxBrake != 0.0f) ? (phys.MaxBrake * accMult * 0.0099999998f) : baseAcc;
    mMaxSteer = (phys.MaxSteerForce != 0.0f)
              ? (phys.MaxSteerForce * accMult * 0.0099999998f)
              : baseAcc;

    mInvTurnRadius = (phys.TurnRadius == 0.0f)
                   ? std::numeric_limits<float>::infinity()
                   : (phys.TurnRadius / turnMult);

    mRotateOnSpot = phys.RotateOnSpot;
    mRotateOnSpotThreshold = phys.RotateOnSpotThreshold;
  }
} // namespace moho

gpg::RType* CAiPathSpline::sType = nullptr;
gpg::RType* SContinueInfo::sType = nullptr;

namespace
{
  class FastVectorCPathPointTypeInfo final : public gpg::RType, public gpg::RIndexed
  {
  public:
    [[nodiscard]] const char* GetName() const override;
    [[nodiscard]] msvc8::string GetLexical(const gpg::RRef& ref) const override;
    [[nodiscard]] const gpg::RIndexed* IsIndexed() const override;
    void Init() override;
    gpg::RRef SubscriptIndex(void* obj, int ind) const override;
    size_t GetCount(void* obj) const override;
    void SetCount(void* obj, int count) const override;
  };

  static_assert(sizeof(FastVectorCPathPointTypeInfo) == 0x68, "FastVectorCPathPointTypeInfo size must be 0x68");

  class ECollisionTypePrimitiveSerializer
  {
  public:
    static void Deserialize(gpg::ReadArchive* archive, int objectPtr, int version, gpg::RRef* ownerRef);
    static void Serialize(gpg::WriteArchive* archive, int objectPtr, int version, gpg::RRef* ownerRef);
    virtual void RegisterSerializeFunctions();

  public:
    gpg::SerHelperBase* mHelperNext;
    gpg::SerHelperBase* mHelperPrev;
    gpg::RType::load_func_t mDeserialize;
    gpg::RType::save_func_t mSerialize;
  };

  static_assert(
    offsetof(ECollisionTypePrimitiveSerializer, mHelperNext) == 0x04,
    "ECollisionTypePrimitiveSerializer::mHelperNext offset must be 0x04"
  );
  static_assert(
    offsetof(ECollisionTypePrimitiveSerializer, mHelperPrev) == 0x08,
    "ECollisionTypePrimitiveSerializer::mHelperPrev offset must be 0x08"
  );
  static_assert(
    offsetof(ECollisionTypePrimitiveSerializer, mDeserialize) == 0x0C,
    "ECollisionTypePrimitiveSerializer::mDeserialize offset must be 0x0C"
  );
  static_assert(
    offsetof(ECollisionTypePrimitiveSerializer, mSerialize) == 0x10,
    "ECollisionTypePrimitiveSerializer::mSerialize offset must be 0x10"
  );
  static_assert(
    sizeof(ECollisionTypePrimitiveSerializer) == 0x14, "ECollisionTypePrimitiveSerializer size must be 0x14"
  );
} // namespace

namespace
{
  alignas(moho::SCollisionInfoTypeInfo)
    unsigned char gSCollisionInfoTypeInfoStorage[sizeof(moho::SCollisionInfoTypeInfo)] = {};
  bool gSCollisionInfoTypeInfoConstructed = false;

  alignas(moho::ECollisionTypeTypeInfo)
    unsigned char gECollisionTypeTypeInfoStorage[sizeof(moho::ECollisionTypeTypeInfo)] = {};
  bool gECollisionTypeTypeInfoConstructed = false;

  alignas(ECollisionTypePrimitiveSerializer)
    unsigned char gECollisionTypePrimitiveSerializerStorage[sizeof(ECollisionTypePrimitiveSerializer)] = {};
  bool gECollisionTypePrimitiveSerializerConstructed = false;

  alignas(moho::SCollisionInfoSerializer)
    unsigned char gSCollisionInfoSerializerStorage[sizeof(moho::SCollisionInfoSerializer)] = {};
  bool gSCollisionInfoSerializerConstructed = false;

  alignas(moho::EPathPointStateTypeInfo)
    unsigned char gEPathPointStateTypeInfoStorage[sizeof(moho::EPathPointStateTypeInfo)] = {};
  bool gEPathPointStateTypeInfoConstructed = false;

  alignas(moho::CPathPointTypeInfo) unsigned char gCPathPointTypeInfoStorage[sizeof(moho::CPathPointTypeInfo)] = {};
  bool gCPathPointTypeInfoConstructed = false;
  alignas(FastVectorCPathPointTypeInfo)
    unsigned char gFastVectorCPathPointTypeStorage[sizeof(FastVectorCPathPointTypeInfo)] = {};
  bool gFastVectorCPathPointTypeConstructed = false;

  moho::EPathPointStatePrimitiveSerializer gEPathPointStatePrimitiveSerializer{};
  moho::CPathPointSerializer gCPathPointSerializer{};

  gpg::RType* gWeakUnitType = nullptr;
  gpg::RType* gSCollisionInfoType = nullptr;
  gpg::RType* gECollisionTypeType = nullptr;
  gpg::RType* gEPathPointStateType = nullptr;
  gpg::RType* gVector3fType = nullptr;
  gpg::RType* gCPathPointType = nullptr;
  gpg::RType* gFastVectorCPathPointType = nullptr;
  msvc8::string gFastVectorCPathPointTypeName{};
  bool gFastVectorCPathPointTypeNameCleanupRegistered = false;
  gpg::RType* gPathSplineTypeType = nullptr;
  gpg::RType* gPathSplineContinuationType = nullptr;
  EngineStats* gRecoveredAiPathSplineStartupStatsSlot = nullptr;

  template <typename TSerializer>
  [[nodiscard]] gpg::SerHelperBase* SerializerSelfNode(TSerializer& serializer) noexcept
  {
    return reinterpret_cast<gpg::SerHelperBase*>(&serializer.mHelperNext);
  }

  template <typename TSerializer>
  void InitializeSerializerNode(TSerializer& serializer) noexcept
  {
    gpg::SerHelperBase* const self = SerializerSelfNode(serializer);
    serializer.mHelperNext = self;
    serializer.mHelperPrev = self;
  }

  template <typename TSerializer>
  [[nodiscard]] gpg::SerHelperBase* UnlinkSerializerNode(TSerializer& serializer) noexcept
  {
    if (serializer.mHelperNext != nullptr && serializer.mHelperPrev != nullptr) {
      serializer.mHelperNext->mPrev = serializer.mHelperPrev;
      serializer.mHelperPrev->mNext = serializer.mHelperNext;
    }

    gpg::SerHelperBase* const self = SerializerSelfNode(serializer);
    serializer.mHelperPrev = self;
    serializer.mHelperNext = self;
    return self;
  }

  [[nodiscard]] gpg::RType* ResolveVector3fType()
  {
    if (gVector3fType == nullptr) {
      gVector3fType = gpg::LookupRType(typeid(Wm3::Vector3<float>));
    }
    return gVector3fType;
  }

  [[nodiscard]] gpg::RType* ResolveWeakUnitType()
  {
    if (gWeakUnitType == nullptr) {
      gWeakUnitType = gpg::LookupRType(typeid(WeakPtr<Unit>));
    }
    return gWeakUnitType;
  }

  [[nodiscard]] gpg::RType* ResolveSCollisionInfoType()
  {
    if (gSCollisionInfoType == nullptr) {
      gSCollisionInfoType = gpg::LookupRType(typeid(moho::SCollisionInfo));
    }
    return gSCollisionInfoType;
  }

  [[nodiscard]] gpg::RType* ResolveECollisionTypeType()
  {
    if (gECollisionTypeType == nullptr) {
      gECollisionTypeType = gpg::LookupRType(typeid(moho::ECollisionType));
    }
    return gECollisionTypeType;
  }

  [[nodiscard]] gpg::RType* ResolveEPathPointStateType()
  {
    if (gEPathPointStateType == nullptr) {
      gEPathPointStateType = gpg::LookupRType(typeid(moho::EPathPointState));
    }
    return gEPathPointStateType;
  }

  [[nodiscard]] gpg::RType* ResolveCPathPointType()
  {
    if (gCPathPointType == nullptr) {
      gCPathPointType = gpg::LookupRType(typeid(moho::CPathPoint));
    }
    return gCPathPointType;
  }

  [[nodiscard]] moho::SCollisionInfoTypeInfo* AcquireSCollisionInfoTypeInfo()
  {
    if (!gSCollisionInfoTypeInfoConstructed) {
      new (gSCollisionInfoTypeInfoStorage) moho::SCollisionInfoTypeInfo();
      gSCollisionInfoTypeInfoConstructed = true;
    }

    return reinterpret_cast<moho::SCollisionInfoTypeInfo*>(gSCollisionInfoTypeInfoStorage);
  }

  [[nodiscard]] moho::ECollisionTypeTypeInfo* AcquireECollisionTypeTypeInfo()
  {
    if (!gECollisionTypeTypeInfoConstructed) {
      new (gECollisionTypeTypeInfoStorage) moho::ECollisionTypeTypeInfo();
      gECollisionTypeTypeInfoConstructed = true;
    }

    return reinterpret_cast<moho::ECollisionTypeTypeInfo*>(gECollisionTypeTypeInfoStorage);
  }

  [[nodiscard]] ECollisionTypePrimitiveSerializer* AcquireECollisionTypePrimitiveSerializer()
  {
    if (!gECollisionTypePrimitiveSerializerConstructed) {
      new (gECollisionTypePrimitiveSerializerStorage) ECollisionTypePrimitiveSerializer();
      gECollisionTypePrimitiveSerializerConstructed = true;
    }

    return reinterpret_cast<ECollisionTypePrimitiveSerializer*>(gECollisionTypePrimitiveSerializerStorage);
  }

  [[nodiscard]] moho::SCollisionInfoSerializer* AcquireSCollisionInfoSerializer()
  {
    if (!gSCollisionInfoSerializerConstructed) {
      new (gSCollisionInfoSerializerStorage) moho::SCollisionInfoSerializer();
      gSCollisionInfoSerializerConstructed = true;
    }

    return reinterpret_cast<moho::SCollisionInfoSerializer*>(gSCollisionInfoSerializerStorage);
  }

  [[nodiscard]] FastVectorCPathPointTypeInfo* AcquireFastVectorCPathPointType()
  {
    if (!gFastVectorCPathPointTypeConstructed) {
      new (gFastVectorCPathPointTypeStorage) FastVectorCPathPointTypeInfo();
      gFastVectorCPathPointTypeConstructed = true;
    }

    return reinterpret_cast<FastVectorCPathPointTypeInfo*>(gFastVectorCPathPointTypeStorage);
  }

  [[nodiscard]] gpg::RType* preregister_FastVectorCPathPointType();

  [[nodiscard]] gpg::RType* ResolveFastVectorCPathPointType()
  {
    if (gFastVectorCPathPointType == nullptr) {
      gFastVectorCPathPointType = gpg::LookupRType(typeid(gpg::fastvector<moho::CPathPoint>));
      if (gFastVectorCPathPointType == nullptr) {
        gFastVectorCPathPointType = preregister_FastVectorCPathPointType();
      }
    }
    return gFastVectorCPathPointType;
  }

  [[nodiscard]] gpg::RType* ResolvePathSplineTypeType()
  {
    if (gPathSplineTypeType == nullptr) {
      gPathSplineTypeType = gpg::LookupRType(typeid(moho::EPathType));
    }
    return gPathSplineTypeType;
  }

  [[nodiscard]] gpg::RType* ResolvePathSplineContinuationType()
  {
    if (gPathSplineContinuationType == nullptr) {
      gPathSplineContinuationType = SContinueInfo::sType;
      if (gPathSplineContinuationType == nullptr) {
        gPathSplineContinuationType = gpg::LookupRType(typeid(moho::SContinueInfo));
        SContinueInfo::sType = gPathSplineContinuationType;
      }
    }
    return gPathSplineContinuationType;
  }

  void LoadFastVectorCPathPoint(gpg::ReadArchive* archive, int objectPtr, int, gpg::RRef* ownerRef)
  {
    if (!archive || objectPtr == 0) {
      return;
    }

    auto& view = gpg::AsFastVectorRuntimeView<moho::CPathPoint>(reinterpret_cast<void*>(objectPtr));
    unsigned int count = 0;
    archive->ReadUInt(&count);

    const moho::CPathPoint fill{};
    gpg::FastVectorRuntimeResizeFill(&fill, count, view);

    gpg::RType* const elementType = ResolveCPathPointType();
    const gpg::RRef owner = ownerRef ? *ownerRef : gpg::RRef{};
    for (unsigned int i = 0; i < count; ++i) {
      archive->Read(elementType, view.ElementAtUnchecked(i), owner);
    }
  }

  void SaveFastVectorCPathPoint(gpg::WriteArchive* archive, int objectPtr, int, gpg::RRef* ownerRef)
  {
    if (!archive || objectPtr == 0) {
      return;
    }

    const auto& view = gpg::AsFastVectorRuntimeView<moho::CPathPoint>(reinterpret_cast<const void*>(objectPtr));
    const unsigned int count = view.begin ? static_cast<unsigned int>(view.Size()) : 0u;
    archive->WriteUInt(count);

    gpg::RType* const elementType = ResolveCPathPointType();
    const gpg::RRef owner = ownerRef ? *ownerRef : gpg::RRef{};
    for (unsigned int i = 0; i < count; ++i) {
      archive->Write(elementType, view.ElementAtUnchecked(i), owner);
    }
  }

  /**
   * Address: 0x005B58C0 (FUN_005B58C0, preregister_FastVectorCPathPointType)
   *
   * What it does:
   * Constructs and preregisters startup RTTI descriptor for
   * `gpg::fastvector<CPathPoint>`.
   */
  [[nodiscard]] gpg::RType* preregister_FastVectorCPathPointType()
  {
    FastVectorCPathPointTypeInfo* const type = AcquireFastVectorCPathPointType();
    gpg::PreRegisterRType(typeid(gpg::fastvector<moho::CPathPoint>), type);
    gFastVectorCPathPointType = type;
    return type;
  }

  /**
   * Address: 0x0062F520 (FUN_0062F520, construct_EPathPointStateTypeInfo)
   *
   * What it does:
   * Constructs and preregisters `EPathPointStateTypeInfo` in static startup
   * storage.
   */
  [[nodiscard]] gpg::REnumType* construct_EPathPointStateTypeInfo()
  {
    if (!gEPathPointStateTypeInfoConstructed) {
      auto* const typeInfo = new (gEPathPointStateTypeInfoStorage) moho::EPathPointStateTypeInfo();
      gpg::PreRegisterRType(typeid(moho::EPathPointState), typeInfo);
      gEPathPointStateType = typeInfo;
      gEPathPointStateTypeInfoConstructed = true;
    }

    return reinterpret_cast<gpg::REnumType*>(gEPathPointStateTypeInfoStorage);
  }

  /**
   * Address: 0x00BFA7E0 (FUN_00BFA7E0, cleanup_EPathPointStateTypeInfo)
   *
   * What it does:
   * Tears down the recovered `EPathPointState` enum type descriptor.
   */
  void cleanup_EPathPointStateTypeInfo()
  {
    if (!gEPathPointStateTypeInfoConstructed) {
      return;
    }

    reinterpret_cast<moho::EPathPointStateTypeInfo*>(gEPathPointStateTypeInfoStorage)->~EPathPointStateTypeInfo();
    gEPathPointStateType = nullptr;
    gEPathPointStateTypeInfoConstructed = false;
  }

  /**
   * Address: 0x00BFA7F0 (FUN_00BFA7F0, cleanup_EPathPointStatePrimitiveSerializer)
   *
   * What it does:
   * Unlinks the recovered `EPathPointState` primitive serializer helper node.
   */
  gpg::SerHelperBase* cleanup_EPathPointStatePrimitiveSerializer()
  {
    return UnlinkSerializerNode(gEPathPointStatePrimitiveSerializer);
  }

  void cleanup_EPathPointStatePrimitiveSerializer_atexit()
  {
    (void)cleanup_EPathPointStatePrimitiveSerializer();
  }

  /**
   * Address: 0x0062F650 (FUN_0062F650, construct_CPathPointTypeInfo)
   *
   * What it does:
   * Constructs and preregisters `CPathPointTypeInfo` in static startup storage.
   */
  [[nodiscard]] gpg::RType* construct_CPathPointTypeInfo()
  {
    if (!gCPathPointTypeInfoConstructed) {
      auto* const typeInfo = new (gCPathPointTypeInfoStorage) moho::CPathPointTypeInfo();
      gpg::PreRegisterRType(typeid(moho::CPathPoint), typeInfo);
      gCPathPointType = typeInfo;
      gCPathPointTypeInfoConstructed = true;
    }

    return reinterpret_cast<gpg::RType*>(gCPathPointTypeInfoStorage);
  }

  /**
   * Address: 0x00BFA820 (FUN_00BFA820, cleanup_CPathPointTypeInfo)
   *
   * What it does:
   * Tears down the recovered `CPathPoint` type descriptor.
   */
  void cleanup_CPathPointTypeInfo()
  {
    if (!gCPathPointTypeInfoConstructed) {
      return;
    }

    reinterpret_cast<moho::CPathPointTypeInfo*>(gCPathPointTypeInfoStorage)->~CPathPointTypeInfo();
    gCPathPointType = nullptr;
    gCPathPointTypeInfoConstructed = false;
  }

  /**
   * Address: 0x00BFA880 (FUN_00BFA880, cleanup_CPathPointSerializer)
   *
   * What it does:
   * Unlinks the recovered `CPathPointSerializer` helper node.
   */
  gpg::SerHelperBase* cleanup_CPathPointSerializer()
  {
    return UnlinkSerializerNode(gCPathPointSerializer);
  }

  void cleanup_CPathPointSerializer_atexit()
  {
    (void)cleanup_CPathPointSerializer();
  }

  /**
   * Address: 0x00BF6510 (FUN_00BF6510, Moho::ECollisionTypeTypeInfo::~ECollisionTypeTypeInfo)
   *
   * What it does:
   * Tears down startup-owned `ECollisionTypeTypeInfo` reflection storage.
   */
  void cleanup_ECollisionTypeTypeInfo()
  {
    if (!gECollisionTypeTypeInfoConstructed) {
      return;
    }

    AcquireECollisionTypeTypeInfo()->~ECollisionTypeTypeInfo();
    gECollisionTypeTypeInfoConstructed = false;
    gECollisionTypeType = nullptr;
  }

  /**
   * Address: 0x00BF6520 (FUN_00BF6520, cleanup_PrimitiveSerHelper_ECollisionType_int)
   *
   * What it does:
   * Unlinks the recovered primitive serializer helper node for `ECollisionType`.
   */
  [[nodiscard]] gpg::SerHelperBase* cleanup_PrimitiveSerHelper_ECollisionType_int()
  {
    if (!gECollisionTypePrimitiveSerializerConstructed) {
      return nullptr;
    }
    return UnlinkSerializerNode(*AcquireECollisionTypePrimitiveSerializer());
  }

  void cleanup_PrimitiveSerHelper_ECollisionType_int_atexit()
  {
    (void)cleanup_PrimitiveSerHelper_ECollisionType_int();
  }

  /**
   * Address: 0x00BF6550 (FUN_00BF6550, cleanup_SCollisionInfoTypeInfo)
   *
   * What it does:
   * Tears down startup-owned `SCollisionInfoTypeInfo` reflection storage.
   */
  void cleanup_SCollisionInfoTypeInfo()
  {
    if (!gSCollisionInfoTypeInfoConstructed) {
      return;
    }

    AcquireSCollisionInfoTypeInfo()->~SCollisionInfoTypeInfo();
    gSCollisionInfoTypeInfoConstructed = false;
    gSCollisionInfoType = nullptr;
  }

  /**
   * Address: 0x00BF65B0 (FUN_00BF65B0, cleanup_SCollisionInfoSerializer)
   *
   * What it does:
   * Unlinks the recovered `SCollisionInfo` serializer helper node.
   */
  [[nodiscard]] gpg::SerHelperBase* cleanup_SCollisionInfoSerializer()
  {
    if (!gSCollisionInfoSerializerConstructed) {
      return nullptr;
    }
    return UnlinkSerializerNode(*AcquireSCollisionInfoSerializer());
  }

  void cleanup_SCollisionInfoSerializer_atexit()
  {
    (void)cleanup_SCollisionInfoSerializer();
  }

  /**
   * Address: 0x00BF75A0 (FUN_00BF75A0, cleanup_FastVectorCPathPointType)
   *
   * What it does:
   * Tears down startup-owned `fastvector<CPathPoint>` reflection storage.
   */
  void cleanup_FastVectorCPathPointType()
  {
    if (!gFastVectorCPathPointTypeConstructed) {
      return;
    }

    AcquireFastVectorCPathPointType()->~FastVectorCPathPointTypeInfo();
    gFastVectorCPathPointTypeConstructed = false;
    gFastVectorCPathPointType = nullptr;
  }

  /**
   * Address: 0x00BF7570 (FUN_00BF7570, cleanup_FastVectorCPathPointTypeName)
   *
   * What it does:
   * Clears cached lexical type-name storage for
   * `gpg::RFastVectorType_CPathPoint::GetName`.
   */
  void cleanup_FastVectorCPathPointTypeName()
  {
    gFastVectorCPathPointTypeName.clear();
    gFastVectorCPathPointTypeNameCleanupRegistered = false;
  }

  /**
   * Address: 0x00BF7600 (FUN_00BF7600, cleanup_CAiPathSplineStartupStats)
   *
   * What it does:
   * Tears down one startup-owned AI path-spline stats slot.
   */
  void cleanup_CAiPathSplineStartupStats()
  {
    if (!gRecoveredAiPathSplineStartupStatsSlot) {
      return;
    }

    delete gRecoveredAiPathSplineStartupStatsSlot;
    gRecoveredAiPathSplineStartupStatsSlot = nullptr;
  }

  /**
   * Address: 0x0062F980 (FUN_0062F980, Deserialize_EPathPointState)
   *
   * What it does:
   * Reads one enum lane as `int` and stores it into `EPathPointState`.
   */
  void Deserialize_EPathPointState(gpg::ReadArchive* const archive, const int objectPtr, const int, gpg::RRef*)
  {
    if (archive == nullptr || objectPtr == 0) {
      return;
    }

    int value = 0;
    archive->ReadInt(&value);
    *reinterpret_cast<moho::EPathPointState*>(static_cast<std::uintptr_t>(objectPtr)) =
      static_cast<moho::EPathPointState>(value);
  }

  /**
   * Address: 0x0062F9A0 (FUN_0062F9A0, Serialize_EPathPointState)
   *
   * What it does:
   * Writes one `EPathPointState` enum lane as `int`.
   */
  void Serialize_EPathPointState(gpg::WriteArchive* const archive, const int objectPtr, const int, gpg::RRef*)
  {
    if (archive == nullptr || objectPtr == 0) {
      return;
    }

    const auto* const value = reinterpret_cast<const moho::EPathPointState*>(static_cast<std::uintptr_t>(objectPtr));
    archive->WriteInt(static_cast<int>(*value));
  }
} // namespace

/**
 * Address: 0x00596560 (FUN_00596560, sub_596560)
 */
void moho::ResetCollisionInfo(SCollisionInfo& info)
{
  RemoveFromIntrusiveCollisionChain(info);
  info.mPos = Wm3::Vector3f::Zero();
  info.mCollisionType = COLLISIONTYPE_None;
  info.mTickGate = -1;
}

/**
 * Address: 0x005984E0 (FUN_005984E0, Moho::SCollisionInfo::MemberDeserialize)
 */
void SCollisionInfo::MemberDeserialize(gpg::ReadArchive* const archive)
{
  if (!archive) {
    return;
  }

  const gpg::RRef ownerRef{};
  gpg::RType* const weakUnitType = ResolveWeakUnitType();
  gpg::RType* const vectorType = ResolveVector3fType();
  gpg::RType* const collisionType = ResolveECollisionTypeType();
  GPG_ASSERT(weakUnitType != nullptr);
  GPG_ASSERT(vectorType != nullptr);
  GPG_ASSERT(collisionType != nullptr);

  archive->Read(weakUnitType, &mUnit, ownerRef);
  archive->Read(vectorType, &mPos, ownerRef);
  archive->Read(collisionType, &mCollisionType, ownerRef);
  archive->ReadUInt(reinterpret_cast<unsigned int*>(&mTickGate));
}

/**
 * Address: 0x005985A0 (FUN_005985A0, Moho::SCollisionInfo::MemberSerialize)
 */
void SCollisionInfo::MemberSerialize(gpg::WriteArchive* const archive) const
{
  if (!archive) {
    return;
  }

  const gpg::RRef ownerRef{};
  gpg::RType* const weakUnitType = ResolveWeakUnitType();
  gpg::RType* const vectorType = ResolveVector3fType();
  gpg::RType* const collisionType = ResolveECollisionTypeType();
  GPG_ASSERT(weakUnitType != nullptr);
  GPG_ASSERT(vectorType != nullptr);
  GPG_ASSERT(collisionType != nullptr);

  archive->Write(weakUnitType, const_cast<SCollisionLink*>(&mUnit), ownerRef);
  archive->Write(vectorType, const_cast<Wm3::Vector3f*>(&mPos), ownerRef);
  archive->Write(collisionType, const_cast<ECollisionType*>(&mCollisionType), ownerRef);
  archive->WriteUInt(static_cast<unsigned int>(mTickGate));
}

/**
 * Address: 0x005B5530 (FUN_005B5530, Moho::SContinueInfo::MemberDeserialize)
 */
void SContinueInfo::MemberDeserialize(gpg::ReadArchive* const archive)
{
  if (!archive) {
    return;
  }

  const gpg::RRef ownerRef{};
  gpg::RType* const vectorType = ResolveVector3fType();
  gpg::RType* const stateType = ResolveEPathPointStateType();
  GPG_ASSERT(vectorType != nullptr);
  GPG_ASSERT(stateType != nullptr);

  archive->Read(vectorType, &mOldPosition, ownerRef);
  archive->Read(vectorType, &mOldDirection, ownerRef);
  archive->Read(vectorType, &mOldVelocity, ownerRef);
  archive->Read(stateType, &mState, ownerRef);
}

/**
 * Address: 0x005B5610 (FUN_005B5610, Moho::SContinueInfo::MemberSerialize)
 */
void SContinueInfo::MemberSerialize(gpg::WriteArchive* const archive) const
{
  if (!archive) {
    return;
  }

  const gpg::RRef ownerRef{};
  gpg::RType* const vectorType = ResolveVector3fType();
  gpg::RType* const stateType = ResolveEPathPointStateType();
  GPG_ASSERT(vectorType != nullptr);
  GPG_ASSERT(stateType != nullptr);

  archive->Write(vectorType, const_cast<Wm3::Vector3f*>(&mOldPosition), ownerRef);
  archive->Write(vectorType, const_cast<Wm3::Vector3f*>(&mOldDirection), ownerRef);
  archive->Write(vectorType, const_cast<Wm3::Vector3f*>(&mOldVelocity), ownerRef);
  archive->Write(stateType, const_cast<EPathPointState*>(&mState), ownerRef);
}

/**
 * Address: 0x005B2550 (FUN_005B2550, ??0CAiPathSpline@Moho@@QAE@@Z)
 */
CAiPathSpline::CAiPathSpline()
  : mCurrentNodeIndex(0)
  , mNodeCount(0)
  , mPathType(PT_0)
  , mContinuation{Wm3::Vector3f::Zero(), Wm3::Vector3f::Zero(), Wm3::Vector3f::Zero(), PPS_8}
  , mTailWord(0)
{
  ResetNodesToInline();
}

/**
 * Address: 0x005D45D0 (FUN_005D45D0, ??1CAiPathSpline@Moho@@QAE@@Z)
 */
CAiPathSpline::~CAiPathSpline()
{
  ResetNodesToInline();
}

/**
 * Address: 0x005B2550 (FUN_005B2550)
 */
void CAiPathSpline::ResetNodesToInline()
{
  nodes.ResetStorageToInline();
}

/**
 * Address: 0x005965E0 (FUN_005965E0, sub_5965E0)
 */
CPathPoint* CAiPathSpline::TryGetNode(const std::uint32_t index)
{
  if (index >= mNodeCount) {
    return nullptr;
  }
  return &nodes[index];
}

/**
 * Address: 0x005965E0 (FUN_005965E0, sub_5965E0)
 */
const CPathPoint* CAiPathSpline::TryGetNode(const std::uint32_t index) const
{
  if (index >= mNodeCount) {
    return nullptr;
  }
  return &nodes[index];
}

/**
 * Address: 0x005B26C0 (FUN_005B26C0, Moho::CAiPathSpline::Update)
 *
 * IDA signature:
 * int __userpurge Moho::CAiPathSpline::Update@<eax>(
 *   Moho::Unit *a1@<eax>, Moho::CAiPathSpline *a2@<edi>, int a3)
 *
 * What it does:
 * Runs a deceleration-prediction simulation starting from the unit's current
 * position and velocity, applying brake steering each step until the unit is
 * stopped (or the stop condition for the requested mode is reached). Pushes
 * CPathPoint nodes describing the predicted stopping trajectory and returns
 * the final node count.
 */
int CAiPathSpline::Update(Unit* const unit, const int updateMode)
{
  ResetNodesToInline();
  mCurrentNodeIndex = 0;
  mNodeCount = 0;
  mPathType = static_cast<EPathType>(updateMode);

  // ── Forward direction from unit quaternion ────────────────────────────────
  const VTransform& xf = unit->GetTransform();
  const float qx = xf.orient_.x;
  const float qy = xf.orient_.y;
  const float qz = xf.orient_.z;
  const float qw = xf.orient_.w;
  Wm3::Vector3f fwd3D{};
  fwd3D.x = (qx * qz + qw * qy) * 2.0f;
  fwd3D.y = (qw * qz - qx * qy) * 2.0f;
  fwd3D.z = 1.0f - 2.0f * (qz * qz + qy * qy);

  // ── Initial path point = current position, direction = forward ─────────────
  const Wm3::Vec3f& posRef = unit->GetPosition();
  CPathPoint curPoint{};
  curPoint.mPosition = posRef;
  curPoint.mDirection = fwd3D;
  curPoint.mState = PPS_1;

  const float topSpeed = unit->mInfoCache.mFormationTopSpeed;

  // ── Velocity magnitude + fwd-scaled vel3D working vector ──────────────────
  Wm3::Vec3f velSample = unit->GetVelocity();
  float speed = std::sqrt(
    velSample.x * velSample.x + velSample.y * velSample.y + velSample.z * velSample.z
  );

  Wm3::Vector3f vel3D = fwd3D;
  SetVecLength(vel3D, speed);

  // ── Normalized velocity direction (or FLT_MAX sentinel when stationary) ────
  Wm3::Vec3f vel2 = unit->GetVelocity();
  Wm3::Vector3f normVelDir{};
  if (speed == 0.0f) {
    normVelDir.x = std::numeric_limits<float>::max();
    normVelDir.y = std::numeric_limits<float>::max();
    normVelDir.z = std::numeric_limits<float>::max();
  } else {
    const float inv = 1.0f / speed;
    normVelDir.x = vel2.x * inv;
    normVelDir.y = vel2.y * inv;
    normVelDir.z = vel2.z * inv;
  }

  const RUnitBlueprintPhysics& phys = unit->GetBlueprint()->Physics;
  Sim* const sim = unit->SimulationRef;
  STIMap* const mapData = sim ? sim->mMapData : nullptr;

  // ── Pick initial state: forward vs backward motion ────────────────────────
  const float velFwdDot =
    normVelDir.y * fwd3D.y + normVelDir.z * fwd3D.z + normVelDir.x * fwd3D.x;

  bool skipLoop = false;
  if (velFwdDot >= 0.0f) {
    if (curPoint.mState == PPS_8) {
      skipLoop = true;
    }
  } else {
    vel3D.x = -vel3D.x;
    vel3D.y = -vel3D.y;
    vel3D.z = -vel3D.z;
    curPoint.mState = PPS_2;
  }

  // ── Deceleration loop ──────────────────────────────────────────────────────
  while (!skipLoop) {
    // Save continuation snapshot each iteration
    mContinuation.mOldDirection = fwd3D;
    mContinuation.mOldPosition = curPoint.mPosition;
    mContinuation.mOldVelocity = vel3D;
    mContinuation.mState = PPS_0;

    // Virtual destination used only to build SSteeringParams direction lanes
    const Wm3::Vector3f virtualDest{
      normVelDir.x + curPoint.mPosition.x,
      normVelDir.y + curPoint.mPosition.y,
      normVelDir.z + curPoint.mPosition.z
    };

    SSteeringParams sp(unit, curPoint.mPosition, virtualDest, normVelDir, topSpeed, false);

    if (mPathType == PT_4) {
      sp.mMaxAcceleration *= 2.0f;
      sp.mMaxBrake *= 2.0f;
    }

    // 2D braking acceleration vector opposing velocity (y = -3D z convention)
    const float oldVx = vel3D.x;
    const float oldVz = vel3D.z;
    const float oldNZ = -oldVz; // 2D-y lane = -3D-z
    float accX = -oldVx;
    float acc2DY = oldVz; // = -(-vel3D.z) in 2D-y convention → matches -oldNZ sign flip
    float speedSq2D = oldNZ * oldNZ + oldVx * oldVx;

    float newVx = oldVx;
    float newVz = oldVz;

    if (speedSq2D > 0.0f) {
      // Original cond (dot(-acc, -vel)) is always ≤ 0 → always brake magnitude
      const float maxBrake = sp.mMaxBrake;
      const float accMagSq = acc2DY * acc2DY + accX * accX;
      if (accMagSq > maxBrake * maxBrake) {
        const float scale = maxBrake / std::sqrt(accMagSq);
        accX *= scale;
        acc2DY *= scale;
      }

      // New 2D velocity = old + accel (in 2D convention)
      float cand2DX = accX + oldVx;
      float cand2DY = acc2DY + oldNZ;

      // Clamp to mMaxSpeed
      const float candMagSq = cand2DY * cand2DY + cand2DX * cand2DX;
      if (candMagSq > sp.mMaxSpeed * sp.mMaxSpeed) {
        const float s = sp.mMaxSpeed / std::sqrt(candMagSq);
        cand2DX *= s;
        cand2DY *= s;
      }

      const float cand3DZ = -cand2DY;
      const float finalMagSq = cand3DZ * cand3DZ + cand2DX * cand2DX;
      if (finalMagSq <= 1.0e-6f) {
        vel3D.x = 0.0f;
        vel3D.y = 0.0f;
        vel3D.z = 0.0f;
        newVx = 0.0f;
        newVz = 0.0f;
      } else {
        curPoint.mPosition.x += cand2DX;
        curPoint.mPosition.z += cand3DZ;
        vel3D.x = cand2DX;
        vel3D.y = 0.0f;
        vel3D.z = cand3DZ;
        newVx = cand2DX;
        newVz = cand3DZ;
      }
    }

    const float curSpeedSq2D = newVz * newVz + newVx * newVx;

    // Stopping condition for path-types 3/4
    if (curSpeedSq2D <= 1.0e-6f) {
      if (updateMode == 3 || (updateMode == 4 && nodes.size() > 5u)) {
        curPoint.mState = PPS_8;
      }
    }

    // Height field lookup for ground/water units
    if (mapData != nullptr && mapData->mHeightField) {
      const CHeightField& hf = *mapData->mHeightField;
      const float groundElev = hf.GetElevation(curPoint.mPosition.x, curPoint.mPosition.z);
      const ERuleBPUnitMovementType motionType = phys.MotionType;
      if (motionType == RULEUMT_Water
          || motionType == RULEUMT_AmphibiousFloating
          || motionType == RULEUMT_Hover) {
        float waterElev = groundElev;
        if (mapData->mWaterEnabled != 0 && mapData->mWaterElevation > groundElev) {
          waterElev = mapData->mWaterElevation;
        }
        curPoint.mPosition.y = waterElev;
      } else {
        curPoint.mPosition.y = groundElev;
      }
    }

    nodes.PushBack(curPoint);

    if (curPoint.mState == PPS_8) {
      break;
    }
  }

  (void)speed; // used by SetVecLength earlier
  mNodeCount = static_cast<std::uint32_t>(nodes.size());
  return static_cast<int>(mNodeCount);
}

/**
 * Address: 0x005B2FF0 (FUN_005B2FF0, Moho::CAiPathSpline::Generate)
 *
 * IDA signature:
 * void __userpurge Moho::CAiPathSpline::Generate(
 *   Moho::Unit *unit@<eax>, Moho::CAiPathSpline *this,
 *   Wm3::Vector3f *destin, int pathType, char allowContinuation)
 *
 * What it does:
 * Generates a full steering-path spline from the unit's current position toward
 * `destination`. Runs a per-tick physics simulation with a 7-state state machine
 * (PPS_1..PPS_7 + PPS_8 as terminator) that handles forward driving, reverse
 * backup, deceleration, and forward-alignment gating. Each iteration pushes one
 * CPathPoint into the node buffer. The loop terminates when the node cap is
 * reached, the unit arrives at the destination cell, or PPS_8 is set.
 *
 * States: PPS_1 = forward approach, PPS_2 = backward moving, PPS_3 = braking
 * to stop, PPS_4 = decelerating before reverse, PPS_5 = backing up,
 * PPS_6 = decelerating after backup, PPS_7 = normal forward steering,
 * PPS_8 = done.
 */
void CAiPathSpline::Generate(
  Unit* const unit,
  const Wm3::Vector3f& destination,
  const int pathType,
  const bool allowContinuation
)
{
  // ── Phase 1: Init ──────────────────────────────────────────────────────────
  if (unit->IsDead()) {
    gpg::Logf("Attempting to generate path spline for a dead unit!!!");
    return;
  }

  ResetNodesToInline();
  mCurrentNodeIndex = 0;
  mNodeCount = 0;
  mPathType = static_cast<EPathType>(pathType);

  const RUnitBlueprintPhysics& phys = unit->GetBlueprint()->Physics;

  // Node cap: 20 by default, 5 if in a forming formation, ×3 if has turn radius
  std::uint32_t nodeCap = 20;
  if (CAiFormationInstance* const fi = unit->GetFormation()) {
    if (fi->CommandIsForm()) {
      nodeCap = formation_path_value; // = 5
    }
  }
  const bool hasTurnRadius = phys.TurnRadius > phys.TurnRate;
  if (hasTurnRadius) {
    nodeCap *= 3;
  }

  // ── Phase 2: Forward from quaternion + singularity guard ───────────────────
  const VTransform& xf = unit->GetTransform();
  const float qx = xf.orient_.x;
  const float qy = xf.orient_.y;
  const float qz = xf.orient_.z;
  const float qw = xf.orient_.w;
  const float half_fwd_x = qx * qz + qw * qy;
  Wm3::Vector3f fwd3D{};
  fwd3D.x = half_fwd_x * 2.0f;
  fwd3D.y = (qw * qz - qx * qy) * 2.0f;
  fwd3D.z = 1.0f - (qz * qz + qy * qy) * 2.0f;
  if (std::fabs(fwd3D.y) > 0.99000001f) {
    fwd3D = {0.0f, 0.0f, 1.0f};
  }

  // ── Phase 3: Position, destination, velocity ──────────────────────────────
  const Wm3::Vec3f& posRef = unit->GetPosition();
  float curX = posRef.x;
  float curY = posRef.y;
  float curZ = posRef.z;
  Wm3::Vector3f curDir = fwd3D;
  EPathPointState state = PPS_7;

  const float topSpeed = unit->mInfoCache.mFormationTopSpeed;
  Sim* const sim = unit->SimulationRef;
  STIMap* const mapData = sim ? sim->mMapData : nullptr;

  // Delta to destination
  float dx = destination.x - curX;
  float dz = destination.z - curZ;
  float distToDest = std::sqrt(dx * dx + dz * dz);
  if (distToDest < 0.001f) {
    return;
  }

  // Normalized direction to destination
  Wm3::Vector3f toDestNorm{};
  if (distToDest == 0.0f) {
    toDestNorm = {std::numeric_limits<float>::max(),
                  std::numeric_limits<float>::max(),
                  std::numeric_limits<float>::max()};
  } else {
    const float inv = 1.0f / distToDest;
    toDestNorm = {dx * inv, 0.0f, dz * inv};
  }

  // Velocity and speed
  Wm3::Vec3f vel3D = unit->GetVelocity();
  float curSpeed = std::sqrt(vel3D.x * vel3D.x + vel3D.y * vel3D.y + vel3D.z * vel3D.z);
  float speedRatio = curSpeed * 10.0f / phys.MaxSpeed;

  // Forward-alignment: dot(toDestNorm, fwd3D)
  float fwdDot = toDestNorm.z * fwd3D.z + toDestNorm.y * fwd3D.y
               + toDestNorm.x * fwd3D.x;

  bool doBackup = false;

  // ── Phase 4: Continuation restore ─────────────────────────────────────────
  if (!allowContinuation) {
    const EPathPointState contState = mContinuation.mState;
    curX = mContinuation.mOldPosition.x;
    curY = mContinuation.mOldPosition.y;
    curZ = mContinuation.mOldPosition.z;
    fwd3D = mContinuation.mOldDirection;
    vel3D = mContinuation.mOldVelocity;
    if (contState != PPS_0) {
      state = contState;
    }
    goto label_32;
  }

  // ── Phase 5: Initial state selection for reverse-capable units ─────────────
  if (phys.MaxSpeedReverse > 0.0f && phys.RotateOnSpot == 0) {
    const Wm3::Vec3f velSample = unit->GetVelocity();
    const bool movingBackward = (velSample.z * fwd3D.z + velSample.y * fwd3D.y
                               + velSample.x * fwd3D.x) < 0.0f;
    if (fwdDot < 0.0f && (speedRatio < 0.5f || hasTurnRadius)) {
      if (speedRatio > 0.0099999998f && !movingBackward) {
        state = PPS_4;
        goto label_40;
      }
      goto label_29;
    }
    if (movingBackward) {
      state = (fwdDot >= 0.0f) ? PPS_6 : PPS_5;
      goto label_40;
    }
  }

label_32:
  if (state == PPS_5) {
label_29:
    if (phys.BackUpDistance > distToDest && fwdDot < -0.5f) {
      doBackup = true;
    }
  } else if (state == PPS_8) {
    goto generate_done;
  }

  // ── Phase 6: Main generation loop ─────────────────────────────────────────
  for (;;) {
label_40:
    // ── Save continuation ──
    mContinuation.mOldDirection = fwd3D;
    mContinuation.mOldPosition = {curX, curY, curZ};
    mContinuation.mOldVelocity = vel3D;
    mContinuation.mState = PPS_0;

    // ── AmphibiousFloating speed scale for water/land transition ──
    float speedScale = 1.0f;
    if (phys.LayerChangeOffsetHeight < -0.5f
        && phys.MotionType == RULEUMT_AmphibiousFloating
        && mapData != nullptr && mapData->mHeightField) {
      const float layerThreshold = phys.LayerChangeOffsetHeight * 3.0f;
      const float groundElev = mapData->mHeightField->GetElevation(curX, curZ);
      const float waterElev = mapData->GetWaterElevation();
      const float depth = groundElev - waterElev;
      if (depth > layerThreshold && depth < 0.0f) {
        speedScale = (depth / layerThreshold > 0.5f) ? (depth / layerThreshold) : 0.5f;
      }
    }

    // ── Build SSteeringParams ──
    const Wm3::Vector3f curPos3D{curX, curY, curZ};
    SSteeringParams sp(unit, curPos3D, destination, fwd3D, speedScale * topSpeed, false);

    // 2D velocity in steering convention (y = -3D z)
    float xOldVel = vel3D.x;
    float zOldVel = -vel3D.z;

    // Speed magnitude
    curSpeed = std::sqrt(zOldVel * zOldVel + xOldVel * xOldVel);

    // Turn rate: max(curSpeed / turnRadius, turnRate)
    float turnRate = curSpeed / sp.mInvTurnRadius;
    if (sp.mTurnRate > turnRate) {
      turnRate = sp.mTurnRate;
    }

    // PT_2 doubles accel and turn rate
    if (mPathType == PT_2) {
      sp.mMaxAcceleration *= 2.0f;
      sp.mMaxBrake *= 2.0f;
      turnRate *= 2.0f;
    }

    // ── SteerTurnVec: rotate heading toward destination ──
    float newFwdX, newFwdY;
    if (doBackup) {
      Wm3::Vector2f tmp{};
      SteerTurnVec(&tmp, turnRate, -sp.mVec1.x, -sp.mVec1.y, sp.mDX, sp.mDZ);
      newFwdX = -tmp.x;
      newFwdY = -tmp.y;
    } else {
      Wm3::Vector2f tmp{};
      SteerTurnVec(&tmp, turnRate, sp.mVec1.x, sp.mVec1.y, sp.mDX, sp.mDZ);
      newFwdX = tmp.x;
      newFwdY = tmp.y;
    }

    // Store new forward in 3D (for curDir assignment later)
    const float savedNewFwdX = newFwdX;
    const float savedNewFwdZ = -newFwdY;
    fwd3D = {newFwdX, 0.0f, -newFwdY};

    // For backward states, negate steering direction
    if (state == PPS_5 || state == PPS_6 || state == PPS_2) {
      newFwdX = -newFwdX;
      newFwdY = -newFwdY;
    }

    // ── Lateral velocity removal ──
    const float fwdLenSq = newFwdX * newFwdX + newFwdY * newFwdY;
    float projVelX = 0.0f, projVelY = 0.0f;
    if (fwdLenSq > 0.0f) {
      const float dot = (newFwdY * zOldVel + newFwdX * xOldVel) / fwdLenSq;
      projVelX = dot * newFwdX;
      projVelY = dot * newFwdY;
    }
    float lateralX = xOldVel - projVelX;
    float lateralY = zOldVel - projVelY;
    const float lateralMagSq = lateralX * lateralX + lateralY * lateralY;
    if (lateralMagSq > sp.mMaxSteer * sp.mMaxSteer) {
      const float s = sp.mMaxSteer / std::sqrt(lateralMagSq);
      lateralX *= s;
      lateralY *= s;
    }
    float velX = xOldVel - lateralX;
    float velZ = zOldVel - lateralY;

    // ── Wanted speed calculation (state-dependent) ──
    float wantSpeed = 0.0f;
    float maxSpeedLocal = 0.0f;
    bool needSmoothing = false;

    if (state == PPS_1 || state == PPS_3 || state == PPS_4 || state == PPS_6) {
      needSmoothing = true;
    } else if (state == PPS_5 || state == PPS_2) {
      wantSpeed = WantedSpeed(sp, speedRatio);
      if (wantSpeed > sp.mMaxReserveSpeed) {
        wantSpeed = sp.mMaxReserveSpeed;
      }
      maxSpeedLocal = wantSpeed;
    } else {
      wantSpeed = WantedSpeed(sp, speedRatio);
      if (wantSpeed > sp.mMaxSpeed) {
        maxSpeedLocal = sp.mMaxSpeed;
        wantSpeed = sp.mMaxSpeed;
      } else {
        maxSpeedLocal = wantSpeed;
      }
    }

    // PT_0 braking cap
    if (!needSmoothing && mPathType == PT_0) {
      float brakeCap = sp.mDist;
      if (sp.mMaxBrake < sp.mDist) {
        brakeCap = static_cast<float>(std::sqrt(
          static_cast<double>(sp.mDist) * static_cast<double>(sp.mMaxBrake) * 2.0));
      }
      if (brakeCap <= maxSpeedLocal) {
        maxSpeedLocal = brakeCap;
        wantSpeed = brakeCap;
      }
    }

    // hasTurnRadius dot-product speed scale
    if (!needSmoothing && hasTurnRadius) {
      float dotVal = doBackup ? -fwdDot : fwdDot;
      if (dotVal < -0.5f) {
        dotVal = -0.5f;
      }
      maxSpeedLocal = (dotVal + 1.0f) * 0.5f * maxSpeedLocal;
      wantSpeed = maxSpeedLocal;
    }

    // ── Velocity smoothing (80/20 blend with forward projection) ──
    float smoothedX = velX;
    float smoothedZ = velZ;

    if (needSmoothing || maxSpeedLocal < 0.001f) {
      // Blend 80% raw velocity + 20% forward-projected velocity
      float blendX = newFwdX;
      float blendY = newFwdY;
      if (fwdLenSq != 0.0f) {
        const float rawMag = std::sqrt(velX * velX + velZ * velZ);
        const float projScale = rawMag / std::sqrt(fwdLenSq);
        blendX = newFwdX * projScale;
        blendY = newFwdY * projScale;
      }
      smoothedX = blendX * 0.2f + velX * 0.80000001f;
      smoothedZ = blendY * 0.2f + velZ * 0.80000001f;
      maxSpeedLocal = wantSpeed;
    }

    // ── Acceleration toward desired velocity ──
    float accX = newFwdX * maxSpeedLocal - smoothedX;
    float accY = newFwdY * maxSpeedLocal - smoothedZ;

    // Pick brake or acceleration magnitude based on dot(acc, vel)
    const float maxForce = (accX * smoothedX + accY * smoothedZ <= 0.0f)
                         ? sp.mMaxBrake
                         : sp.mMaxAcceleration;
    const float accMagSq = accX * accX + accY * accY;
    if (accMagSq > maxForce * maxForce) {
      const float s = maxForce / std::sqrt(accMagSq);
      accX *= s;
      accY *= s;
    }

    float moveX = accX + smoothedX;
    float moveZ = accY + smoothedZ;

    // Speed clamp (MaxReserveSpeed for backward states, MaxSpeed for others)
    if (state == PPS_5 || state == PPS_6 || state == PPS_2) {
      const float moveMagSq = moveX * moveX + moveZ * moveZ;
      if (moveMagSq > sp.mMaxReserveSpeed * sp.mMaxReserveSpeed) {
        const float s = sp.mMaxReserveSpeed / std::sqrt(moveMagSq);
        moveX *= s;
        moveZ *= s;
      }
    } else {
      const float moveMagSq = moveX * moveX + moveZ * moveZ;
      if (moveMagSq > sp.mMaxSpeed * sp.mMaxSpeed) {
        const float s = sp.mMaxSpeed / std::sqrt(moveMagSq);
        moveX *= s;
        moveZ *= s;
      }
    }

    // ── Position update ──
    curX += moveX;
    curZ -= moveZ; // 2D-Y → 3D-Z (negated)

    // Update curDir, vel3D, speed
    curDir = {savedNewFwdX, 0.0f, savedNewFwdZ};
    vel3D = {moveX, 0.0f, -moveZ};

    curSpeed = std::sqrt(moveX * moveX + moveZ * moveZ);
    speedRatio = curSpeed / sp.mMaxSpeed;

    // ── Height field lookup ──
    if (mapData != nullptr && mapData->mHeightField) {
      const ERuleBPUnitMovementType motionType = phys.MotionType;
      if (motionType == RULEUMT_Water
          || motionType == RULEUMT_AmphibiousFloating
          || motionType == RULEUMT_Hover) {
        float elev = mapData->mHeightField->GetElevation(curX, curZ);
        if (mapData->mWaterEnabled != 0 && mapData->mWaterElevation > elev) {
          elev = mapData->mWaterElevation;
        }
        curY = elev;
      } else {
        curY = mapData->mHeightField->GetElevation(curX, curZ);
      }
    }

    // ── Push path node ──
    CPathPoint pt{};
    pt.mPosition = {curX, curY, curZ};
    pt.mDirection = curDir;
    pt.mState = state;
    nodes.PushBack(pt);

    // ── Distance / node-cap check ──
    dx = destination.x - curX;
    dz = destination.z - curZ;
    distToDest = std::sqrt(dx * dx + dz * dz);

    if (nodes.size() >= nodeCap || distToDest < 0.001f) {
      mContinuation.mState = state;
      state = PPS_8;
    }

    // Update toDestNorm and fwdDot for the new position
    if (distToDest == 0.0f) {
      toDestNorm = {std::numeric_limits<float>::max(),
                    std::numeric_limits<float>::max(),
                    std::numeric_limits<float>::max()};
    } else {
      const float inv = 1.0f / distToDest;
      toDestNorm = {dx * inv, 0.0f, dz * inv};
    }
    fwdDot = toDestNorm.z * fwd3D.z + toDestNorm.x * fwd3D.x;

    // IsAtPosition check
    if (unit->IsAtPosition(destination)) {
      state = PPS_8;
    }

    // ── State machine transitions ────────────────────────────────────────────
    switch (state) {
    case PPS_3:
      if (speedRatio <= 0.0099999998f) goto generate_done;
      goto label_135;

    case PPS_4:
      if (speedRatio > 0.0099999998f) goto label_135;
      if (phys.MaxSpeedReverse <= 0.0f || phys.RotateOnSpot != 0) goto label_159;
      state = PPS_5;
      continue;

    case PPS_5: {
      float maxAcc5 = phys.MaxAcceleration;
      if (phys.MaxBrake > maxAcc5) maxAcc5 = phys.MaxBrake;
      const float stopDist5 = (maxAcc5 <= 0.0f) ? 0.0f
        : (curSpeed * 10.0f) * (curSpeed * 10.0f) / (maxAcc5 * 2.0f);
      const SFootprint& fp5 = unit->GetFootprint();
      const float fpSize5 = static_cast<float>(std::max(fp5.mSizeX, fp5.mSizeZ));
      Wm3::Vector3f probe5{vel3D.x, vel3D.y, vel3D.z};
      SetVecLength(probe5, fpSize5 + stopDist5);
      probe5.x += curX;
      probe5.y += curY;
      probe5.z += curZ;
      if (UnitWontFitAt(probe5, unit)) {
        state = PPS_6;
        continue;
      }
      if (doBackup ? (stopDist5 <= distToDest) : (fwdDot <= 0.15000001f)) {
        goto label_135;
      }
      state = PPS_6;
      continue;
    }

    case PPS_6:
      if (speedRatio > 0.0099999998f) goto label_135;
      if (doBackup) goto generate_done;
label_159:
      state = PPS_7;
      break;

    case PPS_7: {
      float maxAcc7 = phys.MaxAcceleration;
      if (phys.MaxBrake > maxAcc7) maxAcc7 = phys.MaxBrake;
      const float stopDist7 = (maxAcc7 <= 0.0f) ? 0.0f
        : (curSpeed * 10.0f) * (curSpeed * 10.0f) / (maxAcc7 * 2.0f);
      if (mPathType == PT_0 && stopDist7 > distToDest) {
        state = PPS_3;
      }
      if (fwdDot >= 0.86600000f) goto label_135;
      const SFootprint& fp7 = unit->GetFootprint();
      const float fpSize7 = static_cast<float>(std::max(fp7.mSizeX, fp7.mSizeZ));
      Wm3::Vector3f probe7{vel3D.x, vel3D.y, vel3D.z};
      SetVecLength(probe7, fpSize7 + stopDist7);
      probe7.x += curX;
      probe7.y += curY;
      probe7.z += curZ;
      if (UnitWontFitAt(probe7, unit)) {
        state = PPS_4;
      } else if (hasTurnRadius && IsUnitBlockedAt(probe7, unit, 2)) {
        state = PPS_4;
      } else {
        goto label_135;
      }
      continue;
    }

    default:
label_135:
      if (state == PPS_8) goto generate_done;
      break;
    }
  }

generate_done:
  mNodeCount = static_cast<std::uint32_t>(nodes.size());
}

/**
 * Address: 0x005B5FB0 (FUN_005B5FB0, Moho::CAiPathSpline::MemberDeserialize)
 */
void CAiPathSpline::MemberDeserialize(gpg::ReadArchive* const archive)
{
  if (!archive) {
    return;
  }

  const gpg::RRef ownerRef{};
  gpg::RType* const pathNodeVectorType = ResolveFastVectorCPathPointType();
  archive->Read(pathNodeVectorType, this, ownerRef);

  auto& nodeView = gpg::AsFastVectorRuntimeView<CPathPoint>(this);
  const std::size_t nodeCount = nodeView.begin ? nodeView.Size() : 0u;
  gpg::RType* const nodeType = ResolveCPathPointType();
  for (std::size_t i = 0; i < nodeCount; ++i) {
    gpg::RRef nodeRef{};
    nodeRef.mObj = nodeView.ElementAtUnchecked(i);
    nodeRef.mType = nodeType;
    archive->TrackPointer(nodeRef);
  }

  archive->ReadUInt(&mCurrentNodeIndex);
  archive->ReadUInt(&mNodeCount);

  gpg::RType* const pathTypeType = ResolvePathSplineTypeType();
  archive->Read(pathTypeType, &mPathType, ownerRef);

  gpg::RType* const continuationType = ResolvePathSplineContinuationType();
  archive->Read(continuationType, &mContinuation, ownerRef);
}

/**
 * Address: 0x005B60E0 (FUN_005B60E0, Moho::CAiPathSpline::MemberSerialize)
 */
void CAiPathSpline::MemberSerialize(gpg::WriteArchive* const archive) const
{
  if (!archive) {
    return;
  }

  const gpg::RRef ownerRef{};
  gpg::RType* const pathNodeVectorType = ResolveFastVectorCPathPointType();
  archive->Write(pathNodeVectorType, this, ownerRef);

  const auto& nodeView = gpg::AsFastVectorRuntimeView<CPathPoint>(this);
  const std::size_t nodeCount = nodeView.begin ? nodeView.Size() : 0u;
  gpg::RType* const nodeType = ResolveCPathPointType();
  for (std::size_t i = 0; i < nodeCount; ++i) {
    gpg::RRef nodeRef{};
    nodeRef.mObj = const_cast<CPathPoint*>(nodeView.ElementAtUnchecked(i));
    nodeRef.mType = nodeType;
    archive->PreCreatedPtr(nodeRef);
  }

  archive->WriteUInt(mCurrentNodeIndex);
  archive->WriteUInt(mNodeCount);

  gpg::RType* const pathTypeType = ResolvePathSplineTypeType();
  archive->Write(pathTypeType, &mPathType, ownerRef);

  gpg::RType* const continuationType = ResolvePathSplineContinuationType();
  archive->Write(continuationType, &mContinuation, ownerRef);
}

/**
 * Address: 0x00596730 (FUN_00596730, Moho::SCollisionInfoTypeInfo::SCollisionInfoTypeInfo)
 */
SCollisionInfoTypeInfo::SCollisionInfoTypeInfo()
{
  gpg::PreRegisterRType(typeid(SCollisionInfo), this);
  gSCollisionInfoType = this;
}

SCollisionInfoTypeInfo::~SCollisionInfoTypeInfo() = default;

/**
 * Address: 0x005967B0 (FUN_005967B0, Moho::SCollisionInfoTypeInfo::GetName)
 */
const char* SCollisionInfoTypeInfo::GetName() const
{
  return "SCollisionInfo";
}

/**
 * Address: 0x00596790 (FUN_00596790, SCollisionInfoTypeInfo::Init)
 */
void SCollisionInfoTypeInfo::Init()
{
  size_ = sizeof(SCollisionInfo);
  gpg::RType::Init();
  Finish();
}

/**
 * Address: 0x00596600 (FUN_00596600, Moho::ECollisionTypeTypeInfo::ECollisionTypeTypeInfo)
 */
ECollisionTypeTypeInfo::ECollisionTypeTypeInfo()
{
  gpg::PreRegisterRType(typeid(ECollisionType), this);
  gECollisionTypeType = this;
}

/**
 * Address: 0x00BF6510 (FUN_00BF6510, Moho::ECollisionTypeTypeInfo::~ECollisionTypeTypeInfo)
 */
ECollisionTypeTypeInfo::~ECollisionTypeTypeInfo() = default;

/**
 * Address: 0x00596680 (FUN_00596680, Moho::ECollisionTypeTypeInfo::GetName)
 */
const char* ECollisionTypeTypeInfo::GetName() const
{
  return "ECollisionType";
}

/**
 * Address: 0x00596660 (FUN_00596660, Moho::ECollisionTypeTypeInfo::Init)
 */
void ECollisionTypeTypeInfo::Init()
{
  size_ = sizeof(ECollisionType);
  gpg::RType::Init();
  Finish();
}

CPathPointTypeInfo::~CPathPointTypeInfo() = default;

const char* CPathPointTypeInfo::GetName() const
{
  return "CPathPoint";
}

/**
 * Address: 0x0062F6B0 (FUN_0062F6B0, CPathPointTypeInfo::Init)
 */
void CPathPointTypeInfo::Init()
{
  size_ = sizeof(CPathPoint);
  gpg::RType::Init();
  Finish();
}

EPathPointStateTypeInfo::~EPathPointStateTypeInfo() = default;

const char* EPathPointStateTypeInfo::GetName() const
{
  return "EPathPointState";
}

void EPathPointStateTypeInfo::Init()
{
  size_ = sizeof(EPathPointState);
  gpg::RType::Init();
  Finish();
}

/**
 * Address: 0x005B4950 (FUN_005B4950, gpg::RFastVectorType_CPathPoint::GetName)
 *
 * What it does:
 * Lazily builds and caches the reflected `fastvector<CPathPoint>` type name
 * from the resolved `CPathPoint` element RTTI lane.
 */
const char* FastVectorCPathPointTypeInfo::GetName() const
{
  if (gFastVectorCPathPointTypeName.empty()) {
    const gpg::RType* const elementType = ResolveCPathPointType();
    const char* const elementName = elementType ? elementType->GetName() : "CPathPoint";
    gFastVectorCPathPointTypeName = gpg::STR_Printf("fastvector<%s>", elementName ? elementName : "CPathPoint");
    if (!gFastVectorCPathPointTypeNameCleanupRegistered) {
      gFastVectorCPathPointTypeNameCleanupRegistered = true;
      (void)std::atexit(&cleanup_FastVectorCPathPointTypeName);
    }
  }

  return gFastVectorCPathPointTypeName.c_str();
}

/**
 * Address: 0x005B4A10 (FUN_005B4A10, gpg::RFastVectorType_CPathPoint::GetLexical)
 *
 * What it does:
 * Formats vector lexical text and appends the runtime path-point count.
 */
msvc8::string FastVectorCPathPointTypeInfo::GetLexical(const gpg::RRef& ref) const
{
  const msvc8::string base = gpg::RType::GetLexical(ref);
  return gpg::STR_Printf("%s, size=%d", base.c_str(), static_cast<int>(GetCount(ref.mObj)));
}

const gpg::RIndexed* FastVectorCPathPointTypeInfo::IsIndexed() const
{
  return this;
}

void FastVectorCPathPointTypeInfo::Init()
{
  size_ = 0x10;
  version_ = 1;
  serLoadFunc_ = &LoadFastVectorCPathPoint;
  serSaveFunc_ = &SaveFastVectorCPathPoint;
}

gpg::RRef FastVectorCPathPointTypeInfo::SubscriptIndex(void* obj, const int ind) const
{
  gpg::RRef out{};
  out.mType = ResolveCPathPointType();
  out.mObj = nullptr;
  if (!obj || ind < 0) {
    return out;
  }

  auto& view = gpg::AsFastVectorRuntimeView<CPathPoint>(obj);
  if (!view.begin || static_cast<std::size_t>(ind) >= GetCount(obj)) {
    return out;
  }

  out.mObj = view.ElementAtUnchecked(static_cast<std::size_t>(ind));
  return out;
}

size_t FastVectorCPathPointTypeInfo::GetCount(void* obj) const
{
  if (!obj) {
    return 0u;
  }

  const auto& view = gpg::AsFastVectorRuntimeView<CPathPoint>(obj);
  if (!view.begin) {
    return 0u;
  }

  return view.Size();
}

void FastVectorCPathPointTypeInfo::SetCount(void* obj, const int count) const
{
  if (!obj || count < 0) {
    return;
  }

  auto& view = gpg::AsFastVectorRuntimeView<CPathPoint>(obj);
  const CPathPoint fill{};
  gpg::FastVectorRuntimeResizeFill(&fill, static_cast<unsigned int>(count), view);
}

void EPathPointStatePrimitiveSerializer::RegisterSerializeFunctions()
{
  gpg::RType* const type = ResolveEPathPointStateType();
  GPG_ASSERT(type != nullptr);
  GPG_ASSERT(type->serLoadFunc_ == nullptr || type->serLoadFunc_ == mDeserialize);
  GPG_ASSERT(type->serSaveFunc_ == nullptr || type->serSaveFunc_ == mSerialize);
  type->serLoadFunc_ = mDeserialize;
  type->serSaveFunc_ = mSerialize;
}

/**
 * Address: 0x00598400 (FUN_00598400, gpg::PrimitiveSerHelper_ECollisionType_int::Deserialize)
 */
void ECollisionTypePrimitiveSerializer::Deserialize(
  gpg::ReadArchive* const archive,
  const int objectPtr,
  const int,
  gpg::RRef* const
)
{
  if (archive == nullptr || objectPtr == 0) {
    return;
  }

  int value = 0;
  archive->ReadInt(&value);
  *reinterpret_cast<ECollisionType*>(static_cast<std::uintptr_t>(objectPtr)) = static_cast<ECollisionType>(value);
}

/**
 * Address: 0x00598420 (FUN_00598420, gpg::PrimitiveSerHelper_ECollisionType_int::Serialize)
 */
void ECollisionTypePrimitiveSerializer::Serialize(
  gpg::WriteArchive* const archive,
  const int objectPtr,
  const int,
  gpg::RRef* const
)
{
  if (archive == nullptr || objectPtr == 0) {
    return;
  }

  const auto* const value = reinterpret_cast<const ECollisionType*>(static_cast<std::uintptr_t>(objectPtr));
  archive->WriteInt(static_cast<int>(*value));
}

void ECollisionTypePrimitiveSerializer::RegisterSerializeFunctions()
{
  gpg::RType* const type = ResolveECollisionTypeType();
  GPG_ASSERT(type != nullptr);
  GPG_ASSERT(type->serLoadFunc_ == nullptr || type->serLoadFunc_ == mDeserialize);
  GPG_ASSERT(type->serSaveFunc_ == nullptr || type->serSaveFunc_ == mSerialize);
  type->serLoadFunc_ = mDeserialize;
  type->serSaveFunc_ = mSerialize;
}

/**
 * Address: 0x00596870 (FUN_00596870, Moho::SCollisionInfoSerializer::Deserialize)
 */
void SCollisionInfoSerializer::Deserialize(gpg::ReadArchive* const archive, const int objectPtr, const int, gpg::RRef* const)
{
  auto* const info = reinterpret_cast<SCollisionInfo*>(static_cast<std::uintptr_t>(objectPtr));
  if (archive == nullptr || info == nullptr) {
    return;
  }
  info->MemberDeserialize(archive);
}

/**
 * Address: 0x00596880 (FUN_00596880, Moho::SCollisionInfoSerializer::Serialize)
 */
void SCollisionInfoSerializer::Serialize(gpg::WriteArchive* const archive, const int objectPtr, const int, gpg::RRef* const)
{
  auto* const info = reinterpret_cast<SCollisionInfo*>(static_cast<std::uintptr_t>(objectPtr));
  if (archive == nullptr || info == nullptr) {
    return;
  }
  info->MemberSerialize(archive);
}

/**
 * Address: 0x00598390 (FUN_00598390, gpg::SerSaveLoadHelper<Moho::SCollisionInfo>::Init)
 */
void SCollisionInfoSerializer::RegisterSerializeFunctions()
{
  gpg::RType* const type = ResolveSCollisionInfoType();
  GPG_ASSERT(type != nullptr);
  GPG_ASSERT(type->serLoadFunc_ == nullptr);
  type->serLoadFunc_ = mDeserialize;
  GPG_ASSERT(type->serSaveFunc_ == nullptr);
  type->serSaveFunc_ = mSerialize;
}

/**
 * Address: 0x0062F790 (FUN_0062F790, CPathPointSerializer::Deserialize)
 *
 * What it does:
 * Loads `mPosition`, `mDirection`, and `mState` lanes for a `CPathPoint`.
 */
void CPathPointSerializer::Deserialize(
  gpg::ReadArchive* const archive,
  const int objectPtr,
  const int,
  gpg::RRef* const
)
{
  if (archive == nullptr || objectPtr == 0) {
    return;
  }

  auto* const point = reinterpret_cast<CPathPoint*>(static_cast<std::uintptr_t>(objectPtr));
  gpg::RRef valueRef{};
  gpg::RRef directionRef{};
  gpg::RRef stateRef{};

  gpg::RType* const vectorType = ResolveVector3fType();
  GPG_ASSERT(vectorType != nullptr);
  archive->Read(vectorType, &point->mPosition, valueRef);
  archive->Read(vectorType, &point->mDirection, directionRef);

  gpg::RType* const stateType = ResolveEPathPointStateType();
  GPG_ASSERT(stateType != nullptr);
  archive->Read(stateType, &point->mState, stateRef);
}

/**
 * Address: 0x0062F7A0 (FUN_0062F7A0, CPathPointSerializer::Serialize)
 *
 * What it does:
 * Saves `mPosition`, `mDirection`, and `mState` lanes for a `CPathPoint`.
 */
void CPathPointSerializer::Serialize(
  gpg::WriteArchive* const archive,
  const int objectPtr,
  const int,
  gpg::RRef* const
)
{
  if (archive == nullptr || objectPtr == 0) {
    return;
  }

  auto* const point = reinterpret_cast<CPathPoint*>(static_cast<std::uintptr_t>(objectPtr));
  gpg::RRef valueRef{};
  gpg::RRef directionRef{};
  gpg::RRef stateRef{};

  gpg::RType* const vectorType = ResolveVector3fType();
  GPG_ASSERT(vectorType != nullptr);
  archive->Write(vectorType, &point->mPosition, valueRef);
  archive->Write(vectorType, &point->mDirection, directionRef);

  gpg::RType* const stateType = ResolveEPathPointStateType();
  GPG_ASSERT(stateType != nullptr);
  archive->Write(stateType, &point->mState, stateRef);
}

void CPathPointSerializer::RegisterSerializeFunctions()
{
  gpg::RType* const type = ResolveCPathPointType();
  GPG_ASSERT(type != nullptr);
  GPG_ASSERT(type->serLoadFunc_ == nullptr || type->serLoadFunc_ == mDeserialize);
  GPG_ASSERT(type->serSaveFunc_ == nullptr || type->serSaveFunc_ == mSerialize);
  type->serLoadFunc_ = mDeserialize;
  type->serSaveFunc_ = mSerialize;
}

namespace
{
  /**
   * Address: 0x00598440 (FUN_00598440, construct_PrimitiveSerHelper_ECollisionType_int)
   *
   * What it does:
   * Constructs startup serializer-helper state for `ECollisionType` and binds
   * raw integer load/save callbacks.
   */
  [[nodiscard]] ECollisionTypePrimitiveSerializer* construct_PrimitiveSerHelper_ECollisionType_int()
  {
    ECollisionTypePrimitiveSerializer* const serializer = AcquireECollisionTypePrimitiveSerializer();
    InitializeSerializerNode(*serializer);
    serializer->mDeserialize = &ECollisionTypePrimitiveSerializer::Deserialize;
    serializer->mSerialize = &ECollisionTypePrimitiveSerializer::Serialize;
    return serializer;
  }
} // namespace

/**
 * Address: 0x00BCBDB0 (FUN_00BCBDB0, register_SCollisionInfoTypeInfo)
 */
int moho::register_SCollisionInfoTypeInfo()
{
  (void)AcquireSCollisionInfoTypeInfo();
  return std::atexit(&cleanup_SCollisionInfoTypeInfo);
}

/**
 * Address: 0x00BCBD50 (FUN_00BCBD50, register_ECollisionTypeTypeInfo)
 */
void moho::register_ECollisionTypeTypeInfo()
{
  (void)AcquireECollisionTypeTypeInfo();
  (void)std::atexit(&cleanup_ECollisionTypeTypeInfo);
}

/**
 * Address: 0x00BCBD70 (FUN_00BCBD70, register_PrimitiveSerHelper_ECollisionType_int)
 */
void moho::register_PrimitiveSerHelper_ECollisionType_int()
{
  (void)construct_PrimitiveSerHelper_ECollisionType_int();
  (void)std::atexit(&cleanup_PrimitiveSerHelper_ECollisionType_int_atexit);
}

/**
 * Address: 0x00BCBDD0 (FUN_00BCBDD0, register_SCollisionInfoSerializer)
 */
void moho::register_SCollisionInfoSerializer()
{
  SCollisionInfoSerializer* const serializer = AcquireSCollisionInfoSerializer();
  InitializeSerializerNode(*serializer);
  serializer->mDeserialize = &SCollisionInfoSerializer::Deserialize;
  serializer->mSerialize = &SCollisionInfoSerializer::Serialize;
  serializer->RegisterSerializeFunctions();
  (void)std::atexit(&cleanup_SCollisionInfoSerializer_atexit);
}

/**
 * Address: 0x00BD20C0 (FUN_00BD20C0, register_EPathPointStateTypeInfo)
 *
 * What it does:
 * Constructs/preregisters `EPathPointState` type info and schedules teardown.
 */
int moho::register_EPathPointStateTypeInfo()
{
  (void)construct_EPathPointStateTypeInfo();
  return std::atexit(&cleanup_EPathPointStateTypeInfo);
}

/**
 * Address: 0x00BD20E0 (FUN_00BD20E0, register_EPathPointStatePrimitiveSerializer)
 *
 * What it does:
 * Registers primitive serializer callbacks for `EPathPointState`.
 */
int moho::register_EPathPointStatePrimitiveSerializer()
{
  InitializeSerializerNode(gEPathPointStatePrimitiveSerializer);
  gEPathPointStatePrimitiveSerializer.mDeserialize = reinterpret_cast<gpg::RType::load_func_t>(
    &Deserialize_EPathPointState
  );
  gEPathPointStatePrimitiveSerializer.mSerialize = reinterpret_cast<gpg::RType::save_func_t>(
    &Serialize_EPathPointState
  );
  gEPathPointStatePrimitiveSerializer.RegisterSerializeFunctions();
  return std::atexit(&cleanup_EPathPointStatePrimitiveSerializer_atexit);
}

/**
 * Address: 0x00BD2120 (FUN_00BD2120, register_CPathPointTypeInfo)
 *
 * What it does:
 * Constructs/preregisters `CPathPoint` type info and schedules teardown.
 */
int moho::register_CPathPointTypeInfo()
{
  (void)construct_CPathPointTypeInfo();
  return std::atexit(&cleanup_CPathPointTypeInfo);
}

/**
 * Address: 0x00BD2140 (FUN_00BD2140, register_CPathPointSerializer)
 *
 * What it does:
 * Registers `CPathPoint` serializer callbacks and schedules helper teardown.
 */
void moho::register_CPathPointSerializer()
{
  InitializeSerializerNode(gCPathPointSerializer);
  gCPathPointSerializer.mDeserialize = &CPathPointSerializer::Deserialize;
  gCPathPointSerializer.mSerialize = &CPathPointSerializer::Serialize;
  gCPathPointSerializer.RegisterSerializeFunctions();
  (void)std::atexit(&cleanup_CPathPointSerializer_atexit);
}

/**
 * Address: 0x00BCD390 (FUN_00BCD390, register_FastVectorCPathPointTypeAtexit)
 *
 * What it does:
 * Constructs/preregisters startup RTTI metadata for `gpg::fastvector<CPathPoint>`
 * and installs process-exit teardown.
 */
int moho::register_FastVectorCPathPointTypeAtexit()
{
  (void)preregister_FastVectorCPathPointType();
  return std::atexit(&cleanup_FastVectorCPathPointType);
}

/**
 * Address: 0x00BCD3B0 (FUN_00BCD3B0, register_CAiPathSplineStartupStatsCleanup)
 *
 * What it does:
 * Installs process-exit cleanup for one startup-owned AI path-spline stats slot.
 */
int moho::register_CAiPathSplineStartupStatsCleanup()
{
  return std::atexit(&cleanup_CAiPathSplineStartupStats);
}

namespace
{
  struct CPathPointReflectionBootstrap
  {
    CPathPointReflectionBootstrap()
    {
      (void)moho::register_SCollisionInfoTypeInfo();
      moho::register_ECollisionTypeTypeInfo();
      moho::register_PrimitiveSerHelper_ECollisionType_int();
      moho::register_SCollisionInfoSerializer();
      (void)moho::register_FastVectorCPathPointTypeAtexit();
      (void)moho::register_CAiPathSplineStartupStatsCleanup();
      (void)moho::register_EPathPointStateTypeInfo();
      (void)moho::register_EPathPointStatePrimitiveSerializer();
      (void)moho::register_CPathPointTypeInfo();
      moho::register_CPathPointSerializer();
    }
  };

  [[maybe_unused]] CPathPointReflectionBootstrap gCPathPointReflectionBootstrap;
} // namespace
