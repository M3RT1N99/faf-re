#pragma once

// The one place where the graphics build creates a gal Device for the Diligent backend, and the
// hook a decorator Device (galtrace's recorder, port/graphics/trace) uses to wrap it.
//
// Owned by the backend (port/graphics/diligent, M6c component K). The trace component (T) installs
// its decorator through this header only; changes go through K.
//
// How it is used:
//   - Device::Create (src/sdk/gpg/gal/Device.cpp, inside FAF_PORT_GRAPHICS_DILIGENT) calls
//     diligent::CreateDevice() and installs what it returns, then calls diligent::SetupDevice() on
//     it (GalDiligent.h). CreateDevice makes the backend device and, when a decorator is installed,
//     returns decorator->Wrap(backend) instead; SetupDevice sets up the backend (not the wrapper)
//     and then tells the decorator, so the recorder sees the DeviceContext the device was set up
//     with. The engine owns and deletes the installed pointer (the wrapper), so the wrapper owns
//     the backend.
//   - A tool without the engine command line (galplay) picks the API with CreateDeviceForApi.
//   - The D3D9 backend is wrapped the same way: Device::Create's D3D9 case calls
//     DecorateCreatedDevice / NotifyDeviceSetup around its own `new` and Setup, inside
//     FAF_PORT_GRAPHICS (M6c). Device::GetInstance answers ResolveActiveInstance, and
//     SupportsVertexTextureFormat casts BackendOfInstalled, so engine and backend code that
//     static_casts the active device still gets the backend while a decorator is installed.
//
// Header-only and free of Diligent and engine types beyond the two gal forward declarations, so
// any graphics-build TU (capture, trace, engine hooks) can include it. The decorator slot is an
// inline function's static, one per process.

namespace gpg::gal
{
    class Device;
    class DeviceContext;
} // namespace gpg::gal

namespace gpg::gal::diligent
{
    /**
     * A Device that forwards to the backend and does something on the side (galtrace records).
     * Installed before the engine creates its device; not owned by the factory.
     */
    class DeviceDecorator
    {
    public:
        virtual ~DeviceDecorator() = default;

        /**
         * Called once per device creation, before setup. Returns the Device the engine installs:
         * a wrapper that owns `backend` (and deletes it in its destructor), or `backend` itself to
         * leave this device alone.
         */
        virtual Device* Wrap(Device* backend) = 0;

        /**
         * Called after the backend's setup returned, with the device the engine installed (what
         * Wrap returned) and the context the backend was set up with. Not called when setup threw
         * (the engine then deletes the installed device and may create another one).
         */
        virtual void OnSetup(Device* installed, Device* backend, const DeviceContext& context) = 0;

        /**
         * The backend device behind `installed` when `installed` is this decorator's wrapper, else
         * nullptr. For engine code that static_casts the active device to its backend type
         * (gpg::gal::SupportsVertexTextureFormat), whichever backend was wrapped (D3D9 too).
         */
        virtual Device* BackendOf(Device* installed)
        {
            (void)installed;
            return nullptr;
        }

        /**
         * Device::GetInstance's answer for the installed device: the decorator's wrapper for the
         * engine, the backend for backend code that runs inside one of the wrapper's forwarded calls
         * (the D3D9 backend static_casts Device::GetInstance() to DeviceD3D9).
         */
        virtual Device* ResolveInstance(Device* installed)
        {
            return installed;
        }
    };

    /** The process's decorator slot. */
    inline DeviceDecorator*& DeviceDecoratorSlot()
    {
        static DeviceDecorator* slot = nullptr;
        return slot;
    }

    /** Installs `decorator` for the devices created from now on (nullptr removes it). */
    inline void SetDeviceDecorator(DeviceDecorator* const decorator)
    {
        DeviceDecoratorSlot() = decorator;
    }

    [[nodiscard]] inline DeviceDecorator* GetDeviceDecorator()
    {
        return DeviceDecoratorSlot();
    }

    /** What a backend's creation hook installs: the decorator's wrapper, or `backend` itself. */
    [[nodiscard]] inline Device* DecorateCreatedDevice(Device* const backend)
    {
        DeviceDecorator* const decorator = DeviceDecoratorSlot();
        return decorator != nullptr ? decorator->Wrap(backend) : backend;
    }

    /** The backend behind an installed device, whichever backend it is (itself when undecorated). */
    [[nodiscard]] inline Device* BackendOfInstalled(Device* const installed)
    {
        DeviceDecorator* const decorator = DeviceDecoratorSlot();
        Device* const backend = decorator != nullptr ? decorator->BackendOf(installed) : nullptr;
        return backend != nullptr ? backend : installed;
    }

    /** Device::GetInstance's answer (DeviceDecorator::ResolveInstance; `installed` when undecorated). */
    [[nodiscard]] inline Device* ResolveActiveInstance(Device* const installed)
    {
        DeviceDecorator* const decorator = DeviceDecoratorSlot();
        return decorator != nullptr ? decorator->ResolveInstance(installed) : installed;
    }

    /** What a backend's creation hook calls after a successful setup. */
    inline void NotifyDeviceSetup(Device* const installed, Device* const backend, const DeviceContext& context)
    {
        if (DeviceDecorator* const decorator = DeviceDecoratorSlot()) {
            decorator->OnSetup(installed, backend, context);
        }
    }

    /**
     * The Diligent APIs the backend implements, as `/gal diligent:<api>` names them: "d3d11", "vk",
     * "gl". (Defined in DeviceDiligent.cpp.)
     */
    [[nodiscard]] bool IsDiligentApiSupported(const char* api);

    /**
     * Like GalDiligent.h's CreateDevice, but the device uses `api` whatever the command line says
     * (for galplay, which has no `/gal`). The result is decorated like CreateDevice's, and goes to
     * SetupDevice (GalDiligent.h) as usual. Returns nullptr for an unsupported api.
     */
    [[nodiscard]] Device* CreateDeviceForApi(const char* api);

    /** The backend device behind a device CreateDevice/CreateDeviceForApi returned (itself when undecorated). */
    [[nodiscard]] Device* GetBackendDevice(Device* installed);
} // namespace gpg::gal::diligent
