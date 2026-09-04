#pragma once

#include "Slic3r/App/Yoga/ContextPopup.hpp"
#include "Slic3r/App/Render/ImguiTypes.hpp"

namespace Slic3r::App::Yoga {

class MenuItem;
class ScrollArea;

class Menu : public ContextPopup
{
public:
    Menu(const std::string& name, Position position);

    MenuItem* append_item(
        const std::string& label,
        Render::Icon icon           = Render::Icon::None,
        const std::string& shortcut = {},
        bool action_closes_parent   = true
    );
    void remove_item(size_t index);
    void clear();
    size_t menu_item_count() const;
    MenuItem* item_at(size_t index) const;

    void append_separator();
    void close_all_submenus() const;

private:
    std::vector<MenuItem*> m_items;
    ScrollArea* m_scroll_area{nullptr};
};

} // namespace Slic3r::App::Yoga
