///|/ Copyright (c) Prusa Research 2018 - 2023 Oleksandra Iushchenko @YuSanka, Lukáš Matěna @lukasmatena, David Kocík @kocikdav, Vojtěch Bubník @bubnikv, Tomáš Mészáros @tamasmeszaros, Enrico Turri @enricoturri1966, Filip Sykala @Jony01, Lukáš Hejl @hejllukas, Vojtěch Král @vojtechkral
///|/ Copyright (c) 2021 Jason Scurtu @xarbit
///|/ Copyright (c) 2019 John Drake @foxox
///|/
///|/ ported from lib/Slic3r/GUI/MainFrame.pm:
///|/ Copyright (c) Prusa Research 2016 - 2019 Vojtěch Bubník @bubnikv, Vojtěch Král @vojtechkral, Oleksandra Iushchenko @YuSanka, Tomáš Mészáros @tamasmeszaros, Enrico Turri @enricoturri1966
///|/ Copyright (c) Slic3r 2014 - 2016 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2014 Mark Hindess
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "MainFrame.hpp"

#include <wx/panel.h>
#include <wx/notebook.h>
#include <wx/listbook.h>
#include <wx/simplebook.h>
#include <wx/icon.h>
#include <wx/sizer.h>
#include <wx/menu.h>
#include <wx/progdlg.h>
#include <wx/tooltip.h>
#include <wx/accel.h>
//#include <wx/glcanvas.h>
#include <wx/filename.h>
#include <wx/debug.h>
#if wxUSE_SECRETSTORE 
#include <wx/secretstore.h>
#endif

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/log/trivial.hpp>

#include "libslic3r/Print.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/SLAPrint.hpp"
#include "libslic3r/PresetBundle.hpp"

#include "Tab.hpp"
#include "3DScene.hpp"
#include "PrintHostDialogs.hpp"
#include "wxExtensions.hpp"
#include "GUI_ObjectList.hpp"
#include "Mouse3DController.hpp"
#include "RemovableDriveManager.hpp"
#include "InstanceCheck.hpp"
#include "I18N.hpp"
#include "GLCanvas3D.hpp"
#include "Plater.hpp"
#include "../Utils/Process.hpp"
#include "format.hpp"
#include "slic3r/GUI/InstanceCheck.hpp" // IWYU pragma: keep

#include <fstream>
#include <string_view>

#include "GUI_App.hpp"
#include "UnsavedChangesDialog.hpp"
#include "MsgDialog.hpp"
#include "TopBar.hpp"
#include "GUI_Factories.hpp"
#include "GUI_ObjectList.hpp"
#include "GalleryDialog.hpp"
#include "NotificationManager.hpp"
#include "Preferences.hpp"
#include "WebViewDialog.hpp"
#include "UserAccount.hpp"

#ifdef _WIN32
#include <dbt.h>
#include <shlobj.h>
#endif // _WIN32

namespace Slic3r {
namespace GUI {

enum class ERescaleTarget
{
    Mainframe,
    SettingsDialog
};

#ifdef __APPLE__
class PrusaSlicerTaskBarIcon : public wxTaskBarIcon
{
public:
    PrusaSlicerTaskBarIcon(wxTaskBarIconType iconType = wxTBI_DEFAULT_TYPE) : wxTaskBarIcon(iconType) {}
    wxMenu *CreatePopupMenu() override {
        wxMenu *menu = new wxMenu;
        if(wxGetApp().app_config->get("single_instance") == "0") {
            // Only allow opening a new PrusaSlicer instance on OSX if "single_instance" is disabled, 
            // as starting new instances would interfere with the locking mechanism of "single_instance" support.
            append_menu_item(menu, wxID_ANY, _L("Open new instance"), _L("Open a new PrusaSlicer instance"),
            [](wxCommandEvent&) { start_new_slicer(); }, "", nullptr);
        }
        append_menu_item(menu, wxID_ANY, _L("G-code preview") + dots, _L("Open G-code viewer"),
            [](wxCommandEvent&) { start_new_gcodeviewer_open_file(); }, "", nullptr);
        return menu;
    }
};
class GCodeViewerTaskBarIcon : public wxTaskBarIcon
{
public:
    GCodeViewerTaskBarIcon(wxTaskBarIconType iconType = wxTBI_DEFAULT_TYPE) : wxTaskBarIcon(iconType) {}
    wxMenu *CreatePopupMenu() override {
        wxMenu *menu = new wxMenu;
        append_menu_item(menu, wxID_ANY, _L("Open PrusaSlicer"), _L("Open a new PrusaSlicer instance"),
            [](wxCommandEvent&) { start_new_slicer(nullptr, true); }, "", nullptr);
        append_menu_item(menu, wxID_ANY, _L("G-code preview") + dots, _L("Open new G-code viewer"),
            [](wxCommandEvent&) { start_new_gcodeviewer_open_file(); }, "", nullptr);
        return menu;
    }
};
#endif // __APPLE__

// Load the icon either from the exe, or from the ico file.
static wxIcon main_frame_icon(GUI_App::EAppMode app_mode)
{
#if _WIN32
    std::wstring path(size_t(MAX_PATH), wchar_t(0));
    int len = int(::GetModuleFileName(nullptr, path.data(), MAX_PATH));
    if (len > 0 && len < MAX_PATH) {
        path.erase(path.begin() + len, path.end());
        if (app_mode == GUI_App::EAppMode::GCodeViewer) {
            // Only in case the slicer was started with --gcodeviewer parameter try to load the icon from prusa-gcodeviewer.exe
            // Otherwise load it from the exe.
            for (const std::wstring_view exe_name : { std::wstring_view(L"prusa-slicer.exe"), std::wstring_view(L"prusa-slicer-console.exe") })
                if (boost::iends_with(path, exe_name)) {
                    path.erase(path.end() - exe_name.size(), path.end());
                    path += L"prusa-gcodeviewer.exe";
                    break;
                }
        }
    }
    return wxIcon(path, wxBITMAP_TYPE_ICO);
#else // _WIN32
    return wxIcon(Slic3r::var(app_mode == GUI_App::EAppMode::Editor ? "PrusaSlicer_128px.png" : "PrusaSlicer-gcodeviewer_128px.png"), wxBITMAP_TYPE_PNG);
#endif // _WIN32
}

MainFrame::MainFrame(const int font_point_size) :
DPIFrame(NULL, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxDEFAULT_FRAME_STYLE, "mainframe", font_point_size),
    m_printhost_queue_dlg(new PrintHostQueueDialog(this))
    , m_recent_projects(9)
    , m_settings_dialog(this)
    , diff_dialog(this)
{
    // Fonts were created by the DPIFrame constructor for the monitor, on which the window opened.
    wxGetApp().update_fonts(this);
/*
#ifndef __WXOSX__ // Don't call SetFont under OSX to avoid name cutting in ObjectList 
    this->SetFont(this->normal_font());
#endif
    // Font is already set in DPIFrame constructor
*/

#ifdef __APPLE__
    // Initialize the docker task bar icon.
    switch (wxGetApp().get_app_mode()) {
    default:
    case GUI_App::EAppMode::Editor:
        m_taskbar_icon = std::make_unique<PrusaSlicerTaskBarIcon>(wxTBI_DOCK);
        m_taskbar_icon->SetIcon(wxIcon(Slic3r::var("PrusaSlicer-mac_128px.png"), wxBITMAP_TYPE_PNG), "PrusaSlicer");
        break;
    case GUI_App::EAppMode::GCodeViewer:
        m_taskbar_icon = std::make_unique<GCodeViewerTaskBarIcon>(wxTBI_DOCK);
        m_taskbar_icon->SetIcon(wxIcon(Slic3r::var("PrusaSlicer-gcodeviewer-mac_128px.png"), wxBITMAP_TYPE_PNG), "G-code Viewer");
        break;
    }
#endif // __APPLE__

    // Load the icon either from the exe, or from the ico file.
    SetIcon(main_frame_icon(wxGetApp().get_app_mode()));

    wxGetApp().set_searcher(&m_searcher);

    // initialize tabpanel and menubar
    init_tabpanel();
    if (wxGetApp().is_gcode_viewer())
        init_menubar_as_gcodeviewer();
    else
        init_menubar_as_editor();

#ifndef __APPLE__
    std::vector<wxAcceleratorEntry*>& entries_cache = accelerator_entries_cache();
    assert(entries_cache.size() + 6 < 100);
    wxAcceleratorEntry entries[100];

    int id = 0;
    //for (const auto* entry : entries_cache)
    //    entries[id++].Set(entry->GetFlags(), entry->GetKeyCode(), entry->GetMenuItem()->GetId());

#if _WIN32
    // This is needed on Windows to fake the CTRL+# of the window menu when using the numpad
    entries[id++].Set(wxACCEL_CTRL, WXK_NUMPAD1, wxID_HIGHEST + 1);
    entries[id++].Set(wxACCEL_CTRL, WXK_NUMPAD2, wxID_HIGHEST + 2);
    entries[id++].Set(wxACCEL_CTRL, WXK_NUMPAD3, wxID_HIGHEST + 3);
    entries[id++].Set(wxACCEL_CTRL, WXK_NUMPAD4, wxID_HIGHEST + 4);
    entries[id++].Set(wxACCEL_CTRL, WXK_NUMPAD5, wxID_HIGHEST + 5);
    entries[id++].Set(wxACCEL_CTRL, WXK_NUMPAD6, wxID_HIGHEST + 6);
#endif // _WIN32

    wxAcceleratorTable accel(id, entries);
    SetAcceleratorTable(accel);

    // clear cache with wxAcceleratorEntry, because it's no need anymore
    for (auto entry : entries_cache)
        delete entry;
    entries_cache.clear();
#endif

    // set default tooltip timer in msec
    // SetAutoPop supposedly accepts long integers but some bug doesn't allow for larger values
    // (SetAutoPop is not available on GTK.)
    wxToolTip::SetAutoPop(32767);

    m_loaded = true;

    // initialize layout
    m_main_sizer = new wxBoxSizer(wxVERTICAL);
    wxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(m_main_sizer, 1, wxEXPAND);
    SetSizer(sizer);
    // initialize layout from config
    update_layout();
    sizer->SetSizeHints(this);
    Fit();

    const wxSize min_size = wxGetApp().get_min_size(this);
#ifdef __APPLE__
    // Using SetMinSize() on Mac messes up the window position in some cases
    // cf. https://groups.google.com/forum/#!topic/wx-users/yUKPBBfXWO0
    SetSize(min_size/*wxSize(760, 490)*/);
#else
    SetMinSize(min_size/*wxSize(760, 490)*/);
    SetSize(GetMinSize());
#endif
    Layout();

    update_title();

    // declare events
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) {
        if (event.CanVeto() && m_plater->canvas3D()->get_gizmos_manager().is_in_editing_mode(true)) {
            // prevents to open the save dirty project dialog
            event.Veto();
            return;
        }

        if (m_plater != nullptr) {
            int saved_project = m_plater->save_project_if_dirty(_L("Closing PrusaSlicer. Current project is modified."));
            if (saved_project == wxID_CANCEL) {
                event.Veto();
                return;
            }
            // check unsaved changes only if project wasn't saved
            else if (plater()->is_project_dirty() && saved_project == wxID_NO && event.CanVeto() &&
                     (plater()->is_presets_dirty() && !wxGetApp().check_and_save_current_preset_changes(_L("PrusaSlicer is closing"), _L("Closing PrusaSlicer while some presets are modified.")))) {
                event.Veto();
                return;
            }
        }

        if (event.CanVeto() && !wxGetApp().check_print_host_queue()) {
            event.Veto();
            return;
        }
        this->shutdown();
        // propagate event
        event.Skip();
    });

    //FIXME it seems this method is not called on application start-up, at least not on Windows. Why?
    // The same applies to wxEVT_CREATE, it is not being called on startup on Windows.
    Bind(wxEVT_ACTIVATE, [this](wxActivateEvent& event) {
        if (m_plater != nullptr)
            m_plater->on_activate(event.GetActive());
        event.Skip();
    });

    Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        event.Skip();
#ifdef _WIN32
        // Update window property to mainframe so other instances can indentify it.
        wxGetApp().other_instance_message_handler()->update_windows_properties(this);
#endif //WIN32
        if (m_layout == ESettingsLayout::Dlg || m_layout == ESettingsLayout::Old) {
            if (m_layout == ESettingsLayout::Old)
                m_tabpanel->UpdateSearchSizeAndPosition();
            else
                m_tmp_top_bar->UpdateSearchSizeAndPosition();
        }
    });

    Bind(wxEVT_MOVE, [](wxMoveEvent& event) {
// OSX specific issue:
// When we move application between Retina and non-Retina displays, The legend on a canvas doesn't redraw
// So, redraw explicitly canvas, when application is moved
//FIXME maybe this is useful for __WXGTK3__ as well?
#if __APPLE__
        wxGetApp().plater()->get_current_canvas3D()->set_as_dirty();
        wxGetApp().plater()->get_current_canvas3D()->request_extra_frame();
#endif
        wxGetApp().searcher().update_dialog_position();
        event.Skip();
    });

    wxGetApp().persist_window_geometry(this, true);
    wxGetApp().persist_window_geometry(&m_settings_dialog, true);

    update_ui_from_settings();    // FIXME (?)

    if (m_plater != nullptr) {
#if ENABLE_HACK_GCODEVIEWER_SLOW_ON_MAC
        // When the application is run as GCodeViewer the collapse toolbar is set as enabled, but rendered outside of the screen
        m_plater->get_collapse_toolbar().set_enabled(wxGetApp().is_gcode_viewer() ?
            true : wxGetApp().app_config->get_bool("show_collapse_button"));
#else
        m_plater->get_collapse_toolbar().set_enabled(wxGetApp().app_config->get_bool("show_collapse_button"));
#endif // ENABLE_HACK_GCODEVIEWER_SLOW_ON_MAC
        m_plater->show_action_buttons(true);

        preferences_dialog = new PreferencesDialog(this);
    }

    if (wxGetApp().is_editor()) {
        // jump to found option from SearchDialog
        Bind(wxCUSTOMEVT_JUMP_TO_OPTION, [](wxCommandEvent& evt) { wxGetApp().jump_to_option(evt.GetInt()); });
    }
}

void MainFrame::update_layout()
{
    auto restore_to_creation = [this]() {
        auto clean_sizer = [](wxSizer* sizer) {
            while (!sizer->GetChildren().IsEmpty()) {
                sizer->Detach(0);
            }
        };

        // On Linux m_plater needs to be removed from m_tabpanel before to reparent it
        int plater_page_id = m_tabpanel->FindPage(m_plater);
        if (plater_page_id != wxNOT_FOUND)
            m_tabpanel->RemovePage(plater_page_id);

        if (m_plater->GetParent() != this)
            m_plater->Reparent(this);

        if (m_tabpanel->GetParent() != this)
            m_tabpanel->Reparent(this);

        plater_page_id = (m_plater_page != nullptr) ? m_tabpanel->FindPage(m_plater_page) : wxNOT_FOUND;
        if (plater_page_id != wxNOT_FOUND) {
            m_tabpanel->DeletePage(plater_page_id);
            m_plater_page = nullptr;
        }

        clean_sizer(m_main_sizer);
        clean_sizer(m_settings_dialog.GetSizer());

        if (m_settings_dialog.IsShown())
            m_settings_dialog.Close();

        m_tabpanel->Hide();
        m_tmp_top_bar->Hide();
        m_plater->Hide();

        Layout();
    };

    ESettingsLayout layout = wxGetApp().is_gcode_viewer() ? ESettingsLayout::GCodeViewer :
        (wxGetApp().app_config->get_bool("old_settings_layout_mode") ? ESettingsLayout::Old :
         wxGetApp().app_config->get_bool("dlg_settings_layout_mode") ? ESettingsLayout::Dlg : ESettingsLayout::Old);

    if (m_layout == layout)
        return;

    wxBusyCursor busy;

    Freeze();

    // Remove old settings
    if (m_layout != ESettingsLayout::Unknown)
        restore_to_creation();

#ifdef __WXMSW__
    enum class State {
        noUpdate,
        fromDlg,
        toDlg
    };
    State update_scaling_state = //m_layout == ESettingsLayout::Unknown   ? State::noUpdate   : // don't scale settings dialog from the application start
                                 m_layout == ESettingsLayout::Dlg       ? State::fromDlg    :
                                 layout   == ESettingsLayout::Dlg       ? State::toDlg      : State::noUpdate;
#endif //__WXMSW__

    m_layout = layout;

    // From the very beginning the Print settings should be selected
    m_last_selected_tab = m_layout == ESettingsLayout::Dlg ? 0 : 1;

    // Set new settings
    switch (m_layout)
    {
    case ESettingsLayout::Unknown:
    {
        break;
    }
    case ESettingsLayout::Old:
    {
        m_plater->Reparent(m_tabpanel);
        m_plater->Layout();

        m_main_sizer->Add(m_tabpanel, 1, wxEXPAND | wxTOP, 1);
        m_plater->Show();
        m_tabpanel->ShowFull();
        m_tmp_top_bar->Hide();
        break;
    }
    case ESettingsLayout::Dlg:
    {
        const int sel = m_tabpanel->GetSelection();

        m_plater->Reparent(this);
        m_main_sizer->Add(m_tmp_top_bar, 0, wxEXPAND | wxTOP, 1);
        m_main_sizer->Add(m_plater, 1, wxEXPAND | wxTOP, 1);
        m_plater->Layout();
        m_tmp_top_bar->ShowFull();
        m_plater->Show();

        m_tabpanel->Reparent(&m_settings_dialog);
        m_tabpanel->SetSelection(sel > 0 ? (sel - 1) : 0);
        m_tabpanel->ShowJustMode();
        m_settings_dialog.GetSizer()->Add(m_tabpanel, 1, wxEXPAND | wxTOP, 2);
        m_settings_dialog.Layout();
        break;
    }
    case ESettingsLayout::GCodeViewer:
    {
        m_main_sizer->Add(m_plater, 1, wxEXPAND);
        m_plater->set_default_bed_shape();
#if ENABLE_HACK_GCODEVIEWER_SLOW_ON_MAC
        m_plater->get_collapse_toolbar().set_enabled(true);
#else
        m_plater->get_collapse_toolbar().set_enabled(false);
#endif // !ENABLE_HACK_GCODEVIEWER_SLOW_ON_MAC
        m_plater->collapse_sidebar(true);
        m_plater->Show();
        break;
    }
    }

#ifdef __WXMSW__
    if (update_scaling_state != State::noUpdate)
    {
        int mainframe_dpi   = get_dpi_for_window(this);
        int dialog_dpi      = get_dpi_for_window(&m_settings_dialog);
        if (mainframe_dpi != dialog_dpi) {
            wxSize oldDPI = update_scaling_state == State::fromDlg ? wxSize(dialog_dpi, dialog_dpi) : wxSize(mainframe_dpi, mainframe_dpi);
            wxSize newDPI = update_scaling_state == State::toDlg   ? wxSize(dialog_dpi, dialog_dpi) : wxSize(mainframe_dpi, mainframe_dpi);

            if (update_scaling_state == State::fromDlg)
                this->enable_force_rescale();
            else
                (&m_settings_dialog)->enable_force_rescale();

            wxWindow* win { nullptr };
            if (update_scaling_state == State::fromDlg)
                win = this;
            else
                win = &m_settings_dialog;

#if wxVERSION_EQUAL_OR_GREATER_THAN(3,1,3)
            m_tabpanel->MSWUpdateOnDPIChange(oldDPI, newDPI);
            win->GetEventHandler()->AddPendingEvent(wxDPIChangedEvent(oldDPI, newDPI));
#else
            win->GetEventHandler()->AddPendingEvent(DpiChangedEvent(EVT_DPI_CHANGED_SLICER, newDPI, win->GetRect()));
#endif // wxVERSION_EQUAL_OR_GREATER_THAN
        }
    }
#endif //__WXMSW__

    if (m_layout == ESettingsLayout::Old)
        m_tabpanel->InsertNewPage(0, m_plater, _L("Plater"), "", true);

    update_topbars();

    Layout();
    Thaw();
}

// Called when closing the application and when switching the application language.
void MainFrame::shutdown()
{
#ifdef _WIN32
	if (m_hDeviceNotify) {
		::UnregisterDeviceNotification(HDEVNOTIFY(m_hDeviceNotify));
		m_hDeviceNotify = nullptr;
	}
 	if (m_ulSHChangeNotifyRegister) {
        SHChangeNotifyDeregister(m_ulSHChangeNotifyRegister);
        m_ulSHChangeNotifyRegister = 0;
 	}
#endif // _WIN32

    if (m_plater != nullptr) {
        m_plater->get_ui_job_worker().cancel_all();

        // Unbinding of wxWidgets event handling in canvases needs to be done here because on MAC,
        // when closing the application using Command+Q, a mouse event is triggered after this lambda is completed,
        // causing a crash
        m_plater->unbind_canvas_event_handlers();

        // Cleanup of canvases' volumes needs to be done here or a crash may happen on some Linux Debian flavours
        // see: https://github.com/prusa3d/PrusaSlicer/issues/3964
        m_plater->reset_canvas_volumes();
    }

    // Weird things happen as the Paint messages are floating around the windows being destructed.
    // Avoid the Paint messages by hiding the main window.
    // Also the application closes much faster without these unnecessary screen refreshes.
    // In addition, there were some crashes due to the Paint events sent to already destructed windows.
    this->Show(false);

    if (m_settings_dialog.IsShown())
        // call Close() to trigger call to lambda defined into GUI_App::persist_window_geometry()
        m_settings_dialog.Close();

    if (m_plater != nullptr) {
        // Stop the background thread (Windows and Linux).
        // Disconnect from a 3DConnextion driver (OSX).
        m_plater->get_mouse3d_controller().shutdown();
        // Store the device parameter database back to appconfig.
        m_plater->get_mouse3d_controller().save_config(*wxGetApp().app_config);
    }

    // Stop the background thread of the removable drive manager, so that no new updates will be sent to the Plater.
    wxGetApp().removable_drive_manager()->shutdown();
	//stop listening for messages from other instances
	wxGetApp().other_instance_message_handler()->shutdown(this);
    // Save the slic3r.ini.Usually the ini file is saved from "on idle" callback,
    // but in rare cases it may not have been called yet.
    if (wxGetApp().app_config->dirty())
        wxGetApp().app_config->save();
//         if (m_plater)
//             m_plater->print = undef;
//         Slic3r::GUI::deregister_on_request_update_callback();

    // set to null tabs and a plater
    // to avoid any manipulations with them from App->wxEVT_IDLE after of the mainframe closing 
    wxGetApp().tabs_list.clear();
    wxGetApp().plater_ = nullptr;
}

GalleryDialog* MainFrame::gallery_dialog()
{
    if (!m_gallery_dialog)
        m_gallery_dialog = new GalleryDialog(this);
    return m_gallery_dialog;
}

void MainFrame::update_title()
{
    wxString title = wxEmptyString;
    if (m_plater != nullptr) {
        // m_plater->get_project_filename() produces file name including path, but excluding extension.
        // Don't try to remove the extension, it would remove part of the file name after the last dot!
        wxString project = from_path(into_path(m_plater->get_project_filename()).filename());
//        wxString dirty_marker = (!m_plater->model().objects.empty() && m_plater->is_project_dirty()) ? "*" : "";
        wxString dirty_marker = m_plater->is_project_dirty() ? "*" : "";
        if (!dirty_marker.empty() || !project.empty()) {
            if (!dirty_marker.empty() && project.empty())
                project = _L("Untitled");
            title = dirty_marker + project + " - ";
        }
    }

    std::string build_id = SLIC3R_BUILD_ID;
    if (! wxGetApp().is_editor())
        boost::replace_first(build_id, SLIC3R_APP_NAME, GCODEVIEWER_APP_NAME);
    size_t 		idx_plus = build_id.find('+');
    if (idx_plus != build_id.npos) {
    	// Parse what is behind the '+'. If there is a number, then it is a build number after the label, and full build ID is shown.
    	int commit_after_label;
    	if (! boost::starts_with(build_id.data() + idx_plus + 1, "UNKNOWN") && 
            (build_id.at(idx_plus + 1) == '-' || sscanf(build_id.data() + idx_plus + 1, "%d-", &commit_after_label) == 0)) {
    		// It is a release build.
    		build_id.erase(build_id.begin() + idx_plus, build_id.end());    		
#if defined(_WIN32) && ! defined(_WIN64)
    		// People are using 32bit slicer on a 64bit machine by mistake. Make it explicit.
            build_id += " 32 bit";
#endif
    	}
    }

    title += wxString(build_id);
    if (wxGetApp().is_editor())
        title += (" " + _L("based on Slic3r"));

    SetTitle(title);
}

static wxString GetTooltipForSettingsButton(PrinterTechnology pt)
{
    std::string tooltip = _u8L("Switch to Settings") + "\n" + "[" + shortkey_ctrl_prefix() + "2] - " + _u8L("Print Settings Tab") +
                                                       "\n" + "[" + shortkey_ctrl_prefix() + "3] - " + (pt == ptFFF ? _u8L("Filament Settings Tab") : _u8L("Material Settings Tab")) +
                                                       "\n" + "[" + shortkey_ctrl_prefix() + "4] - " + _u8L("Printer Settings Tab");
    return from_u8(tooltip);
}

void MainFrame::update_topbars()
{
    if (wxGetApp().is_gcode_viewer())
        return;

    const bool show_login = !wxGetApp().app_config->has("show_login_button") || wxGetApp().app_config->get_bool("show_login_button");
    m_tmp_top_bar->ShowUserAccount(show_login);
    m_tabpanel->ShowUserAccount(show_login);

    if (!show_login) {
        if (auto user_account = wxGetApp().plater()->get_user_account();
            user_account && user_account->is_logged())
            user_account->do_logout();
    }
}

void MainFrame::set_callbacks_for_topbar_menus()
{
    m_bar_menus.set_workspaces_menu_callbacks(
        []()                             -> int         { return wxGetApp().get_mode(); },
        [](/*ConfigOptionMode*/int mode) -> void        { wxGetApp().save_mode(mode); },
        [](/*ConfigOptionMode*/int mode) -> std::string { return wxGetApp().get_mode_btn_color(mode); }
    );

    m_bar_menus.set_account_menu_callbacks(
        []() -> void { wxGetApp().plater()->act_with_user_account(); },
        [this]() -> void {
            wxString preferences_item = _L("Show Log in button in application top bar");
            wxString msg =
                _L("PrusaSlicer will remember your choice.") + "\n\n" +
                format_wxstr(_L("Visit \"Preferences\" and check \"%1%\"\nto changes your choice."), preferences_item);

            MessageDialog msg_dlg(this, msg, _L("PrusaSlicer: Don't ask me again"), wxOK | wxCANCEL | wxICON_INFORMATION);
            if (msg_dlg.ShowModal() == wxID_OK) {
                wxGetApp().app_config->set("show_login_button", "0");

                m_bar_menus.RemoveHideLoginItem();
                update_topbars();
            }
        },
        []() -> TopBarMenus::UserAccountInfo {
            if (auto user_account = wxGetApp().plater()->get_user_account())
                return { user_account->is_logged(),
                         user_account->get_username(),
                         user_account->get_avatar_path(true) };
            return TopBarMenus::UserAccountInfo();
        }
    );

    // we need "Hide Log in button" menu item only till "show_login_button" wasn't changed
    if (wxGetApp().app_config->has("show_login_button"))
        m_bar_menus.RemoveHideLoginItem();
}

void MainFrame::init_tabpanel()
{
    wxGetApp().update_ui_colours_from_appconfig();

    set_callbacks_for_topbar_menus();

    if (wxGetApp().is_editor()) {
        m_tmp_top_bar = new TopBar(this, &m_bar_menus, [this]() -> void { select_tab(); });
        m_tmp_top_bar->SetFont(Slic3r::GUI::wxGetApp().normal_font());
        m_tmp_top_bar->Hide();
    }

    // wxNB_NOPAGETHEME: Disable Windows Vista theme for the Notebook background. The theme performance is terrible on Windows 10
    // with multiple high resolution displays connected.
    m_tabpanel = new TopBar(this, &m_bar_menus);

    m_tabpanel->SetFont(Slic3r::GUI::wxGetApp().normal_font());
    m_tabpanel->Hide();
    m_settings_dialog.set_tabpanel(m_tabpanel);

    m_tabpanel->Bind(wxEVT_BOOKCTRL_PAGE_CHANGED, [this](wxBookCtrlEvent& e) {
        if (int old_selection = e.GetOldSelection();
            old_selection != wxNOT_FOUND && old_selection < static_cast<int>(m_tabpanel->GetPageCount())) {
            Tab* old_tab = dynamic_cast<Tab*>(m_tabpanel->GetPage(old_selection));
            if (old_tab)
                old_tab->validate_custom_gcodes();
        }

        wxWindow* panel = m_tabpanel->GetCurrentPage();
        Tab* tab = dynamic_cast<Tab*>(panel);

        // There shouldn't be a case, when we try to select a tab, which doesn't support a printer technology
        if (panel == nullptr || (tab != nullptr && !tab->supports_printer_technology(m_plater->printer_technology())))
            return;

        // temporary fix - WebViewPanel is not inheriting from Tab -> would jump to select Plater
        if (panel && !tab)
            return;

        auto& tabs_list = wxGetApp().tabs_list;
        if (tab && std::find(tabs_list.begin(), tabs_list.end(), tab) != tabs_list.end()) {
            // On GTK, the wxEVT_NOTEBOOK_PAGE_CHANGED event is triggered
            // before the MainFrame is fully set up.
            tab->OnActivate();
            m_last_selected_tab = m_tabpanel->GetSelection();
        }
        else
            select_tab(size_t(0)); // select Plater
    });

    m_plater = new Plater(this, this);
    m_plater->Hide();

    wxGetApp().plater_ = m_plater;

    if (wxGetApp().is_editor())
        create_preset_tabs();

    if (m_plater) {
        // load initial config
        auto full_config = wxGetApp().preset_bundle->full_config();
        m_plater->on_config_change(full_config);

        // Show a correct number of filament fields.
        // nozzle_diameter is undefined when SLA printer is selected
        if (full_config.has("nozzle_diameter")) {
            m_plater->sidebar().set_extruders_count(full_config.option<ConfigOptionFloats>("nozzle_diameter")->values.size());
        }

        if (wxGetApp().is_editor())
            m_tmp_top_bar->SetSettingsButtonTooltip(GetTooltipForSettingsButton(m_plater->printer_technology()));
    }
}

#ifdef WIN32
void MainFrame::register_win32_callbacks()
{
    //static GUID GUID_DEVINTERFACE_USB_DEVICE  = { 0xA5DCBF10, 0x6530, 0x11D2, 0x90, 0x1F, 0x00, 0xC0, 0x4F, 0xB9, 0x51, 0xED };
    //static GUID GUID_DEVINTERFACE_DISK        = { 0x53f56307, 0xb6bf, 0x11d0, 0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b };
    //static GUID GUID_DEVINTERFACE_VOLUME      = { 0x71a27cdd, 0x812a, 0x11d0, 0xbe, 0xc7, 0x08, 0x00, 0x2b, 0xe2, 0x09, 0x2f };
    static GUID GUID_DEVINTERFACE_HID           = { 0x4D1E55B2, 0xF16F, 0x11CF, 0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30 };

    // Register USB HID (Human Interface Devices) notifications to trigger the 3DConnexion enumeration.
    DEV_BROADCAST_DEVICEINTERFACE NotificationFilter = { 0 };
    NotificationFilter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
    NotificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    NotificationFilter.dbcc_classguid = GUID_DEVINTERFACE_HID;
    m_hDeviceNotify = ::RegisterDeviceNotification(this->GetHWND(), &NotificationFilter, DEVICE_NOTIFY_WINDOW_HANDLE);

// or register for file handle change?
//      DEV_BROADCAST_HANDLE NotificationFilter = { 0 };
//      NotificationFilter.dbch_size = sizeof(DEV_BROADCAST_HANDLE);
//      NotificationFilter.dbch_devicetype = DBT_DEVTYP_HANDLE;

    // Using Win32 Shell API to register for media insert / removal events.
    LPITEMIDLIST ppidl;
    if (SHGetSpecialFolderLocation(this->GetHWND(), CSIDL_DESKTOP, &ppidl) == NOERROR) {
        SHChangeNotifyEntry shCNE;
        shCNE.pidl       = ppidl;
        shCNE.fRecursive = TRUE;
        // Returns a positive integer registration identifier (ID).
        // Returns zero if out of memory or in response to invalid parameters.
        m_ulSHChangeNotifyRegister = SHChangeNotifyRegister(this->GetHWND(),        // Hwnd to receive notification
            SHCNE_DISKEVENTS,                                                       // Event types of interest (sources)
            SHCNE_MEDIAINSERTED | SHCNE_MEDIAREMOVED,
            //SHCNE_UPDATEITEM,                                                     // Events of interest - use SHCNE_ALLEVENTS for all events
            WM_USER_MEDIACHANGED,                                                   // Notification message to be sent upon the event
            1,                                                                      // Number of entries in the pfsne array
            &shCNE);                                                                // Array of SHChangeNotifyEntry structures that 
                                                                                    // contain the notifications. This array should 
                                                                                    // always be set to one when calling SHChnageNotifyRegister
                                                                                    // or SHChangeNotifyDeregister will not work properly.
        assert(m_ulSHChangeNotifyRegister != 0);    // Shell notification failed
    } else {
        // Failed to get desktop location
        assert(false); 
    }

    {
        static constexpr int device_count = 1;
        RAWINPUTDEVICE devices[device_count] = { 0 };
        // multi-axis mouse (SpaceNavigator, etc.)
        devices[0].usUsagePage = 0x01;
        devices[0].usUsage = 0x08;
        if (! RegisterRawInputDevices(devices, device_count, sizeof(RAWINPUTDEVICE)))
            BOOST_LOG_TRIVIAL(error) << "RegisterRawInputDevices failed";
    }
}
#endif // _WIN32

void MainFrame::create_preset_tabs()
{
    add_created_tab(new TabPrint(m_tabpanel), "cog");
    add_created_tab(new TabFilament(m_tabpanel), "spool");
    add_created_tab(new TabSLAPrint(m_tabpanel), "cog");
    add_created_tab(new TabSLAMaterial(m_tabpanel), "resin");
    add_created_tab(new TabPrinter(m_tabpanel), wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() == ptFFF ? "printer" : "sla_printer");
    
    m_connect_webview = new ConnectWebViewPanel(m_tabpanel);
    m_printer_webview = new PrinterWebViewPanel(m_tabpanel, L"");
    // new created tabs have to be hidden by default
    m_connect_webview->Hide();
    m_printer_webview->Hide();

}

void MainFrame::add_connect_webview_tab()
{
    if (m_connect_webview_added) {
        m_connect_webview->resend_config();
        return;
    }
    // parameters of InsertNewPage (to prevent ambigous overloaded function)
    // insert "Connect" tab to position next to "Printer" tab
    // order of tabs: Plater - Print Settings - Filaments - Printers - Prusa Connect - Prusa Link

    int n = m_tabpanel->FindPage(wxGetApp().get_tab(Preset::TYPE_PRINTER)) + 1;
    wxWindow* page = m_connect_webview;
    const wxString text(L"Prusa Connect");
    const std::string bmp_name = "";
    bool bSelect = false;
    m_tabpanel->InsertNewPage(n, page, text, bmp_name, bSelect);
    m_connect_webview->load_default_url_delayed();
    m_connect_webview_added = true;
}
void MainFrame::remove_connect_webview_tab()
{
    if (!m_connect_webview_added) {
        return;
    }
    int n = m_tabpanel->FindPage(m_connect_webview);
    if (m_tabpanel->GetSelection() == n)
        m_tabpanel->SetSelection(0);
    m_tabpanel->RemovePage(size_t(n));
    m_connect_webview_added = false;
    m_connect_webview->logout();
}

void MainFrame::show_printer_webview_tab(DynamicPrintConfig* dpc)
{
    // if physical printer is selected
    if (dpc && dpc->option<ConfigOptionEnum<PrintHostType>>("host_type")->value != htPrusaConnect) {
        std::string url = dpc->opt_string("print_host");

        if (url.find("http://") != 0 && url.find("https://") != 0) {
            url = "http://" + url;
        }

        // set password / api key
        if (dynamic_cast<const ConfigOptionEnum<AuthorizationType>*>(dpc->option("printhost_authorization_type"))->value == AuthorizationType::atKeyPassword) {
            set_printer_webview_api_key(dpc->opt_string("printhost_apikey"));
        }
        else {
            set_printer_webview_credentials(dpc->opt_string("printhost_user"), dpc->opt_string("printhost_password"));
        }
        // add printer or change url
        if (get_printer_webview_tab_added()) {
            set_printer_webview_tab_url(from_u8(url));
        }
        else {
            add_printer_webview_tab(from_u8(url));
        }
    }
    // if physical printer isn't selected, so delete page from TopBar
    else {
        if (m_tabpanel->GetPageText(m_tabpanel->GetSelection()) == _L("Physical Printer"))
            select_tab(size_t(0));
        remove_printer_webview_tab();
    }
}

void MainFrame::add_printer_webview_tab(const wxString& url)
{
    if (m_printer_webview_added) {
        set_printer_webview_tab_url(url);
        return;
    }
    m_printer_webview_added = true;
    // add as the last (rightmost) panel
    m_tabpanel->AddNewPage(m_printer_webview, _L("Physical Printer"), "");
    m_printer_webview->set_default_url(url);
    m_printer_webview->load_default_url_delayed();
}
void MainFrame::remove_printer_webview_tab()
{
    if (!m_printer_webview_added) {
        return;
    }
    m_printer_webview_added = false;
    m_printer_webview->Hide();
    m_tabpanel->RemovePage(m_tabpanel->FindPage(m_printer_webview));
}
void MainFrame::set_printer_webview_tab_url(const wxString& url)
{
    if (!m_printer_webview_added) {
        add_printer_webview_tab(url);
        return;
    }
    // TODO: this will reset already filled credential when bundle loaded,
    //  what's the reason of clearing credentials here?
    //m_printer_webview->clear();
    m_printer_webview->set_default_url(url);

    if (m_tabpanel->GetSelection() == m_tabpanel->FindPage(m_printer_webview)) {
        m_printer_webview->load_url(url);
    } else {
        m_printer_webview->load_default_url_delayed();
    }
}

void MainFrame::set_printer_webview_api_key(const std::string& key)
{
    m_printer_webview->set_api_key(key);
}
void MainFrame::set_printer_webview_credentials(const std::string& usr, const std::string& psk)
{
    m_printer_webview->set_credentials(usr, psk);
}

void Slic3r::GUI::MainFrame::refresh_account_menu(bool avatar/* = false */)
{
    // Update User name in TopBar
    m_bar_menus.UpdateAccountMenu();

    m_tabpanel->GetTopBarItemsCtrl()->UpdateAccountButton(avatar);
    m_tmp_top_bar->GetTopBarItemsCtrl()->UpdateAccountButton(avatar);
}

void MainFrame::add_created_tab(Tab* panel,  const std::string& bmp_name /*= ""*/)
{
    panel->create_preset_tab();

    const auto printer_tech = wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology();

    if (panel->supports_printer_technology(printer_tech))
        m_tabpanel->AddNewPage(panel, panel->title(), bmp_name);
}

bool MainFrame::is_active_and_shown_tab(Tab* tab)
{
    int page_id = m_tabpanel->FindPage(tab);

    if (m_tabpanel->GetSelection() != page_id)
        return false;

    if (m_layout == ESettingsLayout::Dlg)
        return m_settings_dialog.IsShown();
    
    return true;
}

bool MainFrame::can_start_new_project() const
{
    return m_plater && (!m_plater->get_project_filename(".3mf").IsEmpty() || 
                        GetTitle().StartsWith('*')||
                        wxGetApp().has_current_preset_changes() || 
                        !m_plater->model().objects.empty() );
}

bool MainFrame::can_save() const
{
    return (m_plater != nullptr) &&
        !m_plater->canvas3D()->get_gizmos_manager().is_in_editing_mode(false) &&
        m_plater->is_project_dirty();
}

bool MainFrame::can_save_as() const
{
    return (m_plater != nullptr) &&
        !m_plater->canvas3D()->get_gizmos_manager().is_in_editing_mode(false);
}

void MainFrame::save_project()
{
    save_project_as(m_plater->get_project_filename(".3mf"));
}

bool MainFrame::save_project_as(const wxString& filename)
{
    bool ret = (m_plater != nullptr) ? m_plater->export_3mf(into_path(filename)) : false;
    if (ret) {
        // Make a copy of the active presets for detecting changes in preset values.
        wxGetApp().update_saved_preset_from_current_preset();
        // Save the names of active presets and project specific config into ProjectDirtyStateManager.
        // Reset ProjectDirtyStateManager's state as saved, mark active UndoRedo step as saved with project.
        m_plater->reset_project_dirty_after_save();
    }
    return ret;
}

bool MainFrame::can_export_model() const
{
    return (m_plater != nullptr) && !m_plater->model().objects.empty();
}

bool MainFrame::can_export_toolpaths() const
{
    return (m_plater != nullptr) && (m_plater->printer_technology() == ptFFF) && m_plater->is_preview_shown() && m_plater->is_preview_loaded() && m_plater->has_toolpaths_to_export();
}

bool MainFrame::can_export_supports() const
{
    if ((m_plater == nullptr) || (m_plater->printer_technology() != ptSLA) || m_plater->model().objects.empty())
        return false;

    bool can_export = false;
    const PrintObjects& objects = m_plater->sla_print().objects();
    for (const SLAPrintObject* object : objects)
    {
        if (!object->support_mesh().empty() || !object->pad_mesh().empty())
        {
            can_export = true;
            break;
        }
    }
    return can_export;
}

bool MainFrame::can_export_gcode() const
{
    if (m_plater == nullptr)
        return false;

    if (m_plater->model().objects.empty())
        return false;

    if (m_plater->is_export_gcode_scheduled())
        return false;

    // TODO:: add other filters

    return true;
}

bool MainFrame::can_send_gcode() const
{
    if (m_plater && ! m_plater->model().objects.empty())
        if (const DynamicPrintConfig *cfg = wxGetApp().preset_bundle->physical_printers.get_selected_printer_config(); cfg)
            if (const auto *print_host_opt = cfg->option<ConfigOptionString>("print_host"); print_host_opt)
                return ! print_host_opt->value.empty();
    return false;
}

bool MainFrame::can_export_gcode_sd() const
{
	if (m_plater == nullptr)
		return false;

	if (m_plater->model().objects.empty())
		return false;

	if (m_plater->is_export_gcode_scheduled())
		return false;

	// TODO:: add other filters

	return wxGetApp().removable_drive_manager()->status().has_removable_drives;
}

bool MainFrame::can_eject() const
{
	return wxGetApp().removable_drive_manager()->status().has_eject;
}

bool MainFrame::can_slice() const
{
    bool bg_proc = wxGetApp().app_config->get_bool("background_processing");
    return (m_plater != nullptr) ? !m_plater->model().objects.empty() && !bg_proc : false;
}

bool MainFrame::can_change_view() const
{
    switch (m_layout)
    {
    default:                   { return false; }
    case ESettingsLayout::Dlg: { return true; }
    case ESettingsLayout::Old: { 
        int page_id = m_tabpanel->GetSelection();
        return page_id != wxNOT_FOUND && dynamic_cast<const Slic3r::GUI::Plater*>(m_tabpanel->GetPage((size_t)page_id)) != nullptr;
    }
    case ESettingsLayout::GCodeViewer: { return true; }
    }
}

bool MainFrame::can_select() const
{
    return (m_plater != nullptr) && !m_plater->model().objects.empty();
}

bool MainFrame::can_deselect() const
{
    return (m_plater != nullptr) && !m_plater->is_selection_empty();
}

bool MainFrame::can_delete() const
{
    return (m_plater != nullptr) && !m_plater->is_selection_empty();
}

bool MainFrame::can_delete_all() const
{
    return (m_plater != nullptr) && !m_plater->model().objects.empty();
}

bool MainFrame::can_reslice() const
{
    return (m_plater != nullptr) && !m_plater->model().objects.empty();
}

void MainFrame::on_dpi_changed(const wxRect& suggested_rect)
{
    wxGetApp().update_fonts(this);
    this->SetFont(this->normal_font());

#ifdef _WIN32
    if (m_tmp_top_bar && m_tmp_top_bar->IsShown())
        m_tmp_top_bar->Rescale();
    m_tabpanel->Rescale();
#endif

    // update Plater
    wxGetApp().plater()->msw_rescale();

    // update Tabs
    if (m_layout != ESettingsLayout::Dlg) // Do not update tabs if the Settings are in the separated dialog
        for (auto tab : wxGetApp().tabs_list)
            tab->msw_rescale();

    wxGetApp().searcher().dlg_msw_rescale();

    return; // #ysFIXME - delete_after_testing - It looks like next code is no need any more

    // Workarounds for correct Window rendering after rescale

    /* Even if Window is maximized during moving,
     * first of all we should imitate Window resizing. So:
     * 1. cancel maximization, if it was set
     * 2. imitate resizing
     * 3. set maximization, if it was set
     */
    const bool is_maximized = this->IsMaximized();
    if (is_maximized)
        this->Maximize(false);

    /* To correct window rendering (especially redraw of a status bar)
     * we should imitate window resizing.
     */
    const wxSize& sz = this->GetSize();
    this->SetSize(sz.x + 1, sz.y + 1);
    this->SetSize(sz);

    this->Maximize(is_maximized);
}

void MainFrame::on_sys_color_changed()
{
    wxBusyCursor wait;

    // update label colors in respect to the system mode
    wxGetApp().init_ui_colours();

    if (wxGetApp().is_gcode_viewer()) {
        MenuFactory::sys_color_changed(m_menubar);
        return;
    }

    // but if there are some ui colors in appconfig, they have to be applied
    wxGetApp().update_ui_colours_from_appconfig();
#ifdef __WXMSW__
    wxGetApp().UpdateDarkUI(m_tabpanel);
    wxGetApp().UpdateDarkUI(m_tmp_top_bar);
#endif
    m_tabpanel->OnColorsChanged();
    m_tmp_top_bar->OnColorsChanged();

    // update Plater
    wxGetApp().plater()->sys_color_changed();

    // update Tabs
    for (Tab* tab : wxGetApp().tabs_list)
        tab->sys_color_changed();

    if (m_connect_webview)
        m_connect_webview->sys_color_changed();
    if (m_printer_webview)
        m_printer_webview->sys_color_changed();

    MenuFactory::sys_color_changed(m_menubar);

    this->Refresh();

    wxGetApp().searcher().dlg_sys_color_changed();
}

void MainFrame::update_mode_markers()
{
    // update markers in common mode sizer
    m_tmp_top_bar->UpdateModeMarkers();
    m_tabpanel->UpdateModeMarkers();

    // update mode markers in tabs
    for (auto tab : wxGetApp().tabs_list)
        tab->update_mode_markers();
}

#ifdef _MSC_VER
    // \xA0 is a non-breaking space. It is entered here to spoil the automatic accelerators,
    // as the simple numeric accelerators spoil all numeric data entry.
static const wxString sep = "\t\xA0";
static const wxString sep_space = "\xA0";
#else
static const wxString sep = " - ";
static const wxString sep_space = "";
#endif

static void append_about_menu_item(wxMenu* target_menu, int insert_pos = wxNOT_FOUND)
{
    if (wxGetApp().is_editor())
        append_menu_item(target_menu, wxID_ANY, wxString::Format(_L("&About %s"), SLIC3R_APP_NAME), _L("Show about dialog"),
            [](wxCommandEvent&) { Slic3r::GUI::about(); }, nullptr, nullptr, []() {return true; }, nullptr, insert_pos);
    else
        append_menu_item(target_menu, wxID_ANY, wxString::Format(_L("&About %s"), GCODEVIEWER_APP_NAME), _L("Show about dialog"),
            [](wxCommandEvent&) { Slic3r::GUI::about(); }, nullptr, nullptr, []() {return true; }, nullptr, insert_pos);
}

#ifdef __APPLE__
static void init_macos_application_menu(wxMenuBar* menu_bar, MainFrame* main_frame)
{
    wxMenu* apple_menu = menu_bar->OSXGetAppleMenu();
    if (apple_menu != nullptr) {
        append_about_menu_item(apple_menu, 0);

        // This fixes a bug on macOS where the quit command doesn't emit window close events.
        // wx bug: https://trac.wxwidgets.org/ticket/18328
        apple_menu->Bind(wxEVT_MENU, [main_frame](wxCommandEvent&) { main_frame->Close(); }, wxID_EXIT);
    }
}
#endif // __APPLE__

static wxMenu* generate_help_menu()
{
    wxMenu* helpMenu = new wxMenu();

    append_menu_item(helpMenu, wxID_ANY, wxString::Format(_L("%s &Website"), SLIC3R_APP_NAME),
        wxString::Format(_L("Open the %s website in your browser"), SLIC3R_APP_NAME),
        [](wxCommandEvent&) { wxGetApp().open_web_page_localized("https://www.prusa3d.com/slicerweb"); });
    // TRN Item from "Help" menu
    append_menu_item(helpMenu, wxID_ANY, wxString::Format(_L("&Quick Start"), SLIC3R_APP_NAME),
        wxString::Format(_L("Open the %s website in your browser"), SLIC3R_APP_NAME),
        [](wxCommandEvent&) { wxGetApp().open_browser_with_warning_dialog("https://help.prusa3d.com/article/first-print-with-prusaslicer_1753", nullptr, false); });
    // TRN Item from "Help" menu
    append_menu_item(helpMenu, wxID_ANY, wxString::Format(_L("Sample &G-codes and Models"), SLIC3R_APP_NAME),
        wxString::Format(_L("Open the %s website in your browser"), SLIC3R_APP_NAME),
        [](wxCommandEvent&) { wxGetApp().open_browser_with_warning_dialog("https://help.prusa3d.com/article/sample-g-codes_529630", nullptr, false); });
    helpMenu->AppendSeparator();
    append_menu_item(helpMenu, wxID_ANY, _L("Prusa 3D &Drivers"), _L("Open the Prusa3D drivers download page in your browser"),
        [](wxCommandEvent&) { wxGetApp().open_web_page_localized("https://www.prusa3d.com/downloads"); });
    append_menu_item(helpMenu, wxID_ANY, _L("Software &Releases"), _L("Open the software releases page in your browser"),
        [](wxCommandEvent&) { wxGetApp().open_browser_with_warning_dialog("https://github.com/prusa3d/PrusaSlicer/releases", nullptr, false); });
//#        my $versioncheck = $self->_append_menu_item($helpMenu, "Check for &Updates...", "Check for new Slic3r versions", sub{
//#            wxTheApp->check_version(1);
//#        });
//#        $versioncheck->Enable(wxTheApp->have_version_check);
//        append_menu_item(helpMenu, wxID_ANY, wxString::Format(_L("%s &Manual"), SLIC3R_APP_NAME),
//                                             wxString::Format(_L("Open the %s manual in your browser"), SLIC3R_APP_NAME),
//            [this](wxCommandEvent&) { wxGetApp().open_browser_with_warning_dialog("http://manual.slic3r.org/"); });
    helpMenu->AppendSeparator();
    append_menu_item(helpMenu, wxID_ANY, _L("System &Info"), _L("Show system information"),
        [](wxCommandEvent&) { wxGetApp().system_info(); });
    append_menu_item(helpMenu, wxID_ANY, _L("Show &Configuration Folder"), _L("Show user configuration folder (datadir)"),
        [](wxCommandEvent&) { Slic3r::GUI::desktop_open_datadir_folder(); });
    append_menu_item(helpMenu, wxID_ANY, _L("Report an I&ssue"), wxString::Format(_L("Report an issue on %s"), SLIC3R_APP_NAME),
        [](wxCommandEvent&) { wxGetApp().open_browser_with_warning_dialog("https://github.com/prusa3d/slic3r/issues/new", nullptr, false); });
#ifndef __APPLE__
    append_about_menu_item(helpMenu);
#endif // __APPLE__
    append_menu_item(helpMenu, wxID_ANY, _L("Show Tip of the Day")
#if 0//debug
        + "\tCtrl+Shift+T"
#endif
        ,_L("Opens Tip of the day notification in bottom right corner or shows another tip if already opened."),
        [](wxCommandEvent&) { wxGetApp().plater()->get_notification_manager()->push_hint_notification(false); });
    helpMenu->AppendSeparator();
    append_menu_item(helpMenu, wxID_ANY, _L("Keyboard Shortcuts") + sep + "&?", _L("Show the list of the keyboard shortcuts"),
        [](wxCommandEvent&) { wxGetApp().keyboard_shortcuts(); });
#if ENABLE_THUMBNAIL_GENERATOR_DEBUG
    helpMenu->AppendSeparator();
    append_menu_item(helpMenu, wxID_ANY, "DEBUG gcode thumbnails", "DEBUG ONLY - read the selected gcode file and generates png for the contained thumbnails",
        [](wxCommandEvent&) { wxGetApp().gcode_thumbnails_debug(); });
#endif // ENABLE_THUMBNAIL_GENERATOR_DEBUG

    return helpMenu;
}

static void add_common_view_menu_items(wxMenu* view_menu, MainFrame* mainFrame, std::function<bool(void)> can_change_view)
{
    // The camera control accelerators are captured by GLCanvas3D::on_char().
    append_menu_item(view_menu, wxID_ANY, _L("Iso") + sep + "&0", _L("Iso View"), [mainFrame](wxCommandEvent&) { mainFrame->select_view("iso"); },
        "", nullptr, [can_change_view]() { return can_change_view(); }, mainFrame);
    view_menu->AppendSeparator();
    //TRN Main menu: View->Top 
    append_menu_item(view_menu, wxID_ANY, _L("Top") + sep + "&1", _L("Top View"), [mainFrame](wxCommandEvent&) { mainFrame->select_view("top"); },
        "", nullptr, [can_change_view]() { return can_change_view(); }, mainFrame);
    //TRN Main menu: View->Bottom 
    append_menu_item(view_menu, wxID_ANY, _L("Bottom") + sep + "&2", _L("Bottom View"), [mainFrame](wxCommandEvent&) { mainFrame->select_view("bottom"); },
        "", nullptr, [can_change_view]() { return can_change_view(); }, mainFrame);
    append_menu_item(view_menu, wxID_ANY, _L("Front") + sep + "&3", _L("Front View"), [mainFrame](wxCommandEvent&) { mainFrame->select_view("front"); },
        "", nullptr, [can_change_view]() { return can_change_view(); }, mainFrame);
    append_menu_item(view_menu, wxID_ANY, _L("Rear") + sep + "&4", _L("Rear View"), [mainFrame](wxCommandEvent&) { mainFrame->select_view("rear"); },
        "", nullptr, [can_change_view]() { return can_change_view(); }, mainFrame);
    append_menu_item(view_menu, wxID_ANY, _L("Left") + sep + "&5", _L("Left View"), [mainFrame](wxCommandEvent&) { mainFrame->select_view("left"); },
        "", nullptr, [can_change_view]() { return can_change_view(); }, mainFrame);
    append_menu_item(view_menu, wxID_ANY, _L("Right") + sep + "&6", _L("Right View"), [mainFrame](wxCommandEvent&) { mainFrame->select_view("right"); },
        "", nullptr, [can_change_view]() { return can_change_view(); }, mainFrame);
}

void MainFrame::init_menubar_as_editor()
{
#ifdef __APPLE__
    wxMenuBar::SetAutoWindowMenu(false);
#endif

    // File menu
    wxMenu* fileMenu = new wxMenu;
    {
        append_menu_item(fileMenu, wxID_ANY, _L("&New Project") + "\tCtrl+N", _L("Start a new project"),
            [this](wxCommandEvent&) { if (m_plater) m_plater->new_project(); }, "", nullptr,
            [this](){return m_plater != nullptr && can_start_new_project(); }, this);
        append_menu_item(fileMenu, wxID_ANY, _L("&Open Project") + dots + "\tCtrl+O", _L("Open a project file"),
            [this](wxCommandEvent&) { if (m_plater) m_plater->load_project(); }, "open", nullptr,
            [this](){return m_plater != nullptr; }, this);

        wxMenu* recent_projects_menu = new wxMenu();
        wxMenuItem* recent_projects_submenu = append_submenu(fileMenu, recent_projects_menu, wxID_ANY, _L("Recent projects"), "");
        m_recent_projects.UseMenu(recent_projects_menu);
        Bind(wxEVT_MENU, [this](wxCommandEvent& evt) {
            size_t file_id = evt.GetId() - wxID_FILE1;
            wxString filename = m_recent_projects.GetHistoryFile(file_id);
            if (wxFileExists(filename)) {
                if (wxGetApp().can_load_project())
                    m_plater->load_project(filename);
            }
            else
            {
                //wxMessageDialog msg(this, _L("The selected project is no longer available.\nDo you want to remove it from the recent projects list?"), _L("Error"), wxYES_NO | wxYES_DEFAULT);
                MessageDialog msg(this, _L("The selected project is no longer available.\nDo you want to remove it from the recent projects list?"), _L("Error"), wxYES_NO | wxYES_DEFAULT);
                if (msg.ShowModal() == wxID_YES)
                {
                    m_recent_projects.RemoveFileFromHistory(file_id);
                        std::vector<std::string> recent_projects;
                        size_t count = m_recent_projects.GetCount();
                        for (size_t i = 0; i < count; ++i)
                        {
                            recent_projects.push_back(into_u8(m_recent_projects.GetHistoryFile(i)));
                        }
                    wxGetApp().app_config->set_recent_projects(recent_projects);
                }
            }
            }, wxID_FILE1, wxID_FILE9);

        std::vector<std::string> recent_projects = wxGetApp().app_config->get_recent_projects();
        std::reverse(recent_projects.begin(), recent_projects.end());
        for (const std::string& project : recent_projects)
        {
            m_recent_projects.AddFileToHistory(from_u8(project));
        }

        Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& evt) { evt.Enable(m_recent_projects.GetCount() > 0); }, recent_projects_submenu->GetId());

        append_menu_item(fileMenu, wxID_ANY, _L("&Save Project") + "\tCtrl+S", _L("Save current project file"),
            [this](wxCommandEvent&) { save_project(); }, "save", nullptr,
            [this](){return m_plater != nullptr && can_save(); }, this);
#ifdef __APPLE__
        append_menu_item(fileMenu, wxID_ANY, _L("Save Project &as") + dots + "\tCtrl+Shift+S", _L("Save current project file as"),
#else
        append_menu_item(fileMenu, wxID_ANY, _L("Save Project &as") + dots + "\tCtrl+Alt+S", _L("Save current project file as"),
#endif // __APPLE__
            [this](wxCommandEvent&) { save_project_as(); }, "save", nullptr,
            [this](){return m_plater != nullptr && can_save_as(); }, this);

        fileMenu->AppendSeparator();

        wxMenu* import_menu = new wxMenu();
        append_menu_item(import_menu, wxID_ANY, _L("Import STL/3MF/STEP/OBJ/AM&F") + dots + "\tCtrl+I", _L("Load a model"),
            [this](wxCommandEvent&) { if (m_plater) m_plater->add_model(); }, "import_plater", nullptr,
            [this](){return m_plater != nullptr; }, this);
        
        append_menu_item(import_menu, wxID_ANY, _L("Import STL (Imperial Units)"), _L("Load an model saved with imperial units"),
            [this](wxCommandEvent&) { if (m_plater) m_plater->add_model(true); }, "import_plater", nullptr,
            [this](){return m_plater != nullptr; }, this);
        
        append_menu_item(import_menu, wxID_ANY, _L("Import SLA Archive") + dots, _L("Load an SLA archive"),
            [this](wxCommandEvent&) { if (m_plater) m_plater->import_sl1_archive(); }, "import_plater", nullptr,
            [this](){return m_plater != nullptr && m_plater->get_ui_job_worker().is_idle(); }, this);
    
        append_menu_item(import_menu, wxID_ANY, _L("Import ZIP Archive") + dots, _L("Load a ZIP archive"),
            [this](wxCommandEvent&) { if (m_plater) m_plater->import_zip_archive(); }, "import_plater", nullptr,
            [this]() {return m_plater != nullptr; }, this);

        import_menu->AppendSeparator();
        append_menu_item(import_menu, wxID_ANY, _L("Import &Config") + dots + "\tCtrl+L", _L("Load exported configuration file"),
            [this](wxCommandEvent&) { load_config_file(); }, "import_config", nullptr,
            []() {return true; }, this);
        append_menu_item(import_menu, wxID_ANY, _L("Import Config from &Project") + dots +"\tCtrl+Alt+L", _L("Load configuration from project file"),
            [this](wxCommandEvent&) { if (m_plater) m_plater->extract_config_from_project(); }, "import_config", nullptr,
            []() {return true; }, this);
        import_menu->AppendSeparator();
        append_menu_item(import_menu, wxID_ANY, _L("Import Config &Bundle") + dots, _L("Load presets from a bundle"),
            [this](wxCommandEvent&) { load_configbundle(); }, "import_config_bundle", nullptr,
            []() {return true; }, this);
        append_submenu(fileMenu, import_menu, wxID_ANY, _L("&Import"), "");

        wxMenu* export_menu = new wxMenu();
        wxMenuItem* item_export_gcode = append_menu_item(export_menu, wxID_ANY, _L("Export &G-code") + dots + "\tCtrl+G", _L("Export current plate as G-code"),
            [this](wxCommandEvent&) { if (m_plater) m_plater->export_gcode(false); }, "export_gcode", nullptr,
            [this](){return can_export_gcode(); }, this);
        m_changeable_menu_items.push_back(item_export_gcode);
        wxMenuItem* item_send_gcode = append_menu_item(export_menu, wxID_ANY, _L("S&end G-code") + dots + "\tCtrl+Shift+G", _L("Send to print current plate as G-code"),
            [this](wxCommandEvent&) { if (m_plater) m_plater->send_gcode(); }, "export_gcode", nullptr,
            [this](){return can_send_gcode(); }, this);
        m_changeable_menu_items.push_back(item_send_gcode);
		append_menu_item(export_menu, wxID_ANY, _L("Export G-code to SD Card / Flash Drive") + dots + "\tCtrl+U", _L("Export current plate as G-code to SD card / Flash drive"),
			[this](wxCommandEvent&) { if (m_plater) m_plater->export_gcode(true); }, "export_to_sd", nullptr,
			[this]() {return can_export_gcode_sd(); }, this);
        export_menu->AppendSeparator();
        append_menu_item(export_menu, wxID_ANY, _L("Export Plate as &STL/OBJ") + dots, _L("Export current plate as STL/OBJ"),
            [this](wxCommandEvent&) { if (m_plater) m_plater->export_stl_obj(); }, "export_plater", nullptr,
            [this](){return can_export_model(); }, this);
        append_menu_item(export_menu, wxID_ANY, _L("Export Plate as STL/OBJ &Including Supports") + dots, _L("Export current plate as STL/OBJ including supports"),
            [this](wxCommandEvent&) { if (m_plater) m_plater->export_stl_obj(true); }, "export_plater", nullptr,
            [this](){return can_export_supports(); }, this);
        export_menu->AppendSeparator();
        append_menu_item(export_menu, wxID_ANY, _L("Export &Toolpaths as OBJ") + dots, _L("Export toolpaths as OBJ"),
            [this](wxCommandEvent&) { if (m_plater) m_plater->export_toolpaths_to_obj(); }, "export_plater", nullptr,
            [this]() {return can_export_toolpaths(); }, this);
        export_menu->AppendSeparator();
        append_menu_item(export_menu, wxID_ANY, _L("Export &Config") + dots +"\tCtrl+E", _L("Export current configuration to file"),
            [this](wxCommandEvent&) { export_config(); }, "export_config", nullptr,
            []() {return true; }, this);
        append_menu_item(export_menu, wxID_ANY, _L("Export Config &Bundle") + dots, _L("Export all presets to file"),
            [this](wxCommandEvent&) { export_configbundle(); }, "export_config_bundle", nullptr,
            []() {return true; }, this);
        append_menu_item(export_menu, wxID_ANY, _L("Export Config Bundle With Physical Printers") + dots, _L("Export all presets including physical printers to file"),
            [this](wxCommandEvent&) { export_configbundle(true); }, "export_config_bundle", nullptr,
            []() {return true; }, this);
        append_submenu(fileMenu, export_menu, wxID_ANY, _L("&Export"), "");

        wxMenu* convert_menu = new wxMenu();
        append_menu_item(convert_menu, wxID_ANY, _L("Convert ASCII G-code to &binary") + dots, _L("Convert a G-code file from ASCII to binary format"),
            [this](wxCommandEvent&) { if (m_plater != nullptr) m_plater->convert_gcode_to_binary(); }, "convert_file", nullptr,
            []() { return true; }, this);
        append_menu_item(convert_menu, wxID_ANY, _L("Convert binary G-code to &ASCII") + dots, _L("Convert a G-code file from binary to ASCII format"),
            [this](wxCommandEvent&) { if (m_plater != nullptr) m_plater->convert_gcode_to_ascii(); }, "convert_file", nullptr,
            []() { return true; }, this);
        append_submenu(fileMenu, convert_menu, wxID_ANY, _L("&Convert"), "");

		append_menu_item(fileMenu, wxID_ANY, _L("Ejec&t SD Card / Flash Drive") + dots + "\tCtrl+T", _L("Eject SD card / Flash drive after the G-code was exported to it."),
			[this](wxCommandEvent&) { if (m_plater) m_plater->eject_drive(); }, "eject_sd", nullptr,
			[this]() {return can_eject(); }, this);

        fileMenu->AppendSeparator();

        m_menu_item_reslice_now = append_menu_item(fileMenu, wxID_ANY, _L("(Re)Slice No&w") + "\tCtrl+R", _L("Start new slicing process"),
            [this](wxCommandEvent&) { reslice_now(); }, "re_slice", nullptr,
            [this]() { return m_plater != nullptr && can_reslice(); }, this);
        fileMenu->AppendSeparator();
        append_menu_item(fileMenu, wxID_ANY, _L("&Repair STL file") + dots, _L("Automatically repair an STL file"),
            [this](wxCommandEvent&) { repair_stl(); }, "wrench", nullptr,
            []() { return true; }, this);
        fileMenu->AppendSeparator();
        append_menu_item(fileMenu, wxID_ANY, _L("&G-code Preview") + dots, _L("Open G-code viewer"),
            [this](wxCommandEvent&) { start_new_gcodeviewer_open_file(this); }, "", nullptr);
        fileMenu->AppendSeparator();
        #ifdef _WIN32
            append_menu_item(fileMenu, wxID_EXIT, _L("E&xit"), wxString::Format(_L("Exit %s"), SLIC3R_APP_NAME),
        #else
            append_menu_item(fileMenu, wxID_EXIT, _L("&Quit"), wxString::Format(_L("Quit %s"), SLIC3R_APP_NAME),
        #endif
            [this](wxCommandEvent&) { Close(false); }, "exit");
    }

    // Edit menu
    wxMenu* editMenu = nullptr;
    if (m_plater != nullptr)
    {
        editMenu = new wxMenu();
    #ifdef __APPLE__
        // Backspace sign
        wxString hotkey_delete = "\u232b";
    #else
        wxString hotkey_delete = "Del";
    #endif
        append_menu_item(editMenu, wxID_ANY, _L("&Select All") + sep + GUI::shortkey_ctrl_prefix() + sep_space + "A",
            _L("Selects all objects"), [this](wxCommandEvent&) { m_plater->select_all(); },
            "", nullptr, [this](){return can_select(); }, this);
        append_menu_item(editMenu, wxID_ANY, _L("D&eselect All") + sep + "Esc",
            _L("Deselects all objects"), [this](wxCommandEvent&) { m_plater->deselect_all(); },
            "", nullptr, [this](){return can_deselect(); }, this);
        editMenu->AppendSeparator();
        append_menu_item(editMenu, wxID_ANY, _L("&Delete Selected") + sep + hotkey_delete,
            _L("Deletes the current selection"),[this](wxCommandEvent&) { m_plater->remove_selected(); },
            "remove_menu", nullptr, [this](){return can_delete(); }, this);
        append_menu_item(editMenu, wxID_ANY, _L("Delete &All") + sep + GUI::shortkey_ctrl_prefix() + sep_space + hotkey_delete,
            _L("Deletes all objects"), [this](wxCommandEvent&) { m_plater->reset_with_confirm(); },
            "delete_all_menu", nullptr, [this](){return can_delete_all(); }, this);

        editMenu->AppendSeparator();
        append_menu_item(editMenu, wxID_ANY, _L("&Undo") + sep + GUI::shortkey_ctrl_prefix() + sep_space + "Z",
            _L("Undo"), [this](wxCommandEvent&) { m_plater->undo(); },
            "undo_menu", nullptr, [this](){return m_plater->can_undo(); }, this);
        append_menu_item(editMenu, wxID_ANY, _L("&Redo") + sep + GUI::shortkey_ctrl_prefix() + sep_space + "Y",
            _L("Redo"), [this](wxCommandEvent&) { m_plater->redo(); },
            "redo_menu", nullptr, [this](){return m_plater->can_redo(); }, this);

        editMenu->AppendSeparator();
        append_menu_item(editMenu, wxID_ANY, _L("&Copy") + sep + GUI::shortkey_ctrl_prefix() + sep_space + "C",
            _L("Copy selection to clipboard"), [this](wxCommandEvent&) { m_plater->copy_selection_to_clipboard(); },
            "copy_menu", nullptr, [this](){return m_plater->can_copy_to_clipboard(); }, this);
        append_menu_item(editMenu, wxID_ANY, _L("&Paste") + sep + GUI::shortkey_ctrl_prefix() + sep_space + "V",
            _L("Paste clipboard"), [this](wxCommandEvent&) { m_plater->paste_from_clipboard(); },
            "paste_menu", nullptr, [this](){return m_plater->can_paste_from_clipboard(); }, this);
        
        editMenu->AppendSeparator();
#ifdef __APPLE__
        append_menu_item(editMenu, wxID_ANY, _L("Re&load from Disk") + dots + "\tCtrl+Shift+R",
            _L("Reload the plater from disk"), [this](wxCommandEvent&) { m_plater->reload_all_from_disk(); },
            "", nullptr, [this]() {return !m_plater->model().objects.empty(); }, this);
#else
        append_menu_item(editMenu, wxID_ANY, _L("Re&load from Disk") + sep + "F5",
            _L("Reload the plater from disk"), [this](wxCommandEvent&) { m_plater->reload_all_from_disk(); },
            "", nullptr, [this]() {return !m_plater->model().objects.empty(); }, this);
#endif // __APPLE__

        editMenu->AppendSeparator();
        append_menu_item(editMenu, wxID_ANY, _L("Searc&h") + "\tCtrl+F",
            _L("Search in settings"), [](wxCommandEvent&) { wxGetApp().show_search_dialog(); },
            "search", nullptr, []() {return true; }, this);
    }

    // Window menu
    auto windowMenu = new wxMenu();
    {
        if (m_plater) {
            append_menu_item(windowMenu, wxID_HIGHEST + 1, _L("&Plater Tab") + "\tCtrl+1", _L("Show the plater"),
                [this](wxCommandEvent&) { select_tab(size_t(0)); }, "plater", nullptr,
                []() {return true; }, this);
            windowMenu->AppendSeparator();
        }
        append_menu_item(windowMenu, wxID_HIGHEST + 2, _L("P&rint Settings Tab") + "\tCtrl+2", _L("Show the print settings"),
            [this/*, tab_offset*/](wxCommandEvent&) { select_tab(1); }, "cog", nullptr,
            []() {return true; }, this);
        wxMenuItem* item_material_tab = append_menu_item(windowMenu, wxID_HIGHEST + 3, _L("&Filament Settings Tab") + "\tCtrl+3", _L("Show the filament settings"),
            [this/*, tab_offset*/](wxCommandEvent&) { select_tab(2); }, "spool", nullptr,
            []() {return true; }, this);
        m_changeable_menu_items.push_back(item_material_tab);
        wxMenuItem* item_printer_tab = append_menu_item(windowMenu, wxID_HIGHEST + 4, _L("Print&er Settings Tab") + "\tCtrl+4", _L("Show the printer settings"),
            [this/*, tab_offset*/](wxCommandEvent&) { select_tab(3); }, "printer", nullptr,
            []() {return true; }, this);
        m_changeable_menu_items.push_back(item_printer_tab);
        if (m_plater) {
            windowMenu->AppendSeparator();
            append_menu_item(windowMenu, wxID_HIGHEST + 5, _L("3&D") + "\tCtrl+5", _L("Show the 3D editing view"),
                [this](wxCommandEvent&) { m_plater->select_view_3D("3D"); }, "editor_menu", nullptr,
                [this](){return can_change_view(); }, this);
            append_menu_item(windowMenu, wxID_HIGHEST + 6, _L("Pre&view") + "\tCtrl+6", _L("Show the 3D slices preview"),
                [this](wxCommandEvent&) { m_plater->select_view_3D("Preview"); }, "preview_menu", nullptr,
                [this](){return can_change_view(); }, this);
        }

        windowMenu->AppendSeparator();
        append_menu_item(windowMenu, wxID_ANY, _L("Shape Gallery"), _L("Open the dialog to modify shape gallery"),
            [this](wxCommandEvent&) {
                if (gallery_dialog()->show(true) == wxID_OK) {
                    wxArrayString input_files;
                    m_gallery_dialog->get_input_files(input_files);
                    if (!input_files.IsEmpty())
                        m_plater->sidebar().obj_list()->load_shape_object_from_gallery(input_files);
                }
            }, "shape_gallery", nullptr, []() {return true; }, this);
        
        windowMenu->AppendSeparator();
        append_menu_item(windowMenu, wxID_ANY, _L("Print &Host Upload Queue") + "\tCtrl+J", _L("Display the Print Host Upload Queue window"),
            [this](wxCommandEvent&) { m_printhost_queue_dlg->Show(); }, "upload_queue", nullptr, []() {return true; }, this);
        
        windowMenu->AppendSeparator();
        append_menu_item(windowMenu, wxID_ANY, _L("Open New Instance") + "\tCtrl+Shift+I", _L("Open a new PrusaSlicer instance"),
            [](wxCommandEvent&) { start_new_slicer(); }, "", nullptr, [this]() {return m_plater != nullptr && !wxGetApp().app_config->get_bool("single_instance"); }, this);

        windowMenu->AppendSeparator();
        append_menu_item(windowMenu, wxID_ANY, _L("Compare Presets")/* + "\tCtrl+F"*/, _L("Compare presets"), 
            [this](wxCommandEvent&) { diff_dialog.show();}, "compare", nullptr, []() {return true; }, this);
    }

    // View menu
    wxMenu* viewMenu = nullptr;
    if (m_plater) {
        viewMenu = new wxMenu();
        add_common_view_menu_items(viewMenu, this, std::bind(&MainFrame::can_change_view, this));
        viewMenu->AppendSeparator();
        append_menu_check_item(viewMenu, wxID_ANY, _L("Show &Labels") + sep + "E", _L("Show object/instance labels in 3D scene"),
            [this](wxCommandEvent&) { m_plater->show_view3D_labels(!m_plater->are_view3D_labels_shown()); }, this,
            [this]() { return m_plater->is_view3D_shown(); }, [this]() { return m_plater->are_view3D_labels_shown(); }, this);
        append_menu_check_item(viewMenu, wxID_ANY, _L("Show Legen&d") + sep + "L", _L("Show legend in preview"),
            [this](wxCommandEvent&) { m_plater->show_legend(!m_plater->is_legend_shown()); }, this,
            [this]() { return m_plater->is_preview_shown(); }, [this]() { return m_plater->is_legend_shown(); }, this);
        append_menu_check_item(viewMenu, wxID_ANY, _L("&Collapse Sidebar") + sep + "Shift+" + sep_space + "Tab", _L("Collapse sidebar"),
            [this](wxCommandEvent&) { m_plater->collapse_sidebar(!m_plater->is_sidebar_collapsed()); }, this,
            []() { return true; }, [this]() { return m_plater->is_sidebar_collapsed(); }, this);
#ifndef __APPLE__
        // OSX adds its own menu item to toggle fullscreen.
        append_menu_check_item(viewMenu, wxID_ANY, _L("&Fullscreen") + "\t" + "F11", _L("Fullscreen"),
            [this](wxCommandEvent&) { this->ShowFullScreen(!this->IsFullScreen(), 
                // wxFULLSCREEN_ALL: wxFULLSCREEN_NOMENUBAR | wxFULLSCREEN_NOTOOLBAR | wxFULLSCREEN_NOSTATUSBAR | wxFULLSCREEN_NOBORDER | wxFULLSCREEN_NOCAPTION
                wxFULLSCREEN_NOSTATUSBAR | wxFULLSCREEN_NOBORDER | wxFULLSCREEN_NOCAPTION); }, 
            this, []() { return true; }, [this]() { return this->IsFullScreen(); }, this);
#endif // __APPLE__
    }

    // Help menu
    auto helpMenu = generate_help_menu();

#if 0//ndef__APPLE__
    // append menus for Menu button from TopBar

    m_bar_menus.AppendMenuItem(fileMenu, _L("&File"));
    if (editMenu) 
        m_bar_menus.AppendMenuItem(editMenu, _L("&Edit"));

    m_bar_menus.AppendMenuSeparaorItem();

    m_bar_menus.AppendMenuItem(windowMenu, _L("&Window"));
    if (viewMenu) 
        m_bar_menus.AppendMenuItem(viewMenu, _L("&View"));
    
    m_bar_menus.AppendMenuItem(wxGetApp().get_config_menu(this), _L("&Configuration"));

    m_bar_menus.AppendMenuSeparaorItem();

    m_bar_menus.AppendMenuItem(helpMenu, _L("&Help"));

#else

    // menubar
    // assign menubar to frame after appending items, otherwise special items
    // will not be handled correctly
    m_menubar = new wxMenuBar();
    m_menubar->SetFont(this->normal_font());
    m_menubar->Append(fileMenu, _L("&File"));
    if (editMenu) m_menubar->Append(editMenu, _L("&Edit"));
    m_menubar->Append(windowMenu, _L("&Window"));
    if (viewMenu) m_menubar->Append(viewMenu, _L("&View"));
    // Add additional menus from C++
    m_menubar->Append(wxGetApp().get_config_menu(this), _L("&Configuration"));
    m_menubar->Append(helpMenu, _L("&Help"));

    SetMenuBar(m_menubar);

#ifdef __APPLE__
    init_macos_application_menu(m_menubar, this);
#endif // __APPLE__

#endif

    if (plater()->printer_technology() == ptSLA)
        update_menubar();
}

void MainFrame::open_menubar_item(const wxString& menu_name,const wxString& item_name)
{
    if (m_menubar == nullptr)
        return;
    // Get menu object from menubar
    int     menu_index = m_menubar->FindMenu(menu_name);
    wxMenu* menu       = m_menubar->GetMenu(menu_index);
    if (menu == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "Mainframe open_menubar_item function couldn't find menu: " << menu_name;
        return;
    }
    // Get item id from menu
    int     item_id   = menu->FindItem(item_name);
    if (item_id == wxNOT_FOUND)
    {
        // try adding three dots char
        item_id = menu->FindItem(item_name + dots);
    }
    if (item_id == wxNOT_FOUND)
    {
        BOOST_LOG_TRIVIAL(error) << "Mainframe open_menubar_item function couldn't find item: " << item_name;
        return;
    }
    // wxEVT_MENU will trigger item
    wxPostEvent((wxEvtHandler*)menu, wxCommandEvent(wxEVT_MENU, item_id));
}

void MainFrame::init_menubar_as_gcodeviewer()
{
    wxMenu* fileMenu = new wxMenu;
    {
        append_menu_item(fileMenu, wxID_ANY, _L("&Open G-code") + dots + "\tCtrl+O", _L("Open a G-code file"),
            [this](wxCommandEvent&) { if (m_plater != nullptr) m_plater->load_gcode(); }, "open", nullptr,
            [this]() {return m_plater != nullptr; }, this);
#ifdef __APPLE__
        append_menu_item(fileMenu, wxID_ANY, _L("Re&load from Disk") + dots + "\tCtrl+Shift+R",
            _L("Reload the plater from disk"), [this](wxCommandEvent&) { m_plater->reload_gcode_from_disk(); },
            "", nullptr, [this]() { return !m_plater->get_last_loaded_gcode().empty(); }, this);
#else
        append_menu_item(fileMenu, wxID_ANY, _L("Re&load from Disk") + sep + "F5",
            _L("Reload the plater from disk"), [this](wxCommandEvent&) { m_plater->reload_gcode_from_disk(); },
            "", nullptr, [this]() { return !m_plater->get_last_loaded_gcode().empty(); }, this);
#endif // __APPLE__
        fileMenu->AppendSeparator();
        append_menu_item(fileMenu, wxID_ANY, _L("Convert ASCII G-code to &binary") + dots, _L("Convert a G-code file from ASCII to binary format"),
            [this](wxCommandEvent&) { if (m_plater != nullptr) m_plater->convert_gcode_to_binary(); }, "convert_file", nullptr,
            []() { return true; }, this);
        append_menu_item(fileMenu, wxID_ANY, _L("Convert binary G-code to &ASCII") + dots, _L("Convert a G-code file from binary to ASCII format"),
            [this](wxCommandEvent&) { if (m_plater != nullptr) m_plater->convert_gcode_to_ascii(); }, "convert_file", nullptr,
            []() { return true; }, this);
        fileMenu->AppendSeparator();
        append_menu_item(fileMenu, wxID_ANY, _L("Export &Toolpaths as OBJ") + dots, _L("Export toolpaths as OBJ"),
            [this](wxCommandEvent&) { if (m_plater != nullptr) m_plater->export_toolpaths_to_obj(); }, "export_plater", nullptr,
            [this]() {return can_export_toolpaths(); }, this);
        append_menu_item(fileMenu, wxID_ANY, _L("Open &PrusaSlicer") + dots, _L("Open PrusaSlicer"),
            [](wxCommandEvent&) { start_new_slicer(); }, "", nullptr,
            []() { return true; }, this);
        fileMenu->AppendSeparator();
        append_menu_item(fileMenu, wxID_EXIT, _L("&Quit"), wxString::Format(_L("Quit %s"), SLIC3R_APP_NAME),
            [this](wxCommandEvent&) { Close(false); });
    }

    // View menu
    wxMenu* viewMenu = nullptr;
    if (m_plater != nullptr) {
        viewMenu = new wxMenu();
        add_common_view_menu_items(viewMenu, this, std::bind(&MainFrame::can_change_view, this));
        viewMenu->AppendSeparator();
        append_menu_check_item(viewMenu, wxID_ANY, _L("Show Legen&d") + sep + "L", _L("Show legend"),
            [this](wxCommandEvent&) { m_plater->show_legend(!m_plater->is_legend_shown()); }, this,
            [this]() { return m_plater->is_preview_shown(); }, [this]() { return m_plater->is_legend_shown(); }, this);
    }

    // helpmenu
    auto helpMenu = generate_help_menu();

    m_menubar = new wxMenuBar();
    m_menubar->Append(fileMenu, _L("&File"));
    if (viewMenu != nullptr) m_menubar->Append(viewMenu, _L("&View"));
    // Add additional menus from C++
    m_menubar->Append(wxGetApp().get_config_menu(this), _L("&Configuration"));
    m_menubar->Append(helpMenu, _L("&Help"));
    SetMenuBar(m_menubar);

#ifdef __APPLE__
    init_macos_application_menu(m_menubar, this);
#endif // __APPLE__
}

void MainFrame::update_menubar()
{
    if (wxGetApp().is_gcode_viewer())
        return;

    const bool is_fff = plater()->printer_technology() == ptFFF;

    m_changeable_menu_items[miExport]       ->SetItemLabel((is_fff ? _L("Export &G-code")         : _L("E&xport"))        + dots    + "\tCtrl+G");
    m_changeable_menu_items[miSend]         ->SetItemLabel((is_fff ? _L("S&end G-code")           : _L("S&end to print")) + dots    + "\tCtrl+Shift+G");

    m_changeable_menu_items[miMaterialTab]  ->SetItemLabel((is_fff ? _L("&Filament Settings Tab") : _L("Mate&rial Settings Tab"))   + "\tCtrl+3");
    m_changeable_menu_items[miMaterialTab]  ->SetBitmap(*get_bmp_bundle(is_fff ? "spool"   : "resin"));

    m_changeable_menu_items[miPrinterTab]   ->SetBitmap(*get_bmp_bundle(is_fff ? "printer" : "sla_printer"));
}


void MainFrame::reslice_now()
{
    if (m_plater)
        m_plater->reslice();
}

void MainFrame::repair_stl()
{
    wxString input_file;
    {
        wxFileDialog dlg(this, _L("Select the STL file to repair:"),
            wxGetApp().app_config->get_last_dir(), "",
            file_wildcards(FT_STL), wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dlg.ShowModal() != wxID_OK)
            return;
        input_file = dlg.GetPath();
    }

    wxString output_file = input_file;
    {
        wxFileDialog dlg( this, L("Save OBJ file (less prone to coordinate errors than STL) as:"),
                                        get_dir_name(output_file), get_base_name(output_file, ".obj"),
                                        file_wildcards(FT_OBJ), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if (dlg.ShowModal() != wxID_OK)
            return;
        output_file = dlg.GetPath();
    }

    Slic3r::TriangleMesh tmesh;
    tmesh.ReadSTLFile(input_file.ToUTF8().data());
    tmesh.WriteOBJFile(output_file.ToUTF8().data());
    Slic3r::GUI::show_info(this, L("Your file was repaired."), L("Repair"));
}

void MainFrame::export_config()
{
    // Generate a cummulative configuration for the selected print, filaments and printer.
    auto config = wxGetApp().preset_bundle->full_config();
    // Validate the cummulative configuration.
    auto valid = config.validate();
    if (! valid.empty()) {
        show_error(this, valid);
        return;
    }
    // Ask user for the file name for the config file.
    wxFileDialog dlg(this, _L("Save configuration as:"),
        !m_last_config.IsEmpty() ? get_dir_name(m_last_config) : wxGetApp().app_config->get_last_dir(),
        !m_last_config.IsEmpty() ? get_base_name(m_last_config) : "config.ini",
        file_wildcards(FT_INI), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    wxString file;
    if (dlg.ShowModal() == wxID_OK)
        file = dlg.GetPath();
    if (!file.IsEmpty()) {
        wxGetApp().app_config->update_config_dir(get_dir_name(file));
        m_last_config = file;
        config.save(file.ToUTF8().data());
    }
}

// Load a config file containing a Print, Filament & Printer preset.
void MainFrame::load_config_file()
{
    if (!wxGetApp().check_and_save_current_preset_changes(_L("Loading of a configuration file"), "", false))
        return;
    wxFileDialog dlg(this, _L("Select configuration to load:"),
        !m_last_config.IsEmpty() ? get_dir_name(m_last_config) : wxGetApp().app_config->get_last_dir(),
        "config.ini", "INI files (*.ini, *.gcode, *.bgcode)|*.ini;*.INI;*.gcode;*.g;*.bgcode;*.bgc", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    wxString file;
    if (dlg.ShowModal() == wxID_OK)
        file = dlg.GetPath();
    if (! file.IsEmpty() && this->load_config_file(file.ToUTF8().data())) {
        DynamicPrintConfig config = wxGetApp().preset_bundle->full_config();
        const auto* post_process = config.opt<ConfigOptionStrings>("post_process");
        if (post_process != nullptr && !post_process->values.empty()) {
            const wxString msg = _L("The selected config file contains a post-processing script.\nPlease review the script carefully before exporting G-code.");
            std::string text;
            for (const std::string& s : post_process->values) {
                text += s;
            }

            InfoDialog msg_dlg(nullptr, msg, from_u8(text), true, wxOK | wxICON_WARNING);
            msg_dlg.set_caption(wxString(SLIC3R_APP_NAME " - ") + _L("Attention!"));
            msg_dlg.ShowModal();
        }

        wxGetApp().app_config->update_config_dir(get_dir_name(file));
        m_last_config = file;
    }
}

// Load a config file containing a Print, Filament & Printer preset from command line.
bool MainFrame::load_config_file(const std::string &path)
{
    try {
        ConfigSubstitutions config_substitutions = wxGetApp().preset_bundle->load_config_file(path, ForwardCompatibilitySubstitutionRule::Enable);
        if (!config_substitutions.empty())
            show_substitutions_info(config_substitutions, path);
    } catch (const std::exception &ex) {
        show_error(this, ex.what());
        return false;
    }

    m_plater->notify_about_installed_presets();
    wxGetApp().load_current_presets();
    return true;
}

void MainFrame::export_configbundle(bool export_physical_printers /*= false*/)
{
    if (!wxGetApp().check_and_save_current_preset_changes(_L("Exporting configuration bundle"),
                                                          _L("Some presets are modified and the unsaved changes will not be exported into configuration bundle."), false, true))
        return;
    // validate current configuration in case it's dirty
    auto err = wxGetApp().preset_bundle->full_config().validate();
    if (! err.empty()) {
        show_error(this, err);
        return;
    }
    // Ask user for a file name.
    wxFileDialog dlg(this, _L("Save presets bundle as:"),
        !m_last_config.IsEmpty() ? get_dir_name(m_last_config) : wxGetApp().app_config->get_last_dir(),
        SLIC3R_APP_KEY "_config_bundle.ini",
        file_wildcards(FT_INI), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    wxString file;
    if (dlg.ShowModal() == wxID_OK)
        file = dlg.GetPath();
    if (!file.IsEmpty()) {
        // Export the config bundle.
#if wxUSE_SECRETSTORE
        bool passwords_to_plain = false;
        bool passwords_dialog_shown = false;
#endif
        // callback function thats going to be passed to preset bundle (so preset bundle doesnt have to include WX secret lib)
        std::function<bool(const std::string&, const std::string&, std::string&)> load_password = [&](const std::string& printer_id, const std::string& opt, std::string& out_psswd)->bool{
            out_psswd = std::string();
#if wxUSE_SECRETSTORE
            // First password prompts user with dialog
            if (!passwords_dialog_shown) {
                wxString msg = _L("Some of the exported printers contain passwords, which are stored in the system password store." 
                                  " Do you want to include the passwords in the plain text form in the exported file?");
                MessageDialog dlg_psswd(this, msg, wxMessageBoxCaptionStr, wxYES_NO | wxYES_DEFAULT | wxICON_QUESTION);
                if (dlg_psswd.ShowModal() == wxID_YES)
                    passwords_to_plain = true;
                passwords_dialog_shown = true;
            }
            if (!passwords_to_plain)
                return false;
            wxSecretStore store = wxSecretStore::GetDefault();
            wxString errmsg;
            if (!store.IsOk(&errmsg)) {
                std::string msg = GUI::format("%1% (%2%).", _u8L("Failed to load credentials from the system password store."), errmsg);
                BOOST_LOG_TRIVIAL(error) << msg;
                show_error(nullptr, msg);
                // Do not try again. System store is not reachable.
                passwords_to_plain = false;
                return false;
            }
            const wxString service = GUI::format_wxstr(L"%1%/PhysicalPrinter/%2%/%3%", SLIC3R_APP_NAME, printer_id, opt);
            wxString username;
            wxSecretValue password;
            if (!store.Load(service, username, password)) {
                std::string msg = GUI::format(_u8L("Failed to load credentials from the system password store for printer %1%."), printer_id);
                BOOST_LOG_TRIVIAL(error) << msg;
                show_error(nullptr, msg);
                return false;
            }
            out_psswd = into_u8(password.GetAsString());
            return true;
#else
            return false;
#endif // wxUSE_SECRETSTORE 
        };

        wxGetApp().app_config->update_config_dir(get_dir_name(file));
        try {
            wxGetApp().preset_bundle->export_configbundle(file.ToUTF8().data(), false, export_physical_printers, load_password);
        } catch (const std::exception &ex) {
			show_error(this, ex.what());
        }
    }
}

// Loading a config bundle with an external file name used to be used
// to auto - install a config bundle on a fresh user account,
// but that behavior was not documented and likely buggy.
void MainFrame::load_configbundle(wxString file/* = wxEmptyString, const bool reset_user_profile*/)
{
    if (!wxGetApp().check_and_save_current_preset_changes(_L("Loading of a configuration bundle"), "", false))
        return;
    if (file.IsEmpty()) {
        wxFileDialog dlg(this, _L("Select configuration to load:"),
            !m_last_config.IsEmpty() ? get_dir_name(m_last_config) : wxGetApp().app_config->get_last_dir(),
            "config.ini", file_wildcards(FT_INI), wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dlg.ShowModal() != wxID_OK)
            return;
        file = dlg.GetPath();
	}

    wxGetApp().app_config->update_config_dir(get_dir_name(file));

    size_t presets_imported = 0;
    PresetsConfigSubstitutions config_substitutions;
    try {
        // Report all substitutions.
        std::tie(config_substitutions, presets_imported) = wxGetApp().preset_bundle->load_configbundle(
            file.ToUTF8().data(), PresetBundle::LoadConfigBundleAttribute::SaveImported, ForwardCompatibilitySubstitutionRule::Enable);
    } catch (const std::exception &ex) {
        show_error(this, ex.what());
        return;
    }

    if (! config_substitutions.empty())
        show_substitutions_info(config_substitutions);

    // Load the currently selected preset into the GUI, update the preset selection box.
	wxGetApp().load_current_presets();

    const auto message = wxString::Format(_L("%d presets successfully imported."), presets_imported);
    Slic3r::GUI::show_info(this, message, wxString("Info"));
}

// Load a provied DynamicConfig into the Print / Filament / Printer tabs, thus modifying the active preset.
// Also update the plater with the new presets.
void MainFrame::load_config(const DynamicPrintConfig& config)
{
	PrinterTechnology printer_technology = wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology();
	const auto       *opt_printer_technology = config.option<ConfigOptionEnum<PrinterTechnology>>("printer_technology");
	if (opt_printer_technology != nullptr && opt_printer_technology->value != printer_technology) {
		printer_technology = opt_printer_technology->value;
		this->plater()->set_printer_technology(printer_technology);
	}
#if 0
	for (auto tab : wxGetApp().tabs_list)
		if (tab->supports_printer_technology(printer_technology)) {
			if (tab->type() == Slic3r::Preset::TYPE_PRINTER)
				static_cast<TabPrinter*>(tab)->update_pages();
			tab->load_config(config);
		}
    if (m_plater)
        m_plater->on_config_change(config);
#else
	// Load the currently selected preset into the GUI, update the preset selection box.
    //FIXME this is not quite safe for multi-extruder printers,
    // as the number of extruders is not adjusted for the vector values.
    // (see PresetBundle::update_multi_material_filament_presets())
    // Better to call PresetBundle::load_config() instead?
    for (auto tab : wxGetApp().tabs_list)
        if (tab->supports_printer_technology(printer_technology)) {
            // Only apply keys, which are present in the tab's config. Ignore the other keys.
			for (const std::string &opt_key : tab->get_config()->diff(config))
				// Ignore print_settings_id, printer_settings_id, filament_settings_id etc.
				if (! boost::algorithm::ends_with(opt_key, "_settings_id"))
					tab->get_config()->option(opt_key)->set(config.option(opt_key));
        }
    
    wxGetApp().load_current_presets();
#endif
}

void MainFrame::update_search_lines(const std::string search_line)
{
    wxString search = from_u8(search_line);
    m_tabpanel   ->UpdateSearch(search);
    m_tmp_top_bar->UpdateSearch(search);
}

void MainFrame::select_tab(Tab* tab)
{
    if (!tab)
        return;
    int page_idx = m_tabpanel->FindPage(tab);
    if (page_idx != wxNOT_FOUND && m_layout == ESettingsLayout::Dlg)
        page_idx++;
    select_tab(size_t(page_idx));
}

void MainFrame::select_tab(size_t tab/* = size_t(-1)*/)
{
    if (!wxGetApp().is_editor())
        return;
    bool tabpanel_was_hidden = false;

    // Controls on page are created on active page of active tab now.
    // We should select/activate tab before its showing to avoid an UI-flickering
    auto select = [this, tab](bool was_hidden) {
        // when tab == -1, it means we should show the last selected tab
        size_t new_selection = tab == (size_t)(-1) ? m_last_selected_tab : (m_layout == ESettingsLayout::Dlg && tab != 0) ? tab - 1 : tab;

        if (m_tabpanel->GetSelection() != (int)new_selection)
            m_tabpanel->SetSelection(new_selection);

        if (tab == 0 && m_layout == ESettingsLayout::Old)
            m_plater->canvas3D()->render();
        else if (was_hidden) {
            Tab* cur_tab = dynamic_cast<Tab*>(m_tabpanel->GetPage(new_selection));
            if (cur_tab)
                cur_tab->OnActivate();
        }
    };

    if (m_layout == ESettingsLayout::Dlg) {
        if (tab==0) {
            if (m_settings_dialog.IsShown())
                this->SetFocus();
            return;
        }
        // Show/Activate Settings Dialog
#ifdef __WXOSX__ // Don't call SetFont under OSX to avoid name cutting in ObjectList
        if (m_settings_dialog.IsShown())
            m_settings_dialog.Hide();
        else
            tabpanel_was_hidden = true;
            
        select(tabpanel_was_hidden);
        m_tabpanel->Show();
        m_settings_dialog.Show();
#else
        if (m_settings_dialog.IsShown()) {
            select(false);
            m_settings_dialog.SetFocus();
        }
        else {
            tabpanel_was_hidden = true;
            select(tabpanel_was_hidden);
            m_tabpanel->Show();
            m_settings_dialog.Show();
        }
#endif
        if (m_settings_dialog.IsIconized())
            m_settings_dialog.Iconize(false);
    }
    else {
        select(false);
    }

    // When we run application in ESettingsLayout::Dlg mode, tabpanel is hidden from the very beginning
    // and as a result Tab::update_changed_tree_ui() function couldn't update m_is_nonsys_values values,
    // which are used for update TreeCtrl and "revert_buttons".
    // So, force the call of this function for Tabs, if tab panel was hidden
    if (tabpanel_was_hidden)
        for (auto cur_tab : wxGetApp().tabs_list)
            cur_tab->update_changed_tree_ui();

    //// when tab == -1, it means we should show the last selected tab
    //size_t new_selection = tab == (size_t)(-1) ? m_last_selected_tab : (m_layout == ESettingsLayout::Dlg && tab != 0) ? tab - 1 : tab;
    //if (m_tabpanel->GetSelection() != new_selection)
    //    m_tabpanel->SetSelection(new_selection);
    //if (tabpanel_was_hidden)
    //    static_cast<Tab*>(m_tabpanel->GetPage(new_selection))->OnActivate();
}

// Set a camera direction, zoom to all objects.
void MainFrame::select_view(const std::string& direction)
{
     if (m_plater)
         m_plater->select_view(direction);
}

// #ys_FIXME_to_delete
void MainFrame::on_presets_changed(SimpleEvent &event)
{
    auto *tab = dynamic_cast<Tab*>(event.GetEventObject());
    wxASSERT(tab != nullptr);
    if (tab == nullptr) {
        return;
    }

    // Update preset combo boxes(Print settings, Filament, Material, Printer) from their respective tabs.
    auto presets = tab->get_presets();
    if (m_plater != nullptr && presets != nullptr) {

        // FIXME: The preset type really should be a property of Tab instead
        Slic3r::Preset::Type preset_type = tab->type();
        if (preset_type == Slic3r::Preset::TYPE_INVALID) {
            wxASSERT(false);
            return;
        }

        m_plater->on_config_change(*tab->get_config());
        m_plater->sidebar().update_presets(preset_type);
    }
}

void MainFrame::on_config_changed(DynamicPrintConfig* config) const
{
    if (m_plater)
        m_plater->on_config_change(*config); // propagate config change events to the plater
}

void MainFrame::add_to_recent_projects(const wxString& filename)
{
    if (wxFileExists(filename))
    {
        m_recent_projects.AddFileToHistory(filename);
        std::vector<std::string> recent_projects;
        size_t count = m_recent_projects.GetCount();
        for (size_t i = 0; i < count; ++i)
        {
            recent_projects.push_back(into_u8(m_recent_projects.GetHistoryFile(i)));
        }
        wxGetApp().app_config->set_recent_projects(recent_projects);
    }
}

void MainFrame::technology_changed()
{
    PrinterTechnology pt = plater()->printer_technology();
    m_tmp_top_bar->SetSettingsButtonTooltip(GetTooltipForSettingsButton(pt));

    if (!m_menubar)
        return;
    // update menu titles
    if (int id = m_menubar->FindMenu(pt == ptFFF ? _L("Material Settings") : _L("Filament Settings")); id != wxNOT_FOUND)
        m_menubar->SetMenuLabel(id , pt == ptSLA ? _L("Material Settings") : _L("Filament Settings"));

    //if (wxGetApp().tab_panel()->GetSelection() != wxGetApp().tab_panel()->GetPageCount() - 1)
    //    wxGetApp().tab_panel()->SetSelection(wxGetApp().tab_panel()->GetPageCount() - 1);

}

//
// Called after the Preferences dialog is closed and the program settings are saved.
// Update the UI based on the current preferences.
void MainFrame::update_ui_from_settings()
{
//    const bool bp_on = wxGetApp().app_config->get_bool("background_processing");
//     m_menu_item_reslice_now->Enable(!bp_on);
//    m_plater->sidebar().show_reslice(!bp_on);
//    m_plater->sidebar().show_export(bp_on);
//    m_plater->sidebar().Layout();

    update_topbars();

    if (m_plater)
        m_plater->update_ui_from_settings();
    for (auto tab: wxGetApp().tabs_list)
        tab->update_ui_from_settings();
}

std::string MainFrame::get_base_name(const wxString &full_name, const char *extension) const 
{
    boost::filesystem::path filename = boost::filesystem::path(full_name.wx_str()).filename();
    if (extension != nullptr)
		filename = filename.replace_extension(extension);
    return filename.string();
}

std::string MainFrame::get_dir_name(const wxString &full_name) const 
{
    return boost::filesystem::path(full_name.wx_str()).parent_path().string();
}


// ----------------------------------------------------------------------------
// SettingsDialog
// ----------------------------------------------------------------------------

SettingsDialog::SettingsDialog(MainFrame* mainframe)
:DPIFrame(NULL, wxID_ANY, wxString(SLIC3R_APP_NAME) + " - " + _L("Settings"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_FRAME_STYLE, "settings_dialog", mainframe->normal_font().GetPointSize()),
//: DPIDialog(mainframe, wxID_ANY, wxString(SLIC3R_APP_NAME) + " - " + _L("Settings"), wxDefaultPosition, wxDefaultSize,
//        wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxMINIMIZE_BOX | wxMAXIMIZE_BOX, "settings_dialog"),
    m_main_frame(mainframe)
{
    if (wxGetApp().is_gcode_viewer())
        return;

    // Load the icon either from the exe, or from the ico file.
#if _WIN32
    {
        TCHAR szExeFileName[MAX_PATH];
        GetModuleFileName(nullptr, szExeFileName, MAX_PATH);
        SetIcon(wxIcon(szExeFileName, wxBITMAP_TYPE_ICO));
    }
#else
    SetIcon(wxIcon(var("PrusaSlicer_128px.png"), wxBITMAP_TYPE_PNG));
#endif // _WIN32

    this->Bind(wxEVT_SHOW, [this](wxShowEvent& evt) {

        auto key_up_handker = [this](wxKeyEvent& evt) {
            if ((evt.GetModifiers() & wxMOD_CONTROL) != 0) {
                switch (evt.GetKeyCode()) {
                case '1': { m_main_frame->select_tab(size_t(0)); break; }
                case '2': { m_main_frame->select_tab(1); break; }
                case '3': { m_main_frame->select_tab(2); break; }
                case '4': { m_main_frame->select_tab(3); break; }
#ifdef __APPLE__
                case 'f':
#else /* __APPLE__ */
                case WXK_CONTROL_F:
#endif /* __APPLE__ */
                case 'F': { wxGetApp().show_search_dialog(); break; }
                default:break;
                }
            }

            evt.Skip();
        };

        if (evt.IsShown()) {
            if (m_tabpanel != nullptr)
                m_tabpanel->Bind(wxEVT_KEY_UP, key_up_handker);
        }
        else {
            if (m_tabpanel != nullptr)
                m_tabpanel->Unbind(wxEVT_KEY_UP, key_up_handker);
        }
        });

    //just hide the Frame on closing
    this->Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& evt) { this->Hide(); });

    this->Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        event.Skip();
        if (m_tabpanel)
            m_tabpanel->UpdateSearchSizeAndPosition();
    });

    // initialize layout
    auto sizer = new wxBoxSizer(wxVERTICAL);
    sizer->SetSizeHints(this);
    SetSizer(sizer);
    Fit();

    const wxSize min_size = wxSize(85 * em_unit(), 50 * em_unit());
#ifdef __APPLE__
    // Using SetMinSize() on Mac messes up the window position in some cases
    // cf. https://groups.google.com/forum/#!topic/wx-users/yUKPBBfXWO0
    SetSize(min_size);
#else
    SetMinSize(min_size);
    SetSize(GetMinSize());
#endif
    Layout();

    Bind(wxEVT_MOVE, [](wxMoveEvent& event) {
        wxGetApp().searcher().update_dialog_position();
        event.Skip();
    });
}

void SettingsDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    if (wxGetApp().is_gcode_viewer())
        return;

// #ysFIXME - delete_after_testing
//    const int& em = em_unit();
//    const wxSize& size = wxSize(85 * em, 50 * em);

#ifdef _WIN32
    m_tabpanel->Rescale();
#endif

    // update Tabs
    for (auto tab : wxGetApp().tabs_list)
        tab->msw_rescale();

// #ysFIXME - delete_after_testing
//    SetMinSize(size);
//    Fit();
//    Refresh();
}


} // GUI
} // Slic3r
