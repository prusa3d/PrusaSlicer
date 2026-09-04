#include "Slic3r/App/Yoga/Menu.hpp"

#include "Slic3r/App/Yoga/MenuItem.hpp"
#include "Slic3r/App/Yoga/Separator.hpp"
#include "Slic3r/App/Yoga/ScrollArea.hpp"

namespace Slic3r::App::Yoga {

Menu::Menu(const std::string& name, Position position)
{
    set_object_name("Menu");
    set_position(position);
    set_orientation(Orientation::Vertical);
    set_padding({3.f, 3.f});
    set_flags(ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
    set_flex_shrink(0);
    m_scroll_area = emplace_back<ScrollArea>();
    m_scroll_area->set_orientation(Orientation::Vertical);
    m_scroll_area->set_gap(3.f);
    m_scroll_area->set_padding(Paddings{0, 0, 13, 0});
}

MenuItem* Menu::append_item(
    const std::string& label,
    Render::Icon icon,
    const std::string& shortcut,
    bool action_closes_parent
)
{
    MenuItem* item =
        m_scroll_area->emplace_back<MenuItem>(this, label, icon, shortcut, action_closes_parent);

    m_items.push_back(item);
    return item;
}

void Menu::remove_item(size_t index)
{
    m_scroll_area->remove(m_items.at(index));
    m_items.erase(m_items.cbegin() + index);
}

void Menu::clear()
{
    for (MenuItem* item : m_items) {
        m_scroll_area->remove_later(item);
    }
    m_items.clear();
}

size_t Menu::menu_item_count() const
{
    return m_items.size();
}

MenuItem* Menu::item_at(size_t index) const
{
    return m_items.at(index);
}

void Menu::append_separator()
{
    m_scroll_area->emplace_back<Separator>();
}

void Menu::close_all_submenus() const
{
    for (MenuItem* item : m_items) {
        item->close_submenu();
    }
}

} // namespace Slic3r::App::Yoga
