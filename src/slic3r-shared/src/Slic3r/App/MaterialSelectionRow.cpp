#include "Slic3r/App/MaterialSelectionRow.hpp"

#include "Slic3r/Biz/Preset/PresetSelectionCheck.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

MaterialSelectionRow::MaterialSelectionRow(
    size_t index,
    const Biz::Preset::PresetItem& data,
    FnClicked on_clicked_extention,
    FnIndexClicked on_favorite_clicked,
    FnChecked is_favorite_checked,
    FnClicked on_cog_clicked,
    size_t& material_index,
    Biz::Preset::PresetInteractor& preset_interactor
) :
    Biz::DataObserver<Biz::Preset::PresetItem>(index, data),
    LayoutButton(data.ui_preset_name(), Render::Icon::LightLockClosed),
    m_material_index(material_index),
    m_on_clicked_extention(on_clicked_extention),
    m_on_favorite_clicked(on_favorite_clicked),
    m_is_favorite_checked(is_favorite_checked),
    m_on_cog_clicked(on_cog_clicked),
    m_preset_interactor(preset_interactor)
{
    set_flex_shrink(0);
    set_min_width(30);
    set_min_height(30);
    set_content_padding(5.f);
    set_expand_label(true);
    set_content_justify_content(YGJustifyFlexStart);
    set_content_direction(YGDirectionRTL);
    set_allow_overlap(true);

    set_text_wrap_mode(Text::WrapMode::WrapElide);

    auto switch_matrial = [this]() -> bool
    {
        if (Biz::Preset::PresetSelectionCheck::can_select_material_preset(
                m_preset_interactor,
                m_material_index,
                m_state->id
            ))
        {
            m_preset_interactor.select_material_preset(m_material_index, m_state->id, true);
            return true;
        }
        return false;
    };

    callbacks().action = [switch_matrial, this]()
    {
        switch_matrial();
        m_on_clicked_extention();
    };

    auto add_button = [this](Render::Icon icon, const std::string& tooltip)
    {
        LayoutButton* btn = emplace<LayoutButton>(1, std::string{}, icon, tooltip);
        btn->set_self_align(YGAlignCenter);
        btn->set_margin(-2.f);
        btn->set_width(24.f);
        btn->set_height(24.f);
        btn->set_background_color(Platform::Color::ButtonTransparent);
        btn->callbacks().hovered_changed = [this](bool) { update_buttons_visibility(); };
        return btn;
    };

    m_cog_btn = add_button(Render::Icon::Cog, Biz::_u8L("Show material settings"));
    m_cog_btn->callbacks().action = [this, switch_matrial]()
    {
        auto selected_preset = m_preset_interactor.selected_printer_preset();
        if (selected_preset.materials[m_material_index].id != m_state->id) {
            if (!switch_matrial()) {
                return;
            }
        }
        m_on_cog_clicked();
    };

    m_favorite_btn = add_button(Render::Icon::Star, Biz::_u8L("Add to favorites"));
    m_favorite_btn->callbacks().action = [this]()
    {
        m_on_favorite_clicked(m_index);
        on_data_update();
    };

    update_buttons_visibility();
}

void MaterialSelectionRow::on_data_update()
{
    set_label(m_state->ui_preset_name());
    set_icon(
        m_state->origin == Domain::Preset::PresetOrigin::System ? Render::Icon::LightLockClosed :
                                                                  Render::Icon::None
    );
    m_favorite_btn->set_icon(
        m_is_favorite_checked(m_index) ? Render::Icon::StarSolid : Render::Icon::Star
    );
    update_buttons_visibility();
}

void MaterialSelectionRow::checked_updated_internal()
{
    RectangleButton::checked_updated_internal();
    update_buttons_visibility();
}

void MaterialSelectionRow::hovered_updated_internal()
{
    RectangleButton::hovered_updated_internal();
    update_buttons_visibility();
}

void MaterialSelectionRow::update_buttons_visibility()
{
    m_cog_btn->set_visible(hovered() || m_cog_btn->hovered());
}

} // namespace Slic3r::App
