///|/ Copyright (c) 2026
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_MaintenanceColdPullDialog_hpp_
#define slic3r_MaintenanceColdPullDialog_hpp_

#include <wx/dialog.h>
#include <wx/checkbox.h>
#include <wx/radiobox.h>
#include <wx/spinctrl.h>

#include <vector>

namespace Slic3r { namespace GUI {

// Pre-flight gate shown BEFORE anything is sent to the printer. Lists the
// physical/settings prerequisites the host cannot verify or perform itself,
// each as a checkbox; Continue stays disabled until every box is ticked.
//
// This is deliberately a hard gate rather than a wall of text: skipping a
// prerequisite doesn't fail loudly, it fails subtly (an enabled filament
// sensor grabs the filament during hand-insertion), and by then the nozzle is
// already hot. The last item is not an action but consent: auto retract has no
// switch on INDX from firmware 6.9.0, so the procedure suppresses it by marking
// the tool FLEX -- a persistent change to the printer's settings.
class MaintenanceColdPullPreflightDialog : public wxDialog
{
public:
    // for_serial adds the serial-only prerequisite ("Serial Printing Screen"),
    // which does not apply to the G-code print-job routes.
    MaintenanceColdPullPreflightDialog(wxWindow* parent, bool for_serial);

private:
    void update_continue_enabled();

    std::vector<wxCheckBox*> m_checks;
};

// How the procedure reaches the printer.
enum class ColdPullDelivery {
    // Write a G-code file the user runs as an ordinary print job. Preferred:
    // a print job is immune to the serial-print inactivity timeout.
    SaveGcode,
    // Upload the same file to the configured printer (PrusaLink / Connect).
    UploadGcode,
    // Stream commands over USB serial, driving the printer directly.
    Serial,
};

// Configuration dialog shown before running the INDX cold-pull maintenance
// procedure. Collects the delivery method, nozzle number and temperatures,
// and returns them via the accessors after ShowModal() == wxID_OK.
class MaintenanceColdPullDialog : public wxDialog
{
public:
    // upload_available gates the "Upload to printer" choice: it is only
    // offered when a physical printer with a print host is configured.
    //
    // serial_available gates the serial choice: with a printer detected on USB
    // serial it is the default, otherwise that entry is disabled and the
    // G-code file route becomes the default.
    MaintenanceColdPullDialog(wxWindow* parent,
                              bool upload_available,
                              bool serial_available);

    ColdPullDelivery delivery() const;

    // 0-based tool index for the firmware T command. The dialog displays the
    // nozzle number as labeled on the machine (1–8) and converts here.
    int tool()         const;
    int flush_temp_c() const;
    int pull_temp_c()  const;

private:
    wxRadioBox* m_delivery{nullptr};
    wxSpinCtrl* m_tool{nullptr};
    wxSpinCtrl* m_flush_temp{nullptr};
    wxSpinCtrl* m_pull_temp{nullptr};

    bool m_upload_available{false};
    bool m_serial_available{false};
};

}} // namespace Slic3r::GUI

#endif // slic3r_MaintenanceColdPullDialog_hpp_
