#pragma once

#include "Slic3r/App/ConfigSettingsDialog.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
class ConfigBoxInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::Biz::Preset {
class PresetInteractor;
} // namespace Slic3r::Biz::Preset

namespace Slic3r::App {

class Navigator;
class AppConfigInteractor;

class PreferencesDialog :
    public ConfigSettingsDialog,
    public Biz::IListObserver<Biz::ConfigBoxInteractor>
{
public:
    explicit PreferencesDialog(AppConfigInteractor& app_config_interactor, Navigator& navigator);

    void on_reset() override;

protected:
    void close_action() override;

private:
    void on_about_to_show() override;

    // The config tab creates all pages, they can be removed only when the dialog is in the tree.
    bool m_mouse_3d_page_visible{true};
};

} // namespace Slic3r::App
