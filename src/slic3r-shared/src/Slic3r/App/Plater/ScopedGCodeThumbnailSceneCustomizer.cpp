#include "Slic3r/App/Plater/ScopedGCodeThumbnailSceneCustomizer.hpp"
#include "Slic3r/Domain/Project.hpp"
#include "Slic3r/Domain/BedRef.hpp"

namespace Slic3r::App::Plater {

ScopedGCodeThumbnailSceneCustomizer::ScopedGCodeThumbnailSceneCustomizer(Scene::Scene& scene, const Domain::Project& project,
    Domain::SelectionId bed_instance_id, Scene::CameraProjectionType camera_type)
    : ScopedThumbnailSceneCustomizerBase(scene, project, camera_type)
{
    const auto* cc = m_project.find_config_container_by_bed_instance_id(bed_instance_id);
    DEBUG_ASSERT(cc != nullptr);
    const Domain::BedInstance* bed_instance = m_project.find_bed_instance_by_id(bed_instance_id);
    DEBUG_ASSERT(bed_instance != nullptr);
    Domain::BedRef bed_ref{cc->id().id, bed_instance_id};

    // store values that are going to be changed
    store_shading_type();
    store_background_enabled();

    // hide geometry
    hide_gizmos();
    hide_selection_aabb();
    hide_non_selected_bed_instances(bed_ref);
    hide_bed_accessories();
    hide_volumes_outside_selected_bed_instances(*bed_instance);
    hide_non_printable_volumes();
    hide_non_part_volumes();

    // set materials
    disable_bed_override_material();
    disable_volumes_override_material();

    // setup scene
    set_background_enabled(false);
    set_shading_type(Scene::ShadingType::PBR);

    Eigen::AlignedBox3d world_aabb = volume_parts_aabb();
    set_camera_trackball(world_aabb);
    zoom_to_box(world_aabb);

    update_camera_frustum();
}

} // namespace Slic3r::App::Plater
