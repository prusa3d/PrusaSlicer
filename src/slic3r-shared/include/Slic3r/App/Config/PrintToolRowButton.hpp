#pragma once

#include "Slic3r/App/Yoga/RectangleButton.hpp"

namespace Slic3r::Biz {
struct PrintToolItem;
class IConfigBoxSetter;
} // namespace Slic3r::Biz

namespace Slic3r::App::Yoga {
class Icon;
class Text;
class LayoutButton;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

class ConfigItemPreview;

class PrintToolRowButton : public Yoga::RectangleButton
{
public:
    PrintToolRowButton(Biz::IConfigBoxSetter& cb_setter);

    void update_data(const Biz::PrintToolItem* print_tool_item);

protected:
    void checked_updated_internal() override;
    void update_rule_visibility();

private:
    Biz::IConfigBoxSetter& m_cb_setter;

    const Biz::PrintToolItem* m_last_print_tool_item{nullptr};
    Yoga::Icon* m_icon_caret{nullptr};
    Yoga::Text* m_label{nullptr};
    Yoga::LayoutButton* m_revert_button{nullptr};
    Yoga::Rectangle* m_compatibility_rule_rect{nullptr};
    Yoga::Text* m_compatibility_rule_label{nullptr};
    ConfigItemPreview* m_config_item_preview{nullptr};
    Yoga::Text* m_per_extruder_label{nullptr};
};

} // namespace Slic3r::App
