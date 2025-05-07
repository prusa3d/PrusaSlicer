///|/ Copyright (c) Prusa Research 2020 - 2023 Oleksandra Iushchenko @YuSanka, David Kocík @kocikdav, Enrico Turri @enricoturri1966, Vojtěch Bubník @bubnikv
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "SavePresetDialog.hpp"

#include <cstddef>
#include <vector>
#include <string>
#include <boost/algorithm/string.hpp>

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include "libslic3r/PresetBundle.hpp"

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "format.hpp"
#include "Tab.hpp"

using Slic3r::GUI::format_wxstr;

namespace Slic3r {
namespace GUI {

constexpr auto BORDER_W = 10;

//-----------------------------------------------
//          SavePresetDialog::Item
//-----------------------------------------------

std::string SavePresetDialog::Item::get_init_preset_name(const std::string &suffix)
{
    const PresetBundle*     preset_bundle = dynamic_cast<SavePresetDialog*>(m_parent)->get_preset_bundle();
    if (!preset_bundle)
        preset_bundle = wxGetApp().preset_bundle;
    m_presets = &preset_bundle->get_presets(m_type);

    const Preset& sel_preset = m_presets->get_selected_preset();
    std::string preset_name = sel_preset.is_default ? "Untitled" :
                              sel_preset.is_system ? (boost::format(("%1% - %2%")) % sel_preset.name % suffix).str() :
                              sel_preset.name;

    // if name contains extension
    if (boost::iends_with(preset_name, ".ini")) {
        size_t len = preset_name.length() - 4;
        preset_name.resize(len);
    }

    return preset_name;
}

void SavePresetDialog::Item::init_casei_preset_names()
{
    m_casei_preset_names.clear();

    auto add_names_from_collection = [this](const PresetCollection& presets) {
        for (const Preset& preset : presets)
            if (!preset.is_default)
                m_casei_preset_names.emplace_back(PresetName({ boost::to_lower_copy<std::string>(preset.name), preset.name }));
    };

    if (m_presets) {
        // This item is a part of SavePresetDialog and will check names inside selected PresetCollection
        m_casei_preset_names.reserve(m_presets->size());
        add_names_from_collection(*m_presets);
    }
    else // This item is a part of ConfigWizard and will check names inside all PresetCollections in respect to the m_printer_technology
    if (m_preset_bundle) {
        auto types_list = PresetBundle::types_list(m_printer_technology);

        size_t presets_cnt = 0;
        for (const Preset::Type& type : types_list)
            presets_cnt += m_preset_bundle->get_presets(type).size();
        m_casei_preset_names.reserve(presets_cnt);

        for (const Preset::Type& type : types_list)
            add_names_from_collection(m_preset_bundle->get_presets(type));
    }

    std::sort(m_casei_preset_names.begin(), m_casei_preset_names.end());
}

void SavePresetDialog::Item::init_input_name_ctrl(wxBoxSizer *input_name_sizer, const std::string preset_name)
{
    if (m_use_text_ctrl) {
#ifdef _WIN32
        long style = wxBORDER_SIMPLE;
#else
        long style = 0L;
#endif
        m_text_ctrl = new wxTextCtrl(m_parent, wxID_ANY, from_u8(preset_name), wxDefaultPosition, wxSize(35 * wxGetApp().em_unit(), -1), style);
        wxGetApp().UpdateDarkUI(m_text_ctrl);
        m_text_ctrl->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { update(); });

        input_name_sizer->Add(m_text_ctrl,1, wxEXPAND, BORDER_W);
    }
    else {
        std::vector<std::string> values;
        for (const Preset&preset : *m_presets) {
            if (preset.is_default || preset.is_system || preset.is_external)
                continue;
            values.push_back(preset.name);
        }

        m_combo = new wxComboBox(m_parent, wxID_ANY, from_u8(preset_name), wxDefaultPosition, wxSize(35 * wxGetApp().em_unit(), -1));
        for (const std::string&value : values)
            m_combo->Append(from_u8(value));

        m_combo->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { update(); });
#ifdef __WXOSX__
        // Under OSX wxEVT_TEXT wasn't invoked after change selection in combobox,
        // So process wxEVT_COMBOBOX too
        m_combo->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) { update(); });
#endif //__WXOSX__

        input_name_sizer->Add(m_combo,    1, wxEXPAND, BORDER_W);
    }
}

static std::map<Preset::Type, std::string> TOP_LABELS =
{
    // type                             Save settings    
    { Preset::Type::TYPE_PRINT,         L("Save print settings as")   },
    { Preset::Type::TYPE_SLA_PRINT,     L("Save print settings as")   },
    { Preset::Type::TYPE_FILAMENT,      L("Save filament settings as")},
    { Preset::Type::TYPE_SLA_MATERIAL,  L("Save material settings as")},
    { Preset::Type::TYPE_PRINTER,       L("Save printer settings as") },
};

SavePresetDialog::Item::Item(Preset::Type type, const std::string& suffix, wxBoxSizer* sizer, SavePresetDialog* parent, bool is_for_multiple_save):
    m_type(type),
    m_use_text_ctrl(parent->is_for_rename()),
    m_parent(parent),
    m_valid_bmp(new wxStaticBitmap(m_parent, wxID_ANY, *get_bmp_bundle("tick_mark"))),
    m_valid_label(new wxStaticText(m_parent, wxID_ANY, ""))
{
    m_valid_label->SetFont(wxGetApp().bold_font());

    wxStaticText* label_top = is_for_multiple_save ? new wxStaticText(m_parent, wxID_ANY, _(TOP_LABELS.at(m_type)) + ":") : nullptr;

    wxBoxSizer* input_name_sizer = new wxBoxSizer(wxHORIZONTAL);
    input_name_sizer->Add(m_valid_bmp,    0, wxALIGN_CENTER_VERTICAL | wxRIGHT, BORDER_W);
    init_input_name_ctrl(input_name_sizer, get_init_preset_name(suffix));

    init_casei_preset_names();

    if (label_top)
        sizer->Add(label_top,   0, wxEXPAND | wxTOP| wxBOTTOM, BORDER_W);
    sizer->Add(input_name_sizer,0, wxEXPAND | (label_top ? 0 : wxTOP) | wxBOTTOM, BORDER_W);
    sizer->Add(m_valid_label,   0, wxEXPAND | wxLEFT,   3*BORDER_W);

    if (m_type == Preset::TYPE_PRINTER)
        parent->add_info_for_edit_ph_printer(sizer);

    update();
}

SavePresetDialog::Item::Item(wxWindow* parent, wxBoxSizer* sizer, const std::string& def_name, PresetBundle* preset_bundle, PrinterTechnology pt /*= ptFFF*/):
    m_preset_name(def_name),
    m_preset_bundle(preset_bundle),
    m_printer_technology(pt),
    m_parent(parent),
    m_valid_bmp(new wxStaticBitmap(m_parent, wxID_ANY, *get_bmp_bundle("tick_mark"))),
    m_valid_label(new wxStaticText(m_parent, wxID_ANY, ""))
{
    m_valid_label->SetFont(wxGetApp().bold_font());

    wxBoxSizer* input_name_sizer = new wxBoxSizer(wxHORIZONTAL);
    input_name_sizer->Add(m_valid_bmp,    0, wxALIGN_CENTER_VERTICAL | wxRIGHT, BORDER_W);
    init_input_name_ctrl(input_name_sizer, m_preset_name);

    init_casei_preset_names();

    sizer->Add(input_name_sizer,0, wxEXPAND | wxBOTTOM, BORDER_W);
    sizer->Add(m_valid_label,   0, wxEXPAND | wxLEFT,   3*BORDER_W);

    update();
}

static std::string get_conflict_name(const std::vector<SavePresetDialog::Item::PresetName>& casei_names, const std::string& preset_name)
{
    if (!casei_names.empty()) {
        const std::string lower_name = boost::to_lower_copy<std::string>(preset_name);
        auto it = Slic3r::lower_bound_by_predicate(casei_names.begin(), casei_names.end(), 
                                                   [lower_name](const auto& l) { return l.casei_name < lower_name;  });
        if (it != casei_names.end() && it->casei_name == lower_name)
            return it->name;
    }
    return std::string();
}

std::string SavePresetDialog::Item::preset_name() const
{
    if (m_use_text_ctrl)
        return m_preset_name;

    const std::string existed_preset_name = get_conflict_name(m_casei_preset_names, m_preset_name);
    if (existed_preset_name.empty())
        return m_preset_name;

    return existed_preset_name;
}

const Preset* SavePresetDialog::Item::get_existing_preset() const 
{
    std::string existed_preset_name = get_conflict_name(m_casei_preset_names, m_preset_name);
    if (existed_preset_name.empty()) {
        // Preset has not been not found in the sorted list of non-default presets. Try the defaults.
        return nullptr;
    }

    if (m_presets)
        return m_presets->find_preset(existed_preset_name, false);

    if (m_preset_bundle) {
        for (const Preset::Type& type : PresetBundle::types_list(m_printer_technology)) {
            const PresetCollection& presets = m_preset_bundle->get_presets(type);
            if (const Preset* preset = presets.find_preset(existed_preset_name, false))
                return preset;
        }
    }

    return nullptr;
}

void SavePresetDialog::Item::update()
{
    m_preset_name = into_u8(m_use_text_ctrl ? m_text_ctrl->GetValue() : m_combo->GetValue());

    m_valid_type = ValidationType::Valid;
    wxString info_line;

    const char* unusable_symbols = "<>[]:/\\|?*\"";

    const std::string unusable_suffix = PresetCollection::get_suffix_modified();//"(modified)";
    for (size_t i = 0; i < std::strlen(unusable_symbols); i++) {
        if (m_preset_name.find_first_of(unusable_symbols[i]) != std::string::npos) {
            info_line = _L("The following characters are not allowed in the name") + ": " + unusable_symbols;
            m_valid_type = ValidationType::NoValid;
            break;
        }
    }

    if (m_valid_type == ValidationType::Valid && m_preset_name.find(unusable_suffix) != std::string::npos) {
        info_line = _L("The following suffix is not allowed in the name") + ":\n\t" +
                    from_u8(unusable_suffix);
        m_valid_type = ValidationType::NoValid;
    }

    if (m_valid_type == ValidationType::Valid && m_preset_name == "- default -") {
        info_line = _L("This name is reserved, use another.");
        m_valid_type = ValidationType::NoValid;
    }

    const Preset* existing = get_existing_preset();
    if (m_valid_type == ValidationType::Valid && existing && (existing->is_default || existing->is_system)) {
        info_line = m_use_text_ctrl ? _L("This name is used for a system profile name, use another.") :
                             _L("Cannot overwrite a system profile.");
        info_line += "\n" + GUI::format_wxstr("(%1%)", existing->name);
        m_valid_type = ValidationType::NoValid;
    }

    if (m_valid_type == ValidationType::Valid && existing && (existing->is_external)) {
        info_line = m_use_text_ctrl ? _L("This name is used for an external profile name, use another.") :
                             _L("Cannot overwrite an external profile.");
        m_valid_type = ValidationType::NoValid;
    }

    SavePresetDialog* dlg = dynamic_cast<SavePresetDialog*>(m_parent);
    if (m_valid_type == ValidationType::Valid && existing)
    {
        if (m_presets && m_preset_name == m_presets->get_selected_preset_name()) {
            if ((!m_use_text_ctrl && m_presets->get_edited_preset().is_dirty) ||
                (dlg && dlg->get_preset_bundle())) // means that we save modifications from the DiffDialog
                info_line = _L("Save preset modifications to existing user profile");
            m_valid_type = ValidationType::Valid;
        }
        else {
            if (existing->is_compatible)
                info_line = from_u8((boost::format(_u8L("Preset with name \"%1%\" already exists.")) % existing->name).str());
            else
                info_line = from_u8((boost::format(_u8L("Preset with name \"%1%\" already exists and is incompatible with selected printer.")) % existing->name).str());
            info_line += "\n" + (m_use_text_ctrl ? _L("Note: This preset will be replaced after renaming") :
                                _L("Note: Preset modifications will be saved exactly into this preset"));
            m_valid_type = ValidationType::Warning;
        }
    }

    if (m_valid_type == ValidationType::Valid && m_preset_name.empty()) {
        info_line = _L("The name cannot be empty.");
        m_valid_type = ValidationType::NoValid;
    }

#ifdef __WXMSW__
    const int max_path_length = MAX_PATH;
#else
    const int max_path_length = 255;
#endif

    if (m_valid_type == ValidationType::Valid && m_presets && m_presets->path_from_name(m_preset_name).length() >= max_path_length) {
        info_line = _L("The name is too long.");
        m_valid_type = ValidationType::NoValid;
    }

    if (m_valid_type == ValidationType::Valid && m_preset_name.find_first_of(' ') == 0) {
        info_line = _L("The name cannot start with space character.");
        m_valid_type = ValidationType::NoValid;
    }

    if (m_valid_type == ValidationType::Valid && m_preset_name.find_last_of(' ') == m_preset_name.length()-1) {
        info_line = _L("The name cannot end with space character.");
        m_valid_type = ValidationType::NoValid;
    }

    if (m_valid_type == ValidationType::Valid && m_presets && m_presets->get_preset_name_by_alias(m_preset_name) != m_preset_name) {
        info_line = _L("The name cannot be the same as a preset alias name.");
        m_valid_type = ValidationType::NoValid;
    }

    if ((dlg && !dlg->get_info_line_extention().IsEmpty()) && m_valid_type != ValidationType::NoValid)
        info_line += "\n\n" + dlg->get_info_line_extention();

    m_valid_label->SetLabel(info_line);
    m_valid_label->Show(!info_line.IsEmpty());

    update_valid_bmp();

    if (dlg && m_type == Preset::TYPE_PRINTER)
        dlg->update_info_for_edit_ph_printer(m_preset_name);

    m_parent->Layout();
}

void SavePresetDialog::Item::update_valid_bmp()
{
    std::string bmp_name =  m_valid_type == ValidationType::Warning ? "exclamation_manifold" :
                            m_valid_type == ValidationType::NoValid ? "exclamation"          : "tick_mark" ;
    m_valid_bmp->SetBitmap(*get_bmp_bundle(bmp_name));
}

void SavePresetDialog::Item::accept()
{
}

void SavePresetDialog::Item::Enable(bool enable /*= true*/)
{
    m_valid_label->Enable(enable);
    m_valid_bmp->Enable(enable);
    m_use_text_ctrl ? m_text_ctrl->Enable(enable) : m_combo->Enable(enable);
}


//-----------------------------------------------
//          SavePresetDialog
//-----------------------------------------------

SavePresetDialog::SavePresetDialog(wxWindow* parent, std::vector<Preset::Type> types, std::string suffix, bool template_filament/* =false*/, PresetBundle* preset_bundle/* = nullptr*/)
    : DPIDialog(parent, wxID_ANY, types.size() == 1 ? _L("Save preset") : _L("Save presets"), 
                wxDefaultPosition, wxSize(45 * wxGetApp().em_unit(), 5 * wxGetApp().em_unit()), wxDEFAULT_DIALOG_STYLE | wxICON_WARNING),
    m_preset_bundle(preset_bundle)
{
    build(types, suffix, template_filament);
}

SavePresetDialog::SavePresetDialog(wxWindow* parent, Preset::Type type, const wxString& info_line_extention)
    : DPIDialog(parent, wxID_ANY, _L("Rename preset"), wxDefaultPosition, wxSize(45 * wxGetApp().em_unit(), 5 * wxGetApp().em_unit()), wxDEFAULT_DIALOG_STYLE | wxICON_WARNING),
    m_use_for_rename(true),
    m_info_line_extention(info_line_extention)
{
    build(std::vector<Preset::Type>{type});
}
SavePresetDialog::~SavePresetDialog()
{
    for (auto  item : m_items) {
        delete item;
    }
}

void SavePresetDialog::build(std::vector<Preset::Type> types, std::string suffix, bool template_filament)
{
    this->SetFont(wxGetApp().normal_font());

#if defined(__WXMSW__)
    // ys_FIXME! temporary workaround for correct font scaling
    // Because of from wxWidgets 3.1.3 auto rescaling is implemented for the Fonts,
    // From the very beginning set dialog font to the wxSYS_DEFAULT_GUI_FONT
//    this->SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));
#else
    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
#endif // __WXMSW__

    if (suffix.empty())
        // TRN Suffix for the preset name. Have to be a noun.
        suffix = _CTX_utf8(L_CONTEXT("Copy", "PresetName"), "PresetName");

    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

    m_presets_sizer = new wxBoxSizer(wxVERTICAL);

    const bool is_for_multiple_save = types.size() > 1;
    for (const Preset::Type& type : types)
        AddItem(type, suffix, is_for_multiple_save);

    // Add dialog's buttons
    wxStdDialogButtonSizer* btns = this->CreateStdDialogButtonSizer(wxOK | wxCANCEL);
    wxButton* btnOK = static_cast<wxButton*>(this->FindWindowById(wxID_OK, this));
    btnOK->Bind(wxEVT_BUTTON,    [this](wxCommandEvent&)        { accept(); });
    btnOK->Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& evt)   { evt.Enable(enable_ok_btn()); });

    topSizer->Add(m_presets_sizer,  0, wxEXPAND | wxALL, BORDER_W);
    
    // Add checkbox for Template filament saving
    if (template_filament && types.size() == 1 && *types.begin() == Preset::Type::TYPE_FILAMENT) {
        m_template_filament_checkbox = new wxCheckBox(this, wxID_ANY, _L("Save as profile derived from current printer only."));
        wxBoxSizer* check_sizer = new wxBoxSizer(wxVERTICAL);
        check_sizer->Add(m_template_filament_checkbox);
        topSizer->Add(check_sizer, 0, wxEXPAND | wxALL, BORDER_W);
    }

    topSizer->Add(btns,             0, wxEXPAND | wxALL, BORDER_W);

    SetSizer(topSizer);
    topSizer->SetSizeHints(this);

    this->CenterOnScreen();

#ifdef _WIN32
    wxGetApp().UpdateDlgDarkUI(this);
#endif
}

void SavePresetDialog::AddItem(Preset::Type type, const std::string& suffix, bool is_for_multiple_save)
{
    m_items.emplace_back(new Item{type, suffix, m_presets_sizer, this, is_for_multiple_save});
}

std::string SavePresetDialog::get_name()
{
    return m_items.front()->preset_name();
}

std::string SavePresetDialog::get_name(Preset::Type type)
{
    for (const Item* item : m_items)
        if (item->type() == type)
            return item->preset_name();
    return "";
}

bool SavePresetDialog::get_template_filament_checkbox()
{
    if (m_template_filament_checkbox)
    {
        return m_template_filament_checkbox->GetValue();
    }
    return false;
}

bool SavePresetDialog::enable_ok_btn() const
{
    for (const Item* item : m_items)
        if (!item->is_valid())
            return false;

    return true;
}

void SavePresetDialog::add_info_for_edit_ph_printer(wxBoxSizer* sizer)
{
    PhysicalPrinterCollection& printers = wxGetApp().preset_bundle->physical_printers;
    m_ph_printer_name = printers.get_selected_printer_name();
    m_old_preset_name = printers.get_selected_printer_preset_name();

    wxString msg_text = from_u8((boost::format(_u8L("You have selected physical printer \"%1%\" \n"
                                                    "with related printer preset \"%2%\"")) %
                                                    m_ph_printer_name % m_old_preset_name).str());
    m_label = new wxStaticText(this, wxID_ANY, msg_text);
    m_label->SetFont(wxGetApp().bold_font());

    m_action = ChangePreset;
    m_radio_sizer = new wxBoxSizer(wxHORIZONTAL);

    wxStaticBox* action_stb = new wxStaticBox(this, wxID_ANY, "");
    if (!wxOSX) action_stb->SetBackgroundStyle(wxBG_STYLE_PAINT);
    action_stb->SetFont(wxGetApp().bold_font());

    wxStaticBoxSizer* stb_sizer = new wxStaticBoxSizer(action_stb, wxVERTICAL);
    for (int id = 0; id < 3; id++) {
        wxRadioButton* btn = new wxRadioButton(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, id == 0 ? wxRB_GROUP : 0);
        btn->SetValue(id == int(ChangePreset));
        btn->Bind(wxEVT_RADIOBUTTON, [this, id](wxCommandEvent&) { m_action = (ActionType)id; });
        stb_sizer->Add(btn, 0, wxEXPAND | wxTOP, 5);
    }
    m_radio_sizer->Add(stb_sizer, 1, wxEXPAND | wxTOP, 2*BORDER_W);

    sizer->Add(m_label,         0, wxEXPAND | wxLEFT | wxTOP,   3*BORDER_W);
    sizer->Add(m_radio_sizer,   1, wxEXPAND | wxLEFT,           3*BORDER_W);
}

void SavePresetDialog::update_info_for_edit_ph_printer(const std::string& preset_name)
{
    bool show = wxGetApp().preset_bundle->physical_printers.has_selection() && m_old_preset_name != preset_name;

    m_label->Show(show);
    m_radio_sizer->ShowItems(show);
    if (!show) {
        this->SetMinSize(wxSize(100,50));
        return;
    }

    if (wxSizerItem* sizer_item = m_radio_sizer->GetItem(size_t(0))) {
        if (wxStaticBoxSizer* stb_sizer = static_cast<wxStaticBoxSizer*>(sizer_item->GetSizer())) {
            wxString msg_text = format_wxstr(_L("What would you like to do with \"%1%\" preset after saving?"), preset_name);
            stb_sizer->GetStaticBox()->SetLabel(msg_text);

            wxString choices[] = { format_wxstr(_L("Change \"%1%\" to \"%2%\" for this physical printer \"%3%\""), m_old_preset_name, preset_name, m_ph_printer_name),
                                   format_wxstr(_L("Add \"%1%\" as a next preset for the the physical printer \"%2%\""), preset_name, m_ph_printer_name),
                                   format_wxstr(_L("Just switch to \"%1%\" preset"), preset_name) };

            size_t n = 0;
            for (const wxString& label : choices)
                stb_sizer->GetItem(n++)->GetWindow()->SetLabel(label);
        }
        Refresh();
    }
}

bool SavePresetDialog::Layout()
{
    const bool ret = DPIDialog::Layout();
    this->Fit();
    return ret;
}

void SavePresetDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    const int& em = em_unit();

    msw_buttons_rescale(this, em, { wxID_OK, wxID_CANCEL });

    for (Item* item : m_items)
        item->update_valid_bmp();

    //const wxSize& size = wxSize(45 * em, 35 * em);
    SetMinSize(/*size*/wxSize(100, 50));

    Fit();
    Refresh();
}

void SavePresetDialog::update_physical_printers(const std::string& preset_name)
{
    if (m_action == UndefAction)
        return;

    PhysicalPrinterCollection& physical_printers = wxGetApp().preset_bundle->physical_printers;
    if (!physical_printers.has_selection())
        return;

    std::string printer_preset_name = physical_printers.get_selected_printer_preset_name();

    if (m_action == Switch)
        // unselect physical printer, if it was selected
        physical_printers.unselect_printer();
    else
    {
        PhysicalPrinter printer = physical_printers.get_selected_printer();

        if (m_action == ChangePreset)
            printer.delete_preset(printer_preset_name);

        if (printer.add_preset(preset_name))
            physical_printers.save_printer(printer);

        physical_printers.select_printer(printer.get_full_name(preset_name));
    }    
}

void SavePresetDialog::accept()
{
    for (Item* item : m_items) {
        item->accept();
        if (item->type() == Preset::TYPE_PRINTER)
            update_physical_printers(item->preset_name());
    }

    EndModal(wxID_OK);
}

}}    // namespace Slic3r::GUI
