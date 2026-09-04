#pragma once

#include "Slic3r/App/Yoga/ColorPickerButton.hpp"
#include "Slic3r/App/Config/ConfigItemControl.hpp"

namespace Slic3r::App {

class ConfigItemColorPicker : public ConfigItemControl, public Yoga::ColorPickerButton
{
public:
    ConfigItemColorPicker(
        size_t index,
        const Domain::ConfigItem& data,
        Biz::IConfigBoxSetter& cb_setter,
        std::vector<size_t> cbi_index
    );

protected:
    void on_data_update() override;
};

} // namespace Slic3r::App
