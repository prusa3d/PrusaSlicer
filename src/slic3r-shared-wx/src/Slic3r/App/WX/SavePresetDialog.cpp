#include "Slic3r/Log.hpp"
#include "Slic3r/App/WX/SavePresetDialog.hpp"

#include "Slic3r/App/WX/StringConversions.hpp"
#include "Slic3r/App/WX/BitmapGetters.hpp"
#include "Slic3r/App/WX/WidgetsConfig.hpp"
#include "Slic3r/App/WX/I18N.hpp"

#include <vector>
#include <memory>
#include <ranges>
#include <fmt/format.h>
#include <fmt/ranges.h>

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/statbmp.h>
#include <wx/textctrl.h>
#include <wx/checkbox.h>
#include <wx/combobox.h>
#include <wx/statline.h>
#include <wx/scrolwin.h>

namespace Slic3r::App::WX {

constexpr auto BORDER_W = 10;

//-----------------------------------------------
// SavePresetDialog::Item
//-----------------------------------------------

void SavePresetDialog::Item::init_input_name_ctrl(
    wxBoxSizer* input_name_sizer,
    const std::string& preset_name
)
{
    if (m_use_text_ctrl) {
#ifdef _WIN32
        long style = wxBORDER_SIMPLE;
#else
        long style = 0L;
#endif
        m_text_ctrl = new wxTextCtrl(
            m_parent,
            wxID_ANY,
            from_u8(preset_name),
            wxDefaultPosition,
            wxSize(35 * w_config()->em_unit(), -1),
            style
        );
        m_text_ctrl->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { update_state(); });

        input_name_sizer->Add(m_text_ctrl, 1, wxEXPAND, BORDER_W);
    } else {
        m_combo = new wxComboBox(
            m_parent,
            wxID_ANY,
            wxEmptyString,
            wxDefaultPosition,
            wxSize(35 * w_config()->em_unit(), -1)
        );
        for (const auto& preset : as_const(m_validator.preset_names())) {
            if (preset.origin == Domain::Preset::PresetOrigin::User) {
                m_combo->Append(from_u8(preset.name));
            }
        }
        m_combo->SetValue(from_u8(preset_name));

        m_combo->Bind(
            wxEVT_TEXT,
            [this](wxCommandEvent&)
            {
                update_state();
                if (m_dialog) {
                    m_dialog->check_reserved_preset_names(this->kind());
                }
            }
        );

        input_name_sizer->Add(m_combo, 1, wxEXPAND, BORDER_W);
    }
}

static std::string top_label(Domain::Preset::PresetKind kind, size_t slot_index)
{
    switch (kind) {
    case Domain::Preset::PresetKind::FdmPrinter:
    case Domain::Preset::PresetKind::SlaPrinter:
        return Biz::_u8L("Save printer settings as");
    case Domain::Preset::PresetKind::FdmPrint:
    case Domain::Preset::PresetKind::SlaPrint:
        return Biz::_u8L("Save print settings as");
    case Domain::Preset::PresetKind::FdmToolPrint:
        return fmt::format(
            // TRN {} is an index of tool print
            fmt::runtime(Biz::_u8L("Save settings for tool print {} as")),
            slot_index + 1
        );
    case Domain::Preset::PresetKind::SlaToolPrint:
        return Biz::_u8L("Save tool settings as");
    case Domain::Preset::PresetKind::FdmMaterial:
        return Biz::_u8L("Save filament settings as");
    case Domain::Preset::PresetKind::SlaMaterial:
        return Biz::_u8L("Save material settings as");
    default:
        PANIC("SavePresetDialog::top_label(): Unknown preset kind");
        return std::string();
    };
}

SavePresetDialog::Item::Item(
    PresetKind kind,
    size_t slot_index,
    const std::string& name,
    const std::string& suffix,
    wxBoxSizer* sizer,
    SavePresetDialog* parent,
    bool is_for_multiple_save
) :
    m_kind(kind),
    m_slot_index(slot_index),
    m_validator(parent->preset_interactor(), kind, name, parent->is_for_rename()),
    m_use_text_ctrl(parent->is_for_rename()),
    m_dialog(parent),
    m_parent(parent->items_parent()),
    m_valid_bmp(new wxStaticBitmap(m_parent, wxID_ANY, *get_bmp_bundle("tick_mark"))),
    m_valid_label(new wxStaticText(m_parent, wxID_ANY, wxEmptyString))
{
    m_valid_label->SetFont(w_config()->bold_font());

    wxCheckBox* preset_selector = is_for_multiple_save ?
        new wxCheckBox(m_parent, wxID_ANY, from_u8(top_label(m_kind, slot_index)) + from_u8(":")) :
        nullptr;

    wxBoxSizer* input_name_sizer = new wxBoxSizer(wxHORIZONTAL);
    input_name_sizer->Add(m_valid_bmp, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, BORDER_W);

    std::string init_name;
    for (const Domain::Preset::PresetName& preset_name : as_const(m_validator.preset_names())) {
        if (preset_name.name == name) {
            init_name = preset_name.origin == Domain::Preset::PresetOrigin::System ?
                fmt::format("{} - {}", name, suffix) :
                name;
            break;
        }
    }
#ifndef NDEBUG
    if (init_name.empty()) {
        SPDLOG_ERROR(
            "Cannot find '{}' in {}",
            name,
            fmt::join(
                m_validator.preset_names()
                    | std::views::transform([](const auto& pn)
                                            { return fmt::format("- '{}'", pn.name); }),
                "\n"
            )
        );
    }
#endif

    init_input_name_ctrl(input_name_sizer, init_name.empty() ? name : init_name);

    if (preset_selector)
        sizer->Add(preset_selector, 0, wxEXPAND | wxTOP | wxBOTTOM, BORDER_W);
    sizer->Add(input_name_sizer, 0, wxEXPAND | (preset_selector ? 0 : wxTOP) | wxBOTTOM, BORDER_W);
    sizer->Add(m_valid_label, 0, wxEXPAND | wxLEFT, 3 * BORDER_W);
    if (preset_selector)
        sizer->Add(new wxStaticLine(m_parent), 0, wxEXPAND | wxTOP | wxBOTTOM, BORDER_W);

    if (preset_selector) {
        preset_selector->SetValue(true);
        preset_selector->Bind(
            wxEVT_CHECKBOX,
            [&](wxCommandEvent& event)
            {
                m_selected = event.IsChecked();
                if (m_dialog) {
                    m_dialog->check_reserved_preset_names(this->kind());
                }
                if (m_combo)
                    m_combo->Enable(m_selected);
            }
        );
    }

    update_state();
}

SavePresetDialog::Item::Item(
    wxWindow* parent,
    wxBoxSizer* sizer,
    const std::string& def_name,
    Domain::PrinterTechnology pt /*= Domain::PrinterTechnology::FFF*/
) :
    m_preset_name(def_name),
    m_parent(parent),
    m_valid_bmp(new wxStaticBitmap(m_parent, wxID_ANY, *get_bmp_bundle("tick_mark"))),
    m_valid_label(new wxStaticText(m_parent, wxID_ANY, wxEmptyString))
{
    m_valid_label->SetFont(w_config()->bold_font());

    wxBoxSizer* input_name_sizer = new wxBoxSizer(wxHORIZONTAL);
    input_name_sizer->Add(m_valid_bmp, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, BORDER_W);
    init_input_name_ctrl(input_name_sizer, m_preset_name);

    sizer->Add(input_name_sizer, 0, wxEXPAND | wxBOTTOM, BORDER_W);
    sizer->Add(m_valid_label, 0, wxEXPAND | wxLEFT, 3 * BORDER_W);

    update_state();
}

std::string SavePresetDialog::Item::preset_name() const
{
    if (m_use_text_ctrl)
        return m_preset_name;

    const std::string existed_preset_name = m_validator.get_conflict_name(m_preset_name);
    if (existed_preset_name.empty())
        return m_preset_name;

    return existed_preset_name;
}

void SavePresetDialog::Item::set_reserved_preset_names(
    const std::vector<std::string>& reserved_preset_names
)
{
    m_validator.set_reserved_preset_names(reserved_preset_names);
    update_state();
}

void SavePresetDialog::Item::update_state()
{
    m_preset_name = into_u8(m_use_text_ctrl ? m_text_ctrl->GetValue() : m_combo->GetValue());

    const auto validation_res = m_validator.validate(m_preset_name);

    m_valid_type       = m_selected ? validation_res.type : ValidationType::Undef;
    wxString info_line = m_selected ? from_u8(validation_res.message) : wxString();

    // Parent is expected to be SavePresetDialog
    SavePresetDialog* dlg = dynamic_cast<SavePresetDialog*>(m_parent);
    if ((dlg && !dlg->get_info_line_extension().IsEmpty())
        && m_valid_type != ValidationType::Invalid)
        info_line += from_u8("\n\n") + dlg->get_info_line_extension();

    m_valid_label->SetLabel(info_line);
    m_valid_label->Show(!info_line.IsEmpty());

    update_valid_bmp();

    if (m_dialog)
        m_dialog->refit();
    else
        m_parent->Layout();
}

void SavePresetDialog::Item::update_valid_bmp()
{
    std::string bmp_name = m_valid_type == ValidationType::Warning ? "exclamation_manifold" :
        m_valid_type == ValidationType::Undef                      ? "exclamation_triangle" :
        m_valid_type == ValidationType::Invalid                    ? "exclamation" :
                                                                     "tick_mark";
    m_valid_bmp->SetBitmap(*get_bmp_bundle(bmp_name));
}

void SavePresetDialog::Item::Enable(bool enable /*= true*/)
{
    m_valid_label->Enable(enable);
    m_valid_bmp->Enable(enable);
    m_use_text_ctrl ? m_text_ctrl->Enable(enable) : m_combo->Enable(enable);
}

//-----------------------------------------------
// SavePresetDialog
//-----------------------------------------------

SavePresetDialog::SavePresetDialog(
    wxWindow* parent,
    NamesPerKindMap names_per_kinds,
    const Biz::Preset::IPresetNameProvider& preset_interactor,
    std::string suffix,
    bool template_filament /* =false*/
) :
    wxDialog(
        parent,
        wxID_ANY,
        names_per_kinds.size() == 1 ? _L("Save preset") : _L("Save presets"),
        wxDefaultPosition,
        wxSize(45 * w_config()->em_unit(), 5 * w_config()->em_unit()),
        wxDEFAULT_DIALOG_STYLE
            | (names_per_kinds.size() == 1 ? 0 : wxRESIZE_BORDER)
            | wxICON_WARNING
    ),
    m_preset_interactor(preset_interactor)
{
    build(names_per_kinds, suffix, template_filament);
}

SavePresetDialog::SavePresetDialog(
    wxWindow* parent,
    PresetKind kind,
    const std::string& name,
    const Biz::Preset::IPresetNameProvider& preset_interactor,
    const std::string& info_line_extension
) :
    wxDialog(
        parent,
        wxID_ANY,
        _L("Rename preset"),
        wxDefaultPosition,
        wxSize(45 * w_config()->em_unit(), 5 * w_config()->em_unit()),
        wxDEFAULT_DIALOG_STYLE | wxICON_WARNING
    ),
    m_use_for_rename(true),
    m_info_line_extension(from_u8(info_line_extension)),
    m_preset_interactor(preset_interactor)
{
    build({{kind, {name}}});
}

SavePresetDialog::~SavePresetDialog() = default;

void SavePresetDialog::build(
    const NamesPerKindMap& names_per_kinds,
    std::string suffix,
    bool template_filament
)
{
    this->SetFont(w_config()->normal_font());

    if (suffix.empty())
        // TRN Suffix for the preset name. Have to be a noun.
        suffix = Biz::_ctx_u8L("Copy", "PresetName");

    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

    m_presets_sizer = new wxBoxSizer(wxVERTICAL);

    const bool is_for_multiple_save = names_per_kinds.size() > 1;
    if (is_for_multiple_save) {
        // When several presets are saved at once, the list of the Items can be quite long,
        // so place it into the scrollable area to keep the dialog inside the screen.
        m_scrolled_panel = new wxScrolledWindow(
            this,
            wxID_ANY,
            wxDefaultPosition,
            wxDefaultSize,
            wxVSCROLL | wxBORDER_NONE
        );
        m_scrolled_panel->SetScrollRate(0, w_config()->em_unit());
        m_scrolled_panel->SetSizer(m_presets_sizer);
    }
    for (const auto& [kind, names] : names_per_kinds) {
        for (size_t slot_index{}; slot_index < names.size(); slot_index++) {
            AddItem(kind, slot_index, names.at(slot_index), suffix, is_for_multiple_save);
        }
    }

    if (m_scrolled_panel) {
        // Limit the height of the scrolled area, but keep the full width,
        // so no horizontal scrolling is needed.
        const int max_height    = 50 * w_config()->em_unit();
        const wxSize content_sz = m_presets_sizer->CalcMin();
        m_scrolled_panel->SetMinSize(wxSize(wxDefaultCoord, std::min(content_sz.y, max_height)));
        m_scrolled_panel->FitInside();
    }

    // Add dialog's buttons
    wxStdDialogButtonSizer* btns = this->CreateStdDialogButtonSizer(wxOK | wxCANCEL);
    wxButton* btnOK              = static_cast<wxButton*>(this->FindWindowById(wxID_OK, this));

    btnOK->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { accept(); });
    btnOK->Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& evt) { evt.Enable(enable_ok_btn()); });

    if (m_scrolled_panel)
        topSizer->Add(m_scrolled_panel, 1, wxEXPAND | wxALL, BORDER_W);
    else
        topSizer->Add(m_presets_sizer, 0, wxEXPAND | wxALL, BORDER_W);

    // Add checkbox for Template filament saving
    if (template_filament
        && names_per_kinds.size() == 1
        && names_per_kinds.begin()->first == PresetKind::FdmMaterial)
    {
        m_template_filament_checkbox = new wxCheckBox(
            this,
            wxID_ANY,
            _L("Save as profile derived from current printer only.")
        );
        wxBoxSizer* check_sizer = new wxBoxSizer(wxVERTICAL);
        check_sizer->Add(m_template_filament_checkbox);
        topSizer->Add(check_sizer, 0, wxEXPAND | wxALL, BORDER_W);
    }

    topSizer->Add(btns, 0, wxEXPAND | wxALL, BORDER_W);

    SetSizer(topSizer);
    topSizer->SetSizeHints(this);

    this->CenterOnScreen();

    w_config()->UpdateDlgDarkUI(this);
}

void SavePresetDialog::AddItem(
    PresetKind kind,
    size_t slot_index,
    const std::string& name,
    const std::string& suffix,
    bool is_for_multiple_save
)
{
    m_items.emplace_back(
        std::make_unique<
            Item>(kind, slot_index, name, suffix, m_presets_sizer, this, is_for_multiple_save)
    );
    check_reserved_preset_names(kind);
}

const Biz::Preset::IPresetNameProvider& SavePresetDialog::preset_interactor() const
{
    return m_preset_interactor;
}

wxWindow* SavePresetDialog::items_parent()
{
    return m_scrolled_panel ? static_cast<wxWindow*>(m_scrolled_panel) :
                              static_cast<wxWindow*>(this);
}

std::string SavePresetDialog::get_name() const
{
    return m_items.front()->preset_name();
}

std::string SavePresetDialog::get_name(PresetKind kind) const
{
    // All preset kinds passed to this dialog must have a corresponding Item.
    // Missing kind indicates a programming error.
    for (const auto& item : m_items)
        if (item->kind() == kind)
            return item->preset_name();
    PANIC("SavePresetDialog::get_name(): preset kind not present in dialog");
}

SavePresetDialog::NamesPerKindMap SavePresetDialog::get_names_per_kind() const
{
    NamesPerKindMap ret{};
    for (const auto& item : m_items) {
        std::string preset_name = item->is_selected() ? item->preset_name() : std::string{};
        if (ret.contains(item->kind())) {
            ret.at(item->kind()).emplace_back(preset_name);
        } else {
            ret[item->kind()] = {preset_name};
        }
    }
    return ret;
}

bool SavePresetDialog::get_template_filament_checkbox() const
{
    if (m_template_filament_checkbox) {
        return m_template_filament_checkbox->GetValue();
    }
    return false;
}

const wxString& App::WX::SavePresetDialog::get_info_line_extension() const
{
    return m_info_line_extension;
}

void SavePresetDialog::set_reserved_preset_names(
    PresetKind kind,
    const std::vector<std::string>& reserved_preset_names
)
{
    for (const auto& item : m_items) {
        if (item->kind() == kind) {
            item->set_reserved_preset_names(reserved_preset_names);
            return;
        }
    }
}

void SavePresetDialog::check_reserved_preset_names(PresetKind kind)
{
    if (m_items.size() == 1)
        return;

    auto items_with_kind =
        m_items | std::views::filter([kind](const auto& item) { return item->kind() == kind; });

    for (const auto& item_checked : items_with_kind) {
        std::vector<std::string> reserved_preset_names{};
        if (size_t slot_index{item_checked->slot_index()}; slot_index > 0) {
            for (const auto& item : items_with_kind) {
                if (item->is_selected() && item->slot_index() < slot_index) {
                    reserved_preset_names.emplace_back(item->preset_name());
                }
            }
        }
        item_checked->set_reserved_preset_names(reserved_preset_names);
    }
}

bool SavePresetDialog::enable_ok_btn() const
{
    bool is_any_selected{false};
    for (const auto& item : m_items) {
        if (item->is_selected() && !item->is_valid())
            return false;
        is_any_selected |= item->is_selected();
    }

    return is_any_selected;
}

void SavePresetDialog::refit()
{
    if (!GetSizer()) {
        // Called from constructor
        return;
    }
    if (m_scrolled_panel) {
        m_scrolled_panel->Layout();
    } else {
        SetMinSize(wxDefaultSize);
        InvalidateBestSize();
        Fit();
    }
}

bool SavePresetDialog::is_for_rename() const
{
    return m_use_for_rename;
}

void SavePresetDialog::accept()
{
    EndModal(wxID_OK);
}

} // namespace Slic3r::App::WX
