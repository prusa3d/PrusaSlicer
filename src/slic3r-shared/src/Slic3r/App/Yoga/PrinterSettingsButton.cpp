#include "Slic3r/App/Yoga/PrinterSettingsButton.hpp"

#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/Icon.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

namespace Slic3r::App::Yoga {

PrinterSettingsButton::PrinterSettingsButton(const std::string& tooltip) : RectangleButton(tooltip)
{
    set_flex_shrink(0);
    set_allow_overlap(true);
    set_content_justify_content(YGJustifyFlexStart);

    m_icon = emplace_back<Icon>(Render::Icon::None);
    m_icon->set_fill_mode(Icon::FillMode::PreservedAspectCentered);
    m_icon->set_aspect_ratio(1);
    m_icon->set_width(64_fpx);

    m_texts_wrapper = emplace_back<Item>();
    m_texts_wrapper->set_orientation(Orientation::Vertical);
    m_texts_wrapper->set_flex_grow(1.f);
    m_texts_wrapper->set_justify_content(YGJustifyCenter);

    m_printer_name = m_texts_wrapper->emplace_back<Text>(std::string{});
    m_printer_name->set_font_type(Render::ImguiFontType::Bold);
    m_printer_name->set_wrap_mode(Text::WrapMode::WrapElide);

    m_preset_name = m_texts_wrapper->emplace_back<Text>(std::string{});
    m_preset_name->set_wrap_mode(Text::WrapMode::WrapElide);

    m_btn_wrapper = emplace_back<Item>();
    m_btn_wrapper->set_flex_shrink(0);
    m_btn_wrapper->set_gap(5_fpx);

    m_printers_btn =
        add_button(Render::Icon::ConfigContainer, Biz::_u8L("Show info about printer"));
    m_cog_btn = add_button(Render::Icon::PrintIconMarker, Biz::_u8L("Show extruder settings"));
}

void PrinterSettingsButton::set_image(const std::string& image)
{
    m_icon->set_image(image);
}

void PrinterSettingsButton::set_printer_name(const std::string& printer_name, bool is_modified)
{
    m_printer_name->set_text(printer_name); 
    m_printer_name->set_text_color(m_theme->color_imgui(
        is_modified ? Platform::Color::AccentTertiary : Platform::Color::Text));
}

void PrinterSettingsButton::set_preset_name(const std::string& preset_name)
{
    m_preset_name->set_text(preset_name);
}

void PrinterSettingsButton::set_printing_state(int state)
{
    // ToDo render colored circle as a printing state with Tooltip
}

void PrinterSettingsButton::set_visible_printer(bool is_visible)
{
    if (m_is_visible_printers != is_visible) {
        m_is_visible_printers = is_visible;
        update_btns_visibility();
    }
}

void PrinterSettingsButton::set_visible_cog(bool is_visible)
{
    if (m_is_visible_cog != is_visible) {
        m_is_visible_cog = is_visible;
        update_btns_visibility();
    }
}

std::function<void()>& PrinterSettingsButton::on_cog()
{
    return m_cog_btn->callbacks().action;
}

std::function<void()>& PrinterSettingsButton::on_printer()
{
    return m_printers_btn->callbacks().action;
}

void PrinterSettingsButton::checked_updated_internal()
{
    RectangleButton::checked_updated_internal();
    if (!checked()) {
        m_printers_btn->set_background_color(background_color());
    }
    update_btns_visibility();
}

void PrinterSettingsButton::hovered_updated_internal()
{
    RectangleButton::hovered_updated_internal();
    update_btns_visibility();

    // const float avail_width =
    //     parent_item() ? parent_item()->width() - parent_item()->padding().horizontal() : width();

    // m_texts_wrapper->set_max_size(
    //     {avail_width
    //          - padding().horizontal()
    //          - 2 * gap()
    //          - m_icon->width()
    //          - m_btn_wrapper->width(),
    //      YGUndefined}
    // ); // WrapElide with flex_grow is borked, so this is a workaround
}

void PrinterSettingsButton::update_btns_visibility()
{
    m_cog_btn->set_visible(m_is_visible_cog && (hovered() || m_cog_btn->hovered()));
    m_printers_btn->set_visible(m_is_visible_printers && (hovered() || m_printers_btn->hovered()));
}

LayoutButton* PrinterSettingsButton::add_button(Render::Icon icon, const std::string& tooltip)
{
    LayoutButton* button = m_btn_wrapper->emplace_back<LayoutButton>(std::string{}, icon, tooltip);
    button->set_self_align(YGAlignCenter);
    button->set_min_width(24_fpx);
    button->set_min_height(24_fpx);
    button->set_background_color(Platform::Color::ButtonTransparent);
    button->set_flex_shrink(0);
    // Extra button is hidden by default.
    // It can be shown in the settings dialog under certain conditions.
    button->set_visible(false);

    button->callbacks().hovered_changed = [this](bool) { update_btns_visibility(); };

    return button;
};

Icon* PrinterSettingsButton::add_extra_icon(Render::Icon icon)
{
    Icon* extra_icon = m_btn_wrapper->emplace_back<Icon>(icon);
    extra_icon->set_self_align(YGAlignCenter);
    extra_icon->set_min_width(24_fpx);
    extra_icon->set_min_height(24_fpx);
    extra_icon->set_flex_shrink(0);
    // Extra icon is hidden by default.
    // It can be shown in the settings dialog under certain conditions.
    extra_icon->set_visible(false);

    return extra_icon;
}

} // namespace Slic3r::App::Yoga
