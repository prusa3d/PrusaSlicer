#pragma once

#include <vector>

#include "Slic3r/Domain/Color.hpp"
#include "Slic3r/Domain/SelectionId.hpp"

namespace Slic3r::Biz {

/**
 * @brief Listener notified when the per-slot color vector of a config container changes.
 *
 * Implement this interface and register with ProjectSettingsInteractor to receive
 * color updates. The config_container_id allows listeners that observe multiple
 * containers to route updates correctly.
 */
class IColorsChangedListener
{
public:
    virtual ~IColorsChangedListener() = default;

    /**
     * @param config_container_id ID of the config container whose colors changed.
     * @param colors              New color vector. One entry per extruder slot.
     */
    virtual void on_colors_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        const std::vector<Domain::ColorRGB>& colors
    ) = 0;
};

} // namespace Slic3r::Biz
