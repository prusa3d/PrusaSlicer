#pragma once

#include "Slic3r/App/Yoga/ComboBox.hpp"
#include "Slic3r/App/Config/ConfigItemControl.hpp"

namespace Slic3r::Biz {
class IConfigBoxSetter;
} // namespace Slic3r::Biz

namespace Slic3r::App {

class ConfigItemLanguageSelection : public ConfigItemControl, public Yoga::ComboBox
{
public:
    ConfigItemLanguageSelection(
        size_t index,
        const Domain::ConfigItem& config_item,
        Biz::IConfigBoxSetter& cb_setter,
        std::vector<size_t> cbi_index
    );

protected:
    void on_data_update() override;
};

} // namespace Slic3r::App
