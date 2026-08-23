///|/ Copyright (c) 2026
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "MaintenanceColdPullDialog.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/statbox.h>
#include <wx/button.h>

namespace Slic3r { namespace GUI {

// ---------------------------------------------------------------------------
// Pre-flight gate
// ---------------------------------------------------------------------------

MaintenanceColdPullPreflightDialog::MaintenanceColdPullPreflightDialog(wxWindow* parent,
                                                                       bool for_serial)
    : wxDialog(parent, wxID_ANY, _L("Cold Pull — Before You Start"),
               wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
{
    SetFont(wxGetApp().normal_font());
    wxGetApp().UpdateDarkUI(this);

    // Target text width. Without an explicit Wrap() wxStaticText breaks only at
    // embedded newlines, so a long paragraph forces the whole dialog as wide as
    // the paragraph is long.
    const int text_width = FromDIP(380);

    auto* top = new wxBoxSizer(wxVERTICAL);

    auto* intro = new wxStaticText(this, wxID_ANY,
        _L("Nothing has been sent to the printer yet. Do these at the printer "
           "first — the host cannot check or change them for you. The last item "
           "is different: it is a change this procedure makes to the printer's "
           "saved settings, and you need to know it is coming."));
    intro->Wrap(text_width);
    top->Add(intro, 0, wxALL, 10);

    auto* list_box = new wxStaticBoxSizer(wxVERTICAL, this,
                                          _L("Confirm each item"));

    // Each entry: the checkbox label (the action) and why it matters. The
    // "why" is what stops people from reflexively ticking boxes.
    struct Item { wxString label, why; };
    std::vector<Item> items = {
        { _L("Filament Sensor is turned OFF"),
          _L("Settings → Filament Sensor. If left on, the printer grabs and "
             "autoloads the filament while you are hand-inserting it.") },
        { _L("The PTFE tube is removed from this tool"),
          _L("The pulled plug travels up and out of the top port. With the "
             "tube fitted there is nowhere for it to go.") },
    };

    // Serial-only. A G-code print job is driven by the media queue and the
    // normal print state machine, so this timeout cannot apply to it -- asking
    // those users to change the setting and reboot would be asking for a change
    // that cannot affect their run.
    if (for_serial) {
        items.push_back(
            { _L("\"Serial Printing Screen\" is turned OFF, and the printer rebooted"),
              _L("Settings → Hardware → Experimental Settings (leaving the screen "
                 "prompts to save and reboot). Left on, the printer treats this "
                 "session as a print, and a 5-second inactivity timeout runs the "
                 "end-of-print sequence mid-procedure — wiping the nozzle and "
                 "docking the tool while the host is still extruding. That crashed "
                 "a real printer with \"E move without tool\".") });
    }

    items.push_back(
        { _L("Light-colored PLA is on hand, and you will stay at the printer"),
          _L("PLA shows the extracted debris clearly. Do not load it yet — a "
             "prompt on the printer's screen says when to insert it. The "
             "procedure stops and waits for knob presses on the printer's "
             "screen at six points, and the printer turns the heaters off "
             "after 30 minutes unattended.") });

    // Last, and a consent rather than an action: firmware 6.9.0 removed the
    // Auto Retract switch on INDX, so the only remaining way to stop the
    // firmware retracting the plug back out of the melt zone is to mark the
    // tool FLEX -- a persistent write the user has to know about and, on the
    // G-code route, put back themselves.
    items.push_back(
        { _L("This nozzle's filament type will be set to FLEX — note what it is now"),
          _L("Firmware 6.9.0 removed the Auto Retract switch on INDX. Left to "
             "itself, auto retract treats the filament as retracted and "
             "silently discards the extrusion and pull moves — the procedure "
             "appears to run but nothing moves. The firmware skips auto retract "
             "for flexible filaments, so this marks the nozzle FLEX. It is a "
             "write to the printer's saved settings, and a power cycle does not "
             "undo it. The serial run reads the current type and puts that exact "
             "value back for you — unless it is a name that cannot be sent over "
             "G-code, in which case it writes nothing and tells you what to set "
             "by hand. The G-code file cannot read anything, so write down what "
             "Settings → Filament shows for this nozzle before you start, and "
             "set it back to that afterwards.") });

    for (const Item& it : items) {
        auto* cb = new wxCheckBox(this, wxID_ANY, it.label);
        cb->Bind(wxEVT_CHECKBOX,
                 [this](wxCommandEvent&) { update_continue_enabled(); });
        m_checks.push_back(cb);
        list_box->Add(cb, 0, wxLEFT | wxRIGHT | wxTOP, 8);

        auto* why = new wxStaticText(this, wxID_ANY, it.why);
        wxFont why_font = why->GetFont();
        why_font.SetPointSize(why_font.GetPointSize() - 1);
        why->SetFont(why_font);
        // Wrap AFTER the smaller font is applied -- Wrap() measures with the
        // font current at call time.
        why->Wrap(text_width - FromDIP(16));
        list_box->Add(why, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
        list_box->AddSpacer(FromDIP(2));
    }

    top->Add(list_box, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);

    auto* buttons = CreateStdDialogButtonSizer(wxOK | wxCANCEL);
    if (auto* ok = static_cast<wxButton*>(FindWindow(wxID_OK)))
        ok->SetLabel(_L("Continue"));
    top->Add(buttons, 0, wxEXPAND | wxALL, 10);

    update_continue_enabled();

    SetSizerAndFit(top);
    CenterOnParent();
}

void MaintenanceColdPullPreflightDialog::update_continue_enabled()
{
    bool all_checked = true;
    for (const wxCheckBox* cb : m_checks)
        all_checked = all_checked && cb->GetValue();
    if (auto* ok = FindWindow(wxID_OK))
        ok->Enable(all_checked);
}

// ---------------------------------------------------------------------------
// Parameters
// ---------------------------------------------------------------------------

MaintenanceColdPullDialog::MaintenanceColdPullDialog(wxWindow* parent,
                                                     bool upload_available,
                                                     bool serial_available)
    : wxDialog(parent, wxID_ANY, _L("Cold Pull (INDX)"),
               wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
    , m_upload_available(upload_available)
    , m_serial_available(serial_available)
{
    SetFont(wxGetApp().normal_font());
    wxGetApp().UpdateDarkUI(this);

    auto* top = new wxBoxSizer(wxVERTICAL);

    // Description — what's about to happen. Prerequisites are handled by the
    // pre-flight dialog that runs before this one. Wrap explicitly, or the
    // paragraph sets the dialog width.
    const int text_width = FromDIP(380);
    auto* intro = new wxStaticText(this, wxID_ANY,
        _L("Runs a guided cold pull on one INDX nozzle to clear a clog: pick "
           "tool, hot flush, pack while cooling, deep cool, then a motorized "
           "pull. You confirm each step on the PRINTER's screen.\n\n"
           "Typical duration: 10–20 minutes, mostly cooling."));
    intro->Wrap(text_width);
    top->Add(intro, 0, wxALL, 10);

    // Delivery method. With a printer detected on USB serial, that route is the
    // default: it is the hardware-validated path and the only one offering live
    // progress and a Cancel that restores printer state. With nothing detected
    // the entry is disabled -- selecting it could only fail -- and the G-code
    // file route becomes the default.
    wxArrayString choices;
    choices.Add(_L("Save G-code file (run it from a USB drive)"));
    if (m_upload_available)
        choices.Add(_L("Upload G-code to the printer"));
    choices.Add(m_serial_available
                    ? _L("Run over USB serial from here")
                    : _L("Run over USB serial from here (no printer detected)"));

    m_delivery = new wxRadioBox(this, wxID_ANY, _L("How to run it"),
                                wxDefaultPosition, wxDefaultSize,
                                choices, 1, wxRA_SPECIFY_COLS);

    // Serial is always the last entry; "Upload" is only present when a print
    // host is configured, so derive the index rather than hard-coding it.
    const int serial_index = m_delivery->GetCount() - 1;
    if (m_serial_available) {
        m_delivery->SetSelection(serial_index);
    } else {
        m_delivery->Enable(serial_index, false);
        m_delivery->SetSelection(0);
    }
    top->Add(m_delivery, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

    auto* delivery_hint = new wxStaticText(this, wxID_ANY,
        m_serial_available
            ? _L("Prompts appear on the printer either way. The serial route "
                 "needs \"Serial Printing Screen\" turned off and shows live "
                 "progress here; the G-code routes run as an ordinary print job.")
            : _L("No printer was found on USB serial, so that route is "
                 "unavailable. Connect the printer over USB and reopen this "
                 "dialog to enable it. The G-code routes run the same procedure "
                 "as an ordinary print job, with the same on-screen prompts."));
    wxFont dh_font = delivery_hint->GetFont();
    dh_font.SetPointSize(dh_font.GetPointSize() - 1);
    delivery_hint->SetFont(dh_font);
    delivery_hint->Wrap(text_width);
    top->Add(delivery_hint, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

    auto* params_box  = new wxStaticBoxSizer(wxVERTICAL, this, _L("Parameters"));
    auto* params_grid = new wxFlexGridSizer(4, 2, 8, 12);
    params_grid->AddGrowableCol(1);

    // Nozzle number as printed on the machine: docks are labeled 1–8.
    // tool() converts to the 0-based index the T command expects.
    params_grid->Add(new wxStaticText(this, wxID_ANY, _L("Nozzle (1–8):")),
                     0, wxALIGN_CENTER_VERTICAL);
    m_tool = new wxSpinCtrl(this, wxID_ANY, wxEmptyString,
                            wxDefaultPosition, wxDefaultSize,
                            wxSP_ARROW_KEYS, 1, 8, 1);
    params_grid->Add(m_tool, 0, wxEXPAND);

    // Flush temperature. Buddy clamps settable nozzle targets to 290 on
    // Core One (HEATER_0_MAXTEMP 305 − 15 safety margin); anything higher
    // would make the heat-wait spin forever, so the spinner caps there.
    params_grid->Add(new wxStaticText(this, wxID_ANY, _L("Flush temp (°C):")),
                     0, wxALIGN_CENTER_VERTICAL);
    m_flush_temp = new wxSpinCtrl(this, wxID_ANY, wxEmptyString,
                                  wxDefaultPosition, wxDefaultSize,
                                  wxSP_ARROW_KEYS, 200, 290, 290);
    params_grid->Add(m_flush_temp, 0, wxEXPAND);

    // Pull temperature.
    params_grid->Add(new wxStaticText(this, wxID_ANY, _L("Pull temp (°C):")),
                     0, wxALIGN_CENTER_VERTICAL);
    m_pull_temp = new wxSpinCtrl(this, wxID_ANY, wxEmptyString,
                                 wxDefaultPosition, wxDefaultSize,
                                 wxSP_ARROW_KEYS, 60, 120, 80);
    params_grid->Add(m_pull_temp, 0, wxEXPAND);

    // Helper note. INDX reads nozzle temperature via an IR estimate that runs
    // high vs. the true melt zone, so these are INDX-frame values — don't
    // substitute numbers from thermistor printers (MK4 etc.).
    params_grid->Add(0, 0);
    auto* hint = new wxStaticText(this, wxID_ANY,
        _L("80 is the field-verified pull temp; raise in 5–10 °C steps only "
           "if the plug won't release."));
    wxFont hint_font = hint->GetFont();
    hint_font.SetPointSize(hint_font.GetPointSize() - 1);
    hint->SetFont(hint_font);
    hint->Wrap(text_width - FromDIP(90)); // sits in the value column
    params_grid->Add(hint, 0, wxALIGN_CENTER_VERTICAL);

    params_box->Add(params_grid, 0, wxEXPAND | wxALL, 8);
    top->Add(params_box, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);

    // Standard buttons.
    auto* buttons = CreateStdDialogButtonSizer(wxOK | wxCANCEL);
    if (auto* ok = static_cast<wxButton*>(FindWindow(wxID_OK)))
        ok->SetLabel(_L("Start"));
    top->Add(buttons, 0, wxEXPAND | wxALL, 10);

    SetSizerAndFit(top);
    CenterOnParent();
}

ColdPullDelivery MaintenanceColdPullDialog::delivery() const
{
    // "Upload" is only present in the list when a print host is configured, so
    // map by position rather than by a fixed index.
    const int sel = m_delivery->GetSelection();
    if (sel == 0)
        return ColdPullDelivery::SaveGcode;
    if (m_upload_available && sel == 1)
        return ColdPullDelivery::UploadGcode;
    return ColdPullDelivery::Serial;
}

// The spinner shows the nozzle number printed on the machine (1–8); the
// firmware's T command is 0-based, so hand back the index, not the label.
int MaintenanceColdPullDialog::tool()         const { return m_tool->GetValue() - 1; }
int MaintenanceColdPullDialog::flush_temp_c() const { return m_flush_temp->GetValue(); }
int MaintenanceColdPullDialog::pull_temp_c()  const { return m_pull_temp->GetValue(); }

}} // namespace Slic3r::GUI
