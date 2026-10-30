#pragma once

#include <array>
#include <optional>
#include <string_view>

namespace Slic3r::Biz::ExportDestination {

enum class ExportDestination
{
    LocalDrive,
    RemovableDrive,
    Connect,
    PrintHost,
};

inline constexpr std::array<ExportDestination, 4> ALL_EXPORT_DESTINATIONS{
    ExportDestination::LocalDrive,
    ExportDestination::RemovableDrive,
    ExportDestination::Connect,
    ExportDestination::PrintHost,
};

std::string_view export_destination_to_string(ExportDestination destination);

std::optional<ExportDestination> export_destination_from_string(std::string_view value);

} // namespace Slic3r::Biz::ExportDestination
