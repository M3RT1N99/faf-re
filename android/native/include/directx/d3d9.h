#pragma once

// The engine's shared render files that include <d3d9.h> use only its types (d3d9types.h); the
// Direct3D 9 interfaces belong to the Windows-only backend. The one interface named outside it,
// the surface (the shadow-map dump), is declared without its methods.
#include <d3d9types.h>

struct IDirect3DSurface9;
