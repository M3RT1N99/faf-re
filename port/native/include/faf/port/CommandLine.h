#pragma once

// The game's command line ("/init init_faf.lua /gpgnet 127.0.0.1:1234 ..."),
// read the way CFG_GetArgOption reads it: an option is a token starting with
// '/', matched case-insensitively, followed by its values. On Android the
// launcher passes the same tokens in the Intent extra "argv", so any launcher
// that can start the desktop game (the FAF client, the Rust client) can start
// this one with the arguments it already builds.

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace faf::port {

  class CommandLine
  {
  public:
    CommandLine() = default;
    explicit CommandLine(std::vector<std::string> args);

    /// True if `option` (with its leading '/') is present.
    [[nodiscard]] bool Has(std::string_view option) const;

    /// The `count` values following the first occurrence of `option`, or
    /// nullopt if it is missing or has fewer values. Values are the tokens that
    /// follow it, even if they start with '/' (paths on Android do).
    [[nodiscard]] std::optional<std::vector<std::string>> Values(std::string_view option, std::size_t count) const;

    /// Shorthand for Values(option, 1)[0].
    [[nodiscard]] std::optional<std::string> Value(std::string_view option) const;

    [[nodiscard]] const std::vector<std::string>& Args() const { return mArgs; }

  private:
    std::vector<std::string> mArgs;
  };

} // namespace faf::port
