#include "Slic3r/Biz/ExportDestination/ExportDestination.hpp"

namespace Slic3r::Biz::ExportDestination {

std::string_view export_destination_to_string(ExportDestination destination)
{
    switch (destination) {
    case ExportDestination::LocalDrive: return "local_drive";
    case ExportDestination::RemovableDrive: return "removable_drive";
    case ExportDestination::Connect: return "connect";
    case ExportDestination::PrintHost: return "print_host";
    }
    return {};
}

std::optional<ExportDestination> export_destination_from_string(std::string_view value)
{
    for (ExportDestination destination : ALL_EXPORT_DESTINATIONS) {
        if (export_destination_to_string(destination) == value) {
            return destination;
        }
    }
    return std::nullopt;
}

} // namespace Slic3r::Biz::ExportDestination
