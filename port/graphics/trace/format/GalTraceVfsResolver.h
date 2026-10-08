#pragma once

// A PayloadResolver (GalTraceResolver.h) over port/native's VirtualFileSystem: the game data mounted
// the way the engine mounts it (init_faf.lua through faf::port::RunDataPathScript, first mount wins,
// case-insensitive). Header only, so the format library itself does not depend on port/native: include
// it where faf::port_core is linked (the Android galplay, the host tool galtrace-refs).
//
//   faf::port::VirtualFileSystem vfs;            // mounted from init_faf.lua's `path` table
//   galtrace::VfsResolver resolver(vfs);
//   reader.SetResolver(&resolver);               // or PlayOptions::resolver for galplay

#include "GalTraceResolver.h"

#include "faf/port/Vfs.h"

#include <string>
#include <vector>

namespace galtrace
{
  class VfsResolver final : public PayloadResolver
  {
  public:
    explicit VfsResolver(const faf::port::VirtualFileSystem& vfs) : vfs_(vfs) {}

    bool ReadGameFile(const std::string& vfsPath, std::vector<std::uint8_t>* out, std::string* error) override
    {
      std::string message;
      if (vfs_.Read(vfsPath, *out, &message)) {
        return true;
      }
      if (error != nullptr) {
        *error = message.empty() ? vfsPath + ": not found in the game data" : message;
      }
      return false;
    }

  private:
    const faf::port::VirtualFileSystem& vfs_;
  };
} // namespace galtrace
