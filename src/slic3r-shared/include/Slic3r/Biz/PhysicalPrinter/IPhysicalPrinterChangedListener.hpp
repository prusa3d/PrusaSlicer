#pragma once

#include <Slic3r/Biz/PhysicalPrinter/PhysicalPrinterConfig.hpp>

namespace Slic3r::Biz::PhysicalPrinter {

class IPhysicalPrinterChangedListener
{
public:
    virtual ~IPhysicalPrinterChangedListener() = default;

    virtual void on_printer_data_changed() {}

    virtual void on_selected_physical_printer_changed(const PhysicalPrinterConfig& printer) = 0;

    /// Fired after on_selected_physical_printer_changed when the user explicitly picked the printer.
    virtual void on_physical_printer_picked() {}
};

} // namespace Slic3r::Biz::PhysicalPrinter
