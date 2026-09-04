#pragma once

#include "Slic3r/Biz/Algorithms/AABBTreeIndirect.hpp"
#include "Slic3r/Biz/Algorithms/TriangleSelector.hpp"
#include "Slic3r/Domain/TriangleMesh.hpp"
#include "Slic3r/Domain/TriangleSelector.hpp"
#include "Slic3r/Domain/Types.hpp"

namespace Slic3r::Biz::Algorithms {

class TriangleSelectorPainter
{
public:
    TriangleSelectorPainter() = delete;

    TriangleSelectorPainter(
        const Domain::TriangleMesh& mesh,
        const Domain::Transform3d& mesh_transform
    );

    void paint_spot(
        const Domain::Vec3f& position,
        float radius,
        Domain::TriangleSelector::TriangleStateType state_type
    );

    Domain::TriangleSelector::TriangleSplittingData serialize() const;

private:
    const Domain::TriangleMesh& m_mesh;
    Domain::Transform3d m_mesh_transform;
    Domain::Transform3d m_mesh_transform_no_translate;
    Domain::Transform3f m_inv_mesh_transform;

    TriangleSelector m_selector;
    AABBTreeIndirect::Tree<3, float> m_triangles_tree;
};

} // namespace Slic3r::Biz::Algorithms
