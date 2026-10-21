#include "Slic3r/App/PhysicalPrinterSettingsButton.hpp"

#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Icon.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/Tooltip.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"

#include "fmt/format.h"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

PhysicalPrinterSettingsButton::PhysicalPrinterSettingsButton(
    size_t index,
    const Biz::PhysicalPrinter::PhysicalPrinterConfig& physical_printer,
    FnIndexClicked on_clicked,
    FnIndexClicked on_cog_clicked,
    FnIndexClicked on_bin_clicked,
    const Biz::Preset::PresetInteractor& preset_interactor,
    Biz::PhysicalPrinter::PhysicalPrinterInteractor& physical_printer_interactor
) :
    Biz::DataObserver<Biz::PhysicalPrinter::PhysicalPrinterConfig>(index, physical_printer),
    m_physical_printer_changed_listener_scope(physical_printer_interactor, *this),
    m_on_clicked(on_clicked),
    m_on_cog_clicked(on_cog_clicked),
    m_on_bin_clicked(on_bin_clicked),
    m_preset_interactor(preset_interactor),
    m_physical_printer_interactor(physical_printer_interactor)
{
    // Connect state circle in front of the preset name
    Item* preset_row = m_texts_wrapper->emplace_back<Item>();
    preset_row->set_align_items(YGAlignCenter);
    preset_row->set_gap(4_fpx);

    m_state_btn = preset_row->emplace_back<LayoutButton>(std::string{}, Render::Icon::CircleFull);
    m_state_btn->set_background_color(Platform::Color::ButtonTransparent);
    m_state_btn->set_content_padding(1_fpx);
    m_state_btn->set_width(12_fpx);
    m_state_btn->set_height(12_fpx);
    m_state_btn->set_flex_shrink(0);
    m_state_btn->set_visible(false);

    preset_row->append(m_texts_wrapper->remove(m_preset_name));
    // WrapElide text has no min width, without flex grow it collapses to zero in a row
    m_preset_name->set_flex_grow(1);
    m_preset_name->set_flex_shrink(1);

    m_attention_icon = m_icon->emplace_back<Icon>(Render::Icon::ErrorTick);
    m_attention_icon->set_fill_mode(Icon::FillMode::PreservedAspectCentered);
    m_attention_icon->set_aspect_ratio(1);
    m_attention_icon->set_tint(m_theme->color_imgui(Platform::Color::Warning));
    m_attention_icon->set_position_type(YGPositionTypeAbsolute);
    m_attention_icon->set_width(22_fpx);
    m_attention_icon->set_left(0_fpx);
    m_attention_icon->set_bottom(0_fpx);
    m_attention_icon->set_visible(false);

    m_connect_icon = m_icon->emplace_back<Icon>(Render::Icon::ConnectUpload);
    m_connect_icon->set_fill_mode(Icon::FillMode::PreservedAspectCentered);
    m_connect_icon->set_aspect_ratio(1);
    m_connect_icon->set_position_type(YGPositionTypeAbsolute);
    m_connect_icon->set_width(16_fpx);
    m_connect_icon->set_right(0_fpx);
    m_connect_icon->set_bottom(0_fpx);
    m_connect_icon->set_visible(false);

    m_bin_btn = add_button(Render::Icon::DeleteBtnIcon, Biz::_u8L("Delete physical printer"));

    callbacks().action = [this]() { m_on_clicked(m_index); };

    set_visible_cog(true);
    on_cog() = [this]() { m_on_cog_clicked(m_index); };

    set_visible_bin(true);
    on_bin() = [this]() { m_on_bin_clicked(m_index); };

    m_expand_icon = add_extra_icon(Render::Icon::CaretDown);

    m_icon->set_width(32);
    m_icon->set_margin(8);

    update();
}

void PhysicalPrinterSettingsButton::on_data_update()
{
    update();
}

void PhysicalPrinterSettingsButton::update_btns_visibility()
{
    PrinterSettingsButton::update_btns_visibility();

    m_bin_btn->set_visible(m_is_visible_bin && (hovered() || m_bin_btn->hovered() || m_cog_btn->hovered()));
    m_cog_btn->set_visible(m_is_visible_cog && (hovered() || m_bin_btn->hovered() || m_cog_btn->hovered()));
}

void PhysicalPrinterSettingsButton::update()
{
    std::string service_name = Biz::PhysicalPrinter::physical_printer_type_to_string(*m_state);
    std::string model        = m_state->hw_config.has_value() ? m_state->hw_config.value().name : "";
    std::string preset_name =
        service_name.empty() ? model : fmt::format("{} - {}", service_name, model);
    set_preset_name(preset_name);
    set_printer_name(Biz::PhysicalPrinter::ui_name(*m_state));
    m_preset_name->set_visible(!preset_name.empty());
    m_state_btn->set_visible(false);
    set_printer_image();

    if (const auto* upload = std::get_if<Biz::PhysicalPrinter::ConnectUpload>(&m_state->payload)) {
        set_connect_state(upload->state);
        m_connect_icon->set_visible(true);
        set_visible_cog(false);
        set_visible_bin(false);
    } else {
        m_connect_icon->set_visible(false);
        set_visible_cog(!m_state->is_runtime);
        set_visible_bin(m_is_bin_supported && !m_state->is_runtime);
    }

    update_tooltip();
    update_selected();
}

void PhysicalPrinterSettingsButton::update_selected()
{
    set_checked(m_state->uuid == m_physical_printer_interactor.selected_uuid());
}

void PhysicalPrinterSettingsButton::on_selected_physical_printer_changed()
{
    update_selected();
}

void PhysicalPrinterSettingsButton::set_connect_state(
    Biz::ConnectPrinter::ConnectPrinterState state
)
{
    using State = Biz::ConnectPrinter::ConnectPrinterState;

    ImColor color = m_theme->color_imgui(Platform::Color::Text);
    switch (state) {
    case State::Finished:
    case State::Ready: color = m_theme->color_imgui(Platform::Color::Success); break;
    case State::Stopped:
    case State::Error: color = m_theme->color_imgui(Platform::Color::Error); break;
    case State::Unknown:
    case State::Busy:
    case State::Attention: color = m_theme->color_imgui(Platform::Color::Warning); break;
    case State::Printing: color = m_theme->color_imgui(Platform::Color::AccentSecondary); break;
    case State::Paused: color = m_theme->color_imgui(Platform::Color::AccentTertiary); break;
    case State::Offline:
        color = m_theme->color_imgui(Platform::Color::Text, Platform::ColorGroup::Disabled);
        break;
    default: break;
    }

    m_state_btn->set_icon_tint(color);
    m_state_btn->set_tooltip(Biz::ConnectPrinter::connect_printer_state_to_string(state));
    m_state_btn->set_visible(true);
}

void PhysicalPrinterSettingsButton::update_tooltip()
{
    std::string tooltip = Biz::PhysicalPrinter::ui_name(*m_state);
    if (m_state->hw_config.has_value()) {
        tooltip += "\n" + m_preset_interactor.ui_hw_config_summary(m_state->hw_config.value());
    }
    if (const auto* upload = std::get_if<Biz::PhysicalPrinter::ConnectUpload>(&m_state->payload)) {
        tooltip += "\n" + Biz::ConnectPrinter::connect_printer_state_to_string(upload->state);
    }
    if (!m_is_compatible) {
        tooltip += "\n" + Biz::_u8L("Non-compatible with current selection");
    }
    set_tooltip(tooltip);
}

void PhysicalPrinterSettingsButton::set_printer_image()
{
    // Reuse the picture of the linked logical printer; fall back to the generic icon.
    if (m_state->hw_config.has_value() && m_state->hw_config->visual.thumbnail.has_value()) {
        set_image(
            m_state->hw_config->relative_path_to_assets()
            + m_state->hw_config->visual.thumbnail.value()
        );
        m_icon->set_width(48);
        m_icon->set_margin(0);
    } else {
        set_icon(Render::Icon::PhysicalPrinterIcon);
        m_icon->set_width(32);
        m_icon->set_margin(8);
    }
}

void PhysicalPrinterSettingsButton::update_button_text()
{
    update();
}

void PhysicalPrinterSettingsButton::set_icon(Render::Icon icon)
{
    m_icon->set_icon(icon);
}

void PhysicalPrinterSettingsButton::set_visible_bin(bool is_visible)
{
    if (m_is_visible_bin != is_visible) {
        m_is_visible_bin = is_visible;
        update_btns_visibility();
    }
}

void PhysicalPrinterSettingsButton::set_bin_supported(bool is_supported)
{
    if (m_is_bin_supported != is_supported) {
        m_is_bin_supported = is_supported;
        update();
    }
}

void PhysicalPrinterSettingsButton::set_visible_expand_icon(bool is_visible)
{
    if (m_expand_icon) {
        m_expand_icon->set_visible(is_visible);
    }
}

std::function<void()>& PhysicalPrinterSettingsButton::on_bin()
{
    return m_bin_btn->callbacks().action;
}

std::string PhysicalPrinterSettingsButton::uuid() const
{
    return m_state->uuid;
}

void PhysicalPrinterSettingsButton::set_compatible(bool compatible)
{
    const bool show_warning = !compatible
        && std::holds_alternative<Slic3r::Biz::PhysicalPrinter::PrinterUpload>(m_state->payload);

    m_attention_icon->set_visible(show_warning);
    m_is_compatible = !show_warning;
    update_tooltip();
}

} // namespace Slic3r::App
