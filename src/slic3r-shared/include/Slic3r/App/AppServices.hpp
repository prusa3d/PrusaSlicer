#pragma once

#include <memory>

namespace Slic3r::App::PopNotification {
class PopNotificationCenter;
} // namespace Slic3r::App::PopNotification

namespace Slic3r::App::Platform {
class IFileExplorerHandler;
} // namespace Slic3r::App::Platform

namespace Slic3r::App::PresetUpdater {
class PresetUpdaterController;
} // namespace Slic3r::App::PresetUpdater

namespace Slic3r::App::Mouse3D {
class Mouse3DController;
} // namespace Slic3r::App::Mouse3D

namespace Slic3r::App {

class AppConfig;
class IDialogManager;
class AppConfigInteractor;
class Theme;

class AppServices
{
public:
    static AppServices& instance();
    ~AppServices();

    void set_pop_notification_center(std::unique_ptr<PopNotification::PopNotificationCenter>&& ptr);
    void set_dialog_manager(std::unique_ptr<IDialogManager>&& manager);
    void set_file_explorer_handler(std::unique_ptr<Platform::IFileExplorerHandler>&& handler);
    void set_app_config(std::unique_ptr<AppConfig>&& app_config);
    void set_preset_updater_controller(std::unique_ptr<PresetUpdater::PresetUpdaterController>&& controller);
    void set_theme(std::unique_ptr<Theme>&& theme);
    void set_mouse_3d_controller(std::unique_ptr<Mouse3D::Mouse3DController>&& controller);

    PopNotification::PopNotificationCenter& pop_notification_center() const;
    IDialogManager& dialog_manager() const;
    Platform::IFileExplorerHandler& file_explorer_handler() const;
    AppConfig& app_config() const;
    AppConfigInteractor& app_config_interactor() const;
    PresetUpdater::PresetUpdaterController& preset_updater_controller() const;
    bool has_preset_updater_controller() const;
    Theme& theme() const;

    Mouse3D::Mouse3DController& mouse_3d_controller() const;
    bool has_mouse_3d_controller() const;

private:
    std::unique_ptr<PopNotification::PopNotificationCenter> m_pop_notification_center;
    std::unique_ptr<IDialogManager> m_dialog_manager{};
    std::unique_ptr<Platform::IFileExplorerHandler> m_file_explorer_handler;
    std::unique_ptr<AppConfig> m_app_config;
    std::unique_ptr<AppConfigInteractor> m_app_config_interactor;
    std::unique_ptr<PresetUpdater::PresetUpdaterController> m_preset_updater_controller;
    std::unique_ptr<Theme> m_theme;
    std::unique_ptr<Mouse3D::Mouse3DController> m_mouse_3d_controller;
};

} // namespace Slic3r::App
