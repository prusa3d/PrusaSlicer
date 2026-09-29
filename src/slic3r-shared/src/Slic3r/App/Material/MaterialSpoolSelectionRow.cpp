#include "Slic3r/App/Material/MaterialSpoolSelectionRow.hpp"

#include "Slic3r/App/Yoga/Icon.hpp"
#include "Slic3r/App/Yoga/ComboBox.hpp"
#include "Slic3r/App/Yoga/Text.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

MaterialSpoolSelectionRow::MaterialSpoolSelectionRow(
    size_t index,
    const Biz::MatDb::MatDbItem& data,
    Biz::MatDb::MatDbInteractor& matdb_interactor,
    const size_t& material_index,
    CheckedFn checked_fn
) :
    Biz::DataObserver<Biz::MatDb::MatDbItem>(index, data),
    RectangleButton(),
    m_matdb_interactor(matdb_interactor),
    m_material_index(material_index),
    m_checked_fn(checked_fn)
{
    set_object_name("MaterialSpoolSelectionRow");
    set_content_gap(5_fpx);
    set_content_orientation(Orientation::Vertical);
    set_flex_shrink(0);
    set_checkable(true);
    set_background_color_checked(Platform::Color::AccentSecondary);
    set_allow_overlap(true);

    Item* top_row = emplace_back<Item>();
    top_row->set_gap(5_fpx);

    m_spool_name = top_row->emplace_back<Text>(std::string{});
    m_spool_name->set_flex_grow(1);
    m_spool_name->set_align({AlignH::Left, AlignV::Center});
    m_spool_name->set_wrap_mode(Text::WrapMode::WrapElide);

    m_spool_icon = top_row->emplace_back<Icon>(Render::Icon::SpoolBig);
    m_spool_icon->set_width(64_fpx);
    m_spool_icon->set_height(32_fpx);
    m_spool_icon->set_fill_mode(Icon::FillMode::PreservedAspectCentered);

    m_bottom_row = emplace_back<Item>();
    m_bottom_row->set_gap(5_fpx);
    m_bottom_row->set_visible(false);
    m_bottom_row->set_align_items(YGAlignCenter);

    m_bottom_row->emplace_back<Text>("Preset");

    m_preset_combobox = m_bottom_row->emplace_back<ComboBox>("MaterialPresetCombobox");
    m_preset_combobox->set_flex_grow(1);

    on_data_update();
}

void MaterialSpoolSelectionRow::on_data_update()
{
    ImColor color = IM_COL32_WHITE;
    if (m_state->material.primary_color.has_value()) {
        Domain::ColorRGBA rgba = m_state->material.primary_color.value().native;
        color.Value.x          = rgba.r();
        color.Value.y          = rgba.g();
        color.Value.z          = rgba.b();
    } else if (!m_state->material.secondary_colors.empty()) {
        Domain::ColorRGBA rgba = m_state->material.secondary_colors.front().native;
        color.Value.x          = rgba.r();
        color.Value.y          = rgba.g();
        color.Value.z          = rgba.b();
    }

    m_spool_icon->set_tint(color);
    m_spool_name->set_text(m_state->material.name);

    std::vector<std::string> presets;
    std::ranges::transform(
        m_state->preset_matches,
        std::back_inserter(presets),
        [](const Biz::MatDb::PresetMatch& preset) { return preset.preset_name; }
    );
    m_preset_combobox->set_items(presets);
}

void MaterialSpoolSelectionRow::checked_updated_internal()
{
    RectangleButton::checked_updated_internal();
    m_bottom_row->set_visible(checked());
    if (checked()) {
        m_checked_fn(m_index);
    }
}

void MaterialSpoolSelectionRow::action_internal()
{
    if (!checked() && !m_state->preset_matches.empty()) {
        m_matdb_interactor.select_material_preset(
            m_material_index,
            m_state->material,
            m_state->preset_matches.at(m_preset_combobox->current_index()).preset_id
        );
    }
}

} // namespace Slic3r::App
