#pragma once

#include "Slic3r/Biz/Platform/ListenerScope.hpp"
#include "Slic3r/Biz/IColorsChangedListener.hpp"
#include "Slic3r/Biz/IVirtualExtrudersChangedListener.hpp"
#include "Slic3r/Biz/Preset/IPresetChangedListener.hpp"
#include "Slic3r/Biz/ProjectSettingsInteractor.hpp"
#include "Slic3r/Biz/VirtualExtruderInteractor.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"

#include <imgui/imgui.h>

#include <functional>
#include <optional>
#include <set>
#include <vector>

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::Domain {
class ConfigItem;
} // namespace Slic3r::Domain

namespace Slic3r::App {

class ExtruderIcon;

namespace ObjectList {

/**
 * @brief Computes and reacts to changes in the "used extruder(s)" indicator for an object or a
 * volume row, mirroring ObjectListOld::render_extruder_marker. Reused by both ObjectButton and
 * VolumeButton since the reactivity surface (colors / preset / virtual extruders, all scoped to
 * a single config container) is identical - only the candidate computation differs.
 */
class ExtruderMarkerReactor :
    public Biz::IColorsChangedListener,
    public Biz::Preset::IPresetChangedListener,
    public Biz::IVirtualExtrudersChangedListener,
    public Biz::Scene::ISceneChangedListener
{
public:
    struct Result
    {
        bool visible{false};
        bool has_invalid_extruder{false};
        std::vector<ImColor> colors;
        std::optional<unsigned>
            single_extruder_index; ///< 1-based, set only if exactly one candidate
    };

    /**
     * @brief Binds this reactor to a config container and starts listening for changes.
     * @param config_container_id If unset (e.g. "out of bed" objects), the marker never shows.
     * @param get_candidates Computes the current set of used extruder indices (0-based).
     * @param on_refreshed Called with the up-to-date Result, immediately and on every change.
     */
    void bind(
        Biz::ProjectInteractor& project_interactor,
        std::optional<size_t> config_container_id,
        std::function<std::set<unsigned>()> get_candidates,
        std::function<void(const Result&)> on_refreshed
    );

    void on_colors_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        const std::vector<Domain::ColorRGB>& colors
    ) override;
    void on_virtual_extruders_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id
    ) override;

    void on_preset_value_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        const Domain::ConfigItem& item
    ) override;

    void on_volume_facets_annotations_changed(
        Domain::SelectionId project_id,
        Domain::FacetsAnnotationKind kind,
        const Domain::ElementRefs& volumes
    ) override;

    void set_config_container(std::optional<size_t> config_container_id);

private:

    void refresh();

    Biz::ProjectInteractor* m_project_interactor{nullptr};
    std::optional<size_t> m_config_container_id;
    std::function<std::set<unsigned>()> m_get_candidates;
    std::function<void(const Result&)> m_on_refreshed;

    std::optional<Biz::ListenerScope<
        Biz::IColorsChangedListener,
        Biz::ProjectSettingsInteractor,
        ExtruderMarkerReactor>>
        m_colors_listener_scope;
    std::optional<Biz::ListenerScope<
        Biz::Preset::IPresetChangedListener,
        Biz::Preset::PresetInteractor,
        ExtruderMarkerReactor>>
        m_preset_listener_scope;
    std::optional<Biz::ListenerScope<
        Biz::IVirtualExtrudersChangedListener,
        Biz::VirtualExtruderInteractor,
        ExtruderMarkerReactor>>
        m_virtual_extruders_listener_scope;
    std::optional<Biz::ListenerScope<
        Biz::Scene::ISceneChangedListener,
        Biz::Scene::SceneInteractor,
        ExtruderMarkerReactor>>
        m_scene_changed_listener_scope;
};

/**
 * @brief Pushes an ExtruderMarkerReactor::Result into an ExtruderIcon widget. Shared by
 * ObjectButton and VolumeButton.
 */
void
refresh_extruder_icon(ExtruderIcon* extruder_icon, const ExtruderMarkerReactor::Result& result);

} // namespace ObjectList
} // namespace Slic3r::App
