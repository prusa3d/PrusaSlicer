#pragma once

#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Config/ConfigItemControl.hpp"

namespace Slic3r::App {

class ConfigItemRammingParams : public ConfigItemControl, public Yoga::LayoutButton
{
public:
    ConfigItemRammingParams(
        size_t index,
        const Domain::ConfigItem& data,
        Biz::IConfigBoxSetter& cb_setter,
        std::vector<size_t> cbi_index
    );

protected:
    void on_data_update() override;
};

} // namespace Slic3r::App
