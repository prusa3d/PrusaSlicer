#include "Slic3r/Biz/Scene/BedPlacement.hpp"
#include "Slic3r/Biz/Scene/BedGeometry.hpp"
#include "Slic3r/Domain/BedContainer.hpp"
#include "Slic3r/Domain/BedInstance.hpp"
#include "Slic3r/Domain/Project.hpp"
#include "Slic3r/Domain/Types.hpp"

#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Biz/Algorithms/Point.hpp"
#include "Slic3r/Domain/Transformation.hpp"

using Slic3r::Domain::BoundingBox2d;
using Slic3r::Domain::Transform3d;
using Slic3r::Domain::Vec2d;

namespace Slic3r::Biz::Scene {

namespace {
Vec2d min(const Vec2d& v1, const Vec2d& v2)
{
    return { std::min(v1.x(), v2.x()), std::min(v1.y(), v2.y()) };
}

Vec2d max(const Vec2d& v1, const Vec2d& v2)
{
    return { std::max(v1.x(), v2.x()), std::max(v1.y(), v2.y()) };
}

Transform3d get_bed_trafo(
    const Domain::Project& project,
    Domain::SelectionId cc_id,
    int bed_instance_index,
    const Vec2d& gap)
{
    using Algorithms::BoundingBox::sizes;
    const Domain::Project::ConfigContainerList& ccs = project.config_containers();

    // Accumulate row offsets for rows 0..i-1.
    double offset_y = 0.0;
    size_t k = 0;
    for (k = 0; k < ccs.size() && ccs[k]->id().id != cc_id; ++k) {
        const Domain::Bed& bed = ccs[k]->bed();
        Vec2d bed_pos = bed.contour_aabb().min;
        Vec2d bed_size = bed.contour_aabb_extent();
        const Domain::TriangleMesh& model = BedGeometry::model(bed);
        if (!model.empty()) {
            Domain::BoundingBox3d model_aabb = model.bounding_box();
            bed_pos = min(bed_pos, Algorithms::Point::to_2d(model_aabb.min));
            bed_size = max(bed_size, Algorithms::Point::to_2d(sizes(model_aabb)));
        }
        offset_y += bed_size.y() + gap.y();
    }

    // Compute bed extent for row k.
    const Domain::Bed& bed = ccs[k]->bed();
    Vec2d bed_pos = bed.contour_aabb().min;
    Vec2d bed_size = bed.contour_aabb_extent();
    const Domain::TriangleMesh& model = BedGeometry::model(bed);
    if (!model.empty()) {
        Domain::BoundingBox3d model_aabb = model.bounding_box();
        bed_pos = min(bed_pos, Algorithms::Point::to_2d(model_aabb.min));
        bed_size = max(bed_size, Algorithms::Point::to_2d(sizes(model_aabb)));
    }

    Vec2d pos = offset_y * Vec2d::UnitY() - bed_pos;
    pos.x() += static_cast<double>(bed_instance_index) * (bed_size.x() + gap.x());
    return Domain::translation_transform(Algorithms::Point::to_3d(pos, 0.0));
}
} // namespace

Domain::ElementRefs BedPlacement::layout(Domain::Project& project, const Vec2d& gap)
{
    Domain::ElementRefs ret;
    Domain::Project::ConfigContainerList& ccs = project.config_containers();
    for (size_t i = 0; i < ccs.size(); ++i) {
        Domain::ConfigContainer::BedInstanceList& instances = ccs[i]->bed_instances();
        for (size_t j = 0; j < instances.size(); ++j) {
            // Note that this is now quadratic in the number of config containers and bed instances,
            // because get_bed_trafo traverses all to compute an offset. Given that both numbers are small,
            // the work is fast and layout is not called, it should be fine.
            const Transform3d xform = get_bed_trafo(project, ccs[i]->id().id, int(j), gap);
            const Transform3d old_bed_trafo = instances[j]->matrix();
            const Transform3d delta_xform = xform * old_bed_trafo.inverse();
            instances[j]->transformation = Domain::Transformation(xform);
            for (Domain::ModelInstance* mi : instances[j]->model_instances) {
                mi->set_transformation(
                    Domain::Transformation(
                        delta_xform * mi->get_transformation().get_matrix()
                    )
                );
                ret.emplace_back(mi->get_object()->id().id, mi->id().id);
            }
        }
    }
    return ret;
}

std::optional<Domain::Transform3d> BedPlacement::next_bed_placement(
    const Domain::Project& project,
    Domain::SelectionId config_container_id,
    const Vec2d& gap
)
{
    const Domain::Project::ConfigContainerList& ccs = project.config_containers();
    for (size_t i = 0; i < ccs.size(); ++i) {
        if (ccs[i]->id().id == config_container_id) {
            const int j = int(ccs[i]->bed_instances().size());
            return get_bed_trafo(project, ccs[i]->id().id, j, gap);
        }
    }
    return std::nullopt;
}

} // namespace Slic3r::Biz::Scene

