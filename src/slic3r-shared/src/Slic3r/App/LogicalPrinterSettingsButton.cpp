#include "Slic3r/App/LogicalPrinterSettingsButton.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"

#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/AppConfig.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

LogicalPrinterSettingsButton::LogicalPrinterSettingsButton(
    size_t index,
    const Biz::Preset::PresetItem& logical_printer,
    FnIndexClicked on_clicked,
    FnIndexClicked on_cog_clicked,
    FnIndexClicked on_favorite_clicked,
    const Biz::Preset::PresetInteractor& preset_interactor
) :
    Biz::DataObserver<Biz::Preset::PresetItem>(index, logical_printer),
    m_on_clicked(on_clicked),
    m_on_cog_clicked(on_cog_clicked),
    m_on_favorite_clicked(on_favorite_clicked),
    m_preset_interactor(preset_interactor)
{
    set_flex_shrink(0);
    set_checkable(false);

    callbacks().action = [this]() { m_on_clicked(m_index); };
    on_cog()           = [this]() { m_on_cog_clicked(m_index); };

    m_favorite_button = add_button(Render::Icon::Star, std::string{});
    m_favorite_button->set_visible(true);
    m_favorite_button->callbacks().action = [this]()
    {
        AppServices::instance().app_config().app_settings_advanced().toggle_printer_favorite_preset(
            m_state->id,
            m_state->hw_printer_config_id
        );
        update_favorite_state();
        m_on_favorite_clicked(m_index);
    };

    on_data_update();

    set_visible_cog(true);
}

const Biz::Preset::PresetItem& LogicalPrinterSettingsButton::preset_item() const
{
    return *m_state;
}

void LogicalPrinterSettingsButton::on_data_update()
{
    if (m_state->origin == Domain::Preset::PresetOrigin::System) {
        set_printer_name(m_state->ui_hw_config_name());
        set_preset_name({});
        m_preset_name->set_visible(false);
        set_tooltip(m_state->ui_hw_config_name());
    } else {
        set_printer_name(m_state->ui_preset_name());
        set_preset_name(m_state->ui_hw_config_name());
        m_preset_name->set_visible(true);
        set_tooltip(m_state->ui_preset_name() + "\n" + m_state->ui_hw_config_name());
    }

    const Domain::Preset::HwPrinterConfig& printer_config =
        m_preset_interactor.get_printer_config(m_state->hw_printer_config_id).first.get();

    if (printer_config.visual.thumbnail.has_value()) {
        const std::string image_path =
            printer_config.relative_path_to_assets() + printer_config.visual.thumbnail.value();
        set_image(image_path);
    }

    update_favorite_state();
}

void LogicalPrinterSettingsButton::update_btns_visibility()
{
    m_cog_btn->set_visible(hovered() || m_cog_btn->hovered() || m_favorite_button->hovered());
}

void LogicalPrinterSettingsButton::update_favorite_state()
{
    if (is_favorited()) {
        m_favorite_button->set_icon(Render::Icon::StarSolid);
        m_favorite_button->set_tooltip(Biz::_u8L("Unfavorite preset"));
    } else {
        m_favorite_button->set_icon(Render::Icon::Star);
        m_favorite_button->set_tooltip(Biz::_u8L("Favorite preset"));
    }
}

bool LogicalPrinterSettingsButton::is_favorited() const
{
    return AppServices::instance()
        .app_config()
        .app_settings_advanced()
        .contains_printer_favorite_preset(m_state->id, m_state->hw_printer_config_id);
}

} // namespace Slic3r::App