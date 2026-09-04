#include "Slic3r/App/Yoga/CollapsibleWindow.hpp"

#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Separator.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"

namespace Slic3r::App::Yoga {

CollapsibleWindow::CollapsibleWindow(const std::string& label, const std::string& window_name) :
    Window(window_name)
{
    set_orientation(Orientation::Vertical);
    set_padding(0.f);
    m_header_row = emplace_back<Item>();
    m_header_row->set_padding({20.f, 0.f});
    m_header_row->set_height(48.f);
    m_header_row->set_gap(5);
    m_header_row->set_align_items(YGAlignCenter);
    m_header_row->set_flex_shrink(0.f);

    m_label = m_header_row->emplace_back<Text>(label);
    m_label->set_font_type(Render::ImguiFontType::Bold);
    m_label->set_flex_grow(1);

    m_collapse_button =
        m_header_row->emplace_back<LayoutButton>(std::string{}, Render::Icon::CaretUp);
    m_collapse_button->callbacks().action = [this] { set_collapsed(!m_collapsed); };
    m_collapse_button->set_width(22);
    m_collapse_button->set_height(22);
    m_collapse_button->set_content_padding({});
    m_collapse_button->set_background_color(Platform::Color::ButtonTransparent);

    m_separator = emplace_back<Separator>(Orientation::Horizontal);

    m_content = emplace_back<Item>();
    m_content->set_padding(Paddings(20.f, 10.f, 20.f, 20.f));
    m_content->set_orientation(Orientation::Vertical);
}

const std::string& CollapsibleWindow::label() const
{
    return m_label->text();
}

void CollapsibleWindow::set_label(const std::string& label)
{
    m_label->set_text(label);
}

bool CollapsibleWindow::collapsed() const
{
    return m_collapsed;
}

void CollapsibleWindow::set_collapsed(bool collapse)
{
    if (m_collapsed == collapse) {
        return;
    }

    m_collapsed = collapse;

    m_content->set_visible(!m_collapsed);
    m_separator->set_visible(!m_collapsed);

    m_collapse_button->set_tooltip(m_collapsed ? Biz::_u8L("Expand") : Biz::_u8L("Collapse"));
    m_collapse_button->set_icon(m_collapsed ? Render::Icon::CaretDown : Render::Icon::CaretUp);

    if (m_collapsed) {
        Item::set_flex_grow(0);
    } else if (m_set_flex_grow) {
        Item::set_flex_grow(1);
    }

    if (m_callbacks.collapsed_changed) {
        m_callbacks.collapsed_changed(m_collapsed);
    }
}

void CollapsibleWindow::set_flex_grow(float flex)
{
    Item::set_flex_grow(flex);
    m_set_flex_grow = flex;
}

Item* CollapsibleWindow::content() const
{
    return m_content;
}

CollapsibleWindow::CollapsibleWindowCallbacks& CollapsibleWindow::collapsible_window_callbacks()
{
    return m_callbacks;
}

} // namespace Slic3r::App::Yoga
