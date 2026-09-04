#pragma once

#include "Slic3r/App/Config/ConfigItemControl.hpp"
#include "Slic3r/App/Yoga/Item.hpp"

namespace Slic3r::Biz {
class IConfigBoxSetter;
} // namespace Slic3r::Biz

namespace Slic3r::App::Yoga {
class ComboBox;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

class ConfigItemComboBoxes : public ConfigItemControl, public Yoga::Item
{
public:
    ConfigItemComboBoxes(
        size_t index,
        const Domain::ConfigItem& config_item,
        Biz::IConfigBoxSetter& cb_setter,
        std::vector<size_t> cbi_index
    );

protected:
    void on_data_update() override;

private:
    void reconstruct_boxes();
    void update_values();

private:
    std::vector<Yoga::ComboBox*> m_combo_boxes;
};

} // namespace Slic3r::App
