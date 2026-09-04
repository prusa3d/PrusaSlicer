#pragma once

#include <string>

namespace Slic3r::Biz::PhysicalPrinter {

class IPhysicalPrinterChangedListener
{
public:
    virtual ~IPhysicalPrinterChangedListener() = default;

    virtual void on_printer_data_changed() {}

    virtual void on_selected_physical_printer_changed() = 0;
};

} //namespace Slic3r::Biz::PhysicalPrinter