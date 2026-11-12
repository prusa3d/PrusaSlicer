#pragma once

#include "Slic3r/Biz/IConfigContainerListChangedListener.hpp"
#include "Slic3r/Biz/IProjectsChangedListener.hpp"
#include "Slic3r/Biz/ISelectedConfigContainerChangedListener.hpp"
#include "Slic3r/Biz/ISelectedProjectChangedListener.hpp"
#include "Slic3r/Biz/Platform/ListenerScope.hpp"
#include "Slic3r/Biz/PrintModeInteractor.hpp"
#include "Slic3r/Biz/Preset/IMacroOverridesChangedListener.hpp"

#include "Slic3r/App/Yoga/Dialog.hpp"

#include <map>
#include <set>
#include <string>

namespace Slic3r::Biz {
class ProjectInteractor;
namespace Preset {
class PresetInteractor;
}
} // namespace Slic3r::Biz

namespace Slic3r::App {

class Navigator;

namespace Yoga {
class ScrollArea;
} // namespace Yoga

class PrintModeDialog :
    public Yoga::Dialog,
    public Biz::IPrintModesChangedListener,
    public Biz::Preset::IMacroOverridesChangedListener,
    public Biz::ISelectedProjectChangedListener,
    public Biz::ISelectedConfigContainerChangedListener,
    public Biz::IConfigContainerListChangedListener,
    public Biz::IProjectsChangedListener
{
public:
    PrintModeDialog(Biz::ProjectInteractor& project_interactor, Navigator& navigator);

    void on_print_modes_changed() override;

    void on_macro_overrides_changed() override;

    void on_selected_project_changed_final(size_t index) override;

    void on_selected_config_container_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId container_id
    ) override;

    void on_config_container_list_changed(Domain::SelectionId project_id) override;

    void on_project_removed(Domain::SelectionId project_id) override;

protected:
    void render(const Yoga::Vec2f& pos, const Yoga::Vec2f& size) override;
    void close_action() override;

private:
    /// Rebuilds the cards. Called only from render(), where no card callback can be running.
    void reload();
    /// Drops the expanded state of config containers that no longer exist.
    void prune_expanded_ids();

    Biz::ProjectInteractor& m_project_interactor;
    Navigator& m_navigator;

    Yoga::ScrollArea* m_list{nullptr};
    bool m_reload_pending{false};
    /// IDs of the modes whose override panel is expanded, per config container. Survives reload().
    std::map<Domain::SelectionId, std::set<std::string>> m_expanded_ids;

    Biz::ListenerScope<Biz::IPrintModesChangedListener, Biz::PrintModeInteractor, PrintModeDialog>
        m_print_mode_listener_scope;
    Biz::ListenerScope<
        Biz::Preset::IMacroOverridesChangedListener,
        Biz::Preset::PresetInteractor,
        PrintModeDialog>
        m_preset_interactor_listener_scope;
    Biz::ListenerScope<Biz::ISelectedProjectChangedListener, Biz::ProjectInteractor, PrintModeDialog>
        m_selected_project_listener_scope;
    Biz::ListenerScope<
        Biz::ISelectedConfigContainerChangedListener,
        Biz::ProjectInteractor,
        PrintModeDialog>
        m_selected_config_container_listener_scope;
    Biz::ListenerScope<
        Biz::IConfigContainerListChangedListener,
        Biz::ProjectInteractor,
        PrintModeDialog>
        m_config_container_list_listener_scope;
    Biz::ListenerScope<Biz::IProjectsChangedListener, Biz::ProjectInteractor, PrintModeDialog>
        m_projects_listener_scope;
};

} // namespace Slic3r::App
