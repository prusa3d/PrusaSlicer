#include "Slic3r/App/PrinterNozzleRow.hpp"

#include "Slic3r/App/Yoga/Rectangle.hpp"
#include "Slic3r/App/Yoga/Text.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

PrinterNozzleRow::PrinterNozzleRow(
    size_t index,
    const Biz::Preset::ToolConfigItemObservableList& data,
    Biz::WeakerPointer<Biz::Preset::ToolConfigItemCompoundObservableList> tool_observable_list
) :
    Biz::DataObserver<Biz::Preset::ToolConfigItemObservableList>(index, data),
    m_tool_observable_list(tool_observable_list)
{
    Rectangle* id_background = emplace_back<Rectangle>();
    id_background->set_fill(m_theme->color_imgui(Platform::Color::WindowBgAlternate));
    id_background->set_flags(ImDrawFlags_RoundCornersTopLeft | ImDrawFlags_RoundCornersBottomLeft);
    id_background->set_align_items(YGAlignCenter);
    id_background->set_justify_content(YGJustifyCenter);
    id_background->set_width(25_fpx);

    m_text_index = id_background->emplace_back<Text>(std::string{});

    m_combo_box = emplace_back<ComboBoxTools>();
    m_combo_box->set_get_name_fn(
        [](const Domain::Preset::HwToolConfigDef* data) -> std::string { return data->name; }
    );
    m_combo_box->set_flex_grow(1);
    m_combo_box->callbacks().selection_changed = [this](int nozzle_index)
    {
        if (nozzle_index >= 0) {
            m_tool_observable_list->at(m_index).set_selected_index(nozzle_index);
        }
    };

    on_data_update();
    on_index_update();
}

PrinterNozzleRow::Callbacks& PrinterNozzleRow::callbacks()
{
    return m_callbacks;
}

void PrinterNozzleRow::on_data_update()
{
    m_combo_box->set_source_list(&m_tool_observable_list->at(m_index));
}

void PrinterNozzleRow::on_index_update()
{
    m_text_index->set_text(std::to_string(m_index + 1));
}

void PrinterNozzleRow::on_view_will_be_reset()
{
    m_combo_box->set_source_list(nullptr);
}

} // namespace Slic3r::App
