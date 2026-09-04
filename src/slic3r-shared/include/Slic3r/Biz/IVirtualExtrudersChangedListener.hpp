#pragma once

#include "Slic3r/Domain/SelectionId.hpp"

namespace Slic3r::Biz {

/**
 * @brief Listener notified when the virtual extruder definitions of a printer group change.
 */
class IVirtualExtrudersChangedListener
{
public:
    virtual ~IVirtualExtrudersChangedListener() = default;

    /**
     * @param project_id ID of the project whose virtual extruders changed.
     * @param config_container_id ID of the printer group whose virtual extruders changed.
     */
    virtual void on_virtual_extruders_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id
    ) = 0;
};

} // namespace Slic3r::Biz
