#pragma once

// galtrace's recorder: a decorator gpg::gal::Device (and a decorator for every gal object it hands
// out) that forwards each call to the real backend and appends it to a galtrace file
// (port/graphics/trace/format). Only the graphics build compiles it (FafPortGraphics,
// port/graphics/port_graphics.props), and it does nothing unless `/galtrace <file>` is on the
// command line.
//
// How it is attached: the recorder installs itself as the process's DeviceDecorator
// (port/graphics/diligent/DeviceFactory.h, owned by the backend component) during static
// initialisation. Every device creation that goes through the factory's hooks is then wrapped:
// Wrap() returns the decorator, which the engine installs as the active device; OnSetup() records
// the context the backend was set up with and starts recording.
//
// What the engine sees is the decorator; what the backend sees is itself. Calls the backend makes
// into the active device while it runs one of the decorator's forwarded calls (the D3D9 effect
// technique calling Device::BeginTechnique, the D3D9 backend's static_cast of
// Device::GetInstance(), the Diligent effect layer's BeginTechnique) are not engine calls: they are
// forwarded without being recorded, and ResolveActiveDevice() makes Device::GetInstance() return
// the backend for them. BackendOfDevice() is for engine code that static_casts the active device to
// its backend type (gpg::gal::SupportsVertexTextureFormat).

namespace gpg::gal
{
    class Device;
} // namespace gpg::gal

namespace port::graphics::trace
{
    /** True when this process records (`/galtrace <file>` on the command line). */
    [[nodiscard]] bool RecorderActive() noexcept;

    /**
     * Device::GetInstance's answer given the installed device: the backend while the calling thread
     * runs inside one of the decorator's forwarded calls (or the backend's own setup), the installed
     * device otherwise. `installed` itself when it is not a galtrace decorator.
     */
    [[nodiscard]] gpg::gal::Device* ResolveActiveDevice(gpg::gal::Device* installed) noexcept;

    /** The backend device behind `installed` (itself when it is not a galtrace decorator). */
    [[nodiscard]] gpg::gal::Device* BackendOfDevice(gpg::gal::Device* installed) noexcept;

    /** Ends the trace now (End record, file closed); later calls are forwarded unrecorded. */
    void FinishRecording(const char* reason);
} // namespace port::graphics::trace
