#include "Slic3r/App/PhysicalPrinterSettingsButton.hpp"

#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Icon.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/Tooltip.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"

#include "fmt/format.h"
#include <type_traits>

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

PhysicalPrinterSettingsButton::PhysicalPrinterSettingsButton(
    size_t index,
    const Biz::PhysicalPrinter::PhysicalPrinterConfig& physical_printer,
    FnIndexClicked on_clicked,
    FnIndexClicked on_cog_clicked,
    FnIndexClicked on_bin_clicked
) :
    Biz::DataObserver<Biz::PhysicalPrinter::PhysicalPrinterConfig>(index, physical_printer),
    m_on_clicked(on_clicked),
    m_on_cog_clicked(on_cog_clicked),
    m_on_bin_clicked(on_bin_clicked)
{
    m_texts_wrapper->set_min_height(36);

    m_attention_icon = m_icon->emplace_back<Icon>(Render::Icon::ErrorTick);
    m_attention_icon->set_fill_mode(Icon::FillMode::PreservedAspectCentered);
    m_attention_icon->set_aspect_ratio(1);
    m_attention_icon->set_tint(m_theme->color_imgui(Platform::Color::Warning));
    m_attention_icon->set_position_type(YGPositionTypeAbsolute);
    m_attention_icon->set_width(22_fpx);
    m_attention_icon->set_left(0_fpx);
    m_attention_icon->set_bottom(0_fpx);
    m_attention_icon->set_visible(false);

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
    std::string model        = m_state->hw_config.name;
    std::string preset_name  = fmt::format("{} - {}", service_name, model);
    set_preset_name(preset_name);
    set_printer_name(m_state->name);
    m_preset_name->set_visible(true);

    std::visit(
        [this](const auto& arg)
        {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, Slic3r::Biz::PhysicalPrinter::ConnectUpload>) {
                set_preset_name({});
                m_preset_name->set_visible(false);
                set_icon(Render::Icon::ConnectUpload);
                set_visible_cog(false);
                set_visible_bin(false);
            } else if constexpr (std::is_same_v<T, Slic3r::Biz::PhysicalPrinter::PrinterUpload>) {
                // Reuse the picture of the linked logical printer; fall back to the generic icon.
                if (m_state->hw_config.visual.thumbnail.has_value()) {
                    set_image(
                        m_state->hw_config.relative_path_to_assets()
                        + m_state->hw_config.visual.thumbnail.value()
                    );
                } else {
                    set_icon(Render::Icon::PhysicalPrinterIcon);
                }
                set_visible_cog(true);
                set_visible_bin(m_is_bin_supported);
            } else if constexpr (
                std::is_same_v<T, Slic3r::Biz::PhysicalPrinter::FileSystemExport>
            ) {
                set_preset_name({});
                m_preset_name->set_visible(false);
                set_icon(
                    arg.prefer_removable ? Render::Icon::ExportToSD : Render::Icon::ExportToLocal
                );
                set_visible_cog(false);
                set_visible_bin(false);
            }
        },
        m_state->payload
    );
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
    set_tooltip(show_warning ? Biz::_u8L("Non-compatible with current selection") : std::string());
}

} // namespace Slic3r::App
