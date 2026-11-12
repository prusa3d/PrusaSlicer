#include "Slic3r/App/Plater/ScopedThumbnailSceneCustomizerBase.hpp"
#include "Slic3r/App/Scene/Scene.hpp"
#include "Slic3r/App/Scene/CameraHelper.hpp"
#include "Slic3r/App/Scene/NodeVisitor.hpp"
#include "Slic3r/App/Plater/PlaterSceneLayer.hpp"
#include "Slic3r/App/Scene/OBBNodeHelper.hpp"
#include "Slic3r/App/Scene/SceneNodeTag.hpp"
#include "Slic3r/App/Scene/BedNodeTag.hpp"
#include "Slic3r/Domain/BedRef.hpp"
#include "Slic3r/Domain/BedInstance.hpp"
#include "Slic3r/Domain/Project.hpp"
#include "Slic3r/Biz/Scene/BedGeometry.hpp"

using Slic3r::App::Scene::SceneNodeTag;

namespace Slic3r::App::Plater {

ScopedThumbnailSceneCustomizerBase::~ScopedThumbnailSceneCustomizerBase()
{
    //
    // restore values that were changed
    //

    // hidden nodes
    for (auto* n : m_cache.hidden_nodes) {
        n->set_enabled(true);
    }

    // shadows
    for (auto& [n, shadows] : m_cache.shadows) {
        n->render_component()->set_shadows(shadows);
    }

    // materials
    for (auto& [n, material] : m_cache.materials) {
        if (material.has_value())
            n->set_material_override(*material);
        else
            n->remove_material_override();
    }

    // background
    m_scene.set_background_enabled(m_cache.background_enabled);
    m_scene.set_use_background_error_color(m_cache.use_background_error_color);

    // shading
    Scene::Scene::set_shading_type(m_cache.shading_type);

    // Notify listeners that thumbnail rendering ends.
    m_scene.notify_thumbnail_render_end();
}

void ScopedThumbnailSceneCustomizerBase::store_shading_type()
{
    m_cache.shading_type = Scene::Scene::graphics_settings().shading_type();
}

void ScopedThumbnailSceneCustomizerBase::store_background_enabled()
{
    m_cache.background_enabled = m_scene.background_enabled();
}

void ScopedThumbnailSceneCustomizerBase::store_use_background_error_color()
{
    m_cache.use_background_error_color = m_scene.use_background_error_color();
}

void ScopedThumbnailSceneCustomizerBase::hide_gizmos()
{
    Scene::visit(m_scene.root(),
        [this](Scene::Node& n) {
            if (n.has_render_component() &&
                n.render_component()->layer_index() == Scene::RenderLayerId(PlaterSceneLayer::GizmoHandles)) {
                n.set_enabled(false);
                m_cache.hidden_nodes.push_back(&n);
            }
        }
    );
}

void ScopedThumbnailSceneCustomizerBase::hide_selection_aabb()
{
    Scene::visit(m_scene.root(),
        [this](Scene::Node& n) {
            if (n.tag_of_type<Scene::AABBNodeTag>() != nullptr) {
                n.set_enabled(false);
                m_cache.hidden_nodes.push_back(&n);
            }
        }
    );
}

void ScopedThumbnailSceneCustomizerBase::hide_non_part_volumes()
{
    Scene::visit(m_scene.root(),
        [this](Scene::Node& n) {
            const auto* tag = n.tag_of_type<SceneNodeTag>();
            if (tag != nullptr &&
                tag->volume_id > 0 &&
                tag->volume_type != Domain::ModelVolumeType::MODEL_PART) {
                n.set_enabled(false);
                m_cache.hidden_nodes.push_back(&n);
            }
        }
    );
}

void ScopedThumbnailSceneCustomizerBase::hide_non_selected_bed_instances(const Domain::BedRef& selected_bed_instance)
{
    Scene::visit(m_scene.root(),
        [&](Scene::Node& n) {
            Scene::BedNodeTag* tag = n.tag_of_type<Scene::BedNodeTag>();
            if (tag != nullptr &&
                tag->type == Scene::BedElementType::Undefined &&
                (tag->config_container_id != selected_bed_instance.config_container_id || tag->instance_id != selected_bed_instance.instance_id)) {
                n.set_enabled(false);
                m_cache.hidden_nodes.push_back(&n);
            }
        }
    );
}

void ScopedThumbnailSceneCustomizerBase::hide_volumes_outside_selected_bed_instances(const Domain::BedInstance& bed_instance)
{
    Scene::visit(m_scene.root(),
        [&](Scene::Node& n) {
            const auto* tag = n.tag_of_type<SceneNodeTag>();
            if (tag != nullptr) {
                auto it = std::find_if(bed_instance.model_instances.begin(), bed_instance.model_instances.end(),
                    [&](Domain::ModelInstance* inst) { return inst->id().id == tag->instance_id; }
                );
                if (it == bed_instance.model_instances.end()) {
                    n.set_enabled(false);
                    m_cache.hidden_nodes.push_back(&n);
                }
            }
        }
    );
}

void ScopedThumbnailSceneCustomizerBase::hide_bed_accessories()
{
    Scene::visit(m_scene.root(),
        [this](Scene::Node& n) {
            Scene::BedNodeTag* tag = n.tag_of_type<Scene::BedNodeTag>();
            if (tag != nullptr) {
                if (tag->type == Scene::BedElementType::AxesMain ||
                    tag->type == Scene::BedElementType::Contour ||
                    tag->type == Scene::BedElementType::PrintVolume ||
                    tag->type == Scene::BedElementType::Label ||
                    tag->type == Scene::BedElementType::SelectionOutline) {
                    n.set_enabled(false);
                    m_cache.hidden_nodes.push_back(&n);
                }
            }
        }
    );
}

void ScopedThumbnailSceneCustomizerBase::hide_non_printable_volumes()
{
    Scene::visit(m_scene.root(),
        [this](Scene::Node& n) {
            const auto* tag = n.tag_of_type<SceneNodeTag>();
            if (tag != nullptr) {
                const Domain::ModelInstance* model_inst = m_project.find_instance_by_id(tag->object_id, tag->instance_id);
                if (!model_inst->printable) {
                    n.set_enabled(false);
                    m_cache.hidden_nodes.push_back(&n);
                }
            }
        }
    );
}

void ScopedThumbnailSceneCustomizerBase::disable_bed_override_material()
{
    Scene::visit(m_scene.root(),
        [this](Scene::Node& n) {
            Scene::BedNodeTag* tag = n.tag_of_type<Scene::BedNodeTag>();
            if (tag != nullptr && n.has_material_override()) {
                m_cache.materials.push_back(std::make_pair(&n, *n.material_override()));
                n.remove_material_override();
            }
        }
    );
}

void ScopedThumbnailSceneCustomizerBase::disable_volumes_override_material()
{
    Scene::visit(m_scene.root(),
        [this](Scene::Node& n) {
            const auto* tag = n.tag_of_type<SceneNodeTag>();
            if (tag) {
                std::optional<Render::Material> material_override = std::nullopt;
                if (n.has_material_override()) {
                    material_override = *n.material_override();
                }
                m_cache.materials.push_back(std::make_pair(&n, material_override));
                n.remove_material_override();
            }
        }
    );
}

void ScopedThumbnailSceneCustomizerBase::override_non_printable_volumes_material()
{
    Scene::visit(m_scene.root(),
        [this](Scene::Node& n) {
            const auto* tag = n.tag_of_type<SceneNodeTag>();
            if (tag != nullptr && n.has_render_component()) {
                const Domain::ModelInstance* model_inst = m_project.find_instance_by_id(tag->object_id, tag->instance_id);
                if (model_inst != nullptr && !model_inst->printable) {
                    Render::Material material = n.render_component()->material();
                    material.set_uniform("uniform_color", Domain::ColorRGBA::GRAY());
                    material.set_uniform("use_uniform_color", true);
                    // Caching is not needed here.
                    // All volume overrides are already cached in disable_volumes_override_material().
                    n.set_material_override(material);
                }
            }
        }
    );
}

void ScopedThumbnailSceneCustomizerBase::set_shadows()
{
    Scene::visit(m_scene.root(),
        [this](Scene::Node& n) {
            if (n.has_render_component()) {
                const SceneNodeTag* vol_tag = n.tag_of_type<SceneNodeTag>();
                if (vol_tag != nullptr && vol_tag->volume_type == Domain::ModelVolumeType::MODEL_PART) {
                    auto rc = n.render_component();
                    m_cache.shadows.push_back(std::make_pair(&n, Render::Shadows{ rc->cast_shadows(), rc->receive_shadows() }));
                    rc->set_shadows(Render::Shadows{ true, true });
                }
                const Scene::BedNodeTag* bed_tag = n.tag_of_type<Scene::BedNodeTag>();
                if (bed_tag != nullptr && 
                    (bed_tag->type == Scene::BedElementType::Model ||
                     bed_tag->type == Scene::BedElementType::PlateDefault ||
                     bed_tag->type == Scene::BedElementType::PlateTextured)) {
                    auto rc = n.render_component();
                    m_cache.shadows.push_back(std::make_pair(&n, Render::Shadows{ rc->cast_shadows(), rc->receive_shadows() }));
                    rc->set_shadows(Render::Shadows{ false, true });
                }
            }
        }
    );
}

void ScopedThumbnailSceneCustomizerBase::set_background_enabled(bool enabled)
{
    m_scene.set_background_enabled(enabled);
}

void ScopedThumbnailSceneCustomizerBase::set_use_background_error_color(bool use)
{
    m_scene.set_use_background_error_color(use);
}

void ScopedThumbnailSceneCustomizerBase::set_shading_type(Scene::ShadingType type)
{
    Scene::Scene::set_shading_type(type);
}

void ScopedThumbnailSceneCustomizerBase::set_camera_trackball(const Eigen::AlignedBox3d& aabb)
{
    m_camera_trackball.set_target(aabb.center());
    m_camera_trackball.set_distance_to_target(aabb.diagonal().norm());
    m_camera_trackball.set_azimuth_and_zenith(0.25 * std::numbers::pi, 0.75 * std::numbers::pi);
}

void ScopedThumbnailSceneCustomizerBase::zoom_to_box(const Eigen::AlignedBox3d& aabb)
{
    Scene::zoom_to_box(m_camera, aabb);
}

void ScopedThumbnailSceneCustomizerBase::update_camera_frustum()
{
    m_camera_frustum_updater.update_scene_aabb(m_scene);
    m_camera_frustum_updater.update_camera_frustum(m_camera);
}

Eigen::AlignedBox3d ScopedThumbnailSceneCustomizerBase::scene_aabb() const
{
    Eigen::AlignedBox3d ret;
    Scene::visit(m_scene.root(),
        [&](const Scene::Node& n) {
            if (n.has_raycast_component())
                ret.extend(n.raycast_component()->world_bounding_box(n.world_transform().matrix()).cast<double>());
        }
    );
    return ret;
}

Eigen::AlignedBox3d ScopedThumbnailSceneCustomizerBase::volume_parts_aabb() const
{
    Eigen::AlignedBox3d ret;
    Scene::visit(m_scene.root(),
        [&](const Scene::Node& n) {
            if (n.has_raycast_component()) {
                const auto* tag = n.tag_of_type<SceneNodeTag>();
                if (tag != nullptr && tag->volume_type == Domain::ModelVolumeType::MODEL_PART)
                    ret.extend(n.raycast_component()->world_bounding_box(n.world_transform().matrix()).cast<double>());
            }
        }
    );
    return ret;
}

} // namespace Slic3r::App::Plater
