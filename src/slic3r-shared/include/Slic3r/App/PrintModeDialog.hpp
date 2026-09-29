#pragma once

#include "Slic3r/Biz/Platform/ListenerScope.hpp"
#include "Slic3r/Biz/Preset/IMacroOverridesChangedListener.hpp"
#include "Slic3r/Biz/Preset/IPresetChangedListener.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"

#include "Slic3r/App/Yoga/Dialog.hpp"

#include <functional>

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App {

class Navigator;

namespace Yoga {
class LayoutButton;
} // namespace Yoga

/// Closing goes through the Navigator so the dialog is tracked per project.
class PrintModeDialog :
    public Yoga::Dialog,
    public Biz::Preset::IMacroOverridesChangedListener,
    public Biz::Preset::IPresetChangedListener
{
public:
    PrintModeDialog(Biz::ProjectInteractor& project_interactor, Navigator& navigator);

    /// True when any print mode (e.g. vase mode) is active in the selected preset.
    bool is_mode_active() const;
    /// Called whenever is_mode_active() may have changed.
    std::function<void(bool active)> mode_active_changed;

    void on_macro_overrides_changed() override;
    void on_config_container_selection_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id
    ) override;

protected:
    void close_action() override;

private:
    void toggle_vase_mode();
    void refresh_vase_mode_button();

    Biz::ProjectInteractor& m_project_interactor;
    Navigator& m_navigator;

    Yoga::LayoutButton* m_vase_mode_btn{nullptr};

    Biz::ListenerScope<
        Biz::Preset::IMacroOverridesChangedListener,
        Biz::Preset::PresetInteractor,
        PrintModeDialog>
        m_macro_overrides_listener_scope;
    Biz::ListenerScope<
        Biz::Preset::IPresetChangedListener,
        Biz::Preset::PresetInteractor,
        PrintModeDialog>
        m_preset_changed_listener_scope;
};

} // namespace Slic3r::App
