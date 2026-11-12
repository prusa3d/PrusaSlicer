#pragma once

#include "Slic3r/Biz/IVirtualExtrudersChangedListener.hpp"
#include "Slic3r/Biz/Platform/WithListeners.hpp"
#include "Slic3r/Biz/Preset/IPresetChangedListener.hpp"
#include "Slic3r/Domain/SelectionId.hpp"
#include "Slic3r/Domain/VirtualExtruder.hpp"
#include "Slic3r/InvokeLaterBag.hpp"

#include <string>
#include <tl/expected.hpp>

namespace Slic3r::Domain {
class ConfigContainer;
class Workbench;
} // namespace Slic3r::Domain

namespace Slic3r::Biz {

/**
 * @brief Owns the virtual extruder definitions of every printer group.
 */
class VirtualExtruderInteractor final :
    public Preset::IPresetChangedListener,
    public WithListeners<IVirtualExtrudersChangedListener>
{
public:
    explicit VirtualExtruderInteractor(Domain::Workbench& workbench);

    /**
     * @brief Returns the current virtual extruder definitions of the given printer group.
     */
    const Domain::VirtualExtruders& virtual_extruders(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id
    ) const;

    /**
     * @brief Replaces the whole virtual extruder list of a printer group.
     *
     * Normalizes the input, then runs the structural checks (unique ids, id inside the
     * valid range, id not inside the physical slot range), the component range check.
     * On any violation nothing is written.
     */
    tl::expected<void, std::string> set_virtual_extruders(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        const Domain::VirtualExtruders& virtual_extruders
    );

    void notify_virtual_extruders_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id
    );

    void restore_virtual_extruders_after_undo(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        Domain::VirtualExtruders virtual_extruders,
        InvokeLaterBag& listener_notifications
    );

    void on_preset_selection_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        Preset::PresetItemType type
    ) override;

private:
    /**
     * @brief The single place where the interactor writes into a config container.
     */
    static void write_virtual_extruders(
        Domain::ConfigContainer& config_container,
        Domain::VirtualExtruders virtual_extruders
    );

    Domain::Workbench& m_workbench;
};

} // namespace Slic3r::Biz
