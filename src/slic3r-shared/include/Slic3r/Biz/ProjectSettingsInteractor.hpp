#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Slic3r/Biz/IColorsChangedListener.hpp"
#include "Slic3r/Biz/IMdb.hpp"
#include "Slic3r/Domain/Color.hpp"
#include "Slic3r/Biz/ISelectedConfigContainerChangedListener.hpp"
#include "Slic3r/Biz/Platform/WithListeners.hpp"
#include "Slic3r/Biz/Preset/IPresetChangedListener.hpp"
#include "Slic3r/Domain/SelectionId.hpp"

namespace Slic3r::Domain {
class Workbench;
} // namespace Slic3r::Domain

namespace Slic3r::Biz {

/**
 * @brief Manages per-config-container project settings, including the extruder slot color map.
 *
 * Invariant: no entry in extruder_colour is ever an empty string.
 */
class ProjectSettingsInteractor :
    public ISelectedConfigContainerChangedListener,
    public Preset::IPresetChangedListener,
    public WithListeners<IColorsChangedListener>
{
public:
    ProjectSettingsInteractor(Domain::Workbench& workbench, const IMdb& mdb);

    /**
     * @brief Returns current colors for the given container (for initial UI population).
     * @return Empty vector if the container is not found or is not FDM.
     */
    std::vector<Domain::ColorRGB> get_colors(Domain::SelectionId config_container_id) const;

    /**
     * @brief User explicitly picked a color for a slot.
     *
     * Stores the color in ProjectSettings. Passing an empty string 
     * re-runs the priority chain.
     *
     * @param config_container_id Container to update.
     * @param slot                0-based extruder slot index.
     * @param color               Hex color string (e.g. "#FF8000"), or empty.
     */
    void set_color_from_user(
        Domain::SelectionId config_container_id,
        int slot,
        std::string color
    );

    /**
     * @brief Connect sync — updates each slot.
     *
     * @param config_container_id Container to update.
     * @param colors              New colors for all slots. Must not contain empty strings.
     */
    void set_colors_from_connect(
        Domain::SelectionId config_container_id,
        std::vector<std::string> colors
    );

    /**
     * @name ISelectedConfigContainerChangedListener
     * @{
     */
    void on_selected_config_container_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId container_id
    ) override;
    /** @} */

    /**
     * @name Preset::IPresetChangedListener
     * @{
     */
    void on_preset_selection_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        Preset::PresetItemType type
    ) override;

    void on_config_container_selection_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id
    ) override;
    /** @} */

    /**
     * @brief Returns the hardcoded palette color for the given slot index.
     *
     * Deterministic, never returns an empty string.
     */
    static std::string palette_color(int slot);

private:
    /**
     * @brief Resolve a color for a slot via the priority chain.
     *
     * Priority: MDB (via uuid) → filament_colour preset → hardcoded palette.
     * Never returns an empty string.
     *
     * @param project_id          Project containing the config container.
     * @param config_container_id Config container to query.
     * @param slot                0-based extruder slot index.
     * @param filament_uuid       Optional MDB UUID for the material in this slot.
     */
    std::string resolve_auto_color(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        int slot,
        std::string_view filament_uuid = {}
    ) const;

    /**
     * @brief Read filament_colour from the resolved preset for the given slot.
     * @return Empty string if no preset is loaded for the slot.
     */
    std::string preset_color(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        int slot
    ) const;

    /**
     * @brief Return the number of extruder slots for the given config container.
     * @return 0 if not found or not FDM.
     */
    int extruder_count(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id
    ) const;

    /**
     * @brief Load stored colors from ProjectSettings, reconcile length with extruder count,
     *        and notify listeners.
     */
    void load_and_reconcile(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id
    );

    /**
     * @brief Write the color vector back to the ProjectSettings of
     *        the config container and notify listeners.

     */
    void store_and_notify(
        Domain::SelectionId config_container_id,
        std::vector<std::string> colors
    );

    Domain::Workbench& m_workbench;
    const IMdb& m_mdb;
};

} // namespace Slic3r::Biz
