#pragma once

#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Config/ConfigItemControl.hpp"

namespace Slic3r::App {

namespace Yoga {
class InputText;
class Tooltip;
class LayoutButton;
} // namespace Yoga

class ConfigItemFilePicker : public ConfigItemControl, public Yoga::Item
{
public:
    ConfigItemFilePicker(
        size_t index,
        const Domain::ConfigItem& data,
        Biz::IConfigBoxSetter& cb_setter,
        std::vector<size_t> cbi_index
    );

protected:
    void on_data_update() override;

private:
    //! InputText is used instead of Text to allow displaying a tooltip for the label on hovering.
    Yoga::InputText* m_file_name{nullptr};
    Yoga::LayoutButton* m_load_btn{nullptr};
    Yoga::LayoutButton* m_remove_btn{nullptr};

    Yoga::Tooltip* m_tooltip = nullptr;
};

} // namespace Slic3r::App
