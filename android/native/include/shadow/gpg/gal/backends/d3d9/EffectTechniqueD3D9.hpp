#pragma once

// Stand-in for src/gpg/gal/backends/d3d9/EffectTechniqueD3D9.hpp in the Android client build
// (android/native/include/shadow/README.md). moho/mesh/HardwareMeshBatch.cpp's open-pass draw
// calls CommitChanges on a technique it began on a Direct3D 9 device, which Android never has:
// declared here, defined in android/native/src/gal/D3D9Unavailable.cpp.

#include <cstddef>
#include <cstdint>

#include "boost/shared_ptr.h"
#include "boost/weak_ptr.h"
#include "gpg/gal/EffectTechnique.hpp"
#include "legacy/containers/String.h"

namespace gpg::gal
{
  class EffectD3D9;

  class EffectTechniqueD3D9 : public EffectTechnique
  {
  public:
    void CommitChanges();
  };
} // namespace gpg::gal
