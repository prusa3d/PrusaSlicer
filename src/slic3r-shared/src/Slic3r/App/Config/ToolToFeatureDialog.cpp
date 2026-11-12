#include "Slic3r/App/Config/ToolToFeatureDialog.hpp"

#include <Slic3r/Domain/Config.hpp>

#include "Slic3r/App/Yoga/AbstractButton.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/Namespace.hpp"
#include "Slic3r/App/Yoga/Separator.hpp"
#include "Slic3r/App/Navigator.hpp"

#include "Slic3r/Biz/IConfigBoxSetter.hpp"
#include "Slic3r/Biz/PrintToolConfigBoxInteractor.hpp"
#include "Slic3r/Biz/PrintToolConfigObservableList.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

namespace Slic3r::App {

using namespace Yoga;

namespace {
Unit buttons_gap = 10_fpx;
} // namespace

static std::string label_from_name(const std::string& name)
{
    if (name == "perimeter_extruder")
        return Biz::_u8L("Perimeter");
    if (name == "infill_extruder")
        return Biz::_u8L("Infill");
    if (name == "solid_infill_extruder")
        return Biz::_u8L("Solid infill");
    if (name == "support_material_extruder")
        return Biz::_u8L("Support");
    if (name == "support_material_interface_extruder")
        return "   ↳  " + Biz::_u8L("Interfaces");
    if (name == "wipe_tower_extruder")
        return Biz::_u8L("Wipe tower");
    if (name == "bed_temperature_extruder")
        return Biz::_u8L("Bed temperature");

    PANIC("Unknown parameter name for extruder assignments");
}

ExtruderOptionRow::ExtruderOptionRow(
    size_t index,
    const Biz::PrintToolItem& data,
    Biz::IConfigBoxSetter& cb_setter,
    Biz::ProjectInteractor& project_interactor
) :
    Biz::DataObserver<Biz::PrintToolItem>(index, data),
    m_cb_setter(cb_setter)
{
    set_object_name("ExtruderOption");
    set_orientation(Orientation::Vertical);
    set_flex_grow(1);
    set_gap(8_fpx);

    emplace_back<Separator>()->set_orientation(Orientation::Horizontal);

    Item* row = emplace_back<Item>();
    row->set_gap(buttons_gap);
    row->set_flex_shrink(0);

    m_name = row->emplace_back<Text>(label_from_name(data.name));
    m_name->set_flex_grow(1);
    m_name->set_align({AlignH::Left, AlignV::Center});
    m_value = row->emplace_back<
        ExtruderIdButtonGroup>(project_interactor, data.print_item->def().min < 1.);
    m_value->callbacks().action = [this](Yoga::AbstractButton*)
    {
        std::optional<int> se_id = m_value->selected_extruder_id();
        int new_extruder_id      = se_id.value_or(m_state->original_print_item->value().get<int>());
        if (!se_id && new_extruder_id == m_state->value.first.get<int>()) {
            // The button was unchecked but related value will not be changed,
            // so, revert button checked
            m_value->set_selected_extruder(new_extruder_id);
        } else {
            m_cb_setter.set_item_value(*m_state->print_item, Domain::ConfigValue(new_extruder_id));
        }
    };

    on_data_update();
}

void ExtruderOptionRow::on_data_update()
{
    m_value->set_selected_extruder(m_state->value.first.get<int>());
    m_name->set_text_color(m_theme->color_imgui(
        m_value->is_selected_extruder_out_of_range() ?
            Platform::Color::Error :
            Platform::Color::Text
    ));
}

ToolToFeatureDialog::ToolToFeatureDialog(
    Biz::ProjectInteractor& project_interactor,
    Navigator& navigator
) :
    Dialog({Biz::_u8L("Extruder Assignment")}, "ExtruderAssignmentDialog"),
    m_project_interactor(project_interactor),
    m_navigator(navigator)
{
    content()->set_min_width(350_fpx);
    content()->set_padding(20_fpx);
    content()->set_orientation(Orientation::Vertical);

    Biz::Preset::PresetInteractor& preset_interactor = project_interactor.preset_interactor();

    m_header =
        content()->emplace_back<ToolToFeatureDialogHeader>(project_interactor, preset_interactor);
    m_header->set_flex_shrink(0);

    m_extruder_assignment_filter = std::make_shared<ExtruderAssignmentFilter>();
    m_extruder_assignment_filter->set_filter_fn(
        [](const Biz::PrintToolItem& item) -> bool
        {
            return item.print_item->def().option_group
                == Domain::ConfigItemDef::OptionGroup::Print_MultiMaterial_ExtruderAssignment;
        }
    );
    m_extruder_assignment_filter->set_sort_fn(
        [](const Biz::PrintToolItem& lhs, const Biz::PrintToolItem& rhs)
        { return lhs.print_item->def().order < rhs.print_item->def().order; }
    );
    m_extruder_assignment_filter->set_source_model(
        preset_interactor.print_tool_cbi().observable_list()
    );
    m_header->set_source_list(m_extruder_assignment_filter.get());

    m_extruder_assignment_list_view = content()->emplace_back<ExtruderOptionListView>(
        ExtruderOptionListViewFactory{preset_interactor, project_interactor}
    );
    m_extruder_assignment_list_view->set_orientation(Orientation::Vertical);
    m_extruder_assignment_list_view->set_gap(8_fpx);
    m_extruder_assignment_list_view->set_source_list(m_extruder_assignment_filter.get());
}

void ToolToFeatureDialog::close_action()
{
    m_navigator.set_opened_dialog(nullptr);
}

} // namespace Slic3r::App
