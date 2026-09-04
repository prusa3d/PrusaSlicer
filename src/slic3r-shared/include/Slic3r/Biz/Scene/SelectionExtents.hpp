#pragma once

#include "Slic3r/Biz/Scene/Selection.hpp"
#include "Slic3r/Biz/Scene/OrientedBoundingBox.hpp"

namespace Slic3r::Biz::Scene {

class SceneInteractor;

struct SelectionExtents
{
    SelectionExtents() = default;

    explicit SelectionExtents(const OrientedBoundingBox& obb, double min_z) :
        m_oriented_bounding_box{obb},
        m_min_z{min_z}
    {}

    const OrientedBoundingBox oriented_bounding_box() const
    {
        return m_oriented_bounding_box;
    }

    void reset_z()
    {
        m_oriented_bounding_box.center.z() -= m_min_z;
        m_min_z = 0.0;
    }

    bool is_floating() const
    {
        return std::abs(m_min_z) > 1e-2;
    }

    double min_z() const
    {
        return m_min_z;
    }

private:
    OrientedBoundingBox m_oriented_bounding_box;
    double m_min_z{};
};

std::optional<SelectionExtents> get_selection_extents(
    Domain::SelectionId project_id,
    const ObjectSelection& selection,
    const SceneInteractor& scene_interactor,
    const Domain::Workbench& workbench
);

} // namespace Slic3r::Biz::Scene
