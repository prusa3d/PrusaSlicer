#pragma once

#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Config/ConfigItemControl.hpp"

namespace Slic3r::App::Yoga {
class IntValidator;
class InputTextWithSpin;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

class ConfigItemSpinBoxes : public ConfigItemControl, public Yoga::Item
{
public:
    ConfigItemSpinBoxes(
        size_t index,
        const Domain::ConfigItem& data,
        Biz::IConfigBoxSetter& cb_setter,
        std::vector<size_t> cbi_index
    );

protected:
    void on_data_update() override;

private:
    void reconstruct_spin_buttons();
    void update_values();

private:
    struct Box
    {
        Yoga::InputTextWithSpin* spinbox{nullptr};
        Yoga::IntValidator* value_validator{nullptr};
    };

    std::vector<Box> m_boxes;
};

} // namespace Slic3r::App
