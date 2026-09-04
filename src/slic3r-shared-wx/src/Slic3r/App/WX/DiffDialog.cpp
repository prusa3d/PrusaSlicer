#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Biz/Preset/PresetSelectionCheck.hpp"
#include "Slic3r/App/Config/CategoryUtils.hpp"

#include "Slic3r/Domain/Preset/Bundle.hpp"

#include "DiffNamespace.hpp"
#include "Slic3r/App/WX/StringConversions.hpp"
#include "Slic3r/App/WX/DiffDialog.hpp"
#include "Slic3r/App/WX/DiffViewCtrl.hpp"
#include "Slic3r/App/WX/DiffDVCModel.hpp"
#include "Slic3r/App/WX/WidgetsConfig.hpp"
#include "Slic3r/App/WX/I18N.hpp"
#include <Slic3r/Biz/I18N/I18N.hpp>

#include <wx/app.h>
#include <wx/stattext.h>
#include <wx/string.h>
#include <wx/scrolwin.h>
#include <fmt/format.h>

using namespace Slic3r::Biz;

namespace Slic3r::App::WX {

using namespace Diff;
using namespace Biz;

static std::string get_preset_name(const Biz::Preset::PresetItem& item)
{
    return item.ui_preset_name();
}

template <typename FdmT, typename Sla>
static std::string get_preset_name(const Domain::Preset::EvaluatedPreset<FdmT, Sla>& preset)
{
    return get_preset_name(Preset::PresetItem{.name=preset.name, .origin=preset.origin});
}

DiffDialog::DiffDialog(
    const Slic3r::Biz::Preset::PresetInteractor& preset_interactor,
    std::optional<PresetKind> kind
) :
    wxDialog(
        wxTheApp->GetTopWindow(),
        wxID_ANY,
        _L("Compare Presets"),
        wxDefaultPosition,
        wxDefaultSize,
        wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER
    ),
    m_preset_interactor(preset_interactor),
    m_kind(kind)
{
    const wxFont font = GetFont().Bold();
    m_top_info_line   = new wxStaticText(this, wxID_ANY, _L("Select presets to compare"));
    m_top_info_line->SetFont(font);

    m_bottom_info_line = new wxStaticText(this, wxID_ANY, _L("info from selection"));
    m_bottom_info_line->SetFont(font);

    const int em = w_config()->em_unit();

    m_printers = new Row(
        this,
        [this](int selection, Location location) { select_printer(selection, location); }
    );
    m_printers->Show(show_printers());

    m_prints = new Row(
        this,
        [this](int selection, Location location) { select_print(selection, location); },
        m_printers
    );
    m_prints->Show(show_prints());

    m_tools_prints = new Row(
        this,
        [this](int selection, Location location)
        {
            BCBClientData* obj = m_tools_prints->client_data(selection, location);
            if (!obj || obj->is_marker())
                return;
            select_tool_print(obj->tool_id, obj->tool_print_id, location);
        },
        m_prints
    );
    m_tools_prints->Show(show_tool_prints());

    m_tool_materials = new Row(
        this,
        [this](int selection, Location location) { select_material(selection, location); },
        m_tools_prints
    );
    m_tool_materials->Show(show_materials());

    m_printers->set_checkbox_callback([this]() { update_tree(); });
    m_printers->show_checkbox(!m_kind);
    m_prints->set_checkbox_callback([this]() { update_tree(); });
    m_tools_prints->set_checkbox_callback([this]() { update_tree(); });
    m_tool_materials->set_checkbox_callback([this]() { update_tree(); });
    m_tool_materials->show_checkbox(!m_kind);

    auto& printers = m_preset_interactor.printer_presets().items();
    for (size_t i = 0; i < printers.size(); i++) {
        auto& printer         = printers.at(i);
        wxString printer_name = from_u8(
            fmt::format(
                "{} ({})",
                get_preset_name(printer),
                printer.hw_printer_config_name
            )
        );
        (*m_printers)[Location::Left]->Append(printer_name);
        (*m_printers)[Location::Right]->Append(printer_name);
    }

    int border             = em;
    wxBoxSizer* main_sizer = new wxBoxSizer(wxVERTICAL);

    create_tree();

    wxString colon_str = from_u8(":");

    main_sizer->Add(m_top_info_line, 0, wxEXPAND | wxALL, border);
    if (show_printers()) {
        main_sizer->Add(
            new wxStaticText(this, wxID_ANY, _L("Printer Presets") + colon_str),
            0,
            wxEXPAND | wxLEFT | wxRIGHT,
            border
        );
        main_sizer->Add(m_printers, 0, wxEXPAND | wxLEFT | wxBOTTOM | wxRIGHT, border);
    }
    if (show_prints()) {
        main_sizer->Add(
            new wxStaticText(this, wxID_ANY, _L("Print Presets") + colon_str),
            0,
            wxEXPAND | wxLEFT | wxRIGHT,
            border
        );
        main_sizer->Add(m_prints, 0, wxEXPAND | wxLEFT | wxBOTTOM | wxRIGHT, border);
    }
    if (show_tool_prints()) {
        main_sizer->Add(
            new wxStaticText(this, wxID_ANY, _L("Tool Print Presets") + colon_str),
            0,
            wxEXPAND | wxLEFT | wxRIGHT,
            border
        );
        main_sizer->Add(m_tools_prints, 0, wxEXPAND | wxLEFT | wxBOTTOM | wxRIGHT, border);
    }
    if (show_materials()) {
        main_sizer->Add(
            new wxStaticText(this, wxID_ANY, _L("Tool Material Presets") + colon_str),
            0,
            wxEXPAND | wxLEFT | wxRIGHT,
            border
        );
        main_sizer->Add(m_tool_materials, 0, wxEXPAND | wxLEFT | wxBOTTOM | wxRIGHT, border);
    }
    main_sizer->Add(m_tree, 1, wxEXPAND | wxALL, border);
    main_sizer->Add(m_bottom_info_line, 0, wxEXPAND | wxALL, border);

    this->SetSizer(main_sizer);
    main_sizer->SetSizeHints(this);

    this->SetSize(wxSize(30 * em, 60 * em));
    this->CenterOnParent();

    init_from_selection();

    this->Bind(wxEVT_DPI_CHANGED, [this](wxDPIChangedEvent& evt) {
        m_tree->model->Rescale();
    });
}

void DiffDialog::init_from_selection()
{
    const Domain::Preset::SelectedPreset& spp = m_preset_interactor.selected_printer_preset();

    const auto& printers = m_preset_interactor.printer_presets().items();
    int selected_printer = -1;
    for (size_t i = 0; i < printers.size(); i++) {
        auto& printer = printers.at(i);
        if (printer.hw_printer_config_id == spp.hw_config.id && printer.id == spp.printer.id) {
            selected_printer = int(i);
            break;
        }
    }
    ASSERT(selected_printer>=0);

    int selected_print = 0;
    bool found{ false };
    for (const auto [print_ref, print_is_runtime] :
         m_preset_interactor.get_print_presets(spp.hw_config.id, spp.printer.id))
    {
        const auto& print_preset = print_ref.get();
        if (print_preset.id == spp.print.id) {
            found = true;
            break;
        }
        selected_print++;
    }
    ASSERT(found);

    bool has_tool_prints = spp.technology() == Domain::PrinterTechnology::FFF;

    int selected_frst_tool_print = has_tool_prints ? 0 : -1;
    found = false;
    if (has_tool_prints) {
        for (const auto [ref, is_runtime] :
             m_preset_interactor
                 .get_tool_print_presets(spp.hw_config.id, spp.printer.id, spp.print.id, 0))
        {
            const auto& tool_print_preset = ref.get();
            if (spp.tools[0].id == tool_print_preset.id) {
                found = true;
                break;
            }
            selected_frst_tool_print++;
        }
    }
    ASSERT(found);

    int selected_material = 0;
    found = false;
    for (const auto [ref, is_runtime] :
         m_preset_interactor
             .get_material_presets(spp.hw_config.id, spp.printer.id, spp.print.id, 0))
    {
        const auto& material_preset = ref.get();
        if (spp.materials[0].id == material_preset.id) {
            found = true;
            break;
        }
        selected_material++;
    }
    ASSERT(found);

    select_printer(
        selected_printer,
        Location::Left,
        selected_print,
        selected_frst_tool_print,
        selected_material
    );
    select_printer(
        selected_printer,
        Location::Right,
        selected_print,
        selected_frst_tool_print,
        selected_material
    );
}

const Domain::Preset::EvaluatedPrinterPreset::Preset&
get_printer_preset(const Slic3r::Biz::Preset::PresetInteractor& preset_interactor, int printer_id)
{
    ASSERT(printer_id >= 0);
    auto& printer = preset_interactor.printer_presets().items().at(printer_id);

    const auto [printer_ref, is_runtime] =
        preset_interactor.get_printer_preset(printer.hw_printer_config_id, printer.id);

    return printer_ref.get();
}

static bool can_compare_printers(
    const Slic3r::Biz::Preset::PresetInteractor& preset_interactor,
    int frst_id,
    int scnd_id
)
{
    if (frst_id < 0 || scnd_id < 0) {
        // undef state or all comboboxes arn'i initialize
        return false;
    }

    return get_printer_preset(preset_interactor, frst_id).kind
        == get_printer_preset(preset_interactor, scnd_id).kind;
}

const Biz::Preset::PresetItem& DiffDialog::printer_item(int selected_printer_id)
{
    ASSERT(selected_printer_id >= 0);
    return m_preset_interactor.printer_presets().items().at(selected_printer_id);
}

void DiffDialog::select_printer(
    int selection,
    Diff::Location location,
    int print_selection,
    int tool_print_selection,
    int material_selection
)
{
    if (selection < 0)
        return;

    m_printers->select(selection, location);

    m_can_compare = can_compare_printers(
        m_preset_interactor,
        m_printers->selection(Location::Left),
        m_printers->selection(Location::Right)
    );

    if (!m_can_compare) {
        m_printers->set_state(State::NotEqual);
        m_prints->set_state(State::Undef);
        m_tools_prints->set_state(State::Undef);
        m_tool_materials->set_state(State::Undef);
        m_bottom_info_line->SetLabelText(_L("Compared presets have different printer technology"));
        this->Refresh();
    }

    const auto& printer = printer_item(selection);

    BCB* prints_cb = (*m_prints)[location];
    prints_cb->Clear();
    for (const auto [ref, is_runtime] :
         m_preset_interactor.get_print_presets(printer.hw_printer_config_id, printer.id))
    {
        const auto& print_preset = ref.get();
        prints_cb->Append(from_u8(get_preset_name(print_preset)));
    }

    select_print(print_selection, location, tool_print_selection, material_selection);
}

void DiffDialog::select_print(
    int selection,
    Location location,
    int tool_print_selection,
    int material_selection
)
{
    if (selection < 0)
        return;
    m_prints->select(selection, location);

    const auto& printer = printer_item(m_printers->selection(location));

    const auto [hw_printer_config_ref, is_runtime] =
        m_preset_interactor.get_printer_config(printer.hw_printer_config_id);
    const Domain::Preset::HwPrinterConfig& hw_printer_config = hw_printer_config_ref.get();

    const auto [print_ref, is_runtime_print] =
        m_preset_interactor.get_print_presets(printer.hw_printer_config_id, printer.id)[selection];
    const Domain::Preset::EvaluatedPrintPreset::Preset& print = print_ref.get();

    BCB* tool_cb = (*m_tools_prints)[location];
    tool_cb->Clear();

    if (hw_printer_config.technology == Domain::PrinterTechnology::SLA) {
        tool_cb->SetLabel(wxEmptyString);
        select_tool_print(0, tool_print_selection, location, material_selection);
        return;
    }

    size_t row_id = 0;
    for (int tool_id = 0; tool_id < hw_printer_config.tool_count; tool_id++) {
        std::string tool_marker =
            fmt::format("       ------- {} {} -------", _u8L("Tool"), tool_id + 1);
        tool_cb->Append(from_u8(tool_marker));
        tool_cb->SetClientObject(row_id++, new BCBClientData());

        size_t tool_print_id = 0;
        for (const auto [ref, is_runtime] : m_preset_interactor.get_tool_print_presets(
                 printer.hw_printer_config_id,
                 printer.id,
                 print.id,
                 tool_id
             ))
        {
            const auto& tool_print_preset = ref.get();

            tool_cb->Append(from_u8(get_preset_name(tool_print_preset)));
            tool_cb->SetClientObject(row_id++, new BCBClientData(tool_id, tool_print_id++));
        }
    }

    select_tool_print(0, tool_print_selection, location, material_selection);
}

void DiffDialog::select_tool_print(
    size_t tool_id,
    size_t tool_print_id,
    Diff::Location location,
    int material_selection
)
{
    BCB* tool_cb = (*m_tools_prints)[location];

    const auto& printer = printer_item(m_printers->selection(location));

    const auto [print_ref, is_runtime_print] = m_preset_interactor.get_print_presets(
        printer.hw_printer_config_id,
        printer.id
    )[m_prints->selection(location)];
    const Domain::Preset::EvaluatedPrintPreset::Preset& print = print_ref.get();

    if (!tool_cb->IsListEmpty()) {
        int selection = 0;
        // get selection id from tool_print_id
        for (size_t i = 0; i <= tool_id; i++) {
            selection++; // marked id
            if (i == tool_id) {
                selection += tool_print_id;
                break;
            }

            size_t preset_per_tool_cnt =
                m_preset_interactor
                    .get_tool_print_presets(printer.hw_printer_config_id, printer.id, print.id, i)
                    .size();
            selection += preset_per_tool_cnt;
        }
        tool_cb->SetSelection(selection);
    }

    BCB* material_cb = (*m_tool_materials)[location];
    material_cb->Clear();
    for (const auto [ref, is_runtime] :
         m_preset_interactor
             .get_material_presets(printer.hw_printer_config_id, printer.id, print.id, tool_id))
    {
        const auto& material_preset = ref.get();
        material_cb->Append(from_u8(get_preset_name(material_preset)));
    }

    select_material(material_selection, location);
}

void DiffDialog::select_material(int selection, Location location)
{
    if (selection < 0)
        return;

    m_tool_materials->select(selection, location);

    if (!(*m_prints)[Location::Right]->IsListEmpty()) {
        // for the correct comparation all comboboxes should be filled
        compare();
        Layout();
    }
}

void DiffDialog::create_tree()
{
    int em = w_config()->em_unit();
    m_tree = new DiffViewCtrl(this, wxSize(em * 80, em * 40));
    m_tree->SetFont(this->GetFont());
    m_tree->AppendBmpTextColumn(wxEmptyString, DiffDVCModel::colIconText, 35);
    {
        // !!! Not the best hack, but we need it to not missed up columns ids but use colIconText as a first column instead of colToggle
        // Note, that we will now edit either of columns row, so wi will not crashed in DiffDVCModel::GetColumnType
        m_tree->AppendToggleColumn_(wxString(L"\u2714"), DiffDVCModel::colToggle, 6);
        m_tree->GetColumn(/*DiffDVCModel::colToggle*/ 1)->SetHidden(true);
    }
    m_tree->AppendBmpTextColumn(_L("Left Preset Value"), DiffDVCModel::colOldValue, 15);
    m_tree->AppendBmpTextColumn(_L("Right Preset Value"), DiffDVCModel::colModValue, 15);
    m_tree->Hide();
}

void DiffDialog::compare()
{
    if (!m_can_compare) {
        // clear tree_view and hide it
        m_tree->Hide();
        return;
    }

    const auto& printer_left  = printer_item(m_printers->selection(Location::Left));
    const auto& printer_right = printer_item(m_printers->selection(Location::Right));

    const auto [printer_left_ref, irt_printer_left] =
        m_preset_interactor.get_printer_preset(printer_left.hw_printer_config_id, printer_left.id);
    const Domain::Preset::EvaluatedPrinterPreset::Preset& printer_preset_left =
        printer_left_ref.get();

    const auto [printer_right_ref, irt_printer_right] = m_preset_interactor.get_printer_preset(
        printer_right.hw_printer_config_id,
        printer_right.id
    );
    const Domain::Preset::EvaluatedPrinterPreset::Preset& printer_preset_right =
        printer_right_ref.get();

    ASSERT(printer_preset_left.kind == printer_preset_right.kind);

    const auto [print_ref_left, irt_print_left] = m_preset_interactor.get_print_presets(
        printer_left.hw_printer_config_id,
        printer_left.id
    )[m_prints->selection(Location::Left)];
    const Domain::Preset::EvaluatedPrintPreset::Preset& print_preset_left = print_ref_left.get();

    const auto [print_ref_right, is_runtime_print] = m_preset_interactor.get_print_presets(
        printer_right.hw_printer_config_id,
        printer_right.id
    )[m_prints->selection(Location::Right)];
    const Domain::Preset::EvaluatedPrintPreset::Preset& print_preset_right = print_ref_right.get();

    BCBClientData* tool_data_left  = m_tools_prints->client_data_selected(Location::Left);
    BCBClientData* tool_data_right = m_tools_prints->client_data_selected(Location::Right);

    std::vector<std::string> diff_keys;
    // full list
    m_diffs_per_kind.clear();

    if (show_printers()) {
        const Domain::ConfigBox& cb_left = printer_preset_left.config_box();
        diff_keys                        = cb_left.diff_keys(printer_preset_right.config_box());
        if (!diff_keys.empty()) {
            Biz::Preset::PresetSelectionCheck::filter_diff_keys(cb_left, diff_keys);
            m_diffs_per_kind[printer_preset_left.kind] = diff_keys;
        }
        m_printers->set_state(diff_keys.empty() ? State::Equal : State::NotEqual);
    }

    if (show_prints()) {
        const Domain::ConfigBox& cb_left = print_preset_left.config_box();
        diff_keys                        = cb_left.diff_keys(print_preset_right.config_box());
        if (!diff_keys.empty()) {
            Biz::Preset::PresetSelectionCheck::filter_diff_keys(cb_left, diff_keys);
            m_diffs_per_kind[print_preset_left.kind] = diff_keys;
        }
        m_prints->set_state(diff_keys.empty() ? State::Equal : State::NotEqual);

        if (printer_preset_left.kind == Domain::Preset::PresetKind::FdmPrinter) {
            const auto [tool_print_ref_left, is_runtime_tool_print_left] =
                m_preset_interactor.get_tool_print_presets(
                    printer_left.hw_printer_config_id,
                    printer_left.id,
                    print_preset_left.id,
                    tool_data_left->tool_id
                )[tool_data_left->tool_print_id];
            const Domain::Preset::EvaluatedToolPrintPreset::Preset& tool_print_preset_left =
                tool_print_ref_left.get();

            const auto [tool_print_ref_right, is_runtime_tool_print_right] =
                m_preset_interactor.get_tool_print_presets(
                    printer_right.hw_printer_config_id,
                    printer_right.id,
                    print_preset_right.id,
                    tool_data_right->tool_id
                )[tool_data_right->tool_print_id];
            const Domain::Preset::EvaluatedToolPrintPreset::Preset& tool_print_preset_right =
                tool_print_ref_right.get();

            const Domain::ConfigBox& cb_left = tool_print_preset_left.config_box();
            diff_keys = cb_left.diff_keys(tool_print_preset_right.config_box());
            if (!diff_keys.empty()) {
                Biz::Preset::PresetSelectionCheck::filter_diff_keys(cb_left, diff_keys);
                m_diffs_per_kind[Domain::Preset::PresetKind::FdmToolPrint] = diff_keys;
            }
            m_tools_prints->set_state(diff_keys.empty() ? State::Equal : State::NotEqual);
        }
    }

    if (show_materials()) {
        bool is_sla = printer_preset_left.kind == Domain::Preset::PresetKind::SlaPrinter;

        const auto [material_ref_left, irt_material_left] =
            m_preset_interactor.get_material_presets(
                printer_left.hw_printer_config_id,
                printer_left.id,
                print_preset_left.id,
                is_sla ? 0 : tool_data_left->tool_id
            )[m_tool_materials->selection(Location::Left)];
        const Domain::Preset::EvaluatedMaterialPreset::Preset& material_preset_left =
            material_ref_left.get();

        const auto [filament_ref_right, irt_material_right] =
            m_preset_interactor.get_material_presets(
                printer_right.hw_printer_config_id,
                printer_right.id,
                print_preset_right.id,
                is_sla ? 0 : tool_data_right->tool_id
            )[m_tool_materials->selection(Location::Right)];
        const Domain::Preset::EvaluatedMaterialPreset::Preset& material_preset_right =
            filament_ref_right.get();

        const Domain::ConfigBox& cb_left = material_preset_left.config_box();
        diff_keys                        = cb_left.diff_keys(material_preset_right.config_box());
        if (!diff_keys.empty()) {
            Biz::Preset::PresetSelectionCheck::filter_diff_keys(cb_left, diff_keys);
            m_diffs_per_kind[material_preset_left.kind] = diff_keys;
        }
        m_tool_materials->set_state(diff_keys.empty() ? State::Equal : State::NotEqual);
    }

    update_tree();

    wxString out = m_diffs_per_kind.empty() ?
        _L("Presets are the same.") :
        m_tree->has_long_strings() ?
        _L("Some fields are too long to fit. Right mouse click reveals the full text.") :
        from_u8("");

    m_bottom_info_line->SetLabelText(out);
    m_tree->Show(!m_diffs_per_kind.empty());
}

static void append_diff_key_in_tree(
    DiffViewCtrl* tree,
    Domain::Preset::PresetKind kind,
    const std::string& kind_preset_name,
    const Domain::ConfigBox* config_left,
    const Domain::ConfigBox* config_right,
    Domain::PrinterTechnology pt,
    const std::vector<std::string>& diff_keys
)
{
    if (kind_preset_name.empty() || diff_keys.empty() || !config_left || !config_right) {
        return;
    }

    tree->model->AddPreset(kind, kind_preset_name);

    for (const std::string& key : diff_keys) {
        const Domain::ConfigItem& item_left = *config_left->find(key).item;
        const Domain::ConfigItemDef& def    = item_left.def();
        tree->Append(
            key,
            kind,
            kind_preset_name,
            Biz::_u8(Domain::ConfigItemDef::translate_category(def.category, pt)),
            Biz::_u8(
                def.option_group == Domain::ConfigItemDef::OptionGroup::Unknown ?
                    def.label :
                    Domain::ConfigItemDef::translate_option_group(def.option_group)
            ),
            Biz::_u8(def.full_label.empty() ? def.label : def.full_label),
            Diff::get_display_value_or_na(config_left, key),
            Diff::get_display_value_or_na(config_right, key),
            "",
            CategoryUtils::category_icon_name(def.category, pt)
        );
    }
}

void DiffDialog::update_tree()
{
    if (!m_can_compare)
        return;

    m_tree->Clear();

    using Domain::Preset::PresetKind;

    const auto& printer_left  = printer_item(m_printers->selection(Location::Left));
    const auto& printer_right = printer_item(m_printers->selection(Location::Right));

    const auto [print_left_ref, is_runtime_print_left] = m_preset_interactor.get_print_presets(
        printer_left.hw_printer_config_id,
        printer_left.id
    )[m_prints->selection(Location::Left)];
    const Domain::Preset::EvaluatedPrintPreset::Preset& print_preset_left = print_left_ref.get();

    const auto [print_right_ref, is_runtime_print_right] = m_preset_interactor.get_print_presets(
        printer_right.hw_printer_config_id,
        printer_right.id
    )[m_prints->selection(Location::Right)];
    const Domain::Preset::EvaluatedPrintPreset::Preset& print_preset_right = print_right_ref.get();

    // Append diffs for priners, prints and SLAMAterial if thare are selctd SLA printers

    for (const auto& [kind, diff_keys] : m_diffs_per_kind) {
        const Domain::ConfigBox* config_left{nullptr};
        const Domain::ConfigBox* config_right{nullptr};
        std::string preset_name;

        Domain::PrinterTechnology pt = print_preset_left.kind == PresetKind::FdmPrint ?
            Domain::PrinterTechnology::FFF :
            Domain::PrinterTechnology::SLA;

        switch (kind) {
        case PresetKind::FdmPrinter:
        case PresetKind::SlaPrinter: {
            if (!m_printers->is_checked_checkbox()) {
                break;
            }
            const auto [printer_left_ref, is_runtime_printer_left] =
                m_preset_interactor.get_printer_preset(
                    printer_left.hw_printer_config_id,
                    printer_left.id
                );
            const Domain::Preset::EvaluatedPrinterPreset::Preset& printer_preset_left =
                printer_left_ref.get();

            const auto [printer_right_ref, is_runtime_printer_right] =
                m_preset_interactor.get_printer_preset(
                    printer_right.hw_printer_config_id,
                    printer_right.id
                );
            const Domain::Preset::EvaluatedPrinterPreset::Preset& printer_preset_right =
                printer_right_ref.get();

            preset_name = fmt::format(
                "{} \"{}({})\" vs \"{}({})\"",
                _u8L("Printer"),
                get_preset_name(printer_preset_left),
                printer_left.ui_hw_config_name(),
                get_preset_name(printer_preset_right),
                printer_right.ui_hw_config_name()
            );

            if (kind == PresetKind::FdmPrinter) {
                config_left  = &std::get<Domain::PrinterSettings>(printer_preset_left.values);
                config_right = &std::get<Domain::PrinterSettings>(printer_preset_right.values);
            } else {
                config_left  = &std::get<Domain::SLAPrinterSettings>(printer_preset_left.values);
                config_right = &std::get<Domain::SLAPrinterSettings>(printer_preset_right.values);
            }
            break;
        }
        case PresetKind::FdmPrint:
        case PresetKind::SlaPrint: {
            if (!m_prints->is_checked_checkbox()) {
                break;
            }
            preset_name = fmt::format(
                "{} \"{}\" vs \"{}\"",
                _u8L("Print"),
                get_preset_name(print_preset_left),
                get_preset_name(print_preset_right)
            );
            if (kind == PresetKind::FdmPrint) {
                config_left  = &std::get<Domain::PrintSettings>(print_preset_left.values);
                config_right = &std::get<Domain::PrintSettings>(print_preset_right.values);
            } else {
                config_left  = &std::get<Domain::SLAPrintSettings>(print_preset_left.values);
                config_right = &std::get<Domain::SLAPrintSettings>(print_preset_right.values);
            }
            break;
        }
        case PresetKind::FdmToolPrint: {
            if (!m_tools_prints->is_checked_checkbox()) {
                break;
            }
            BCBClientData* tool_left_data  = m_tools_prints->client_data_selected(Location::Left);
            BCBClientData* tool_right_data = m_tools_prints->client_data_selected(Location::Right);

            const auto [tool_print_ref_left, is_runtime_tool_print_left] =
                m_preset_interactor.get_tool_print_presets(
                    printer_left.hw_printer_config_id,
                    printer_left.id,
                    print_preset_left.id,
                    tool_left_data->tool_id
                )[tool_left_data->tool_print_id];
            const Domain::Preset::EvaluatedToolPrintPreset::Preset& tool_print_preset_left =
                tool_print_ref_left.get();

            const auto [tool_print_ref_right, is_runtime_tool_print_right] =
                m_preset_interactor.get_tool_print_presets(
                    printer_right.hw_printer_config_id,
                    printer_right.id,
                    print_preset_right.id,
                    tool_right_data->tool_id
                )[tool_right_data->tool_print_id];
            const Domain::Preset::EvaluatedToolPrintPreset::Preset& tool_print_preset_right =
                tool_print_ref_right.get();

            preset_name = fmt::format(
                "{} \"{}\" vs \"{}\"",
                _u8L("Tool Print"),
                get_preset_name(tool_print_preset_left),
                get_preset_name(tool_print_preset_right)
            );

            config_left  = &std::get<Domain::ToolPrintSettings>(tool_print_preset_left.values);
            config_right = &std::get<Domain::ToolPrintSettings>(tool_print_preset_right.values);
            break;
        }
        case PresetKind::FdmMaterial: {
            if (!m_tool_materials->is_checked_checkbox()) {
                break;
            }
            BCBClientData* tool_left_data  = m_tools_prints->client_data_selected(Location::Left);
            BCBClientData* tool_right_data = m_tools_prints->client_data_selected(Location::Right);

            const auto [filament_ref_left, is_runtime_filament_left] =
                m_preset_interactor.get_material_presets(
                    printer_left.hw_printer_config_id,
                    printer_left.id,
                    print_preset_left.id,
                    tool_left_data->tool_id
                )[m_tool_materials->selection(Location::Left)];
            const Domain::Preset::EvaluatedMaterialPreset::Preset& filament_preset_left =
                filament_ref_left.get();

            const auto [filament_ref_right, is_runtime_filament_right] =
                m_preset_interactor.get_material_presets(
                    printer_right.hw_printer_config_id,
                    printer_right.id,
                    print_preset_right.id,
                    tool_right_data->tool_id
                )[m_tool_materials->selection(Location::Right)];
            const Domain::Preset::EvaluatedMaterialPreset::Preset& filament_preset_right =
                filament_ref_right.get();

            config_left  = &std::get<Domain::FilamentSettings>(filament_preset_left.values);
            config_right = &std::get<Domain::FilamentSettings>(filament_preset_right.values);
            preset_name  = fmt::format(
                "{} \"{}\" vs \"{}\"",
                _u8L("Tool Filament"),
                get_preset_name(filament_preset_left),
                get_preset_name(filament_preset_right)
            );
            break;
        }
        case PresetKind::SlaMaterial: {
            if (!m_tool_materials->is_checked_checkbox()) {
                break;
            }

            const auto [sla_material_ref_left, is_runtime_sla_material_left] =
                m_preset_interactor.get_material_presets(
                    printer_left.hw_printer_config_id,
                    printer_left.id,
                    print_preset_left.id,
                    0
                )[m_tool_materials->selection(Location::Left)];
            const Domain::Preset::EvaluatedMaterialPreset::Preset& sla_material_preset_left =
                sla_material_ref_left.get();

            const auto [sla_material_ref_right, is_runtime_sla_material_right] =
                m_preset_interactor.get_material_presets(
                    printer_right.hw_printer_config_id,
                    printer_right.id,
                    print_preset_right.id,
                    0
                )[m_tool_materials->selection(Location::Right)];
            const Domain::Preset::EvaluatedMaterialPreset::Preset& sla_material_preset_right =
                sla_material_ref_right.get();

            config_left  = &std::get<Domain::SLAMaterialSettings>(sla_material_preset_left.values);
            config_right = &std::get<Domain::SLAMaterialSettings>(sla_material_preset_right.values);
            preset_name  = fmt::format(
                "{} \"{}\" vs \"{}\"",
                _u8L("Material"),
                get_preset_name(sla_material_preset_left),
                get_preset_name(sla_material_preset_right)
            );
            break;
        }
        default:
            break;
        };

        if (preset_name.empty()) {
            continue;
        }
        append_diff_key_in_tree(
            m_tree,
            kind,
            preset_name,
            config_left,
            config_right,
            pt,
            diff_keys
        );
    }
}

bool DiffDialog::show_printers() const
{
    return !m_kind
        || m_kind.value() == PresetKind::FdmPrinter
        || m_kind.value() == PresetKind::SlaPrinter;
}

bool DiffDialog::show_prints() const
{
    return !m_kind
        || m_kind.value() == PresetKind::FdmPrint
        || m_kind.value() == PresetKind::SlaPrint;
}

bool DiffDialog::show_tool_prints() const
{
    return show_prints(); // show tool_prints together with tools
    return !m_kind
        || m_kind.value()
        == PresetKind::FdmToolPrint /* || m_kind.value() == PresetKind::SlaPrint*/;
}

bool DiffDialog::show_materials() const
{
    return !m_kind
        || m_kind.value() == PresetKind::FdmMaterial
        || m_kind.value() == PresetKind::SlaMaterial;
}

} // namespace Slic3r::App::WX
