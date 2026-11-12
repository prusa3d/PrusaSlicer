#pragma once

#include "Slic3r/App/Config/ConfigItemControl.hpp"
#include "Slic3r/App/Yoga/Item.hpp"

namespace Slic3r::Biz {
class IConfigBoxSetter;
} // namespace Slic3r::Biz

namespace Slic3r::App::Yoga {
class ToggleButton;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

class ConfigItemCheckBoxes : public ConfigItemControl, public Yoga::Item
{
public:
    ConfigItemCheckBoxes(
        size_t index,
        const Domain::ConfigItem& data,
        Biz::IConfigBoxSetter& cb_setter,
        std::vector<size_t> cbi_index
    );

protected:
    void on_data_update() override;

    std::vector<bool> get_data() const;

private:
    void reconstruct_buttons();
    void update_values();

private:
    std::vector<Yoga::ToggleButton*> m_toggle_buttons;
};

} // namespace Slic3r::App
