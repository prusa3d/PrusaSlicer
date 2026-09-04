#pragma once

#include "Slic3r/App/Yoga/RectangleButton.hpp"
#include "Slic3r/App/Render/ImguiTypes.hpp"

namespace Slic3r::App::Yoga {

class Icon;
class Text;
class Menu;

class MenuItem : public RectangleButton
{
public:
    MenuItem(
        Menu* parent,
        const std::string& label,
        Render::Icon icon           = Render::Icon::None,
        const std::string& shortcut = {},
        bool action_closes_parent   = true
    );
    MenuItem(
        Menu* parent,
        const std::string& label,
        const std::string& shortcut = {},
        bool action_closes_parent   = true
    );

    MenuItem* append_sub_menu_item(
        const std::string& label,
        Render::Icon icon           = Render::Icon::None,
        const std::string& shortcut = {}
    );
    void append_sub_menu_separator();
    void clear_submenu();
    void close_submenu();

    const Menu* submenu() const ;

protected:
    void shortcut_updated_internal() override;

private:
    void create(
        const std::string& label,
        Render::Icon icon           = Render::Icon::None,
        const std::string& shortcut = {},
        bool has_sub_menu           = false
    );
    void add_submenu(const std::string& label);
    void action_internal() override;
    void hovered_updated_internal() override;

    Menu* m_parent_menu{nullptr};
    bool m_action_closes_parent{true};

    Icon* m_icon{nullptr};
    Text* m_label{nullptr};
    Menu* m_sub_menu{nullptr};
    Text* m_shortcut_text{nullptr};
    Icon* m_expander_icon{nullptr};
};

} // namespace Slic3r::App::Yoga
