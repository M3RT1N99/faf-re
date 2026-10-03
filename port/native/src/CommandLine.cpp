#include "faf/port/CommandLine.h"

#include <utility>

#include "FileSystemDetail.h"

namespace faf::port {

  CommandLine::CommandLine(std::vector<std::string> args)
    : mArgs(std::move(args))
  {}

  bool CommandLine::Has(const std::string_view option) const
  {
    return Values(option, 0).has_value();
  }

  std::optional<std::vector<std::string>> CommandLine::Values(const std::string_view option, const std::size_t count)
    const
  {
    if (option.empty()) {
      return std::nullopt;
    }
    // CFG_GetArgOption (src/sdk/moho/misc/StartupHelpers.cpp) skips an
    // occurrence without enough values and keeps looking; a later occurrence
    // has even fewer tokens after it, so the first occurrence decides.
    for (std::size_t index = 0; index < mArgs.size(); ++index) {
      if (!fs::detail::EqualsNoCase(mArgs[index], option)) {
        continue;
      }
      if (mArgs.size() - index - 1 < count) {
        return std::nullopt;
      }
      return std::vector<std::string>(
        mArgs.begin() + static_cast<std::ptrdiff_t>(index + 1),
        mArgs.begin() + static_cast<std::ptrdiff_t>(index + 1 + count)
      );
    }
    return std::nullopt;
  }

  std::optional<std::string> CommandLine::Value(const std::string_view option) const
  {
    std::optional<std::vector<std::string>> values = Values(option, 1);
    if (!values) {
      return std::nullopt;
    }
    return std::move(values->front());
  }

} // namespace faf::port
