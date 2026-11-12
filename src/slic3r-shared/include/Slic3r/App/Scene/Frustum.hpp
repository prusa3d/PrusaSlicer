#pragma once

#include "Slic3r/App/Scene/Plane.hpp"
#include "Slic3r/Domain/Polygon.hpp"

namespace Slic3r::App::Render {
struct Rect;
} // Slic3r::App::Render

namespace Slic3r::App::Scene {

class Camera;

/**
 * @brief A generic (non necessarily orthogonal) frustum
 */
class Frustum
{
public: 
    /**
     * @brief Check if this frustum intersects the given axis aligned box
     * @param box The axis aligned box to test
     * @return True if this frustum and the given axis aligned box interect
     * @note This method uses a fast approximate test which may yield false positives
     */
    bool intersects_fast(const Eigen::AlignedBox3d& box) const;

    /**
     * @brief Check if this frustum intersects the sphere with the given center and radius
     * @param center The center of the sphere to test
     * @param radius The radius of the sphere to test
     * @return True if this frustum and the given sphere interect
     */
    bool intersects(const Domain::Vec3d& center, double radius) const;

    /**
     * @brief Create frustum from a rectangle on screen
     * @param camera The camera in use
     * @param rect The rectangle drawn on the screen
     * @return The frustum corresponding to the rectangle on screen
     *
     * @note
     * The frustum is delimited by six planes, with normals pointing inward, when the rectangle's width and height are non-zero.
     * If the rectangle degenerates to a segment, only three planes are defined.
     */
    void set_from(const Camera& camera, const Render::Rect& rect);

private:
    std::vector<Domain::Vec3d> m_vertices;
    std::vector<Plane> m_planes;
};

} // namespace Slic3r::App::Scene
