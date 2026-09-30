#pragma once

namespace Slic3r::Biz::ExportDestination {

class IExportDestinationChangedListener
{
public:
    virtual ~IExportDestinationChangedListener() = default;

    virtual void on_export_destination_changed() = 0;
};

} // namespace Slic3r::Biz::ExportDestination
