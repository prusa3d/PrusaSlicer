#pragma once

#include "Slic3r/Biz/ISelectedProjectChangedListener.hpp"
#include "Slic3r/Biz/Platform/ListenerScope.hpp"
#include "Slic3r/Biz/PrintModeInteractor.hpp"
#include "Slic3r/Biz/Preset/IMacroOverridesChangedListener.hpp"

#include "Slic3r/App/Yoga/Dialog.hpp"

#include <vector>

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
    public Biz::ISelectedProjectChangedListener
{
public:
    PrintModeDialog(Biz::ProjectInteractor& project_interactor, Navigator& navigator);

    void on_print_modes_changed() override;

    void on_macro_overrides_changed() override;

    void on_selected_project_changed_final(size_t index) override;

protected:
    void render(const Yoga::Vec2f& pos, const Yoga::Vec2f& size) override;
    void close_action() override;

private:
    /// Rebuilds the cards. Called only from render(), where no card callback can be running.
    void reload();

    Biz::ProjectInteractor& m_project_interactor;
    Navigator& m_navigator;

    Yoga::ScrollArea* m_list{nullptr};
    bool m_reload_pending{false};

    Biz::ListenerScope<Biz::IPrintModesChangedListener, Biz::PrintModeInteractor, PrintModeDialog>
        m_print_mode_listener_scope;
    Biz::ListenerScope<
        Biz::Preset::IMacroOverridesChangedListener,
        Biz::Preset::PresetInteractor,
        PrintModeDialog>
        m_preset_interactor_listener_scope;
    Biz::ListenerScope<Biz::ISelectedProjectChangedListener, Biz::ProjectInteractor, PrintModeDialog>
        m_selected_project_listener_scope;
};

} // namespace Slic3r::App
