// The storage of three Direct3D 12 switches, for the Android client build. The console variables
// d3d_PlacedResources, d3d_SharedBuffers and d3d_PipelineCache (moho/render/GpuPathSettings.cpp)
// point at them; the engine defines them in the Direct3D 12 backend's files
// (backends/d3d12/D3D12Core.cpp and PipelineCacheD3D12.cpp), which this build leaves out. No backend
// here reads them: the console keeps what it is given, from the same defaults.

#include "gpg/gal/PipelineDiskCache.h"
#include "gpg/gal/PlacedResources.h"

namespace gpg::gal
{
  bool gPlacedResources = true;
  bool gSharedBuffers = true;
  bool gPipelineDiskCache = true;
} // namespace gpg::gal
