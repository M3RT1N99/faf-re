// The engine's spatial database (moho/mesh/Mesh.cpp: SpatialShard, SpatialShardData, SpatialDB and
// SpatialDBEntry, with Mesh.cpp's explicit instantiations) for the Android headless runner. Real
// engine code, not a stand-in: scripts/port/build_runner.py compiles this file into libfafengine.so
// next to the closure TUs (port/engine/runner/closure.txt).
//
// Mesh.cpp is the D3D9 mesh renderer and cannot be compiled for Android, but the templates its
// second half defines are reached while the runner loads the .scmap, exactly as in the Windows
// runner (main.exe /headlessreplay, call chains recorded with an instrumented Debug|Win32 build,
// T1-T3):
//  - WaveSystem::Load builds one WaveGenerator per wave generator in the map and registers it in
//    the wave system's SpatialDB<WaveGenerator> (WaveSystem.cpp);
//  - CDecalManager::Load reads the map's decals into CWldTerrainDecal objects, which register in
//    the decal manager's SpatialDB<CWldTerrainDecal> (CWldSplat.cpp, CWldTerrainDecal.cpp).
// The stream is read sequentially, so these are not optional, and their ownership (registration,
// teardown) has to be the engine's own.
//
// FAF_PORT_MESH_SPATIAL_DB_ONLY leaves out everything else in Mesh.cpp (the renderer, its console
// variables and the D3D9 includes); main.exe never defines it, so Windows compiles Mesh.cpp as before.

#define FAF_PORT_MESH_SPATIAL_DB_ONLY 1
#include "moho/mesh/Mesh.cpp"
