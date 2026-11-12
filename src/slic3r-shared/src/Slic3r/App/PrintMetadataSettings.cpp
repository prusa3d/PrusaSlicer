#include "Slic3r/App/PrintMetadataSettings.hpp"

#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include "Slic3r/App/Yoga/InputTextField.hpp"
#include "Slic3r/App/Yoga/Text.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

PrintMetadataSettings::PrintMetadataSettings(
    size_t index,
    const Biz::Preset::ToolConfigItemObservableList& data,
    Biz::Preset::PresetInteractor& preset_interactor
) :
    Biz::DataObserver<Biz::Preset::ToolConfigItemObservableList>(index, data),
    m_preset_changed_listener_scope(preset_interactor, *this),
    m_preset_interactor(preset_interactor)
{
    set_orientation(Orientation::Vertical);
    set_gap(5);
    set_padding(10);

    std::unique_ptr<InputTextField> input_id{std::make_unique<InputTextField>()};
    input_id->set_input_flags(ImGuiInputTextFlags_ReadOnly);
    m_input_id = input_id.get();
    add_new_row(Biz::_u8L("Tool print preset UUID"), std::move(input_id));

    std::unique_ptr<InputTextField> input_name{std::make_unique<InputTextField>()};
    input_name->set_input_flags(ImGuiInputTextFlags_ReadOnly);
    m_input_name = input_name.get();
    add_new_row(Biz::_u8L("Tool print preset name"), std::move(input_name));

    // std::unique_ptr<InputTextField> input_expression{std::make_unique<InputTextField>()};
    // m_input_expression = input_expression.get();
    // add_new_row(Biz::_u8L("Expression"), std::move(input_expression));

    update_contents();
}

void PrintMetadataSettings::on_preset_selection_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id,
    Biz::Preset::PresetItemType type
)
{
    if (type == Biz::Preset::PresetItemType::ToolPrintPreset) {
        update_contents();
    }
}

void PrintMetadataSettings::on_data_update()
{
    update_contents();
}

void PrintMetadataSettings::on_index_update()
{
    update_contents();
}

void PrintMetadataSettings::add_new_row(const std::string& label, Yoga::ItemPtr control)
{
    Item* row = emplace_back<Item>();
    row->set_orientation(Orientation::Horizontal);
    row->set_gap(5);
    Text* text = row->emplace_back<Text>(label);
    text->set_wrap_mode(Text::WrapMode::WrapElide);
    text->set_align(Align{AlignH::Center, AlignV::Center});
    text->set_width(150);
    control->set_width(275);
    row->append(std::move(control));
}

void PrintMetadataSettings::update_contents()
{
    const Domain::Preset::SelectedPreset& selected_preset =
        m_preset_interactor.selected_printer_preset();

    if (selected_preset.tools.empty() || m_index >= selected_preset.hw_config.tool_count) {
        return;
    }

    const Domain::Preset::EvaluatedPresetMetadata metadata =
        selected_preset.tools.at(m_index).metadata();
    m_input_id->set_text(metadata.id);
    m_input_name->set_text(metadata.name);
}

} // namespace Slic3r::App
