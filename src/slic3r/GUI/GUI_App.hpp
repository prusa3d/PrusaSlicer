///|/ Copyright (c) Prusa Research 2018 - 2023 Vojtěch Bubník @bubnikv, Oleksandra Iushchenko @YuSanka, Tomáš Mészáros @tamasmeszaros, David Kocík @kocikdav, Lukáš Matěna @lukasmatena, Enrico Turri @enricoturri1966, Filip Sykala @Jony01, Lukáš Hejl @hejllukas, Vojtěch Král @vojtechkral
///|/ Copyright (c) 2021 Li Jiang
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_GUI_App_hpp_
#define slic3r_GUI_App_hpp_

#include <memory>
#include <string>
#include "ImGuiWrapper.hpp"
#include "ConfigWizard.hpp"
#include "OpenGLManager.hpp"
#include "libslic3r/Preset.hpp"
#include "I18N.hpp"

#include <wx/app.h>
#include <wx/colour.h>
#include <wx/font.h>
#include <wx/string.h>
#include <wx/snglinst.h>

#include <mutex>
#include <stack>

class wxMenuItem;
class wxMenuBar;
class wxTopLevelWindow;
class wxDataViewCtrl;
class wxBookCtrlBase;
struct wxLanguageInfo;

namespace Slic3r {

class AppConfig;
class PresetBundle;
class PresetUpdaterWrapper;
class ModelObject;
class PrintHostJobQueue;
class Model;
class AppUpdater;

namespace Search {
    class OptionsSearcher;
}

namespace GUI{

class RemovableDriveManager;
class OtherInstanceMessageHandler;
class MainFrame;
class Sidebar;
class ObjectManipulation;
class ObjectSettings;
class ObjectList;
class ObjectLayers;
class Plater;
class NotificationManager;
class Downloader;
struct GUI_InitParams;
class GalleryDialog;
class PresetArchiveDatabase;

enum FileType
{
    FT_STL,
    FT_OBJ,
    FT_OBJECT,
    FT_STEP,
    FT_AMF,
    FT_3MF,
    FT_GCODE,
    FT_MODEL,
    FT_PROJECT,
    FT_FONTS,
    FT_GALLERY,

    FT_INI,
    FT_SVG,

    FT_TEX,

    FT_SL1,

    FT_ZIP,

    FT_SIZE,
};

extern wxString file_wildcards(FileType file_type, const std::string &custom_extension = {});

wxString sla_wildcards(const char *formatid, const std::string& custom_extension);

enum ConfigMenuIDs {
    ConfigMenuWizard,
    ConfigMenuSnapshots,
    ConfigMenuTakeSnapshot,
    ConfigMenuUpdateConf,
    ConfigMenuUpdateApp,
    ConfigMenuDesktopIntegration,
    ConfigMenuPreferences,
    ConfigMenuModeSimple,
    ConfigMenuModeAdvanced,
    ConfigMenuModeExpert,
    ConfigMenuLanguage,
    ConfigMenuFlashFirmware,
    ConfigMenuCnt,
    ConfigMenuWifiConfigFile
};

class Tab;
class ConfigWizard;

static wxString dots("…", wxConvUTF8);

// Does our wxWidgets version support markup?
// https://github.com/prusa3d/PrusaSlicer/issues/4282#issuecomment-634676371
#if wxUSE_MARKUP && wxCHECK_VERSION(3, 1, 1)
    #define SUPPORTS_MARKUP
#endif


// A wrapper class to allow ignoring some known warnings 
// and not bothering users with redundant messages. 
// see https://github.com/prusa3d/PrusaSlicer/issues/12920
class LogGui : public wxLogGui
{
protected:
    void DoLogText(const wxString& msg) override;
    void DoLogRecord(wxLogLevel level, const wxString& msg, const wxLogRecordInfo& info) override;

private:
    bool ignorred_message(const wxString& msg);
};

class GUI_App : public wxApp
{
public:
    enum class EAppMode : unsigned char
    {
        Editor,
        GCodeViewer
    };

private:
    bool            m_initialized { false };
    bool            m_post_initialized { false };
    bool            m_app_conf_exists{ false };
    bool            m_last_app_conf_lower_version{ false };
    EAppMode        m_app_mode{ EAppMode::Editor };
    bool            m_is_recreating_gui{ false };
    bool            m_opengl_initialized{ false };

    wxColour        m_color_label_modified;
    wxColour        m_color_label_sys;
    wxColour        m_color_label_default;
    wxColour        m_color_window_default;
//#ifdef _WIN32
    wxColour        m_color_highlight_label_default;
    wxColour        m_color_hovered_btn_label;
    wxColour        m_color_default_btn_label;
    wxColour        m_color_highlight_default;
    wxColour        m_color_selected_btn_bg;
#ifdef _WIN32
    bool            m_force_colors_update { false };
#endif
    std::vector<std::string>     m_mode_palette;

    wxFont		    m_small_font;
    wxFont		    m_bold_font;
	wxFont			m_normal_font;
	wxFont			m_code_font;
    wxFont		    m_link_font;

    int             m_em_unit; // width of a "m"-symbol in pixels for current system font
                               // Note: for 100% Scale m_em_unit = 10 -> it's a good enough coefficient for a size setting of controls

    std::unique_ptr<wxLocale> 	  m_wxLocale;
    // System language, from locales, owned by wxWidgets.
    const wxLanguageInfo		 *m_language_info_system = nullptr;
    // Best translation language, provided by Windows or OSX, owned by wxWidgets.
    const wxLanguageInfo		 *m_language_info_best   = nullptr;

    OpenGLManager m_opengl_mgr;

    std::unique_ptr<RemovableDriveManager>          m_removable_drive_manager;
    std::unique_ptr<ImGuiWrapper>                   m_imgui;
    std::unique_ptr<PrintHostJobQueue>              m_printhost_job_queue;
	std::unique_ptr<OtherInstanceMessageHandler>    m_other_instance_message_handler;
    std::unique_ptr<AppUpdater>                     m_app_updater;
    std::unique_ptr<wxSingleInstanceChecker>        m_single_instance_checker;
    std::unique_ptr<Downloader>                     m_downloader;
    
    std::string m_instance_hash_string;
	size_t m_instance_hash_int;

    Search::OptionsSearcher* m_searcher{ nullptr };
    LogGui*                  m_log_gui { nullptr };

public:
    bool            OnInit() override;
    bool            initialized() const { return m_initialized; }

    explicit GUI_App(EAppMode mode = EAppMode::Editor);
    ~GUI_App() override;

    EAppMode get_app_mode() const { return m_app_mode; }
    bool is_editor() const { return m_app_mode == EAppMode::Editor; }
    bool is_gcode_viewer() const { return m_app_mode == EAppMode::GCodeViewer; }
    bool is_recreating_gui() const { return m_is_recreating_gui; }
    std::string logo_name() const { return is_editor() ? "PrusaSlicer" : "PrusaSlicer-gcodeviewer"; }

    Search::OptionsSearcher& searcher() noexcept { return *m_searcher; }
    void                     set_searcher(Search::OptionsSearcher* searcher) { m_searcher = searcher; }
    void                     check_and_update_searcher(ConfigOptionMode mode = comExpert);
    void                     jump_to_option(size_t selected);
    void                     jump_to_option(const std::string& opt_key, Preset::Type type, const std::wstring& category);
    // jump to option which is represented by composite key : "opt_key;tab_name"
    void                     jump_to_option(const std::string& composite_key);
    void                     update_search_lines();
    void                     show_search_dialog();

    // To be called after the GUI is fully built up.
    // Process command line parameters cached in this->init_params,
    // load configs, STLs etc.
    void            post_init();
    // If formatted for github, plaintext with OpenGL extensions enclosed into <details>.
    // Otherwise HTML formatted for the system info dialog.
    static std::string get_gl_info(bool for_github);
    wxGLContext*    init_glcontext(wxGLCanvas& canvas);
    bool            init_opengl();

    static unsigned get_colour_approx_luma(const wxColour &colour);
    static bool     dark_mode();
    const wxColour  get_label_default_clr_system();
    const wxColour  get_label_default_clr_modified();
    const std::vector<std::string> get_mode_default_palette();
    void            init_ui_colours();
    void            update_ui_colours_from_appconfig();
    void            update_label_colours();
    // update color mode for window
    void            UpdateDarkUI(wxWindow *window, bool highlited = false, bool just_font = false);
    // update color mode for whole dialog including all children
    void            UpdateDlgDarkUI(wxDialog* dlg, bool just_buttons_update = false);
    // update color mode for DataViewControl
    void            UpdateDVCDarkUI(wxDataViewCtrl* dvc, bool highlited = false);
    // update color mode for panel including all static texts controls
    void            UpdateAllStaticTextDarkUI(wxWindow* parent);
    void            SetWindowVariantForButton(wxButton* btn);
    void            init_fonts();
	void            update_fonts(const MainFrame *main_frame = nullptr);
    void            set_label_clr_modified(const wxColour& clr);
    void            set_label_clr_sys(const wxColour& clr);

    const wxColour& get_label_clr_modified(){ return m_color_label_modified; }
    const wxColour& get_label_clr_sys()     { return m_color_label_sys; }
    const wxColour& get_label_clr_default() { return m_color_label_default; }
    const wxColour& get_window_default_clr(){ return m_color_window_default; }

    const std::string       get_html_bg_color(wxWindow* html_parent);

    const std::string&      get_mode_btn_color(int mode_id);
    std::vector<wxColour>   get_mode_palette();
    void                    set_mode_palette(const std::vector<wxColour> &palette);

//#ifdef _WIN32
    const wxColour& get_label_highlight_clr()   { return m_color_highlight_label_default; }
    const wxColour& get_highlight_default_clr() { return m_color_highlight_default; }
    const wxColour& get_color_hovered_btn_label() { return m_color_hovered_btn_label; }
    const wxColour& get_color_selected_btn_bg() { return m_color_selected_btn_bg; }
    void            force_colors_update();
#ifdef _MSW_DARK_MODE
    void            force_menu_update();
#endif //_MSW_DARK_MODE
//#endif

    const wxFont&   small_font()            { return m_small_font; }
    const wxFont&   bold_font()             { return m_bold_font; }
    const wxFont&   normal_font()           { return m_normal_font; }
    const wxFont&   code_font()             { return m_code_font; }
    const wxFont&   link_font()             { return m_link_font; }
    int             em_unit() const         { return m_em_unit; }
    bool            suppress_round_corners() const;
    wxSize          get_min_size(wxWindow* display_win) const;
    int             get_max_font_pt_size();
    float           toolbar_icon_scale(bool& is_custom) const;
    void            set_auto_toolbar_icon_scale(float scale) const;
    void            check_printer_presets();

    void            recreate_GUI(const wxString& message);
    void            system_info();
    void            keyboard_shortcuts();
    void            load_project(wxWindow *parent, wxString& input_file) const;
    void            import_model(wxWindow *parent, wxArrayString& input_files) const;
    void            import_zip(wxWindow* parent, wxString& input_file) const;
    void            load_gcode(wxWindow* parent, wxString& input_file) const;

    static bool     catch_error(std::function<void()> cb, const std::string& err);

    void            persist_window_geometry(wxTopLevelWindow *window, bool default_maximized = false);
    void            update_ui_from_settings();

    bool            switch_language();
    bool            load_language(wxString language, bool initial);

    Tab*            get_tab(Preset::Type type);
    ConfigOptionMode get_mode();
    bool            save_mode(const /*ConfigOptionMode*/int mode) ;
    void            update_mode();

    wxMenu*         get_config_menu(MainFrame* main_frame);
    bool            has_unsaved_preset_changes() const;
    bool            has_current_preset_changes() const;
    void            update_saved_preset_from_current_preset();
    std::vector<const PresetCollection*> get_active_preset_collections() const;
    bool            check_and_save_current_preset_changes(const wxString& caption, const wxString& header, bool remember_choice = true, bool use_dont_save_insted_of_discard = false);
    void            apply_keeped_preset_modifications();
    bool            check_and_keep_current_preset_changes(const wxString& caption, const wxString& header, int action_buttons, bool* postponed_apply_of_keeped_changes = nullptr);
    bool            can_load_project();
    bool            check_print_host_queue();
    bool            checked_tab(Tab* tab);
    void            load_current_presets(bool check_printer_presets = true);

    wxString        current_language_code() const { return m_wxLocale->GetCanonicalName(); }
	// Translate the language code to a code, for which Prusa Research maintains translations. Defaults to "en_US".
    wxString 		current_language_code_safe() const;
    bool            is_localized() const { return m_wxLocale->GetLocale() != "English"; }

    void            open_preferences(const std::string& highlight_option = std::string(), const std::string& tab_name = std::string());

    virtual bool OnExceptionInMainLoop() override;
    // Calls wxLaunchDefaultBrowser if user confirms in dialog.
    // Add "Rememeber my choice" checkbox to question dialog, when it is forced or a "suppress_hyperlinks" option has empty value
    bool            open_browser_with_warning_dialog(const wxString& url, wxWindow* parent = nullptr, bool force_remember_choice = true, int flags = 0);
    bool            open_login_browser_with_dialog(const wxString& url, wxWindow* parent = nullptr, int flags = 0);
#ifdef __APPLE__
    void            OSXStoreOpenFiles(const wxArrayString &files) override;
    // wxWidgets override to get an event on open files.
    void            MacOpenFiles(const wxArrayString &fileNames) override;
    void            MacOpenURL(const wxString& url) override;
#endif /* __APPLE */

    Sidebar&             sidebar();
    ObjectManipulation*  obj_manipul();
    ObjectSettings*      obj_settings();
    ObjectList*          obj_list();
    ObjectLayers*        obj_layers();
    Plater*              plater();
    const Plater*        plater() const;
    Model&      		 model();
    NotificationManager* notification_manager();
    GalleryDialog *      gallery_dialog();
    Downloader*          downloader();

    // Parameters extracted from the command line to be passed to GUI after initialization.
    GUI_InitParams* init_params { nullptr };

    AppConfig*      app_config{ nullptr };
    PresetBundle*   preset_bundle{ nullptr };
    MainFrame*      mainframe{ nullptr };
    Plater*         plater_{ nullptr };
	PresetUpdaterWrapper*  get_preset_updater_wrapper() { return m_preset_updater_wrapper.get(); }

    wxBookCtrlBase* tab_panel() const ;
    int             extruders_cnt() const;
    int             extruders_edited_cnt() const;

    std::vector<Tab *>      tabs_list;

	RemovableDriveManager* removable_drive_manager() { return m_removable_drive_manager.get(); }
	OtherInstanceMessageHandler* other_instance_message_handler() { return m_other_instance_message_handler.get(); }
    wxSingleInstanceChecker* single_instance_checker() {return m_single_instance_checker.get();}

	void        init_single_instance_checker(const std::string &name, const std::string &path);
	void        set_instance_hash (const size_t hash) { m_instance_hash_int = hash; m_instance_hash_string = std::to_string(hash); }
    std::string get_instance_hash_string ()           { return m_instance_hash_string; }
	size_t      get_instance_hash_int ()              { return m_instance_hash_int; }

    ImGuiWrapper* imgui() { return m_imgui.get(); }

    PrintHostJobQueue& printhost_job_queue() { return *m_printhost_job_queue.get(); }

    void            open_web_page_localized(const std::string &http_address);
    bool            may_switch_to_SLA_preset(const wxString& caption);
    bool            run_wizard(ConfigWizard::RunReason reason, ConfigWizard::StartPage start_page = ConfigWizard::SP_WELCOME);
    void            update_wizard_login_page();
    void            show_desktop_integration_dialog();
    void            show_downloader_registration_dialog();

#if ENABLE_THUMBNAIL_GENERATOR_DEBUG
    // temporary and debug only -> extract thumbnails from selected gcode and save them as png files
    void            gcode_thumbnails_debug();
#endif // ENABLE_THUMBNAIL_GENERATOR_DEBUG

    GLShaderProgram* get_shader(const std::string& shader_name) { return m_opengl_mgr.get_shader(shader_name); }
    GLShaderProgram* get_current_shader() { return m_opengl_mgr.get_current_shader(); }

    bool is_gl_version_greater_or_equal_to(unsigned int major, unsigned int minor) const { return m_opengl_mgr.get_gl_info().is_version_greater_or_equal_to(major, minor); }
    bool is_glsl_version_greater_or_equal_to(unsigned int major, unsigned int minor) const { return m_opengl_mgr.get_gl_info().is_glsl_version_greater_or_equal_to(major, minor); }
    int  GetSingleChoiceIndex(const wxString& message, const wxString& caption, const wxArrayString& choices, int initialSelection);

#ifdef __WXMSW__
    void            associate_3mf_files();
    void            associate_stl_files();
    void            associate_gcode_files();
    void            associate_bgcode_files();
#endif // __WXMSW__


    // URL download - PrusaSlicer gets system call to open prusaslicer:// URL which should contain address of download
    void            start_download(std::string url);

    void            open_wifi_config_dialog(bool forced, const wxString& drive_path = {});
    bool            get_wifi_config_dialog_shown() const { return m_wifi_config_dialog_shown; }
    
    bool            select_printer_from_connect(const std::string& cmd);
    void            select_filament_from_connect(const std::string& cmd);
    void            handle_connect_request_printer_select(const std::string& cmd);
    void            handle_connect_request_printer_select_inner(const std::string& cmd);
    void            show_printer_webview_tab();
    // return true if preset vas invisible and we have to installed it to make it selectable
    bool            select_printer_preset(const Preset* printer_preset);
    bool            select_filament_preset(const Preset* filament_preset, size_t extruder_index);
    void            search_and_select_filaments(const std::string& material, bool avoid_abrasive, size_t extruder_index, std::string& out_message);
    void            handle_script_message(std::string msg) {}
    void            request_model_download(std::string import_json) {}
    void            download_project(std::string project_id) {}
    void            request_project_download(std::string project_id) {}
    void            request_open_project(std::string project_id) {}
    void            request_remove_project(std::string project_id) {}
    void            printables_download_request(const std::string& download_url, const std::string& model_url);
    void            printables_slice_request(const std::string& download_url, const std::string& model_url);
    void            printables_login_request();
    void            open_link_in_printables(const std::string& url);
private:
    bool            on_init_inner();
	void            init_app_config();
    // returns old config path to copy from if such exists,
    // returns an empty string if such config path does not exists or if it cannot be loaded.
    std::string     check_older_app_config(Semver current_version, bool backup);
    void            legacy_app_config_vendor_check();
    void            window_pos_save(wxTopLevelWindow* window, const std::string &name);
    void            window_pos_restore(wxTopLevelWindow* window, const std::string &name, bool default_maximized = false);
    void            window_pos_sanitize(wxTopLevelWindow* window);
    bool            select_language();

    bool            config_wizard_startup();
    // Returns true if the configuration is fine. 
    // Returns true if the configuration is not compatible and the user decided to rather close the slicer instead of reconfiguring.
	bool            check_updates(const bool invoked_automatically);
    void            on_version_read(wxCommandEvent& evt);
    // if the data from version file are already downloaded, shows dialogs to start download of new version of app
    void            app_updater(bool from_user);
    // inititate read of version file online in separate thread
    void            app_version_check(bool from_user);
#if defined(__linux__) && !defined(SLIC3R_DESKTOP_INTEGRATION) 
    void            remove_desktop_files_dialog();
#endif //(__linux__) && !defined(SLIC3R_DESKTOP_INTEGRATION)

    bool                    m_wifi_config_dialog_shown { false };
    bool                    m_wifi_config_dialog_was_declined { false };
    // change to vector of items when adding more items that require update
    //wxMenuItem*    m_login_config_menu_item { nullptr };
    std::map< ConfigMenuIDs, wxMenuItem*> m_config_menu_updatable_items;

    ConfigWizard* m_config_wizard {nullptr};
    std::unique_ptr<PresetUpdaterWrapper> m_preset_updater_wrapper; 
};

DECLARE_APP(GUI_App)

} // GUI
} // Slic3r

#endif // slic3r_GUI_App_hpp_
