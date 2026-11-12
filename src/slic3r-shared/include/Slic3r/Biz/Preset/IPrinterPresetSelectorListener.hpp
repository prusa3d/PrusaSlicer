#pragma once

#include <string>

namespace Slic3r::Biz::Preset {

class IPrinterPresetSelectorListener
{
public:
    virtual ~IPrinterPresetSelectorListener() = default;

    virtual void on_printer_preset_selection_warning(const std::string& msg) = 0;
};

} // namespace Slic3r::Biz::Preset
