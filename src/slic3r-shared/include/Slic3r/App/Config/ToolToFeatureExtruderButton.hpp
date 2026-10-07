#pragma once

#include "Slic3r/App/Yoga/CircleBorderedButton.hpp"
#include "Slic3r/App/ExtruderIcon.hpp"

#include "Slic3r/Biz/DataObserver.hpp"
#include "Slic3r/Biz/IColorsChangedListener.hpp"
#include "Slic3r/Biz/Platform/ListenerScope.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Biz/ProjectSettingsInteractor.hpp"

#include <functional>

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App {

/**
 * @brief A single checkable button representing one physical tool/extruder slot in
 * ToolToFeatureDialogHeader. Unlike ExtruderIdButton, it is not registered with any
 * Yoga::ButtonGroup: several of these buttons can be checked at once (one per extruder
 * id currently used by any option in the dialog), so there is no mutual exclusivity.
 */
class ToolToFeatureExtruderButton :
    public Biz::DataObserver<Biz::Preset::PresetItemObservableList>,
    public Yoga::CircleBorderedButton,
    public Biz::IColorsChangedListener
{
public:
    ToolToFeatureExtruderButton(
        size_t index,
        const Biz::Preset::PresetItemObservableList& data,
        std::function<void(int extruder_id)> on_clicked,
        Biz::ProjectInteractor& project_interactor
    );

    int extruder_id() const
    {
        return static_cast<int>(m_index) + 1;
    }

    void on_colors_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        const std::vector<Domain::ColorRGB>& colors
    ) override;

private:
    void update_color(const std::vector<Domain::ColorRGB>& colors);

    Biz::ProjectInteractor& m_project_interactor;
    ExtruderIcon* m_icon{nullptr};

    Biz::ListenerScope<
        Biz::IColorsChangedListener,
        Biz::ProjectSettingsInteractor,
        ToolToFeatureExtruderButton>
        m_colors_changed_listener_scope;
};

} // namespace Slic3r::App
