///|/ Copyright (c) 2026
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "FilamentSyncDialog.hpp"

#include <algorithm>
#include <optional>

#include <boost/log/trivial.hpp>

#include <wx/busyinfo.h>
#include <wx/datetime.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/gbsizer.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>
#include <wx/utils.h>

#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Print.hpp"
#include "slic3r/Utils/PrinterTools.hpp"

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "NotificationManager.hpp"
#include "PhysicalPrinterDialog.hpp"
#include "Plater.hpp"
#include "Tab.hpp"
#include "format.hpp"
#include "wxExtensions.hpp"

namespace Slic3r {
namespace GUI {

using namespace filament_sync;

namespace {

// What the last sync in this session found, for warning about prints that use those extruders.
// Not kept across sessions: filament can change while PrusaSlicer is closed.
struct LastSync
{
    std::string          printer;
    // The device read: an edited physical printer keeps its name but can point elsewhere
    std::string          host;
    // What the printer reported; which extruders that leaves out depends on the printer profile
    // in use, which can change while the physical printer stays
    PrinterTools         tools;
    std::string          time;
};
std::optional<LastSync> s_last_sync;

wxString dialog_title() { return _L("Sync Filaments from Printer"); }

// The preview's "Hide empty extruders in the sidebar", on unless turned off
const char* HIDE_EMPTY_KEY = "filament_sync_hide_empty";
bool hide_empty_extruders() { return wxGetApp().app_config->get(HIDE_EMPTY_KEY) != "0"; }

// The last sync applies to the physical printer it read
// The extruders the last sync leaves without filament, for the edited printer profile
UnavailableExtruders unavailable_now()
{
    const auto* diameters = wxGetApp().preset_bundle->printers.get_edited_preset().config.option<ConfigOptionFloats>("nozzle_diameter");
    return unavailable_extruders(s_last_sync->tools, diameters ? diameters->values.size() : 0);
}

bool last_sync_applies()
{
    PresetBundle* bundle = wxGetApp().preset_bundle;
    return s_last_sync && bundle && bundle->printers.get_edited_preset().printer_technology() == ptFFF &&
           bundle->physical_printers.has_selection() && bundle->physical_printers.get_selected_printer().name == s_last_sync->printer &&
           can_sync_filaments_from_printer() &&
           bundle->physical_printers.get_selected_printer_config()->opt_string("print_host") == s_last_sync->host;
}

wxString reported_text(const PrinterTool& tool)
{
    if (!tool.loaded)
        return _L("Empty");
    std::string s;
    for (const std::string* part : {&tool.vendor, &tool.material, &tool.color})
        if (!part->empty())
            s += (s.empty() ? "" : " ") + *part;
    return from_u8(s);
}

wxString action_text(const ExtruderPlan& row)
{
    switch (row.action) {
    case Action::NotLoaded: return _L("No change");
    case Action::Keep:      return format_wxstr(_L("Keeps %1%"), row.preset);
    case Action::Select:    return format_wxstr(_L("Selects %1%"), row.preset);
    case Action::Install:   return format_wxstr(_L("Installs and selects %1%"), row.preset);
    // TRN %1% = new preset name, %2% = the preset it is copied from
    case Action::Create:    return format_wxstr(_L("Creates %1% from %2%"), row.preset, row.template_preset);
    case Action::NoMatch:   return _L("No preset to select");
    }
    return {};
}

bool has_change(const ExtruderPlan& row)
{
    return row.action == Action::Select || row.action == Action::Install || row.action == Action::Create;
}

wxWindow* swatch(wxWindow* parent, const std::string& hex)
{
    const int size = int(1.6 * wxGetApp().em_unit());
    if (hex.empty())
        return new wxStaticText(parent, wxID_ANY, wxString::FromUTF8("–"));
    return new wxStaticBitmap(parent, wxID_ANY, *get_solid_bmp_bundle(size, size, hex));
}

wxStaticText* note_text(wxWindow* parent, const std::string& text, int wrap)
{
    auto* t = new wxStaticText(parent, wxID_ANY, from_u8(text));
    t->SetFont(wxGetApp().small_font());
    t->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
    t->Wrap(wrap);
    return t;
}

// Any extruder with its own color rather than its filament's ("Reset to filament color")
bool has_extruder_colours()
{
    const auto* colours = wxGetApp().preset_bundle->printers.get_edited_preset().config.option<ConfigOptionStrings>("extruder_colour");
    return colours && std::any_of(colours->values.begin(), colours->values.end(), [](const std::string& c) { return !c.empty(); });
}

void apply_plan(wxWindow* parent, const std::string& printer_name, const Plan& plan, const std::vector<bool>& accepted,
                bool reset_colours)
{
    GUI_App&      app    = wxGetApp();
    PresetBundle& bundle = *app.preset_bundle;
    auto*         ft     = dynamic_cast<TabFilament*>(app.get_tab(Preset::TYPE_FILAMENT));
    const int     active = ft ? ft->get_active_extruder() : 0;

    bool touches_active = false;
    for (size_t i = 0; i < plan.extruders.size(); ++i) {
        const ExtruderPlan& row = plan.extruders[i];
        if (accepted[i] && row.idx == active &&
            (row.action == Action::Select || row.action == Action::Install || row.action == Action::Create))
            touches_active = true;
    }
    // The Filament tab will switch presets: settle its unsaved changes first. A "transfer" is
    // kept by the tab and applied when it selects the new preset.
    PresetCollection& fil = bundle.filaments;
    if (touches_active && ft && fil.current_is_dirty()) {
        if (!ft->may_discard_current_dirty_preset())
            return;
        if (fil.current_is_dirty())
            fil.discard_current_changes();
    }

    const ApplyResult r = apply_presets(bundle, app.app_config, plan, accepted);

    // The Filament tab goes to its extruder's new preset before anything else can run
    // update_compatible(), which would otherwise reselect the tab's old preset
    if (ft && touches_active) {
        const std::string name = bundle.extruders_filaments[active].get_selected_preset_name();
        if (!ft->select_preset(name))
            bundle.extruders_filaments[active].select_filament(fil.get_edited_preset().name);
    }

    // Each extruder shows its filament's color: the presets carry the colors. Extruder colors live
    // in the printer preset; through the Printer tab it shows as modified
    bool colours_changed = false;
    Tab* tp              = app.get_tab(Preset::TYPE_PRINTER);
    if (reset_colours && tp) {
        const auto* current = tp->get_config()->option<ConfigOptionStrings>("extruder_colour");
        const std::vector<std::string> before = current ? current->values : std::vector<std::string>();
        const std::vector<std::string> after(bundle.extruders_filaments.size(), std::string());
        if (after != before) {
            DynamicPrintConfig colours;
            colours.set_key_value("extruder_colour", new ConfigOptionStrings(after));
            tp->load_config(colours);
            colours_changed = true;
        }
    }

    app.sidebar().update_presets(Preset::TYPE_FILAMENT);
    app.plater()->on_config_change(bundle.full_config());
    bundle.export_selections(*app.app_config);

    size_t notes = plan.notes.size();
    for (size_t i = 0; i < plan.extruders.size(); ++i)
        if (accepted[i])
            notes += plan.extruders[i].notes.size();

    std::string summary = format(_L("Filaments from %1%: %2% selected, %3% created, %4% installed."), printer_name,
                                 r.selected.size(), r.created.size(), r.installed.size());
    if (colours_changed)
        summary += " " + _u8L("Extruders show their filament colors; save the printer preset to keep that.");
    if (!r.errors.empty())
        summary += " " + _u8L("Some changes failed.");
    if (NotificationManager* nm = app.notification_manager())
        nm->push_notification(NotificationType::CustomNotification,
                              (r.errors.empty() && notes == 0) ? NotificationManager::NotificationLevel::RegularNotificationLevel :
                                                                 NotificationManager::NotificationLevel::WarningNotificationLevel,
                              summary, "", {}, "", 0, /* multiline */ true);
    BOOST_LOG_TRIVIAL(info) << "Filament sync: " << summary;

    if (!r.errors.empty()) {
        std::string text;
        for (const std::string& e : r.errors)
            text += e + "\n";
        show_error(parent, text);
    }
}

} // namespace

void update_unavailable_extruders_warning()
{
    GUI_App&             app    = wxGetApp();
    NotificationManager* nm     = app.notification_manager();
    Plater*              plater = app.plater();
    if (!nm || !plater)
        return;
    nm->close_notification_of_type(NotificationType::FilamentSyncUnavailableExtruders);
    if (!last_sync_applies())
        return;

    std::vector<unsigned int> used;
    for (const std::unique_ptr<Print>& print : plater->get_fff_prints())
        if (print && !print->empty())
            append(used, print->extruders());
    sort_remove_duplicates(used);
    const std::string text = unavailable_extruders_warning(unavailable_now(), used, s_last_sync->printer, s_last_sync->time);
    if (!text.empty())
        nm->push_notification(NotificationType::FilamentSyncUnavailableExtruders,
                              NotificationManager::NotificationLevel::WarningNotificationLevel, text);
}

void update_filament_list_visibility()
{
    Plater* plater = wxGetApp().plater();
    if (!plater)
        return;
    std::vector<int> hidden;
    if (last_sync_applies() && hide_empty_extruders()) {
        const UnavailableExtruders unavailable = unavailable_now();
        hidden = unavailable.empty;
        hidden.insert(hidden.end(), unavailable.uncalibrated.begin(), unavailable.uncalibrated.end());
    }
    plater->sidebar().set_hidden_filament_extruders(std::move(hidden));
}

bool can_sync_filaments_from_printer()
{
    PresetBundle* bundle = wxGetApp().preset_bundle;
    if (!bundle || bundle->printers.get_edited_preset().printer_technology() != ptFFF)
        return false;
    const DynamicPrintConfig* cfg = bundle->physical_printers.get_selected_printer_config();
    if (!cfg)
        return false;
    const auto* host_type = cfg->option<ConfigOptionEnum<PrintHostType>>("host_type");
    return host_type && host_type->value == htPrusaLink && cfg->has("print_host") && !cfg->opt_string("print_host").empty();
}

void sync_filaments_from_printer(wxWindow* parent)
{
    PresetBundle& bundle = *wxGetApp().preset_bundle;
    if (!can_sync_filaments_from_printer()) {
        show_info(parent,
                  _L("To read the filaments loaded in your printer, select a physical printer for this printer profile, "
                     "with PrusaLink as its host and its address and API key or password set.") +
                      "\n\n" + _L("Add one with the cog icon next to the Printer list: Add physical printer."),
                  dialog_title());
        return;
    }

    const PhysicalPrinter& pp  = bundle.physical_printers.get_selected_printer();
    DynamicPrintConfig     cfg = pp.config;
    if (!resolve_stored_printhost_password(pp.name, cfg))
        return; // the error is shown

    PrinterToolsResult res;
    {
        wxBusyCursor wait;
        wxBusyInfo   info(_L("Reading the filaments loaded in the printer…"), parent);
        fetch_printer_tools(cfg, res);
    }

    const std::string host = cfg.opt_string("print_host");
    switch (res.status) {
    case PrinterToolsStatus::Ok: break;
    case PrinterToolsStatus::Unsupported:
        show_info(parent,
                  format_wxstr(_L("%1% doesn't report the filaments in its tools. Syncing needs a CORE One INDX with "
                                  "firmware that has Filament Slots."), pp.name),
                  dialog_title());
        return;
    case PrinterToolsStatus::AuthFailed:
        show_error(parent, format_wxstr(_L("%1% refused the API key or password. Check them in the physical printer's settings."), pp.name));
        return;
    case PrinterToolsStatus::Transport:
        // TRN %1% = printer address, %2% = error detail
        show_error(parent, format_wxstr(_L("Couldn't reach the printer at %1%: %2%"), host, res.message));
        return;
    case PrinterToolsStatus::PrinterError:
        show_error(parent, format_wxstr(_L("The printer answered with an error: %1%"), res.message));
        return;
    case PrinterToolsStatus::Malformed:
        show_error(parent, format_wxstr(_L("The printer's answer couldn't be read: %1%"), res.message));
        return;
    }
    s_last_sync = LastSync{pp.name, cfg.opt_string("print_host"), res.tools,
                           into_u8(wxDateTime::Now().Format("%H:%M"))};
    update_unavailable_extruders_warning();
    update_filament_list_visibility();

    if (res.tools.tools.empty()) {
        show_info(parent, format_wxstr(_L("%1% reports no tools: none of its docks is calibrated."), pp.name), dialog_title());
        return;
    }

    const Plan plan = make_plan(res.tools, collect_extruder_states(bundle), taken_filament_names(bundle.filaments),
                                printer_name_tag(bundle));
    FilamentSyncDialog dlg(parent, plan, pp.name);
    if (dlg.ShowModal() != wxID_OK)
        return;
    apply_plan(parent, pp.name, plan, dlg.accepted(), dlg.reset_extruder_colours());
}

FilamentSyncDialog::FilamentSyncDialog(wxWindow* parent, const Plan& plan, const std::string& printer_name)
    : wxDialog(parent, wxID_ANY, dialog_title(), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , m_plan(plan)
{
    const int em   = wxGetApp().em_unit();
    auto*     top  = new wxBoxSizer(wxVERTICAL);
    auto*     intro = new wxStaticText(this, wxID_ANY, format_wxstr(_L("Filaments loaded in %1%. Choose what to apply:"), printer_name));
    top->Add(intro, 0, wxALL, em);

    auto* grid = new wxGridBagSizer(int(0.4 * em), em);
    int   line = 0;
    const wxString headings[] = {"", _L("Extruder"), _L("In the printer"), _L("Preset"), _L("Preset color")};
    for (int c = 0; c < 5; ++c) {
        auto* h = new wxStaticText(this, wxID_ANY, headings[c]);
        h->SetFont(wxGetApp().bold_font());
        grid->Add(h, wxGBPosition(line, c), wxDefaultSpan, wxALIGN_CENTER_VERTICAL);
    }
    ++line;

    for (const ExtruderPlan& row : m_plan.extruders) {
        wxCheckBox* check = nullptr;
        if (has_change(row)) {
            check = new wxCheckBox(this, wxID_ANY, wxEmptyString);
            check->SetValue(true);
            check->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { update_apply_enabled(); });
            grid->Add(check, wxGBPosition(line, 0), wxDefaultSpan, wxALIGN_CENTER_VERTICAL);
        }
        m_checks.push_back(check);

        grid->Add(new wxStaticText(this, wxID_ANY, format_wxstr(_L("Extruder %1%"), row.idx + 1)), wxGBPosition(line, 1),
                  wxDefaultSpan, wxALIGN_CENTER_VERTICAL);

        auto* reported = new wxBoxSizer(wxHORIZONTAL);
        if (row.tool.loaded) {
            reported->Add(swatch(this, row.tool.color_hex), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, int(0.5 * em));
        }
        reported->Add(new wxStaticText(this, wxID_ANY, reported_text(row.tool)), 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(reported, wxGBPosition(line, 2), wxDefaultSpan, wxALIGN_CENTER_VERTICAL);

        grid->Add(new wxStaticText(this, wxID_ANY, action_text(row)), wxGBPosition(line, 3), wxDefaultSpan, wxALIGN_CENTER_VERTICAL);

        // The preset's filament color, which the extruder shows
        if (row.tool.loaded && !row.preset_colour.empty())
            grid->Add(swatch(this, row.preset_colour), wxGBPosition(line, 4), wxDefaultSpan, wxALIGN_CENTER_VERTICAL);
        ++line;

        for (const std::string& note : row.notes) {
            grid->Add(note_text(this, note, 45 * em), wxGBPosition(line, 2), wxGBSpan(1, 3), wxALIGN_CENTER_VERTICAL);
            ++line;
        }
    }
    top->Add(grid, 0, wxLEFT | wxRIGHT, em);

    std::vector<std::string> footer = m_plan.notes;
    if (std::any_of(m_plan.extruders.begin(), m_plan.extruders.end(), [](const ExtruderPlan& r) { return r.action == Action::NotLoaded; }))
        footer.push_back(_u8L("Empty extruders keep their presets; PrusaSlicer warns when a print uses one."));
    if (std::any_of(m_plan.extruders.begin(), m_plan.extruders.end(), [](const ExtruderPlan& r) { return r.action == Action::Create; }))
        footer.push_back(_u8L("New presets are saved as user presets on this computer."));
    for (const std::string& note : footer)
        top->Add(note_text(this, note, 60 * em), 0, wxLEFT | wxRIGHT | wxTOP, em);

    // Takes effect at once, whether or not anything is applied
    if (s_last_sync && (!unavailable_now().empty.empty() || !unavailable_now().uncalibrated.empty())) {
        auto* hide = new wxCheckBox(this, wxID_ANY, _L("Hide empty extruders in the sidebar"));
        hide->SetValue(hide_empty_extruders());
        hide->Bind(wxEVT_CHECKBOX, [](wxCommandEvent& evt) {
            wxGetApp().app_config->set(HIDE_EMPTY_KEY, evt.IsChecked() ? "1" : "0");
            update_filament_list_visibility();
        });
        top->Add(hide, 0, wxLEFT | wxRIGHT | wxTOP, em);
    }

    if (has_extruder_colours()) {
        m_reset_colours = new wxCheckBox(this, wxID_ANY, _L("Reset extruder colors to filament colors"));
        m_reset_colours->SetToolTip(_L("Like Reset to Filament Color for each extruder in the printer settings. "
                                       "The printer preset then shows as modified until you save it."));
        m_reset_colours->SetValue(true);
        m_reset_colours->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { update_apply_enabled(); });
        top->Add(m_reset_colours, 0, wxLEFT | wxRIGHT | wxTOP, em);
    }

    auto* buttons = CreateStdDialogButtonSizer(wxOK | wxCANCEL);
    if (auto* ok = static_cast<wxButton*>(FindWindow(wxID_OK)))
        ok->SetLabel(_L("Apply"));
    top->Add(buttons, 0, wxEXPAND | wxALL, em);

    wxGetApp().UpdateDlgDarkUI(this);
    SetSizerAndFit(top);
    update_apply_enabled();
    CenterOnParent();
}

std::vector<bool> FilamentSyncDialog::accepted() const
{
    std::vector<bool> out;
    for (const wxCheckBox* check : m_checks)
        out.push_back(check && check->GetValue());
    return out;
}

bool FilamentSyncDialog::reset_extruder_colours() const { return m_reset_colours && m_reset_colours->GetValue(); }

void FilamentSyncDialog::update_apply_enabled()
{
    const bool any = std::any_of(m_checks.begin(), m_checks.end(), [](const wxCheckBox* c) { return c && c->GetValue(); }) ||
                     reset_extruder_colours();
    if (auto* ok = FindWindow(wxID_OK))
        ok->Enable(any);
}

} // namespace GUI
} // namespace Slic3r
