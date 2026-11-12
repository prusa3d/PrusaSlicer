#include "Slic3r/App/SidebarToolHeadRow.hpp"

#include "Slic3r/App/Yoga/Rectangle.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Text.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Preset/PresetSelectionCheck.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

SidebarToolHeadRow::SidebarToolHeadRow(
    size_t index,
    const Biz::Preset::PresetItemObservableList& data,
    Biz::ProjectInteractor& project_interactor
) :
    Biz::DataObserver<Biz::Preset::PresetItemObservableList>(index, data),
    m_project_interactor(project_interactor)
{
    set_flex_shrink(0);
    Rectangle* rect = emplace_back<Rectangle>();
    rect->set_fill(m_theme->color_imgui(Platform::Color::WindowBgAlternate));
    rect->set_justify_content(YGJustifyCenter);
    rect->set_align_items(YGAlignCenter);
    rect->set_width(25);
    rect->emplace_back<Text>(std::to_string(index + 1));
    rect->set_flags(ImDrawFlags_RoundCornersTopLeft | ImDrawFlags_RoundCornersBottomLeft);

    m_combo_box = emplace_back<ComboBoxListViewSelection<Biz::Preset::PresetItem>>();
    m_combo_box->set_get_name_fn(
        [](const Biz::Preset::PresetItem* item) -> std::string
        {
            return item->ui_preset_name();
        }
    );
    m_combo_box->set_flex_grow(1);
    m_combo_box->callbacks().selection_changed = [this](int index)
    {
        if (index >= 0) {
            auto& preset_interactor      = m_project_interactor.preset_interactor();
            const std::string& preset_id = m_state->items().at(static_cast<size_t>(index)).id;
            if (Biz::Preset::PresetSelectionCheck::can_select_tool_print_preset(
                    preset_interactor,
                    m_index,
                    preset_id
                ))
            {
                preset_interactor.select_tool_print_preset(m_index, preset_id, true);
                m_last_selected_index = index;
            } else {
                m_combo_box->set_current_index(m_last_selected_index);
            }
        }
    };

    on_data_update();
}

void SidebarToolHeadRow::on_data_update()
{
    Biz::Preset::PresetItemObservableList* preset_item_observable_list =
        &m_project_interactor.preset_interactor().tool_presets().at(m_index);

    if (m_last_preset_item_observable_list != preset_item_observable_list) {
        on_view_will_be_removed();

        m_last_preset_item_observable_list = preset_item_observable_list;
        m_combo_box->set_source_list(preset_item_observable_list);
    }
}

void SidebarToolHeadRow::on_view_will_be_reset()
{
    if (m_last_preset_item_observable_list) {
        m_combo_box->set_source_list(nullptr);
        m_last_preset_item_observable_list = nullptr;
    }
}

Yoga::ComboBoxListViewSelection<Biz::Preset::PresetItem>* SidebarToolHeadRow::combo_box()
{
    return m_combo_box;
}

} // namespace Slic3r::App
