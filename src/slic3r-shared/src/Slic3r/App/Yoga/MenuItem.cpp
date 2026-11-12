#include "Slic3r/App/Yoga/MenuItem.hpp"

#include "Slic3r/App/Yoga/Menu.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/Icon.hpp"

namespace Slic3r::App::Yoga {

MenuItem::MenuItem(
    Menu* parent,
    const std::string& label,
    Render::Icon icon,
    const std::string& shortcut,
    bool action_closes_parent
) :
    RectangleButton(),
    m_parent_menu(parent),
    m_action_closes_parent(action_closes_parent)
{
    create(label, icon, shortcut);
}

MenuItem::MenuItem(
    Menu* parent,
    const std::string& label,
    const std::string& shortcut,
    bool action_closes_parent
) :
    RectangleButton(),
    m_parent_menu(parent),
    m_action_closes_parent(action_closes_parent)
{
    create(label, Render::Icon::None, shortcut);
}

void MenuItem::create(
    const std::string& label,
    Render::Icon icon,
    const std::string& shortcut,
    bool has_sub_menu
)
{
    set_object_name("MenuItem");
    set_background_color(Platform::Color::ButtonTransparent);
    set_flex_shrink(0);
    set_align_content(YGAlignCenter);

    float icon_size = 16;

    m_icon = emplace_back<Icon>(icon);
    m_icon->set_aspect_ratio(1);
    m_icon->set_width(icon_size);
    m_icon->set_fill_mode(Icon::FillMode::PreservedAspectCentered);
    m_icon->set_self_align(YGAlignFlexEnd);

    m_label = emplace_back<Text>(label);
    m_label->set_flex_grow(1.f);

    m_shortcut_text = emplace_back<Text>(shortcut);
    m_shortcut_text->set_text_color(
        m_theme->color_imgui(Platform::Color::Text, Platform::ColorGroup::Disabled)
    );

    // for expanded menu item use expander icon
    m_expander_icon = emplace_back<Icon>(Render::Icon::CloseArrow);
    m_expander_icon->set_visible(false);
    m_expander_icon->set_aspect_ratio(1.f);
    m_expander_icon->set_width(icon_size);
    m_expander_icon->set_fill_mode(Icon::FillMode::PreservedAspectCentered);
}

MenuItem* MenuItem::append_sub_menu_item(
    const std::string& label,
    Render::Icon icon,
    const std::string& shortcut
)
{
    if (!m_sub_menu) {
        add_submenu(label);
    }
    return m_sub_menu->append_item(label, icon, shortcut);
}

void MenuItem::append_sub_menu_separator()
{
    ASSERT(m_sub_menu);
    m_sub_menu->append_separator();
}

void MenuItem::clear_submenu()
{
    if (m_sub_menu) {
        m_sub_menu->clear();
    }
}

void MenuItem::close_submenu()
{
    if (m_sub_menu && m_sub_menu->opened()) {
        m_sub_menu->close();
    }
}

const Menu* MenuItem::submenu() const
{
    return m_sub_menu;
}

void MenuItem::shortcut_updated_internal()
{
    m_shortcut_text->set_text(shortcut());
}

void MenuItem::action_internal()
{
    if (!m_sub_menu && m_action_closes_parent) {
        m_parent_menu->close();
    }
}

void MenuItem::hovered_updated_internal()
{
    RectangleButton::hovered_updated_internal();
    if (hovered()) {
        if (m_sub_menu) {
            m_sub_menu->open();
        }
    }
}

void MenuItem::add_submenu(const std::string& label)
{
    m_sub_menu = emplace_back<Menu>(label, Position::Right);
    m_sub_menu->set_flags(
        m_sub_menu->flags() | ImGuiWindowFlags_ChildMenu | ImGuiWindowFlags_ChildWindow
    );
    m_sub_menu->set_offset(0);

    m_expander_icon->set_visible(true);
    set_content_justify_content(YGJustifyFlexStart);
}

} // namespace Slic3r::App::Yoga
