#pragma once

#include "Slic3r/Domain/ConfigBoxesFDM.hpp"

namespace Slic3r::Domain {

class IConfigPackFDMViewer
{
public:
    virtual ~IConfigPackFDMViewer() = default;

    virtual const Domain::PrinterSettings& get_printer() const               = 0;
    virtual const Domain::PrintSettings& get_print() const                   = 0;
    virtual const Domain::ToolPrintSettings& get_tool(size_t index) const    = 0;
    virtual const size_t tool_size() const                                   = 0;
    virtual const Domain::FilamentSettings& get_filament(size_t index) const = 0;
    virtual const size_t filament_size() const                               = 0;
};

} // namespace Slic3r::Domain
