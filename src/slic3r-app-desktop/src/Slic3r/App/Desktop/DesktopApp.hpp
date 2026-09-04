#pragma once

#include "Slic3r/App/Platform/WX/WXRenderCanvas.hpp"
#include <memory>

#include <wx/wx.h>

#include <Slic3r/Domain/Workbench.hpp>
#include <Slic3r/Domain/Bed.hpp>
#include <Slic3r/App/Plater/PlaterRenderModule.hpp>
#include <Slic3r/App/Preview/PreviewRenderModule.hpp>
#include <Slic3r/App/Init.hpp>

#include <Slic3r/Biz/ProjectInteractor.hpp>
#include <Slic3r/Biz/Preset/IO/BundlePaths.hpp>
#include <Slic3r/App/LoadingRenderModule.hpp>
#include <Slic3r/App/Navigator.hpp>
#include <Slic3r/App/PrusaLinkStorageListener.hpp>
#include <Slic3r/App/Plater/ThumbnailImageGenerator.hpp>
#include <Slic3r/App/ThumbnailStore.hpp>
#include <Slic3r/App/ThumbnailStoreUpdater.hpp>
#include <Slic3r/App/Undo/Store.hpp>
#include "Slic3r/App/AppConfig.hpp"

#include "SplashScreen.hpp"

#include <wx/weakref.h>

namespace Slic3r::App::Desktop {
class MainFrame;

class DesktopApp : public wxApp
{
public:
    bool OnInit() override;
    ~DesktopApp();

    void set_init_params(const InitParams& init_params)
    {
        m_init_params = init_params;
    }

    /**
     * @brief On Windows, accepting message from other instance must be done in wxApp implementation.
     * See register_win32_device_notification_event()
     */
    void handle_app_instance_message(const std::string& message);

    void handle_HID_device_detached_event(const std::string& message);
    void handle_HID_device_attached_event(const std::string& message);
    void handle_volumes_changed_event();

#ifdef __APPLE__
    void MacOpenURL(const wxString& url) override;
#endif /* __APPLE */

private:
    void init_translations();
    void handle_previous_crash_recovery(AppConfig& app_config);

    /**
     * @brief Loads the presets and brings up everything built on top of them.
     *
     * Deliberately not part of OnInit: it must not run before the preset updater has confirmed that
     * the presets installed on this computer can be used with this build.
     */
    void finish_init();

    void on_preset_updater_forced_state(bool has_forced);

    std::unique_ptr<wxGLContext> m_gl_context; // do NOT change order of this attribute
    std::unique_ptr<Biz::ProjectInteractor> m_project_interactor;

    MainFrame* m_main_frame{nullptr};
    std::unique_ptr<LoadingRenderModule> m_loading_module;
    std::unique_ptr<Plater::PlaterRenderModule> m_plater_module;
    std::unique_ptr<Preview::PreviewRenderModule> m_preview_module;
    Domain::Workbench m_workbench;
    InitParams m_init_params;
    Navigator m_navigator;
    std::unique_ptr<PrintHost::PrusaLinkStorageListener> m_prusalink_storage_listener;

    std::shared_ptr<Plater::ThumbnailImageGenerator> m_thumbnail_image_generator;
    std::shared_ptr<App::ThumbnailStore> m_thumbnail_store;
    std::shared_ptr<App::ThumbnailStoreUpdater> m_thumbnail_store_updater;
    std::shared_ptr<ProjectSaver> m_project_saver;
    Undo::Store* m_undo_store{nullptr};

    Biz::Preset::IO::BundlePaths m_bundle_paths;
    wxWeakRef<SplashScreen> m_splash_screen;
    bool m_init_completed{false};
};

} // namespace Slic3r::App::Desktop
