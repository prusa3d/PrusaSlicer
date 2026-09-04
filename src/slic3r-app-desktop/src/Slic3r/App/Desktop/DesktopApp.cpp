#include "DesktopApp.hpp"

#include "MainFrame.hpp"
#include "Slic3r/App/Undo/Store.hpp"
#include "SplashScreen.hpp"
#include <wx/weakref.h>
#include "AppInstanceCheck.hpp"
#include "SecretStoreFactory.hpp"

#include <Slic3r/Log.hpp>
#include <Slic3r/App/Platform/WX/WXMainThreadDispatcher.hpp>
#include <Slic3r/App/WX/WidgetsConfig.hpp>
#include <Slic3r/App/Init.hpp>
#include <Slic3r/App/Localization.hpp>
#include <Slic3r/App/ResourceResolver.hpp>
#include <Slic3r/App/ThumbnailStore.hpp>
#include <Slic3r/App/ThumbnailStoreUpdater.hpp>
#include <Slic3r/App/PopNotification/PopNotificationCenter.hpp>
#include <Slic3r/App/AppServices.hpp>
#include <Slic3r/App/PresetUpdater/PresetUpdaterController.hpp>
#include <Slic3r/App/AppConfig.hpp>
#include <Slic3r/App/AppConfigInteractor.hpp>
#include <Slic3r/App/AppConfigProvider.hpp>
#include <Slic3r/App/WX/FileExplorerHandler.hpp>
#include <Slic3r/App/Theme.hpp>
#include <Slic3r/App/Plater/ThumbnailImageGenerator.hpp>
#include <Slic3r/App/WX/StringConversions.hpp>
#include <Slic3r/App/WX/DialogManager.hpp>
#include <Slic3r/App/WX/WindowMetrics.hpp>
#include <Slic3r/App/ProjectSaver.hpp>

#include <Slic3r/Directories.hpp>
#include <Slic3r/App/Render/TextureManager.hpp>

#include <Slic3r/Biz/Platform/PlatformServices.hpp>
#include <Slic3r/Biz/Platform/JobManager/JobManager.hpp>
#include <Slic3r/Biz/Platform/Termination.hpp>
#include <Slic3r/Biz/Slicing/SlicingInteractor.hpp>
#include <Slic3r/Biz/ResultExport/ResultExportInteractor.hpp>
#include <Slic3r/Biz/UserAccount/UserAccountInteractor.hpp>
#include <Slic3r/Biz/UserAccount/UserAccountTokenStore.hpp>
#include <Slic3r/Biz/AppInstance/AppInstanceMessageHandlerFactory.hpp>
#include <Slic3r/Biz/AppInstance/AppInstanceUtils.hpp>
#include "Slic3r/Biz/WX/FontManager.hpp"

#include "Slic3r/Biz/Scene/BedGeometry.hpp"
#include <boost/filesystem/path.hpp>
#include <boost/algorithm/string.hpp>

#include <Slic3r/App/WX/format.hpp>
#include <Slic3r/App/WX/I18N.hpp>

#ifdef WIN32
#include <dbt.h>
#include <shlobj.h>
static GUID GUID_DEVINTERFACE_HID =
    {0x4D1E55B2, 0xF16F, 0x11CF, 0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30};
#endif // WIN32

wxIMPLEMENT_APP_NO_MAIN(Slic3r::App::Desktop::DesktopApp);

namespace fs = boost::filesystem;

namespace Slic3r::App::Desktop {

namespace {

#ifdef WIN32

void register_win32_device_notification_event()
{
    wxWindow::MSWRegisterMessageHandler(
        WM_COPYDATA,
        [](wxWindow* win, WXUINT /* nMsg */, WXWPARAM wParam, WXLPARAM lParam)
        {
            auto* app_instance = dynamic_cast<Slic3r::App::Desktop::DesktopApp*>(wxTheApp);
            COPYDATASTRUCT* copy_data_structure = {0};
            copy_data_structure                 = (COPYDATASTRUCT*) lParam;
            if (copy_data_structure->dwData == 1) {
                LPCWSTR arguments = (LPCWSTR) copy_data_structure->lpData;
                std::string args  = WX::into_u8(arguments);
                SPDLOG_INFO("MSG {}", Biz::AppInstance::redact_app_urls(args));
                app_instance->handle_app_instance_message(args);
            }
            return true;
        }
    );

    wxWindow::MSWRegisterMessageHandler(
        WM_DEVICECHANGE,
        [](wxWindow* win, WXUINT /* nMsg */, WXWPARAM wParam, WXLPARAM lParam)
        {
            auto* app_instance      = dynamic_cast<Slic3r::App::Desktop::DesktopApp*>(wxTheApp);
            PDEV_BROADCAST_HDR lpdb = (PDEV_BROADCAST_HDR) lParam;
            switch (wParam) {
            case DBT_DEVICEARRIVAL:
                if (lpdb->dbch_devicetype == DBT_DEVTYP_VOLUME) {
                    app_instance->handle_volumes_changed_event();
                } else if (lpdb->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
                    PDEV_BROADCAST_DEVICEINTERFACE lpdbi = (PDEV_BROADCAST_DEVICEINTERFACE) lpdb;
                    if (lpdbi->dbcc_classguid == GUID_DEVINTERFACE_HID) {
                        app_instance->handle_HID_device_attached_event(
                            WX::into_u8(lpdbi->dbcc_name)
                        );
                    }
                }
                break;
            case DBT_DEVICEREMOVECOMPLETE:
                if (lpdb->dbch_devicetype == DBT_DEVTYP_VOLUME) {
                    app_instance->handle_volumes_changed_event();
                } else if (lpdb->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
                    PDEV_BROADCAST_DEVICEINTERFACE lpdbi = (PDEV_BROADCAST_DEVICEINTERFACE) lpdb;
                    if (lpdbi->dbcc_classguid == GUID_DEVINTERFACE_HID) {
                        app_instance->handle_HID_device_detached_event(
                            WX::into_u8(lpdbi->dbcc_name)
                        );
                    }
                }
                break;
            default:
                break;
            }
            return true;
        }
    );

    wxWindow::MSWRegisterMessageHandler(
        WM_USER_MEDIACHANGED,
        [](wxWindow* win, WXUINT /* nMsg */, WXWPARAM wParam, WXLPARAM lParam)
        {
            auto* app_instance = dynamic_cast<Slic3r::App::Desktop::DesktopApp*>(wxTheApp);
            wchar_t sPath[MAX_PATH];
            if (lParam == SHCNE_MEDIAINSERTED || lParam == SHCNE_MEDIAREMOVED) {
                struct _ITEMIDLIST* pidl = *reinterpret_cast<struct _ITEMIDLIST**>(wParam);
                if (!SHGetPathFromIDList(pidl, sPath)) {
                    return false;
                }
            }
            switch (lParam) {
            case SHCNE_MEDIAINSERTED: {
                app_instance->handle_volumes_changed_event();
                break;
            }
            case SHCNE_MEDIAREMOVED: {
                app_instance->handle_volumes_changed_event();
                break;
            }
            default:
                break;
            }
            return true;
        }
    );
}
#endif // WIN32
} // namespace

int run(const Slic3r::App::InitParams& init_params, AppServices& app_services)
{
#ifdef __WXGTK__
    // https://github.com/prusa3d/PrusaSlicer/issues/12969
    ::setenv("WEBKIT_DISABLE_COMPOSITING_MODE", "1", /* replace */ false);
    ::setenv("WEBKIT_DISABLE_DMABUF_RENDERER", "1", /* replace */ false);

    // On Linux, wxGTK has no support for Wayland, and the app crashes on
    // startup if gtk3 is used. This env var has to be set explicitly to
    // instruct the window manager to fall back to X server mode.
    ::setenv("GDK_BACKEND", "x11", /* replace */ true);

    if (app_services.app_config().get<Theme::Style>("theme") == Theme::Style::Light) {
        setenv("GTK_THEME", "Adwaita:light", 1);
    } else {
        setenv("GTK_THEME", "Adwaita:dark", 1);
    }
#endif

    bool single_instance_app_config =
        init_params.misc.single_instance.has_value() && init_params.misc.single_instance;
    if (AppInstance::instance_check(init_params, single_instance_app_config)) {
        return 1;
    }
    Render::TextureManager::set_resource_resolver(
        std::make_unique<ResourceResolver>(resources_dir(), data_dir())
    );

    Biz::Scene::BedGeometry::set_resolver(
        [](const std::string& p) -> std::string
        { return Render::TextureManager::resource_resolver().resolve(p); }
    );
    auto* app = new Slic3r::App::Desktop::DesktopApp();
    Slic3r::App::Desktop::DesktopApp::SetInstance(app);
    int argc    = init_params.argc;
    char** argv = init_params.argv;
    app->set_init_params(init_params); // this is called before OnInit.
    return wxEntry(argc, argv);
}

// Temporary workaround, to select a favorite printer on application start.
// DELETE this once it is not needed.
static void select_favorite_preset(
    Biz::Preset::PresetInteractor& preset_interactor
)
{
    const auto& app_services{AppServices::instance()};
    const AppConfig& app_config{app_services.app_config()};
    const AppSettingsAdvanced::PrinterFavoritePresets& printer_favorite_presets{
        app_config.app_settings_advanced().printer_favorite_presets
    };

    if (printer_favorite_presets.size() > 0 && app_config.get<bool>("printers_only_favorites")) {
        const std::string& preset_id{*printer_favorite_presets.begin()};

        auto printer_list{preset_interactor.printer_presets().items()};

        std::string hw_config_id;
        for (std::size_t i{}; i < printer_list.size(); ++i) {
            const Slic3r::Biz::Preset::PresetItem& preset{printer_list.at(i)};
            if (preset.id == preset_id) {
                hw_config_id = preset.hw_printer_config_id;
                break;
            }
        }

        if (!hw_config_id.empty()) {
            preset_interactor.select_printer_preset(hw_config_id, preset_id);
        }
    }
}

bool DesktopApp::OnInit()
{
    auto& app_services{AppServices::instance()};
    Theme::Style style = app_services.app_config().get<Theme::Style>("theme");

    app_services.set_theme(std::make_unique<Theme>(style));

    const AppearanceResult result = SetAppearance(
        style == Theme::Style::Dark ? wxAppBase::Appearance::Dark : wxAppBase::Appearance::Light
    );

    if (result == AppearanceResult::Failure) {
        SPDLOG_WARN("Failure to change wxWidgets appearance");
    } else if (result == AppearanceResult::CannotChange) {
        SPDLOG_WARN("wxWidgets appearance cannot be changed");
    }

    // Set initialization of image handlers before any UI actions - See GH issue #7469
    wxInitAllImageHandlers();

    WX::WidgetsConfig::instance(style == Theme::Style::Dark, true);

    const wxString dots = WX::from_u8("...");

    bool is_editor     = true; // is_editor();
    const std::string last_crash_reason =
        app_services.app_config().get<std::string>("crash_reason");
    if (app_services.app_config().get<bool>("show_splash_screen")) {
        // Detect position (display) to show the splash screen
        // Now this position is equal to the mainframe position
        wxPoint splashscreen_pos = wxDefaultPosition;
        const std::string metrics_string =
            app_services.app_config().get<std::string>("mainframe_window_metrics");
        if (!metrics_string.empty() && app_services.app_config().get<bool>("restore_win_position"))
        {
            if (auto metrics = WX::WindowMetrics::deserialize(metrics_string);
                metrics != boost::none)
            {
                splashscreen_pos = metrics->get_rect().GetPosition();
            }
        }

        // workaround for crash related to the positioning of the window on secondary monitor
        app_services.app_config().record_crash("splashscreen_pos", "show_splash_screen");

        // create splash screen with updated bmp
        m_splash_screen = new SplashScreen(is_editor, splashscreen_pos);

        // revert "crash_reason" value if application wasn't crashed
        // on set position for splashscreen
        app_services.app_config().resolve_crash(last_crash_reason, "show_splash_screen");

        wxYield();
        if (m_splash_screen)
            m_splash_screen->SetText(WX::_L("Loading configurations") + dots);
    }

    using Biz::Platform::PlatformServices;
    using Biz::Platform::JobManager::JobManager;
    using Platform::WX::WXMainThreadDispatcher;

    auto& platform_services{PlatformServices::instance()};
    init_translations();

    platform_services.set_main_thread_dispatcher(std::make_unique<WXMainThreadDispatcher>());

    platform_services.set_secret_store(SecretStoreFactory::create_secret_store());
    // Reset Token store if Account not allowed in app_config
    if (!app_services.app_config().is_prusa_account_enabled()) {
        Biz::UserAccount::TokenStore::reset();
    }
    platform_services.set_job_manager(
        std::make_unique<JobManager>(platform_services.main_thread_dispatcher())
    );

    platform_services.set_app_config_provider(std::make_unique<AppConfigProvider>());

    platform_services.set_app_instance_message_handler(
        Biz::AppInstance::create_app_instance_message_handler(
            platform_services.main_thread_dispatcher()
        )
    );

    m_thumbnail_image_generator = std::make_shared<Plater::ThumbnailImageGenerator>();

    m_project_interactor = std::make_unique<Biz::ProjectInteractor>(
        m_workbench,
        platform_services.main_thread_dispatcher(),
        *m_thumbnail_image_generator
    );

    auto undo_store_ptr{std::make_unique<Undo::Store>(*m_project_interactor)};
    m_undo_store = undo_store_ptr.get();

    m_project_interactor->set_undo_provider(std::move(undo_store_ptr));

    m_thumbnail_store = std::make_shared<App::ThumbnailStore>(*m_project_interactor);
    platform_services.set_job_manager(
        std::make_unique<JobManager>(platform_services.main_thread_dispatcher())
    );
    m_thumbnail_store_updater =
        std::make_shared<App::ThumbnailStoreUpdater>(*m_thumbnail_image_generator, m_thumbnail_store);

    app_services.set_dialog_manager(std::make_unique<WX::DialogManager>());
    app_services.set_pop_notification_center(
        std::make_unique<PopNotification::PopNotificationCenter>(*m_project_interactor.get())
    );
    app_services.set_file_explorer_handler(std::make_unique<WX::FileExplorerHandler>());
    app_services.file_explorer_handler()
        .add_listener<Platform::IFileExplorerErrorListener>(
            &app_services.pop_notification_center()
        );
    platform_services.job_manager()
        .add_listener<Biz::Platform::JobManager::IJobManagerStatusChangedListener>(
            &app_services.pop_notification_center()
        );
    m_project_interactor->user_account_interactor()
        .add_listener<Biz::UserAccount::IUserAccountListener>(
            &app_services.pop_notification_center()
        );
    m_project_interactor->connect_message_handler()
        .add_listener<Biz::Connect::IConnectHandlerListener>(
            &app_services.pop_notification_center()
        );
    PresetUpdater::ControllerEnvironment preset_updater_environment;
    preset_updater_environment.timer_queue = &platform_services.timer_queue();
    preset_updater_environment.online_allowed =
        []() { return AppServices::instance().app_config().get<bool>("enable_preset_update"); };
    preset_updater_environment.request_render = []()
    {
        Biz::Platform::PlatformServices::instance().render_request_handler().request_render();
    };

    app_services.set_preset_updater_controller(
        std::make_unique<PresetUpdater::PresetUpdaterController>(
            m_project_interactor->preset_updater_interactor(),
            std::move(preset_updater_environment)
        )
    );

    PresetUpdater::PresetUpdaterController& preset_updater_controller =
        app_services.preset_updater_controller();
    preset_updater_controller.set_forced_state_callback(
        [this](bool has_forced) { on_preset_updater_forced_state(has_forced); }
    );
    preset_updater_controller.set_show_dialog_callback(
        [this]()
        {
            if (m_init_completed) {
                m_navigator.set_modal_dialog(ModalDialog::PresetUpdater);
            } else {
                m_loading_module->set_opened_preset_updater(true);
            }
        }
    );
    preset_updater_controller.set_presets_installed_callback(
        [this]()
        {
            if (m_init_completed) {
                m_project_interactor->preset_interactor().load_preset_bundle(m_bundle_paths);
                return;
            }
            // Whether the install cleared the forced state is the check's answer to give, not ours:
            // it reports presets installed even when some of the vendors failed.
            AppServices::instance().preset_updater_controller().start();
        }
    );

    m_loading_module =
        std::make_unique<LoadingRenderModule>(preset_updater_controller, m_navigator);

    m_project_saver = std::make_shared<ProjectSaver>(*m_project_interactor, *m_thumbnail_store);

    m_main_frame = new MainFrame(m_workbench, *m_project_interactor, m_navigator, m_project_saver);
    platform_services.app_instance_message_handler().init(m_main_frame->GetHandle());
    Platform::WX::WXRenderCanvas& canvas = m_main_frame->get_render_canvas();
    m_gl_context                         = canvas.release_context();
    platform_services.set_render_request_handler(&canvas);

    // Queued as early as it can be: the job manager reports through the render request handler,
    // which only exists now, and nothing preset related has been touched yet.
    preset_updater_controller.start();

    m_navigator.set_canvas(canvas);

    canvas.set_render_module(m_loading_module.get());

    m_main_frame->update_graphics_settings();

    // The main thread dispatcher is drained from the canvas only once the window is up, so the
    // answer of the check started above cannot arrive before this point.
    m_main_frame->Show();

    return true;
}

void DesktopApp::on_preset_updater_forced_state(bool has_forced)
{
    if (!has_forced) {
        finish_init();
        return;
    }

    // Past the loading module the preset updater asks for its own dialog through the navigator.
    if (m_init_completed) {
        return;
    }

    if (m_splash_screen) {
        m_splash_screen->Destroy();
    }
    m_loading_module->set_opened_preset_updater(true);
}

void DesktopApp::finish_init()
{
    if (m_init_completed) {
        return;
    }
    m_init_completed = true;

    auto& app_services{AppServices::instance()};
    auto& platform_services{Biz::Platform::PlatformServices::instance()};

    const wxString dots = WX::from_u8("...");

    auto& preset_interactor = m_project_interactor->preset_interactor();

    preset_interactor.print_tool_cbi().set_app_config_cbol(
        app_services.app_config_interactor().app_config_cbi().config_box_list()
    );

    // load new presets
    m_bundle_paths = Biz::Preset::IO::BundlePaths::make_standard_runtime();
    preset_interactor.load_preset_bundle(m_bundle_paths);

    if (m_splash_screen)
        m_splash_screen->SetText(WX::_L("Initializing Prepare Mode") + dots);

    auto font_manager = std::make_unique<Biz::WX::FontManager>(data_dir());

    m_plater_module = std::make_unique<Plater::PlaterRenderModule>(
        m_workbench,
        *m_project_interactor,
        *m_undo_store,
        m_thumbnail_store,
        m_thumbnail_store_updater,
        m_thumbnail_image_generator,
        std::move(font_manager),
        m_project_saver
    );

    if (m_splash_screen)
        m_splash_screen->SetText(WX::_L("Initializing Preview Mode") + dots);

    m_preview_module = std::make_unique<Preview::PreviewRenderModule>(
        m_workbench,
        *m_project_interactor,
        *m_undo_store,
        m_thumbnail_store,
        m_thumbnail_store_updater,
        m_thumbnail_image_generator,
        m_plater_module.get(),
        &m_plater_module->plugin_system(),
        m_project_saver
    );

    m_project_interactor->removable_drive_service().add_status_listener(
        &app_services.pop_notification_center()
    );

    m_project_interactor->set_dialog_provider(&app_services.dialog_manager());

    preset_interactor.set_dialog_manager(&app_services.dialog_manager());

    m_project_interactor->new_project();

    select_favorite_preset(preset_interactor);

    handle_previous_crash_recovery(app_services.app_config());

    Platform::WX::WXRenderCanvas& canvas = m_main_frame->get_render_canvas();

    m_navigator.on_init(*m_plater_module, *m_preview_module, canvas, m_project_interactor.get());

    // Closed before the module goes, so the model stops counting it as an open dialog.
    m_loading_module->set_opened_preset_updater(false);

    // set_next_render_module, because the plater has to be initialized before it is handed a
    // screen size. The swap itself is done at the end of the frame rendered below, which is why
    // the module is kept alive: the canvas still points at it until then.
    // Rendered here and not just requested: render_module_switched below reads the current one.
    canvas.set_next_render_module(m_plater_module.get());
    canvas.render();
    canvas.request_render();

    // The main frame was shown while the preset updater module was the active one, so the
    // shortcuts of the plater have to be picked up now.
    if (m_navigator.callbacks().render_module_switched) {
        m_navigator.callbacks().render_module_switched();
    }

    m_project_interactor->user_account_interactor()
        .add_listener<Biz::UserAccount::IUserAccountListener>(m_main_frame);

    m_project_interactor->user_account_interactor().init(app_services.app_config().is_prusa_account_enabled());

    m_prusalink_storage_listener =
        std::make_unique<PrintHost::PrusaLinkStorageListener>(*m_project_interactor.get());
    platform_services.job_manager()
        .add_listener<Biz::Platform::JobManager::IJobManagerStatusChangedListener>(
            m_prusalink_storage_listener.get()
        );

    app_services.pop_notification_center().set_switch_left_tab_fn(
        std::bind(
            &MainFrame::switch_left_tab,
            m_main_frame,
            std::placeholders::_1,
            std::placeholders::_2
        )
    );
    app_services.pop_notification_center().set_open_invalid_data_dialog_fn(
        std::bind(&Navigator::open_invalid_data_dialog, &m_navigator)
    );

    m_plater_module->plugin_system().add_listener<Lua::IPluginInstallationListener>(
        &app_services.pop_notification_center()
    );

#ifdef WIN32
    m_main_frame->register_win32_callbacks();
#endif

    if (m_splash_screen)
        m_splash_screen->Destroy();

#if !defined(__linux)
    // Initial repaint
    canvas.render();
#endif

#ifdef WIN32
    register_win32_device_notification_event();
#endif // WIN32

    m_project_interactor->backup_store().start_crash_detection();

    for (int i = 0; i < m_init_params.argc; ++i) {
        std::string arg(m_init_params.argv[i]);
        if (boost::starts_with(arg, "prusaslicer://open")) {
            m_project_interactor->on_download_models({std::move(arg)});
        }
    }

    // Last: the application is up, so the check can report what it finds without being in the way.
    app_services.preset_updater_controller().start_background_check();
}

DesktopApp::~DesktopApp()
{
    // Before m_project_interactor, which owns the interactor the model listens to and calls into.
    AppServices& app_services = AppServices::instance();
    if (app_services.has_preset_updater_controller()) {
        app_services.preset_updater_controller().shutdown();
    }

    Biz::Platform::close();
    flush_logs();
}

void DesktopApp::init_translations()
{
    AppConfig& app_config = AppServices::instance().app_config();
    // Get the active language from PrusaSlicer.ini, or empty string if the key does not exist.
    std::string language             = app_config.get<std::string>("translation_language");
    std::string language_description = localization().language_description(language);
    if (!language.empty())
        SPDLOG_TRACE("translation_language provided by PrusaSlicer.ini: {}", language_description);

    if (!localization().set_language(language)) {
        // Loading the language dictionary failed.
        wxString message =
            WX::format_wxstr("Switching PrusaSlicer to language %1% failed.", language_description);
#if !defined(_WIN32) && !defined(__APPLE__)
        // likely some linux system
        message +=
            "\n"
            + WX::format_wxstr(
                (
                    "You may need to reconfigure the missing locales, likely by running the %1% and %2% commands.\n"
                ),
                "\"locale-gen\"",
                "\"dpkg-reconfigure locales\""
            );
#endif
        message += WX::from_u8("\n\nApplication will close.");
        wxMessageBox(
            message,
            WX::from_u8("PrusaSlicer - Switching language failed"),
            wxOK | wxICON_ERROR
        );

        std::exit(EXIT_FAILURE);
    } else if (!language.empty() && language != localization().active_language()) {
        // Loading the language dictionary failed.
        wxString message =
            WX::format_wxstr("Switching PrusaSlicer to language %1% failed.", language_description);
        message += WX::from_u8("\n\n")
            + WX::format_wxstr(
                       localization().is_alternative_language() ?
                           "Application is started in alternative language %1%." :
                           "Application is started in system language %1%.",
                       localization().active_language()
            );
        wxMessageBox(
            message,
            WX::from_u8("PrusaSlicer - Switching language"),
            wxOK | wxICON_WARNING
        );
        app_config.set(
            "translation_language",
            localization().language_description(localization().active_language())
        );
        app_config.save();
    }
}

void DesktopApp::handle_previous_crash_recovery(AppConfig& app_config)
{
    if (const std::string crash_reason = app_config.get<std::string>("crash_reason");
        !crash_reason.empty())
    {
        const std::string opt_key =
            crash_reason == "splashscreen_pos" ? "show_splash_screen" : "restore_win_position";
        const std::string opt_name = app_config.get_config_box().find(opt_key).item->def().label;
        const std::string preferences_item = Biz::_u8(opt_name);

        const std::string msg = fmt::format(
            fmt::runtime(
                Biz::_u8L(
                    "PrusaSlicer crashed last time when attempting to set window position or size.\n"
                    "We are sorry for the inconvenience, it unfortunately happens with certain multiple-monitor setups.\n"
                    "More precise reason for the crash: \"{0}\".\n"
                    "For more information see our GitHub issue tracker: \"{1}\" and \"{2}\"\n\n"
                    "To avoid this problem, Preferences option \"{3}\" was disabled.\n"
                    "Otherwise, the application will most likely crash again next time."
                )
            ),
            "<b>" + crash_reason + "</b>",
            "<a href=http://github.com/prusa3d/PrusaSlicer/issues/2939>#2939</a>",
            "<a href=http://github.com/prusa3d/PrusaSlicer/issues/5573>#5573</a>",
            "<b>" + preferences_item + "</b>"
        );

        AppServices::instance().dialog_manager().show_info_dialog(
            msg,
            Biz::_u8L("PrusaSlicer started after a crash"),
            true
        );

        app_config.resolve_crash();
    }
}

void DesktopApp::handle_app_instance_message(const std::string& message)
{
    Biz::Platform::PlatformServices::instance()
        .app_instance_message_handler()
        .handle_message(message);
}

void DesktopApp::handle_HID_device_detached_event(const std::string& message)
{
    // TODO: Add 3d mouse control
}

void DesktopApp::handle_HID_device_attached_event(const std::string& message)
{
    // TODO: Add 3d mouse control
}

void DesktopApp::handle_volumes_changed_event()
{
    m_project_interactor->handle_volumes_changed_event();
}

#ifdef __APPLE__
void DesktopApp::MacOpenURL(const wxString& url)
{
    std::string narrow_url = WX::into_u8(url);
    if (boost::starts_with(narrow_url, "prusaslicer://login")) {
        m_project_interactor->user_account_interactor().on_log_in_code_response(narrow_url);
    } else if (boost::starts_with(narrow_url, "prusaslicer://open")) {
        m_project_interactor->on_download_models({std::move(narrow_url)});
    } else {
        SPDLOG_ERROR("MacOpenURL recieved improper URL: {}", narrow_url);
    }
}
#endif /* __APPLE__ */

} // namespace Slic3r::App::Desktop
