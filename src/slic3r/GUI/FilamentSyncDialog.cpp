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
#include <wx/choice.h>
#include <wx/gbsizer.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>
#include <wx/utils.h>

#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Print.hpp"
#include "slic3r/Utils/PrinterTools.hpp"
#include "slic3r/Utils/ServiceConfig.hpp"

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "NotificationManager.hpp"
#include "PhysicalPrinterDialog.hpp"
#include "Plater.hpp"
#include "Tab.hpp"
#include "UserAccount.hpp"
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
    // The physical printer's name, or the Prusa Connect printer's
    std::string          printer;
    // The device read: an edited physical printer keeps its name but can point elsewhere. From
    // Prusa Connect, "connect:<printer uuid>"
    std::string          host;
    // What the printer reported; which extruders that leaves out depends on the printer profile
    // in use, which can change while the physical printer stays
    PrinterTools         tools;
    std::string          time;
    bool                 from_connect = false;
    // From Prusa Connect: the account and the profile's printer_model it was read for, as no
    // physical printer ties it to the profile
    std::string          account;
    std::string          printer_model;
};
std::optional<LastSync> s_last_sync;

// The Prusa Connect printer last synced from, preselected when several fit the profile
const char* CONNECT_PRINTER_KEY = "filament_sync_connect_printer";

wxString dialog_title() { return _L("Sync Filaments from Printer"); }

UserAccount* user_account()
{
    Plater* plater = wxGetApp().plater();
    return plater ? plater->get_user_account() : nullptr;
}

// The edited printer profile's printer_model, which finds its printer in Prusa Connect
std::string edited_printer_model(const PresetBundle& bundle)
{
    const DynamicPrintConfig& printer = bundle.printers.get_edited_preset().config;
    return printer.has("printer_model") ? printer.opt_string("printer_model") : std::string();
}

// A Prusa printer's profile, or one saved from it: only those can be in Prusa Connect. The check
// "Upload to Connect" makes (Plater::priv::can_show_upload_to_connect).
bool is_prusa_printer_profile(const PresetBundle& bundle)
{
    const Preset& printer = bundle.printers.get_selected_preset();
    std::string   vendor_id;
    if (printer.vendor) {
        vendor_id = printer.vendor->id;
    } else if (const std::string inherits = printer.inherits(); !inherits.empty()) {
        if (const Preset* parent = bundle.printers.find_preset(inherits); parent && parent->vendor)
            vendor_id = parent->vendor->id;
    }
    // Some vendor ids carry a repository prefix
    return vendor_id.find("Prusa") != std::string::npos;
}

// The preview's "Hide empty extruders in the sidebar", on unless turned off
const char* HIDE_EMPTY_KEY = "filament_sync_hide_empty";
bool hide_empty_extruders() { return wxGetApp().app_config->get(HIDE_EMPTY_KEY) != "0"; }

// The extruders the last sync leaves without filament, for the edited printer profile
UnavailableExtruders unavailable_now()
{
    const auto* diameters = wxGetApp().preset_bundle->printers.get_edited_preset().config.option<ConfigOptionFloats>("nozzle_diameter");
    UnavailableExtruders out = unavailable_extruders(s_last_sync->tools, diameters ? diameters->values.size() : 0);
    // Prusa Connect lists every tool, an empty one too: one it leaves out isn't a dock the printer
    // skipped for not being calibrated, so nothing is known about it
    if (s_last_sync->from_connect)
        out.uncalibrated.clear();
    return out;
}

// The last sync applies to the physical printer it read, or, from Prusa Connect, to the account
// and printer model it was read for
bool last_sync_applies()
{
    PresetBundle* bundle = wxGetApp().preset_bundle;
    if (!s_last_sync || !bundle)
        return false;
    const FilamentSyncSource source = filament_sync_source();
    if (s_last_sync->from_connect) {
        // Read for this profile's model, as this account, and still the source (no PrusaLink
        // physical printer selected since)
        const UserAccount* account = user_account();
        return source == FilamentSyncSource::Connect && account && account->get_username() == s_last_sync->account &&
               edited_printer_model(*bundle) == s_last_sync->printer_model;
    }
    return source == FilamentSyncSource::PrusaLink && bundle->physical_printers.get_selected_printer().name == s_last_sync->printer &&
           bundle->physical_printers.get_selected_printer_config()->opt_string("print_host") == s_last_sync->host;
}

wxString reported_text(const PrinterTool& tool)
{
    if (!tool.loaded)
        return _L("Empty");
    // A material assigned in Prusa Connect goes by its product name, which names its type and color
    const std::vector<const std::string*> parts = tool.product.empty() ?
                                                      std::vector<const std::string*>{&tool.vendor, &tool.material, &tool.color} :
                                                      std::vector<const std::string*>{&tool.vendor, &tool.product};
    std::string s;
    for (const std::string* part : parts)
        if (!part->empty())
            s += (s.empty() ? "" : " ") + *part;
    return from_u8(s);
}

wxString action_text(const ExtruderPlan& row)
{
    switch (row.action) {
    case Action::NotLoaded: return _L("No change");
    case Action::Keep:      return row.preset.empty() ? _L("No change") : format_wxstr(_L("Keeps %1%"), row.preset);
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

int swatch_size() { return int(1.6 * wxGetApp().em_unit()); }

wxWindow* swatch(wxWindow* parent, const std::string& hex)
{
    if (hex.empty())
        return new wxStaticText(parent, wxID_ANY, wxString::FromUTF8("–"));
    return new wxStaticBitmap(parent, wxID_ANY, *get_solid_bmp_bundle(swatch_size(), swatch_size(), hex));
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

    // The plater's filament colors first, so the lists the sidebar refreshes show the new ones.
    app.plater()->on_config_change(bundle.full_config());
    app.sidebar().update_presets(Preset::TYPE_FILAMENT);
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
    std::vector<std::string> filament_names;
    for (const ExtruderFilaments& ef : app.preset_bundle->extruders_filaments)
        filament_names.push_back(filament_display_name(ef.get_selected_preset_name()));
    const std::string text = unavailable_extruders_warning(unavailable_now(), used, filament_names, s_last_sync->printer, s_last_sync->time);
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

FilamentSyncSource filament_sync_source()
{
    PresetBundle* bundle = wxGetApp().preset_bundle;
    if (!bundle || bundle->printers.get_edited_preset().printer_technology() != ptFFF)
        return FilamentSyncSource::None;
    if (const DynamicPrintConfig* cfg = bundle->physical_printers.get_selected_printer_config()) {
        const auto* host_type = cfg->option<ConfigOptionEnum<PrintHostType>>("host_type");
        if (host_type && host_type->value == htPrusaLink && cfg->has("print_host") && !cfg->opt_string("print_host").empty())
            return FilamentSyncSource::PrusaLink;
    }
    const UserAccount* account = user_account();
    if (account && account->is_logged() && !edited_printer_model(*bundle).empty() && is_prusa_printer_profile(*bundle))
        return FilamentSyncSource::Connect;
    return FilamentSyncSource::None;
}

void forget_filament_sync_from_connect()
{
    if (s_last_sync && s_last_sync->from_connect)
        s_last_sync.reset();
}

bool can_sync_filaments_from_printer() { return filament_sync_source() != FilamentSyncSource::None; }

namespace {

// The preview of what the reported tools would change, and the changes the user accepts
void preview_and_apply(wxWindow* parent, PresetBundle& bundle, const PrinterTools& tools, const std::string& printer_name,
                       bool from_connect)
{
    const Plan plan = make_plan(tools, collect_extruder_states(bundle), taken_filament_names(bundle.filaments), printer_name_tag(bundle));
    FilamentSyncDialog dlg(parent, plan, printer_name, from_connect);
    if (dlg.ShowModal() != wxID_OK)
        return;
    apply_plan(parent, printer_name, dlg.chosen_plan(), dlg.accepted(), dlg.reset_extruder_colours());
}

void sync_from_connect(wxWindow* parent, PresetBundle& bundle)
{
    UserAccount*      account = user_account();
    const std::string model   = edited_printer_model(bundle);
    const std::string token   = account ? account->get_access_token() : std::string();
    if (token.empty()) {
        show_info(parent, _L("Sign in to Prusa Account to read the filaments of your printer from Prusa Connect."), dialog_title());
        return;
    }

    ConnectPrintersResult res;
    {
        wxBusyCursor wait;
        wxBusyInfo   info(_L("Reading the filaments from Prusa Connect…"), parent);
        fetch_connect_printers(Utils::ServiceConfig::instance().connect_printer_list_v2_url(), token, res);
    }
    switch (res.status) {
    case PrinterToolsStatus::Ok: break;
    case PrinterToolsStatus::AuthFailed:
        // The access token is refreshed in the background; the next try will have the new one
        account->request_refresh();
        show_error(parent, _L("Prusa Connect didn't accept your Prusa Account sign-in. PrusaSlicer is renewing it: try again in a "
                              "moment, or sign in again."));
        return;
    case PrinterToolsStatus::Transport:
        // TRN %1% = error detail
        show_error(parent, format_wxstr(_L("Couldn't reach Prusa Connect: %1%"), res.message));
        return;
    case PrinterToolsStatus::Unsupported:
    case PrinterToolsStatus::PrinterError:
        show_error(parent, format_wxstr(_L("Prusa Connect answered with an error: %1%"), res.message));
        return;
    case PrinterToolsStatus::Malformed:
        show_error(parent, format_wxstr(_L("Prusa Connect's answer couldn't be read: %1%"), res.message));
        return;
    }
    for (const std::string& w : res.warnings)
        BOOST_LOG_TRIVIAL(warning) << "Filament sync from Prusa Connect: " << w;

    std::vector<const ConnectPrinter*> fits;
    for (const ConnectPrinter& p : res.printers)
        if (connect_printer_fits(p, model))
            fits.push_back(&p);
    if (fits.empty()) {
        // TRN %1% = printer model, such as COREONE_INDX8T
        show_info(parent,
                  format_wxstr(_L("None of the printers in your Prusa Connect account is a %1%, the printer of this printer profile."), model),
                  dialog_title());
        return;
    }
    const auto display_name = [](const ConnectPrinter& p) { return p.name.empty() ? p.model : p.name; };
    const ConnectPrinter* chosen = fits.front();
    if (fits.size() > 1) {
        AppConfig*    app_config = wxGetApp().app_config;
        const std::string last   = app_config->get(CONNECT_PRINTER_KEY);
        wxArrayString names;
        int           preselect = 0;
        for (size_t i = 0; i < fits.size(); ++i) {
            const std::string state = fits[i]->state.empty() ? std::string() : " (" + fits[i]->state + ")";
            names.Add(from_u8(display_name(*fits[i]) + state));
            if (fits[i]->uuid == last)
                preselect = int(i);
        }
        const int picked = wxGetApp().GetSingleChoiceIndex(
            _L("Several printers in your Prusa Connect account fit this printer profile. Read the filaments of:"), dialog_title(),
            names, preselect);
        if (picked < 0)
            return;
        chosen = fits[size_t(picked)];
    }
    wxGetApp().app_config->set(CONNECT_PRINTER_KEY, chosen->uuid);

    const std::string name = display_name(*chosen);
    if (chosen->has_feeder_slots) {
        show_info(parent,
                  format_wxstr(_L("%1% has an MMU. Syncing from Prusa Connect reads printers whose tools are extruders, such as the "
                                  "CORE One INDX, not the slots of an MMU."),
                               name),
                  dialog_title());
        return;
    }
    for (const std::string& w : chosen->tools.warnings)
        BOOST_LOG_TRIVIAL(warning) << "Filament sync from Prusa Connect, " << name << ": " << w;

    // No tools isn't a printer without calibrated docks, as over PrusaLink: Connect has no data
    // for it (it never reported), or what it has couldn't be read. Nothing is remembered then.
    if (chosen->tools.tools.empty()) {
        if (!chosen->tools.warnings.empty())
            show_error(parent, format_wxstr(_L("Prusa Connect's answer couldn't be read: %1%"), chosen->tools.warnings.front()));
        else
            show_info(parent, format_wxstr(_L("Prusa Connect has no tools of %1% yet."), name), dialog_title());
        return;
    }

    s_last_sync = LastSync{name, "connect:" + chosen->uuid, chosen->tools, into_u8(wxDateTime::Now().Format("%H:%M")),
                           /* from_connect */ true, account->get_username(), model};
    update_unavailable_extruders_warning();
    update_filament_list_visibility();

    preview_and_apply(parent, bundle, chosen->tools, name, /* from_connect */ true);
}

} // namespace

void sync_filaments_from_printer(wxWindow* parent)
{
    PresetBundle& bundle = *wxGetApp().preset_bundle;
    switch (filament_sync_source()) {
    case FilamentSyncSource::PrusaLink: break;
    case FilamentSyncSource::Connect: sync_from_connect(parent, bundle); return;
    case FilamentSyncSource::None: {
        const UserAccount* account = user_account();
        const bool         signed_in = account && account->is_logged();
        wxString           text = _L("To read the filaments loaded in your printer, select a physical printer for this printer profile, "
                                     "with PrusaLink as its host and its address and API key or password set.") +
                        "\n\n" + _L("Add one with the cog icon next to the Printer list: Add physical printer.") + "\n\n";
        // Signed in, the profile is what keeps Prusa Connect out
        text += signed_in ? _L("Prusa Connect can't be read for this printer profile: it isn't a Prusa printer's profile with a "
                               "printer model to find your printer by. Select your Prusa printer's system profile, or one saved "
                               "from it.") :
                            _L("Or sign in to Prusa Account to read the filaments that Prusa Connect has for your printer.");
        show_info(parent, text, dialog_title());
        return;
    }
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
    preview_and_apply(parent, bundle, res.tools, pp.name, /* from_connect */ false);
}

FilamentSyncDialog::FilamentSyncDialog(wxWindow* parent, const Plan& plan, const std::string& printer_name, bool from_connect)
    : wxDialog(parent, wxID_ANY, dialog_title(), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , m_plan(plan)
{
    const int em    = wxGetApp().em_unit();
    auto*     top   = new wxBoxSizer(wxVERTICAL);
    auto*     intro = new wxStaticText(this, wxID_ANY,
                                       from_connect ?
                                           format_wxstr(_L("Filaments of %1% in Prusa Connect. Choose what to apply:"), printer_name) :
                                           format_wxstr(_L("Filaments loaded in %1%. Choose what to apply:"), printer_name));
    top->Add(intro, 0, wxALL, em);

    auto* grid = new wxGridBagSizer(int(0.4 * em), em);
    int   line = 0;
    const wxString headings[] = {_L("Extruder"), from_connect ? _L("In Prusa Connect") : _L("In the printer"), _L("Preset"),
                                 _L("Preset color")};
    for (int c = 0; c < 4; ++c) {
        auto* h = new wxStaticText(this, wxID_ANY, headings[c]);
        h->SetFont(wxGetApp().bold_font());
        grid->Add(h, wxGBPosition(line, c), wxDefaultSpan, wxALIGN_CENTER_VERTICAL);
    }
    ++line;

    for (size_t i = 0; i < m_plan.extruders.size(); ++i) {
        const ExtruderPlan& row = m_plan.extruders[i];
        grid->Add(new wxStaticText(this, wxID_ANY, format_wxstr(_L("Extruder %1%"), row.idx + 1)), wxGBPosition(line, 0),
                  wxDefaultSpan, wxALIGN_CENTER_VERTICAL);

        auto* reported = new wxBoxSizer(wxHORIZONTAL);
        if (row.tool.loaded) {
            reported->Add(swatch(this, row.tool.color_hex), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, int(0.5 * em));
        }
        reported->Add(new wxStaticText(this, wxID_ANY, reported_text(row.tool)), 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(reported, wxGBPosition(line, 1), wxDefaultSpan, wxALIGN_CENTER_VERTICAL);

        // What Apply does: the plan's own action, else one of its alternatives
        wxChoice* choice = nullptr;
        if (row.alternatives.empty()) {
            grid->Add(new wxStaticText(this, wxID_ANY, action_text(row)), wxGBPosition(line, 2), wxDefaultSpan, wxALIGN_CENTER_VERTICAL);
        } else {
            choice = new wxChoice(this, wxID_ANY);
            choice->Append(action_text(row));
            for (size_t a = 0; a < row.alternatives.size(); ++a)
                choice->Append(action_text(with_alternative(row, a)));
            choice->SetSelection(0);
            choice->Bind(wxEVT_CHOICE, [this, i](wxCommandEvent&) {
                update_row(i);
                Layout();
                Fit();
                update_apply_enabled();
            });
            grid->Add(choice, wxGBPosition(line, 2), wxDefaultSpan, wxALIGN_CENTER_VERTICAL);
        }
        m_choices.push_back(choice);

        // The preset's filament color, which the extruder shows; follows the choice
        wxStaticBitmap* colour = nullptr;
        if (row.tool.loaded) {
            colour = new wxStaticBitmap(this, wxID_ANY, *get_solid_bmp_bundle(swatch_size(), swatch_size(), "#000000"));
            grid->Add(colour, wxGBPosition(line, 3), wxDefaultSpan, wxALIGN_CENTER_VERTICAL);
        }
        m_swatches.push_back(colour);
        ++line;

        // The notes of the choice, one per line
        wxStaticText* notes = nullptr;
        if (!row.notes.empty() ||
            std::any_of(row.alternatives.begin(), row.alternatives.end(), [](const Alternative& a) { return !a.notes.empty(); })) {
            notes = note_text(this, "", 45 * em);
            grid->Add(notes, wxGBPosition(line, 1), wxGBSpan(1, 3), wxALIGN_CENTER_VERTICAL);
            ++line;
        }
        m_notes.push_back(notes);
        update_row(i);
    }
    top->Add(grid, 0, wxLEFT | wxRIGHT, em);

    std::vector<std::string> footer = m_plan.notes;
    if (std::any_of(m_plan.extruders.begin(), m_plan.extruders.end(), [](const ExtruderPlan& r) { return r.action == Action::NotLoaded; }))
        footer.push_back(_u8L("Empty extruders keep their presets; PrusaSlicer warns when a print uses one."));
    const auto creates = [](const ExtruderPlan& r) {
        return r.action == Action::Create ||
               std::any_of(r.alternatives.begin(), r.alternatives.end(), [](const Alternative& a) { return a.action == Action::Create; });
    };
    if (std::any_of(m_plan.extruders.begin(), m_plan.extruders.end(), creates))
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

Plan FilamentSyncDialog::chosen_plan() const
{
    Plan out = m_plan;
    for (size_t i = 0; i < m_choices.size(); ++i)
        if (m_choices[i] && m_choices[i]->GetSelection() > 0)
            out.extruders[i] = with_alternative(m_plan.extruders[i], size_t(m_choices[i]->GetSelection() - 1));
    return out;
}

std::vector<bool> FilamentSyncDialog::accepted() const
{
    std::vector<bool> out;
    for (const ExtruderPlan& row : chosen_plan().extruders)
        out.push_back(has_change(row));
    return out;
}

bool FilamentSyncDialog::reset_extruder_colours() const { return m_reset_colours && m_reset_colours->GetValue(); }

void FilamentSyncDialog::update_apply_enabled()
{
    const std::vector<bool> changes = accepted();
    const bool              any     = std::find(changes.begin(), changes.end(), true) != changes.end() || reset_extruder_colours();
    if (auto* ok = FindWindow(wxID_OK))
        ok->Enable(any);
}

void FilamentSyncDialog::update_row(size_t i)
{
    const int          choice = m_choices[i] ? m_choices[i]->GetSelection() : 0;
    const ExtruderPlan row    = choice > 0 ? with_alternative(m_plan.extruders[i], size_t(choice - 1)) : m_plan.extruders[i];
    if (m_swatches[i]) {
        if (!row.preset_colour.empty())
            m_swatches[i]->SetBitmap(*get_solid_bmp_bundle(swatch_size(), swatch_size(), row.preset_colour));
        m_swatches[i]->Show(!row.preset_colour.empty());
    }
    if (m_notes[i]) {
        std::string text;
        for (const std::string& note : row.notes)
            text += (text.empty() ? "" : "\n") + note;
        m_notes[i]->SetLabel(from_u8(text));
        m_notes[i]->Wrap(45 * wxGetApp().em_unit());
        m_notes[i]->Show(!text.empty());
    }
}

} // namespace GUI
} // namespace Slic3r
