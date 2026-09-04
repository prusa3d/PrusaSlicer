#pragma once

#include "Slic3r/App/Scene/Frustum.hpp"

namespace Slic3r::App::Scene {

/**
 * @brief A generic non-orthogonal frustum used for frustum culling during rectangle selection picking
 */
class PickerFrustum : public Frustum
{
public: 
    /**
     * @brief Check if this frustum intersects the given axis aligned box
     * @param box The axis aligned box to test
     * @return True if this frustum and the given axis aligned box interect
     * @note The result of the test is precise and is based on screen-space intersection test
     */
    bool intersects_precise(const Eigen::AlignedBox3d& box) const;

    void set_from(const Camera& camera, const Render::Rect& rect);

private:
    const Camera* m_camera{ nullptr };
    Domain::Polygon m_ss_rectangle;
};

} // namespace Slic3r::App::Scene
