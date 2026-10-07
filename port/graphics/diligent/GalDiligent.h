#pragma once

// The engine-facing seam of the Diligent gal backend (M6 step 1, docs/port/renderer.md).
//
// Only the graphics build includes this (FafPortGraphics=true and a Diligent install, which defines
// FAF_PORT_GRAPHICS_DILIGENT; port/graphics/port_graphics.props). The engine touches the backend in
// two guarded places and nowhere else:
//   - gpg/gal/Device.cpp, Device::Create: the DeviceApiDiligent case;
//   - moho/app/CScApp.cpp, CScApp::CreateDevice: `/gal diligent:<api>` picks that API for the
//     device context (and its fallback context).
// Everything Diligent- or D3DX-specific stays behind this header, so no engine TU sees Diligent's
// headers.

#include "gpg/gal/DeviceContext.hpp"

namespace gpg::gal
{
    class Device;

    /**
     * The `DeviceContext::mDeviceType` value of the Diligent backend. The binary's enum ends at
     * Direct3D10 = 2 (DeviceContext.hpp:20-25); 3 is new. It is a constant here rather than an
     * enumerator so that DeviceContext.hpp, included by some forty engine TUs, stays untouched in
     * the default build. Engine code that switches on the API and has no case for it (MeshVertex.cpp
     * GetHardwareVertexFormatter, HardwareMeshBatch.cpp:1135's `!= Direct3D10` gate) is in-game
     * only; the main menu does not reach it.
     */
    inline constexpr DeviceApi DeviceApiDiligent = static_cast<DeviceApi>(3);

    namespace diligent
    {
        /**
         * True when the command line asks for this backend: `/gal diligent:<api>`. Only `d3d11` is
         * implemented; any other api makes the device setup throw gal::Error, which CScApp's
         * CreateAppFrame reports as "GAL Exception" and the engine then gives up as it does when
         * D3D9 cannot start. `/gal d3d9` (or no `/gal`) keeps the default.
         */
        [[nodiscard]] bool IsRequestedOnCommandLine();

        /** `new DeviceDiligent`, not yet set up (Device::Create installs it first, then sets it up). */
        [[nodiscard]] Device* CreateDevice();

        /** DeviceDiligent::Setup(context) on a device CreateDevice made. */
        void SetupDevice(Device* device, const DeviceContext* context);
    } // namespace diligent
} // namespace gpg::gal
