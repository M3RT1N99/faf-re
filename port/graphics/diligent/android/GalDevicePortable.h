#pragma once

// M7a1 (Android): gpg::gal::Device::GetInstance's answer without Device::Create (GalDevicePortable.cpp).
// galplay installs the device it made through DeviceFactory.h here, so the effect layer's calls into the
// active device (EffectsDiligent.cpp: BeginTechnique/EndTechnique) reach it; null when none is installed.

namespace gpg::gal
{
    class Device;

    namespace diligent
    {
        void SetPortableInstance(Device* device);

        /**
         * Where gpg::Logf/Warnf/Debugf go in galplay (the stand-ins in GalDevicePortable.cpp): level 0
         * debug, 1 info, 2 warning. Null drops them.
         */
        using PortableLogSink = void (*)(int level, const char* line);
        void SetPortableLogSink(PortableLogSink sink);
    } // namespace diligent
} // namespace gpg::gal
