#include "Slic3r/App/Plater/GizmoHelpFactory.hpp"

#include "Slic3r/App/Plater/KeyIcon.hpp"
#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Yoga/Icon.hpp"
#include "Slic3r/App/Yoga/Text.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App::Plater {

void GizmoHelpFactory::init(Item* container)
{
    m_container = container;
}

void GizmoHelpFactory::add_item(const std::vector<HelpItem>& icons, const std::string& title)
{
    ASSERT(m_container);
    Item* help_group = m_container->emplace_back<Item>();
    help_group->set_align_items(YGAlignCenter);
    help_group->set_gap(5);

    const ImColor color = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    size_t index        = 0;
    for (const HelpItem& help_item : std::as_const(icons)) {
        if (std::holds_alternative<std::string>(help_item)) {
            KeyIcon* key_icon = help_group->emplace_back<KeyIcon>(std::get<std::string>(help_item));
            key_icon->set_tint(color);
        } else if (std::holds_alternative<HelpIcon>(help_item)) {
            const HelpIcon help_icon = std::get<HelpIcon>(help_item);

            ASSERT(help_icon.icon != Render::Icon::None);

            Icon* icon = help_group->emplace_back<Icon>(help_icon.icon);
            icon->set_width(help_icon.min_width);
            icon->set_height(help_icon.min_height);
            icon->set_fill_mode(Icon::FillMode::PreservedAspectCentered);
            icon->set_tint(color);
        }

        if (++index < icons.size()) {
            Text* text = help_group->emplace_back<Text>("+");
            text->set_text_color(color);
        }
    }
    Text* text = help_group->emplace_back<Text>(title);
    text->set_text_color(color);
}

} // namespace Slic3r::App::Plater
