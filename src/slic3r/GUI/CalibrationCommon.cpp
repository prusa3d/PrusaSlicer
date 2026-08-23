///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "CalibrationCommon.hpp"

#include "GUI_App.hpp"
#include "Tab.hpp"

#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"

namespace Slic3r { namespace GUI {

void apply_calibration_filename_prefix(const std::string& test_name)
{
    if (test_name.empty())
        return;

    DynamicPrintConfig& config =
        wxGetApp().preset_bundle->prints.get_edited_preset().config;

    const std::string token = "{input_filename_base}";
    std::string fmt = config.opt_string("output_filename_format");
    size_t pos = fmt.find(token);
    if (pos != std::string::npos)
        fmt.replace(pos, token.size(), test_name);
    else if (!fmt.empty())
        fmt = test_name + "_" + fmt;
    else
        fmt = test_name + "_{print_time}.gcode";

    config.set_key_value("output_filename_format", new ConfigOptionString(fmt));
    if (Tab* tab = wxGetApp().get_tab(Preset::TYPE_PRINT))
        tab->reload_config();
}

}} // namespace Slic3r::GUI
