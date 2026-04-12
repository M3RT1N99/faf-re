#pragma once
#include <cstdint>
#include <vector>

#include "gpg/core/containers/BitArray2D.h"
#include "gpg/core/reflection/Reflection.h"

namespace moho
{
  class Sim;
  class Unit;
  struct SOCellPos;

  struct EntityOccupation
  {};

  struct EntityOccupationGrid
  {
    int xPos;
    int zPos;
    int32_t lastIndex;
    int32_t gridWidth;
    std::vector<EntityOccupation>* units;
    std::vector<EntityOccupation>* props;
    std::vector<EntityOccupation>* entities;
    int32_t v8;
    int32_t v9;
    int32_t v10;
    void** start;
    void** end;
    int32_t v13;
  };
  static_assert(sizeof(EntityOccupationGrid) == 0x34, "EntityOccupationGrid size must be 0x34");

  class COGrid
  {
  public:
    inline static gpg::RType* sType = nullptr;

    Sim* sim;
    EntityOccupationGrid entityGrid;
    gpg::BitArray2D terrainOccupation;
    gpg::BitArray2D waterOccupation;
    gpg::BitArray2D mOccupation;

    /**
     * Address: 0x0062A970 (FUN_0062A970, Moho::COGrid::UnitIsBlocked)
     *
     * What it does:
     * Returns true if `unit`'s footprint at the cell position `cellPos` is
     * blocked in the occupancy grid under the requested `flags` mask. `flags`
     * bit 1 (0x2) requests the dynamic-entity layer in the original engine.
     *
     * NOTE: Current SDK representation of entity-grid contents is incomplete;
     * implementation is a conservative stub pending full bit-array semantics.
     */
    [[nodiscard]]
    static bool UnitIsBlocked(const SOCellPos* cellPos, COGrid* grid, Unit* unit, int flags);
  };

  /**
   * VFTABLE: 0x00E3192C
   * COL: 0x00E8E5B4
   */
  class COGridTypeInfo final : public gpg::RType
  {
  public:
    /**
     * Address: 0x00722B80 (FUN_00722B80, Moho::COGridTypeInfo::COGridTypeInfo)
     *
     * What it does:
     * Constructs and preregisters reflected RTTI ownership for `COGrid`.
     */
    COGridTypeInfo();

    /**
     * Address: 0x00722C10 (FUN_00722C10, Moho::COGridTypeInfo::dtr)
     */
    ~COGridTypeInfo() override;

    /**
     * Address: 0x00722C00 (FUN_00722C00, Moho::COGridTypeInfo::GetName)
     */
    [[nodiscard]] const char* GetName() const override;

    /**
     * Address: 0x00722BE0 (FUN_00722BE0, Moho::COGridTypeInfo::Init)
     */
    void Init() override;
  };

  /**
   * Address: 0x00BDAA90 (FUN_00BDAA90, register_COGridTypeInfo)
   *
   * What it does:
   * Materializes startup `COGridTypeInfo` storage and installs process-exit cleanup.
   */
  int register_COGridTypeInfo();

  static_assert(sizeof(COGridTypeInfo) == 0x64, "COGridTypeInfo size must be 0x64");
} // namespace moho
