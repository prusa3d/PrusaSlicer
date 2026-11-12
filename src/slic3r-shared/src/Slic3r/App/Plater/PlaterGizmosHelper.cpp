#include "Slic3r/App/Plater/PlaterGizmosHelper.hpp"
#include <fmt/ostream.h>
#include "Slic3r/App/Scene/SceneNodeTag.hpp"
#include "Slic3r/App/Scene/Node.hpp"
#include "Slic3r/Biz/Scene/Selection.hpp"
#include "Slic3r/App/Plater/TripleInput.hpp"

using Slic3r::App::Scene::SceneNodeTag;

namespace Slic3r::App::Plater {

bool can_be_added_to_object_selection(const Scene::Node& node, const Biz::Scene::ObjectSelection& selection)
{
    if (selection.mode == Biz::Scene::SelectionMode::Volume && !selection.empty()) {
        Domain::ElementRef first_sel_vol_ref = selection.elements.front();
        first_sel_vol_ref.volume_id = 0;
        const auto& tag = *node.tag_of_type<SceneNodeTag>();
        Domain::ElementRef vol_ref = { tag.object_id, tag.instance_id, tag.volume_id };
        return vol_ref.is_part_of(first_sel_vol_ref);
    }

    return true;
}

Domain::SquareMatrix4d get_scale_matrix(
    const Domain::SquareMatrix3d& basis,
    const Domain::Vec3d& center,
    const Domain::Vec3d& scale_by
)
{
    Domain::SquareMatrix3d local_scale{Domain::SquareMatrix3d::Identity()};
    local_scale.diagonal() = scale_by;

    Domain::SquareMatrix3d linear_part{basis * local_scale * basis.transpose()};

    Domain::SquareMatrix4d result{Domain::SquareMatrix4d::Identity()};
    result.block<3, 3>(0, 0) = linear_part;
    result.block<3, 1>(0, 3) = center - linear_part * center;

    return result;
}

Domain::SquareMatrix3d get_local_rotation_matrix(const Domain::Vec3d& rotate_by)
{
    return Domain::SquareMatrix3d{
        Eigen::AngleAxisd(rotate_by(2), Domain::Vec3d::UnitZ())
        * Eigen::AngleAxisd(rotate_by(1), Domain::Vec3d::UnitY())
        * Eigen::AngleAxisd(rotate_by(0), Domain::Vec3d::UnitX())
    };
}

Domain::SquareMatrix4d get_rotation_matrix(
    const Domain::SquareMatrix3d& basis,
    const Domain::Vec3d& center,
    const Domain::Vec3d& rotate_by
)
{
    const Domain::SquareMatrix3d local_rotation{get_local_rotation_matrix(rotate_by)};

    Domain::SquareMatrix3d linear_part{basis * local_rotation * basis.transpose()};

    Domain::SquareMatrix4d result{Domain::SquareMatrix4d::Identity()};
    result.block<3, 3>(0, 0) = linear_part;
    result.block<3, 1>(0, 3) = center - linear_part * center;

    return result;
}

Domain::SquareMatrix4d get_translation_matrix(
    const Domain::SquareMatrix3d& basis,
    const Domain::Vec3d& translate_by
)
{
    const Domain::Vec3d world_translation{basis * translate_by};
    Domain::SquareMatrix4d result{Domain::SquareMatrix4d::Identity()};
    result.block<3,1>(0,3) = world_translation;
    return result;
}

} // namespace Slic3r::App::Plater
