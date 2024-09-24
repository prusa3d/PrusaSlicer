///|/ Copyright (c) Prusa Research 2018 - 2023 Oleksandra Iushchenko @YuSanka, Vojtěch Bubník @bubnikv, David Kocík @kocikdav, Enrico Turri @enricoturri1966, Lukáš Matěna @lukasmatena, Tomáš Mészáros @tamasmeszaros, Vojtěch Král @vojtechkral
///|/ Copyright (c) 2019 John Drake @foxox
///|/
///|/ ported from lib/Slic3r/GUI/MainFrame.pm:
///|/ Copyright (c) Prusa Research 2016 - 2019 Vojtěch Bubník @bubnikv, Vojtěch Král @vojtechkral, Oleksandra Iushchenko @YuSanka, Tomáš Mészáros @tamasmeszaros, Enrico Turri @enricoturri1966
///|/ Copyright (c) Slic3r 2014 - 2016 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2014 Mark Hindess
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_MainFrame_hpp_
#define slic3r_MainFrame_hpp_

#include "libslic3r/PrintConfig.hpp"

#include <wx/frame.h>
#include <wx/settings.h>
#include <wx/string.h>
#include <wx/filehistory.h>
#ifdef __APPLE__
#include <wx/taskbar.h>
#endif // __APPLE__

#include <string>
#include <map>

#include "GUI_Utils.hpp"
#include "Event.hpp"
#include "UnsavedChangesDialog.hpp"
#include "Search.hpp"

#include "TopBarMenus.hpp"

class TopBar;
class wxProgressDialog;

namespace Slic3r {

namespace GUI
{

class Tab;
class PrintHostQueueDialog;
class Plater;
class MainFrame;
class PreferencesDialog;
class GalleryDialog;
class ConnectWebViewPanel; 
class PrinterWebViewPanel;

enum QuickSlice
{
    qsUndef = 0,
    qsReslice = 1,
    qsSaveAs = 2,
    qsExportSVG = 4,
    qsExportPNG = 8
};

struct PresetTab {
    std::string       name;
    Tab*              panel;
    PrinterTechnology technology;
};

// ----------------------------------------------------------------------------
// SettingsDialog
// ----------------------------------------------------------------------------

class SettingsDialog : public DPIFrame//DPIDialog
{
    TopBar*         m_tabpanel { nullptr };
    MainFrame*      m_main_frame { nullptr };
    wxMenuBar*      m_menubar{ nullptr };
public:
    SettingsDialog(MainFrame* mainframe);
    ~SettingsDialog() = default;
    void set_tabpanel(TopBar* tabpanel) { m_tabpanel = tabpanel; }
    wxMenuBar* menubar() { return m_menubar; }

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;
};

class MainFrame : public DPIFrame
{
    bool        m_loaded {false};

    wxString    m_qs_last_input_file = wxEmptyString;
    wxString    m_qs_last_output_file = wxEmptyString;
    wxString    m_last_config = wxEmptyString;
    wxMenuBar*  m_menubar{ nullptr };
    TopBarMenus m_bar_menus;

    wxMenuItem* m_menu_item_reslice_now { nullptr };
    wxSizer*    m_main_sizer{ nullptr };

    size_t      m_last_selected_tab;
    Search::OptionsSearcher m_searcher;

    ConnectWebViewPanel* m_connect_webview{ nullptr };
    bool                 m_connect_webview_added{ false };
    PrinterWebViewPanel* m_printer_webview{ nullptr };
    bool                 m_printer_webview_added{ false };

    std::string     get_base_name(const wxString &full_name, const char *extension = nullptr) const;
    std::string     get_dir_name(const wxString &full_name) const;

    void on_presets_changed(SimpleEvent&);

    bool can_start_new_project() const;
    bool can_export_model() const;
    bool can_export_toolpaths() const;
    bool can_export_supports() const;
    bool can_export_gcode() const;
    bool can_send_gcode() const;
	bool can_export_gcode_sd() const;
	bool can_eject() const;
    bool can_slice() const;
    bool can_change_view() const;
    bool can_select() const;
    bool can_deselect() const;
    bool can_delete() const;
    bool can_delete_all() const;
    bool can_reslice() const;

    // MenuBar items changeable in respect to printer technology 
    enum MenuItems
    {                   //   FFF                  SLA
        miExport = 0,   // Export G-code        Export
        miSend,         // Send G-code          Send to print
        miMaterialTab,  // Filament Settings    Material Settings
        miPrinterTab,   // Different bitmap for Printer Settings
        miLogin,
    };

    // vector of a MenuBar items changeable in respect to printer technology 
    std::vector<wxMenuItem*> m_changeable_menu_items;

    wxFileHistory m_recent_projects;

    enum class ESettingsLayout
    {
        Unknown,
        Old,
        Dlg,
        GCodeViewer
    };
    
    ESettingsLayout m_layout{ ESettingsLayout::Unknown };

protected:
    virtual void on_dpi_changed(const wxRect &suggested_rect) override;
    virtual void on_sys_color_changed() override;

public:
    MainFrame(const int font_point_size);
    ~MainFrame() = default;

    void update_layout();
    void update_mode_markers();

	// Called when closing the application and when switching the application language.
	void 		shutdown();

    Plater*     plater() { return m_plater; }
    GalleryDialog* gallery_dialog();

    void        update_title();

    void        set_callbacks_for_topbar_menus();
    void        update_topbars();
    void        init_tabpanel();
    void        create_preset_tabs();
    void        add_created_tab(Tab* panel, const std::string& bmp_name = "");
    bool        is_active_and_shown_tab(Tab* tab);
    // Register Win32 RawInput callbacks (3DConnexion) and removable media insert / remove callbacks.
    // Called from wxEVT_ACTIVATE, as wxEVT_CREATE was not reliable (bug in wxWidgets?).
    void        register_win32_callbacks();
    void        init_menubar_as_editor();
    void        init_menubar_as_gcodeviewer();
    void        update_menubar();
    // Open item in menu by menu and item name (in actual language)
    void        open_menubar_item(const wxString& menu_name,const wxString& item_name);
    void        update_ui_from_settings();
    bool        is_loaded() const { return m_loaded; }
    bool        is_last_input_file() const  { return !m_qs_last_input_file.IsEmpty(); }
    bool        is_dlg_layout() const { return m_layout == ESettingsLayout::Dlg; }

    void        reslice_now();
    void        repair_stl();
    void        export_config();
    // Query user for the config file and open it.
    void        load_config_file();
    // Open a config file. Return true if loaded.
    bool        load_config_file(const std::string &path);
    void        export_configbundle(bool export_physical_printers = false);
    void        load_configbundle(wxString file = wxEmptyString);
    void        load_config(const DynamicPrintConfig& config);
    void        update_search_lines(const std::string search_line);
    // Select tab in m_tabpanel
    // When tab == -1, will be selected last selected tab
    void        select_tab(Tab* tab);
    void        select_tab(size_t tab = size_t(-1));
    void        select_view(const std::string& direction);
    // Propagate changed configuration from the Tab to the Plater and save changes to the AppConfig
    void        on_config_changed(DynamicPrintConfig* cfg) const ;

    bool can_save() const;
    bool can_save_as() const;
    void save_project();
    bool save_project_as(const wxString& filename = wxString());

    void        add_to_recent_projects(const wxString& filename);
    void        technology_changed();

    void    add_connect_webview_tab();
    void    remove_connect_webview_tab();

    void    show_printer_webview_tab(DynamicPrintConfig* dpc);

    void    add_printer_webview_tab(const wxString& url);
    void    remove_printer_webview_tab();
    void    set_printer_webview_tab_url(const wxString& url);
    bool    get_printer_webview_tab_added() const { return m_printer_webview_added; }
    void    set_printer_webview_api_key(const std::string& key);
    void    set_printer_webview_credentials(const std::string& usr, const std::string& psk);

    void    refresh_account_menu(bool avatar = false);

    PrintHostQueueDialog* printhost_queue_dlg() { return m_printhost_queue_dlg; }

    Plater*               m_plater { nullptr };
    TopBar*               m_tmp_top_bar { nullptr };
    TopBar*               m_tabpanel { nullptr };
    SettingsDialog        m_settings_dialog;
    DiffPresetDialog      diff_dialog;
    wxWindow*             m_plater_page{ nullptr };
//    wxProgressDialog*     m_progress_dialog { nullptr };
    PreferencesDialog*    preferences_dialog { nullptr };
    PrintHostQueueDialog* m_printhost_queue_dlg;
    GalleryDialog*        m_gallery_dialog{ nullptr };
    
#ifdef __APPLE__
    std::unique_ptr<wxTaskBarIcon> m_taskbar_icon;
#endif // __APPLE__

#ifdef _WIN32
    void*				m_hDeviceNotify { nullptr };
    uint32_t  			m_ulSHChangeNotifyRegister { 0 };
	static constexpr int WM_USER_MEDIACHANGED { 0x7FFF }; // WM_USER from 0x0400 to 0x7FFF, picking the last one to not interfere with wxWidgets allocation
#endif // _WIN32
};

} // GUI
} //Slic3r

#endif // slic3r_MainFrame_hpp_
