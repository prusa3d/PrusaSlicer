#include "Slic3r/App/WX/UnsavedChangesDialog.hpp"
#include "Slic3r/App/WX/SavePresetDialog.hpp"

#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Biz/Config/ConfigSerialize.hpp"
#include "Slic3r/App/Config/CategoryUtils.hpp"

#include "Slic3r/Domain/FullConfigFDM.hpp"
#include "Slic3r/Domain/FullConfigSLA.hpp"

#include "Slic3r/App/WX/StringConversions.hpp"
#include "Slic3r/App/WX/DiffViewCtrl.hpp"
#include "Slic3r/App/WX/DiffDVCModel.hpp"
#include "Slic3r/App/WX/DiffNamespace.hpp"
#include "Slic3r/App/WX/WidgetsConfig.hpp"
#include "Slic3r/App/WX/BitmapGetters.hpp"
#include "Slic3r/App/WX/I18N.hpp"
#include <Slic3r/Biz/I18N/I18N.hpp>

#include <wx/app.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/string.h>

#include <fmt/format.h>
#include "Slic3r/Log.hpp"

using namespace Slic3r::Biz;

namespace Slic3r::App::WX {

UnsavedChangesDialog::UnsavedChangesDialog(
    const std::string& dialog_name,
    const Domain::ConfigPack& config_original,
    const Domain::ConfigPack& config_selected,
    Domain::ConfigPack* config_new_selected,
    const Slic3r::Biz::Preset::PresetSelectionNames& preset_names,
    const Slic3r::Biz::Preset::PresetSelectionNames& preset_names_new,
    const Biz::Preset::PresetInteractor& preset_interactor,
    bool new_printer_has_multiple_extruders
) :
    wxDialog(
        wxTheApp->GetTopWindow(),
        wxID_ANY,
        from_u8(dialog_name),
        wxDefaultPosition,
        wxDefaultSize,
        wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER
    ),
    m_config_original(config_original),
    m_config_selected(config_selected),
    m_config_new(config_new_selected),
    m_preset_names(preset_names),
    m_preset_names_new(preset_names_new),
    m_preset_interactor(preset_interactor),
    m_new_printer_has_multiple_extruders(new_printer_has_multiple_extruders)
{
    if (std::get_if<Domain::ConfigPackFDM>(&m_config_original) == nullptr) {
        m_printer_technology = Domain::PrinterTechnology::SLA;
    }

    const wxFont font = GetFont().Bold();

    m_top_info_line = new wxStaticText(this, wxID_ANY, wxEmptyString);
    m_top_info_line->SetFont(font);

    m_bottom_info_line = new wxStaticText(this, wxID_ANY, from_u8("info from selection"));
    m_bottom_info_line->SetFont(font);

    int border             = w_config()->em_unit();
    wxBoxSizer* main_sizer = new wxBoxSizer(wxVERTICAL);

    create_tree();
    compare();

    wxBoxSizer* buttons = new wxBoxSizer(wxHORIZONTAL);
    add_buttons(buttons);

    main_sizer->Add(m_top_info_line, 0, wxEXPAND | wxALL, border);
    main_sizer->Add(m_tree, 1, wxEXPAND | wxALL, border);
    main_sizer->Add(m_bottom_info_line, 0, wxEXPAND | wxALL, border);
    main_sizer->Add(buttons, 0, wxEXPAND | wxALL, border);

    this->SetSizer(main_sizer);
    main_sizer->SetSizeHints(this);

    this->SetMinSize(wxSize(30 * w_config()->em_unit(), 50 * w_config()->em_unit()));
    this->CenterOnParent();

    this->Bind(wxEVT_DPI_CHANGED, [this](wxDPIChangedEvent& evt) { m_tree->model->Rescale(); });

    m_exit_queue = m_diffs_per_kind.size();
    show_current_diffs();
}

void UnsavedChangesDialog::create_tree()
{
    int em = w_config()->em_unit();
    m_tree = new DiffViewCtrl(this, wxSize(em * (m_config_new ? 90 : 65), em * 40));
    m_tree->SetFont(this->GetFont());

#ifdef __linux__
    const int toggle_column_width = 10;
#else
    const int toggle_column_width = 6;
#endif

    m_tree->AppendToggleColumn_(wxString(L"\u2714"), DiffDVCModel::colToggle, toggle_column_width);
    m_tree->AppendBmpTextColumn(wxEmptyString, DiffDVCModel::colIconText, 35);
    m_tree->AppendBmpTextColumn(_L("Original Value"), DiffDVCModel::colOldValue, 15);
    m_tree->AppendBmpTextColumn(_L("Modified Value"), DiffDVCModel::colModValue, 15);
    if (m_config_new)
        m_tree->AppendBmpTextColumn(_L("New Value"), DiffDVCModel::colNewValue, 15);
}

void UnsavedChangesDialog::add_buttons(wxBoxSizer* buttons)
{
    // Add Buttons
    wxFont btn_font = this->GetFont().Scaled(1.4f);

    auto add_btn = [this, buttons, btn_font](
                       wxButton** btn,
                       const std::string& icon_name,
                       Biz::Preset::PresetDiffOperation close_act,
                       const wxString& label,
                       std::function<bool()> is_enabled_fn = nullptr,
                       std::function<void()> fn            = nullptr
                   )
    {
        *btn = new wxButton(
            this,
            wxID_ANY,
            label,
            wxDefaultPosition,
            wxSize(wxDefaultCoord, 30),
            wxBORDER_SIMPLE
        );
        (*btn)->SetBitmap(*get_bmp_bundle(icon_name, 20, 20));
        (*btn)->SetBitmapMargins(int(0.5 * w_config()->em_unit(this)), 0);

        buttons->Add(*btn, 1, wxLEFT, buttons->IsEmpty() ? 0 : 5);
        (*btn)->SetFont(btn_font);

        (*btn)->Bind(
            wxEVT_BUTTON,
            [this, close_act, fn](wxEvent&)
            {
                if (fn) {
                    fn();
                } else {
                    process_button_click(close_act);
                }
            }
        );

        (*btn)->Bind(
            wxEVT_UPDATE_UI,
            [this, is_enabled_fn](wxUpdateUIEvent& evt)
            {
                if (is_enabled_fn)
                    evt.Enable(is_enabled_fn());
            }
        );

        (*btn)->Bind(
            wxEVT_LEAVE_WINDOW,
            [this](wxMouseEvent& e)
            {
                show_info_line(Biz::Preset::PresetDiffOperation::Undef);
                e.Skip();
            }
        );

        (*btn)->Bind(
            wxEVT_ENTER_WINDOW,
            [this, close_act](wxMouseEvent& e)
            {
                show_info_line(close_act);
                e.Skip();
            }
        );
    };

    if (m_diffs_per_kind.size() > 1) {
        add_btn(
            &m_back_btn,
            "chevron_left",
            Biz::Preset::PresetDiffOperation::Undef,
            _L("Back"),
            nullptr,
            [this]()
            {
                m_exit_queue++;
                show_current_diffs();
            }
        );
    }

    if (m_config_new) {
        add_btn(
            &m_transfer_btn,
            "paste_menu",
            Biz::Preset::PresetDiffOperation::Transfer,
            _L("Transfer"),
            [this]() { return m_is_enabled_transfer && m_tree->has_selection(); }
        );
    }

    add_btn(&m_discard_btn, "exit", Biz::Preset::PresetDiffOperation::Discard, _L("Discard"));

    // "Save" button
    add_btn(
        &m_save_btn,
        "save",
        Biz::Preset::PresetDiffOperation::Save,
        _L("Save"),
        [this]() { return m_tree->has_selection(); }
    );
    m_save_btn->Enable(false); // Temporary: until save functionality is implemented

    wxButton* cancel_btn;
    add_btn(&cancel_btn, "cross", Biz::Preset::PresetDiffOperation::Undef, _L("Cancel"));
}

void UnsavedChangesDialog::compare()
{
    m_diffs_per_kind.clear();
    std::vector<std::string> diff_keys;

    if (m_printer_technology == Domain::PrinterTechnology::FFF) {
        auto config_pack_original = std::get<Domain::ConfigPackFDM>(m_config_original);
        auto config_pack_selected = std::get<Domain::ConfigPackFDM>(m_config_selected);

        diff_keys = config_pack_original.printer.diff_keys(config_pack_selected.printer);
        if (!diff_keys.empty()) {
            m_diffs_per_kind[{PresetKind::FdmPrinter}] = diff_keys;
        }

        diff_keys = config_pack_original.print.diff_keys(config_pack_selected.print);
        if (!diff_keys.empty()) {
            m_diffs_per_kind[{PresetKind::FdmPrint}] = diff_keys;
        }

        for (size_t
                 i = 0,
                 n = std::min(config_pack_original.tool.size(), config_pack_selected.tool.size());
             i < n;
             i++)
        {
            diff_keys = config_pack_original.tool[i].diff_keys(config_pack_selected.tool[i]);
            if (!diff_keys.empty()) {
                m_diffs_per_kind[{PresetKind::FdmToolPrint, i}] = diff_keys;
            }
        }

        for (size_t i = 0,
                    n = std::min(
                        config_pack_original.filament.size(),
                        config_pack_selected.filament.size()
                    );
             i < n;
             i++)
        {
            diff_keys =
                config_pack_original.filament[i].diff_keys(config_pack_selected.filament[i]);
            if (!diff_keys.empty()) {
                m_diffs_per_kind[{PresetKind::FdmMaterial, i}] = diff_keys;
            }
        }
    } else {
        auto config_pack_original = std::get<Domain::ConfigPackSLA>(m_config_original);
        auto config_pack_selected = std::get<Domain::ConfigPackSLA>(m_config_selected);

        diff_keys = config_pack_original.sla_printer_settings.diff_keys(
            config_pack_selected.sla_printer_settings
        );
        if (!diff_keys.empty()) {
            m_diffs_per_kind[{PresetKind::SlaPrinter}] = diff_keys;
        }

        diff_keys = config_pack_original.sla_print_settings.diff_keys(
            config_pack_selected.sla_print_settings
        );
        if (!diff_keys.empty()) {
            m_diffs_per_kind[{PresetKind::SlaPrint}] = diff_keys;
        }

        diff_keys = config_pack_original.sla_material_settings.diff_keys(
            config_pack_selected.sla_material_settings
        );
        if (!diff_keys.empty()) {
            m_diffs_per_kind[{PresetKind::SlaMaterial, 0}] = diff_keys;
        }
    }
}

void UnsavedChangesDialog::show_current_diffs()
{
    size_t diffs_cnt = m_diffs_per_kind.size();
    size_t step      = diffs_cnt - m_exit_queue + 1;

    wxString info;
    if (!m_config_new)
        info += _L("Printer technology will be changed.") + from_u8("\n");
    info +=
        _L("Presets below have unsaved modifications.\n"
           "You need to process those modifications first.");

    if (diffs_cnt > 1) {
        const std::string suffix =
            fmt::vformat(_u8L("Step {} from {}"), fmt::make_format_args(step, diffs_cnt));
        info += from_u8("\n\n" + suffix);
    }
    m_top_info_line->SetLabel(info);

    auto it = std::next(m_diffs_per_kind.begin(), step - 1);
    update_tree(it->first, it->second);

    update_transfer_button(it->first);

    if (m_back_btn) {
        m_back_btn->Enable(step > 1);
    }

    show_info_line(PresetDiffOperation::Undef);
}

void UnsavedChangesDialog::update_transfer_button(PresetSwitchKindId kind_id)
{
    if (!m_transfer_btn)
        return;

    bool is_keep{false};
    switch (kind_id.kind) {
    case PresetKind::FdmPrinter:
    case PresetKind::SlaPrinter: {
        is_keep               = m_preset_names.printer == m_preset_names_new.printer;
        m_is_enabled_transfer = true;
        break;
    }
    case PresetKind::FdmPrint:
    case PresetKind::SlaPrint: {
        is_keep               = m_preset_names.print == m_preset_names_new.print;
        m_is_enabled_transfer = true;
        break;
    }
    case PresetKind::FdmToolPrint: {
        ASSERT(kind_id.id);
        size_t tool_id          = kind_id.id.value();
        const size_t tool_count = m_preset_names_new.tools.size();
        m_is_enabled_transfer =
            (tool_count > 1 || m_new_printer_has_multiple_extruders) && tool_id < tool_count;
        if (m_is_enabled_transfer) {
            is_keep = m_preset_names.tools[tool_id] == m_preset_names_new.tools[tool_id];
        }
        break;
    }
    case PresetKind::FdmMaterial:
    case PresetKind::SlaMaterial: {
        size_t tool_id = kind_id.id.value_or(0);
        if ((m_is_enabled_transfer = (m_preset_names_new.materials.size() > tool_id))) {
            is_keep = m_preset_names.materials[tool_id] == m_preset_names_new.materials[tool_id];
        }
        break;
    }
    default:
        break;
    };

    wxString new_label = is_keep ? _L("Keep") : _L("Transfer");
    if (m_transfer_btn->GetLabel() != new_label) {
        m_transfer_btn->SetLabel(new_label);
#ifdef __APPLE__
        // Workaround to invalidate the size and force its recalculation
        m_transfer_btn->SetBitmap(*get_bmp_bundle("paste_menu", 20, 20));
#endif
    }
}

void UnsavedChangesDialog::append_diff_keys(
    Domain::Preset::PresetKind kind,
    const std::string& preset_name,
    const std::string& new_preset_name,
    const Domain::ConfigBox* config_left,
    const Domain::ConfigBox* config_mid,
    const Domain::ConfigBox* config_right,
    const std::vector<std::string>& diff_keys
)
{
    if (preset_name.empty() || diff_keys.empty() || !config_left || !config_mid) {
        return;
    }

    m_tree->model->AddPreset(kind, preset_name, new_preset_name);

    for (const std::string& key : diff_keys) {
        Domain::ConstFindResult find_res = config_left->find(key);
        ASSERT(find_res.item);
        const Domain::ConfigItemDef& def = find_res.item->def();
        m_tree->Append(
            key,
            kind,
            preset_name,
            Biz::_u8(Domain::ConfigItemDef::translate_category(def.category, m_printer_technology)),
            Biz::_u8(
                def.option_group == Domain::ConfigItemDef::OptionGroup::Unknown ?
                    def.label :
                    Domain::ConfigItemDef::translate_option_group(def.option_group)
            ),
            Biz::_u8(def.full_label.empty() ? def.label : def.full_label),
            Diff::get_display_value_or_na(config_left, key),
            Diff::get_display_value_or_na(config_mid, key),
            Diff::get_display_value_or_na(config_right, key),
            CategoryUtils::category_icon_name(def.category, m_printer_technology)
        );
    }
}

static std::string name(Slic3r::Biz::Preset::PresetSelectionNames::PresetName preset_name)
{
    const std::string prefix{preset_name.is_runtime_only ? _u8L("(From 3mf) ") : ""};
    return prefix + preset_name.name;
}

void UnsavedChangesDialog::update_tree(
    PresetSwitchKindId kind_id,
    const std::vector<std::string>& diff_keys
)
{
    m_tree->Clear();

    const Domain::ConfigBox* config_left{nullptr};
    const Domain::ConfigBox* config_mid{nullptr};
    const Domain::ConfigBox* config_right{nullptr};

    std::string preset_name;
    std::string new_preset_name = std::string();

    PresetKind kind = kind_id.kind;

    switch (kind) {
    case PresetKind::FdmPrinter:
    case PresetKind::SlaPrinter: {
        preset_name     = fmt::format("{} \"{}\"", _u8L("Printer"), name(m_preset_names.printer));
        new_preset_name = name(m_preset_names_new.printer);
        if (kind == PresetKind::FdmPrinter) {
            config_left = &std::get<Domain::ConfigPackFDM>(m_config_original).printer;
            config_mid  = &std::get<Domain::ConfigPackFDM>(m_config_selected).printer;
            if (m_config_new) {
                config_right = &std::get<Domain::ConfigPackFDM>(*m_config_new).printer;
            }
        } else {
            config_left = &std::get<Domain::ConfigPackSLA>(m_config_original).sla_printer_settings;
            config_mid  = &std::get<Domain::ConfigPackSLA>(m_config_selected).sla_printer_settings;
            if (m_config_new) {
                config_right = &std::get<Domain::ConfigPackSLA>(*m_config_new).sla_printer_settings;
            }
        }
        break;
    }
    case PresetKind::FdmPrint:
    case PresetKind::SlaPrint: {
        preset_name     = fmt::format("{} \"{}\"", _u8L("Print"), name(m_preset_names.print));
        new_preset_name = name(m_preset_names_new.printer);
        if (kind_id.kind == PresetKind::FdmPrint) {
            config_left = &std::get<Domain::ConfigPackFDM>(m_config_original).print;
            config_mid  = &std::get<Domain::ConfigPackFDM>(m_config_selected).print;
            if (m_config_new) {
                config_right = &std::get<Domain::ConfigPackFDM>(*m_config_new).print;
            }
        } else {
            config_left = &std::get<Domain::ConfigPackSLA>(m_config_original).sla_print_settings;
            config_mid  = &std::get<Domain::ConfigPackSLA>(m_config_selected).sla_print_settings;
            if (m_config_new) {
                config_right = &std::get<Domain::ConfigPackSLA>(*m_config_new).sla_print_settings;
            }
        }
        break;
    }
    case PresetKind::FdmToolPrint: {
        ASSERT(kind_id.id);
        size_t tool_id = kind_id.id.value();
        config_left    = &std::get<Domain::ConfigPackFDM>(m_config_original).tool[tool_id];
        config_mid     = &std::get<Domain::ConfigPackFDM>(m_config_selected).tool[tool_id];
        if (m_config_new && std::get<Domain::ConfigPackFDM>(*m_config_new).tool.size() > tool_id) {
            config_right = &std::get<Domain::ConfigPackFDM>(*m_config_new).tool[tool_id];
        } else {
            config_right = nullptr;
        }

        preset_name = fmt::format(
            "{} {} \"{}\"",
            _u8L("Tool Print"),
            tool_id + 1,
            name(m_preset_names.tools[tool_id])
        );
        if (tool_id < m_preset_names_new.tools.size()) {
            new_preset_name = name(m_preset_names_new.tools[tool_id]);
        }

        break;
    }
    case PresetKind::FdmMaterial: {
        ASSERT(kind_id.id);
        size_t tool_id = kind_id.id.value();
        config_left    = &std::get<Domain::ConfigPackFDM>(m_config_original).filament[tool_id];
        config_mid     = &std::get<Domain::ConfigPackFDM>(m_config_selected).filament[tool_id];
        if (m_config_new
            && std::get<Domain::ConfigPackFDM>(*m_config_new).filament.size() > tool_id)
        {
            config_right = &std::get<Domain::ConfigPackFDM>(*m_config_new).filament[tool_id];
        } else {
            config_right = nullptr;
        }

        preset_name = fmt::format(
            "{} {} \"{}\"",
            _u8L("Tool Filament"),
            tool_id + 1,
            name(m_preset_names.materials[tool_id])
        );
        if (tool_id < m_preset_names_new.materials.size()) {
            new_preset_name = name(m_preset_names_new.materials[tool_id]);
        }
        break;
    }
    case PresetKind::SlaMaterial: {
        preset_name = fmt::format("{} \"{}\"", _u8L("Material"), name(m_preset_names.materials[0]));
        config_left = &std::get<Domain::ConfigPackSLA>(m_config_original).sla_material_settings;
        config_mid  = &std::get<Domain::ConfigPackSLA>(m_config_selected).sla_material_settings;
        if (m_config_new) {
            config_right    = &std::get<Domain::ConfigPackSLA>(*m_config_new).sla_material_settings;
            new_preset_name = name(m_preset_names_new.materials[0]);
        }
        break;
    }
    default:
        break;
    };

    append_diff_keys(
        kind,
        preset_name,
        new_preset_name,
        config_left,
        config_mid,
        config_right,
        diff_keys
    );
}

void UnsavedChangesDialog::show_info_line(
    Biz::Preset::PresetDiffOperation operation,
    std::string preset_name
)
{
    if (operation == Biz::Preset::PresetDiffOperation::Undef && !m_tree->has_long_strings())
        m_bottom_info_line->Hide();
    else {
        wxString text;
        if (operation == Biz::Preset::PresetDiffOperation::Undef)
            text = _L("Some fields are too long to fit. Right mouse click reveals the full text.");
        else if (operation == Biz::Preset::PresetDiffOperation::Discard)
            text = _L("All settings changes will be discarded.");
        else {
            if (preset_name.empty())
                text = operation == Biz::Preset::PresetDiffOperation::Save ?
                    _L("Save the selected options.") :
                    _L("Keep the selected settings.");
            //_L("Transfer the selected settings to the newly selected preset.");
            else
                text = from_u8(
                    fmt::vformat(
                        operation == Biz::Preset::PresetDiffOperation::Save ?
                            _u8L("Save the selected options to preset \"%1%\".") :
                            _u8L(
                                "Transfer the selected options to the newly selected preset \"%1%\"."
                            ),
                        fmt::make_format_args(preset_name)
                    )
                );
            text += from_u8("\n") + _L("Unselected options will be reverted.");
        }
        m_bottom_info_line->SetLabel(text);
        m_bottom_info_line->Show();
    }

    Layout();
    Refresh();
}

static std::string preset_name(
    const Biz::Preset::PresetSelectionNames& preset_names,
    Domain::Preset::PresetKind kind,
    size_t tool_id
)
{
    switch (kind) {
    case Domain::Preset::PresetKind::FdmPrinter:
    case Domain::Preset::PresetKind::SlaPrinter:
        return preset_names.printer.name;
    case Domain::Preset::PresetKind::FdmPrint:
    case Domain::Preset::PresetKind::SlaPrint:
        return preset_names.print.name;
    case Domain::Preset::PresetKind::FdmToolPrint:
    case Domain::Preset::PresetKind::SlaToolPrint:
        return preset_names.tools[tool_id].name;
    case Domain::Preset::PresetKind::FdmMaterial:
    case Domain::Preset::PresetKind::SlaMaterial:
        return preset_names.materials[tool_id].name;
    }
    return std::string();
}

void UnsavedChangesDialog::process_button_click(PresetDiffOperation operation)
{
    if (operation == PresetDiffOperation::Undef) {
        m_exit_states.clear();
        this->EndModal(wxID_CLOSE);
        return;
    }

    const size_t step = m_diffs_per_kind.size() - m_exit_queue;

    PresetSwitchKindId kind_id = std::next(m_diffs_per_kind.begin(), step)->first;

    m_exit_states[kind_id] = {operation, {}, {}};

    if (operation == PresetDiffOperation::Discard) {
        // ignore this state
    } else {
        if (operation == PresetDiffOperation::Save) {
            std::string new_preset_name = "TEMP new name";
            // ToDo: get new preset name using SavePresetDialog

            SavePresetDialog save_dlg(
                this,
                {{kind_id.kind,
                  {preset_name(m_preset_names, kind_id.kind, kind_id.id.value_or(0))}}},
                m_preset_interactor
            );
            if (kind_id.kind == PresetKind::FdmMaterial) {
                save_dlg.set_reserved_preset_names(
                    PresetKind::FdmMaterial,
                    m_reserved_material_preset_names
                );
            } else if (kind_id.kind == PresetKind::FdmToolPrint) {
                save_dlg.set_reserved_preset_names(
                    PresetKind::FdmToolPrint,
                    m_reserved_tool_preset_names
                );
            }

            if (save_dlg.ShowModal() == wxID_OK)
                new_preset_name = save_dlg.get_name();
            else
                return;
            m_exit_states[kind_id].new_preset_name = new_preset_name;

            if (kind_id.kind == PresetKind::FdmMaterial) {
                m_reserved_material_preset_names.emplace_back(new_preset_name);
            } else if (kind_id.kind == PresetKind::FdmToolPrint) {
                m_reserved_tool_preset_names.emplace_back(new_preset_name);
            }
        }

        // Note:
        // For operation == PresetDiffOperation::Save,
        // we get values of *unselected* parameters with values from the original configuration.
        //
        // For operation == PresetDiffOperation::Transfer,
        // we get values of *selected* parameters with values from the selected configuration.

        const Domain::ConfigPack& config_pack =
            operation == PresetDiffOperation::Save ? m_config_original : m_config_selected;

        std::vector<std::string> options = operation == PresetDiffOperation::Save ?
            m_tree->unselected_options() :
            m_tree->selected_options();

        auto store_exit_state_values = [this, kind_id](
                                           const std::vector<std::string>& options,
                                           const Domain::ConfigItems& items,
                                           const Domain::ConfigOverrides& overrides
                                       )
        {
            for (const std::string& key : options) {
                if (const Domain::ConfigItem* item = items.find(key)) {
                    m_exit_states[kind_id].items.insert({key, item->value()});
                }
                if (overrides.empty())
                    continue;
                if (const auto& override = overrides.get(key)) {
                    m_exit_states[kind_id].overrides.insert({key, override->value()});
                } else {
                    std::optional<Domain::ConfigValue> disabled_override{std::nullopt};
                    m_exit_states[kind_id].overrides.insert({key, disabled_override});
                }
            }
        };

        PresetKind kind = kind_id.kind;
        size_t tool_id  = kind_id.id.value_or(0);

        if (m_printer_technology == Domain::PrinterTechnology::FFF) {
            Domain::ConfigPackFDM config = std::get<Domain::ConfigPackFDM>(config_pack);

            const Domain::ConfigItems& items = //
                kind == PresetKind::FdmPrinter   ? config.printer.items :
                kind == PresetKind::FdmPrint     ? config.print.items :
                kind == PresetKind::FdmToolPrint ? config.tool[tool_id].items :
                                                   config.filament[tool_id].items;

            const Domain::ConfigOverrides& overrides = //
                kind == PresetKind::FdmPrinter   ? config.printer.overrides :
                kind == PresetKind::FdmPrint     ? config.print.overrides :
                kind == PresetKind::FdmToolPrint ? config.tool[tool_id].overrides :
                                                   config.filament[tool_id].overrides;

            store_exit_state_values(options, items, overrides);

        } else {
            Domain::ConfigPackSLA config = std::get<Domain::ConfigPackSLA>(config_pack);

            const Domain::ConfigItems& items = //
                kind == PresetKind::SlaPrinter ? config.sla_printer_settings.items :
                kind == PresetKind::SlaPrint   ? config.sla_print_settings.items :
                                                 config.sla_material_settings.items;

            const Domain::ConfigOverrides& overrides = //
                kind == PresetKind::SlaPrinter ? config.sla_printer_settings.overrides :
                kind == PresetKind::SlaPrint   ? config.sla_print_settings.overrides :
                                                 config.sla_material_settings.overrides;

            store_exit_state_values(options, items, overrides);
        }
    }

    m_exit_queue--;
    if (m_exit_queue == 0) {
        this->EndModal(wxID_CLOSE);
    } else {
        show_current_diffs();
    }
}

} // namespace Slic3r::App::WX
