#pragma once

#include <string>

namespace Slic3r::Biz::PhysicalPrinter {

class IPhysicalPrinterChangedListener
{
public:
    virtual ~IPhysicalPrinterChangedListener() = default;

    virtual void on_printer_data_changed() {}

    virtual void on_selected_physical_printer_changed() = 0;

    /// Fired after on_selected_physical_printer_changed when the user explicitly picked the printer.
    virtual void on_physical_printer_picked() {}
};

} //namespace Slic3r::Biz::PhysicalPrinter