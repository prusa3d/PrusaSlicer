#pragma once

#include "Slic3r/Domain/Workbench.hpp"

#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Platform/ListenerScope.hpp"
#include "Slic3r/Biz/UserAccount/IUserAccountListener.hpp"

#include "Slic3r/App/Platform/WX/WXRenderCanvas.hpp"
#include "Slic3r/App/ILanguageChangedListener.hpp"
#include "Slic3r/Domain/Workbench.hpp"
#include "Slic3r/Domain/Bed.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/App/Desktop/TabsBarMenus.hpp"
#include "Slic3r/App/LeftBarTabs.hpp"
#include "Slic3r/App/IAppConfigChangedListener.hpp"
#include "Slic3r/App/IMenuUpdatedListener.hpp"
#include "Slic3r/Biz/UserAccount/IUserAccountListener.hpp"
#include "Slic3r/Biz/PhysicalPrinter/IPhysicalPrinterChangedListener.hpp"

#include <wx/wx.h>

namespace Slic3r::App::Desktop::Preset {
class AbstractEditor;
} // namespace Slic3r::App::Desktop::Preset

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App {
class Navigator;
class ProjectSaver;

namespace WX {
class MacOSNativeMenuBar;
} // namespace WX
} // namespace Slic3r::App

namespace Slic3r::App::Desktop {

#ifdef WIN32
constexpr int WM_USER_MEDIACHANGED{
    0x7FFF
}; // WM_USER from 0x0400 to 0x7FFF, picking the last one to not interfere with wxWidgets allocation
#endif // WIN32

class LeftBar;

class MainFrame :
    public wxFrame,
    public ILanguageChangedListener,
    public IAppConfigChangedListener,
    public Biz::IProjectsChangedListener,
    public Biz::UserAccount::IUserAccountListener,
    public Biz::PhysicalPrinter::IPhysicalPrinterChangedListener,
    public IMenuUpdatedListener
{
public:
    MainFrame(
        Domain::Workbench& workbench,
        Biz::ProjectInteractor& project_interactor,
        Navigator& navigator,
        std::shared_ptr<ProjectSaver> project_saver
    );
    ~MainFrame();

    Platform::WX::WXRenderCanvas& get_render_canvas()
    {
        return *m_canvas;
    }

    void sys_color_changed();
    bool select_language();

    // set language, font and all other ui settings for canvas
    void update_graphics_settings();

#ifdef WIN32
    // Register Win32 RawInput callbacks (3DConnexion) and removable media insert / remove callbacks.
    // Called from wxEVT_ACTIVATE, as wxEVT_CREATE was not reliable (bug in wxWidgets?).
    void register_win32_callbacks();
#endif // WIN32

    void switch_left_tab(LeftBarTabs id, const std::string& data);

    void on_user_account_enabled_state_changed(bool is_enabled) override;
    void on_project_loaded(Domain::SelectionId project_id) override;
    void on_project_saved(Domain::SelectionId project_id) override;

    void on_selected_physical_printer_changed() override;
    void on_printer_data_changed() override;

    void on_menu_updated() override;
private:
    void init_left_bar(Biz::ProjectInteractor& project_interactor);
    void init_printers_page(Biz::ProjectInteractor& project_interactor);
    void init_physical_printer_page(Biz::ProjectInteractor& project_interactor);
    void init_slicing_page();
    void init_printables_page(Biz::ProjectInteractor& project_interactor);
    void init_preferences_button();
    void complete_and_bind_left_bar();
    void remove_left_bar_page(LeftBarTabs page_id);
    void ensure_slicing_page_selected();
    void update_printables_left_bar(bool printables_enabled);

    void on_language_changed() override;
    void on_app_config_changed(const std::string& key) override;

    void on_close(wxCloseEvent& event);

    // @note next frame related functions use wxTopLevelWindow* window
    // because they can be called for settings non-modal dialog in the feature
    void window_pos_save(wxTopLevelWindow* window, const std::string& name);
    void window_pos_restore(
        wxTopLevelWindow* window,
        const std::string& name,
        bool default_maximized = false
    );
    void window_pos_sanitize(wxTopLevelWindow* window);
    void persist_window_geometry(wxTopLevelWindow* window, bool default_maximized = false);

    void update_accel_table();
    void set_accel_table();

#ifdef USE_NATIVE_MENU
    void setup_macos_native_menu_bar();
#endif

private:
    Domain::Workbench& m_workbench;
    Biz::ProjectInteractor& m_project_interactor;
    Biz::Preset::PresetInteractor& m_preset_interactor;
    std::unique_ptr<Platform::WX::WXRenderCanvas> m_canvas;
    Navigator& m_navigator;
    std::shared_ptr<ProjectSaver> m_project_saver;

    Biz::ListenerScope<Biz::IProjectsChangedListener, Biz::ProjectInteractor, MainFrame>
        m_projects_changed_listener_scope;

    TabsBarMenus m_tabs_bar_menus;
    LeftBar* m_left_bar{nullptr};

    wxAcceleratorTable m_accel_table;
    wxWindow* m_accel_table_window{nullptr};

#ifdef WIN32
    void* m_hDeviceNotify{nullptr};
    uint32_t m_ulSHChangeNotifyRegister{0};
#endif // WIN32

    bool m_printables_page_added {false};
    bool m_printers_page_added {false};
    bool m_physical_printer_page_added {false};

#ifdef USE_NATIVE_MENU
    std::unique_ptr<WX::MacOSNativeMenuBar> m_native_menu_bar;
#endif
};

} // namespace Slic3r::App::Desktop
