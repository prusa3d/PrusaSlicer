/// 
#include "Slic3r/App/Yoga/Dialog.hpp"

#include "Slic3r/App/Yoga/Rectangle.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Separator.hpp"

namespace Slic3r::App::Yoga {
constexpr float dialog_padding = 10;

Dialog::Dialog(const std::string& name)
{
    WindowPtr window = std::make_unique<Window>(name.empty() ? "Dialog" : name);

    window->set_orientation(Orientation::Vertical);
    window->set_gap(0);
    window->set_padding(0);
    window->set_flags(ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);

    m_top_row = window->emplace_back<Item>();
    m_top_row->set_max_height(40);
    m_top_row->set_flex_shrink(0);

    m_tab_container = m_top_row->emplace_back<Item>();

    m_tab_button_group.callbacks().checked_changed =
        [this](AbstractButton* current_checked, AbstractButton* last_checked) {
        if (current_checked) {
            std::vector<LayoutButton*>::const_iterator it = std::find(
                m_tab_buttons.cbegin(),
                m_tab_buttons.cend(),
                current_checked
            );

            // This may be a bigger underlying problem.
            if (it == m_tab_buttons.cend()) {
                return;
            }

            const size_t current_index = std::distance(m_tab_buttons.cbegin(), it);
            set_current_tab(current_index);
        }
    };

    Rectangle* buttons_rect = m_top_row->emplace_back<Rectangle>();
    buttons_rect->set_justify_content(YGJustifyFlexEnd);
    buttons_rect->set_align_items(YGAlignCenter);
    buttons_rect->set_padding(dialog_padding);
    buttons_rect->set_fill(m_theme->color_imgui(Platform::Color::WindowBgAlternate));
    buttons_rect->set_flex_grow(1);
    buttons_rect->set_rounding(0);

    m_close_button = buttons_rect->emplace_back<LayoutButton>(std::string{}, Render::Icon::PrintIdle);
    m_close_button->set_min_width(20);
    m_close_button->set_min_height(20);
    m_close_button->callbacks().action = [this] {
        close_action();
    };

    m_content = window->emplace_back<Item>();
    m_content->set_padding(dialog_padding);

    set_content_item(std::move(window));
}

Dialog::Dialog(std::initializer_list<std::string> tabs, const std::string& name) : Dialog(name)
{
    for (const std::string& tab : tabs) {
        append_tab(tab);
    }
}

Dialog::DialogCallbacks& Dialog::dialog_callbacks()
{
    return m_callbacks;
}

bool Dialog::closable() const
{
    return m_closable;
}

void Dialog::set_closable(bool closable)
{
    if (m_closable != closable) {
        m_closable = closable;
        m_close_button->set_visible(m_closable);
    }
}

size_t Dialog::current_tab_index() const
{
    return m_current_tab_index;
}

Item* Dialog::content() const
{
    return m_content;
}

LayoutButton* Dialog::close_button() const
{
    return m_close_button;
}

Separator* Dialog::add_separator()
{
    Separator* separator = m_content->emplace_back<Separator>(Orientation::Horizontal);
    separator->set_margin(Margins(-dialog_padding, 0.f));
    return separator;
}

LayoutButton* Dialog::append_tab(const std::string& tab)
{
    LayoutButton* tab_button = m_tab_container->emplace_back<LayoutButton>(tab);
    tab_button->set_checkable(true);
    tab_button->set_content_padding({10, 3});
    tab_button->set_background_color(Platform::Color::WindowBgAlternate);
    tab_button->set_background_color_checked(m_theme->color_imgui(Platform::Color::WindowBg));

    if (m_tab_buttons.empty()) {
        tab_button->set_checked(true);
        tab_button->set_draw_flags(ImDrawFlags_RoundCornersTopLeft);
    } else {
        tab_button->set_rounding(0);
    }

    m_tab_buttons.push_back(tab_button);
    m_tab_button_group.insert_button(tab_button);

    return tab_button;
}

void Dialog::remove_tab(size_t index)
{
    ASSERT(index < m_tab_buttons.size());
    LayoutButton* button = m_tab_buttons.at(index);
    m_tab_button_group.remove_button(button);
    m_tab_container->remove(m_tab_container->get_item(index));
    m_tab_buttons.erase(m_tab_buttons.cbegin() + index);
}

void Dialog::set_current_tab(size_t current_index)
{
    m_current_tab_index = current_index;
    m_tab_buttons.at(current_index)->set_checked(true); // can be NOOP

    on_tab_selected(current_index);
    if (m_callbacks.tab_selected) {
        m_callbacks.tab_selected(current_index);
    }
}

void Dialog::on_tab_selected(int current_index) {}

void Dialog::close_action()
{
    close();
}

} // namespace Slic3r::App::Yoga
