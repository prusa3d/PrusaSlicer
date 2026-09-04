#pragma once

#include <optional>

#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Domain/ElementRef.hpp"
#include "Slic3r/Domain/Transformation.hpp"

namespace Slic3r::Domain {
class BedContainer;
class Project;
} // namespace Slic3r::Domain

namespace Slic3r::Biz::Scene {

class BedPlacement
{
public:
    /**
     * @brief Perform the layout of bed instances in the scene.
     *
     * @param project The project containing the bed instances to layout.
     * @param gap The space to leave between two adjacent instances.
     * @return References to changed instances.
     */
    [[nodiscard]] Domain::ElementRefs layout(Domain::Project& project, const Domain::Vec2d& gap);

    /**
     * @brief Compute the transform and world-AABB that a *hypothetical* new bed
     *        appended to @p config_container_id would receive, without mutating the project.
     * @return std::nullopt if the target config container is missing or has no bed.
     */
    [[nodiscard]] std::optional<Domain::Transform3d> next_bed_placement(
        const Domain::Project& project,
        Domain::SelectionId config_container_id,
        const Domain::Vec2d& gap
    );
};

} // namespace Slic3r::Biz::Scene
