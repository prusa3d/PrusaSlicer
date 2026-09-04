#include "Slic3r/App/Preview/PreviewScenePresenter.hpp"
#include "Slic3r/App/Scene/BedNodeTag.hpp"
#include "Slic3r/App/Scene/BedNodeBuilder.hpp"
#include "Slic3r/App/Preview/PreviewSceneLayer.hpp"
#include "Slic3r/App/Scene/CameraHelper.hpp"
#include "Slic3r/App/Scene/SceneNodeTag.hpp"
#include "Slic3r/App/Render/GeometryBuilder.hpp"
#include "Slic3r/App/Scene/VolumeColor.hpp"
#include "Slic3r/App/Render/Device.hpp"
#include "Slic3r/App/Scene/NodeVisitor.hpp"
#include <Slic3r/App/libvgcode/SlaObjectNodeTag.hpp>

using Slic3r::Domain::ConfigContainer;
using Slic3r::Domain::SelectionId;

namespace Slic3r::App::Preview {

PreviewScenePresenter::PreviewScenePresenter(
    const Domain::Workbench& workbench,
    Biz::ProjectInteractor& project_interactor,
    Render::Device& device,
    Platform::AnimationManager& animation_manager
) :
    m_workbench(workbench),
    m_project_interactor(project_interactor),
    m_device(device),
    m_animation_manager(animation_manager),
    m_bed_render_updater(*this, workbench, device, project_interactor.scene_interactor())
{
    m_project_interactor.add_listener<Biz::ISelectedProjectChangedListener>(this);
    m_project_interactor.add_listener<Biz::ISelectedProjectChangedListener>(&m_bed_render_updater);

    size_t project_id = m_project_interactor.selected_project_id();
    on_selected_project_changed(project_id);
}

void PreviewScenePresenter::render_scene(Render::CommandBuffer& command_buffer)
{
    if (!m_projects.empty()) {
#if ENABLE_DEBUG_RENDER_SCENE_AABB
        m_camera_frustum_updater.update_scene_aabb(project_context());
        m_camera_frustum_updater.update_scene_aabb_node(project_context(), m_device);
#else
        m_camera_frustum_updater.update_scene_aabb(scene());
#endif // ENABLE_DEBUG_RENDER_SCENE_AABB
        m_camera_frustum_updater.update_camera_frustum(scene().camera());

        project_context().scene().render(m_device, command_buffer, this);
    }
}

void PreviewScenePresenter::render_imgui(const Render::ScreenInfo& screen_info)
{
    if (!m_projects.empty()) {
#if ENABLE_DEBUG_BED_ERROR
        render_imgui_debug_bed_error(project_context().bed_error());
#endif // ENABLE_DEBUG_BED_ERROR
        project_context().scene().render_imgui(screen_info);
    }
}

void PreviewScenePresenter::screen_resized(const Render::Rect& viewport)
{
    m_viewport = viewport;
    update_cameras([&viewport](auto& cam) { cam.set_viewport(viewport); });
}

void PreviewScenePresenter::on_selected_project_changed(size_t index)
{
    m_selected_project_id = index;
    if (m_projects.count(m_selected_project_id) == 0) {
        m_projects.try_emplace(index);
        m_bed_render_updater.on_selected_project_changed(m_selected_project_id);
        // a new camera has been created, add the camera update listeners
        auto& camera = project_context().scene().camera();
        camera.add_listener<Scene::ICameraUpdateListener>(&m_bed_render_updater);
        camera.add_listener<Scene::ICameraUpdateListener>(this);
        camera.set_viewport(m_viewport);
        project_context().scene().add_listener<Scene::ISceneChangedListener>(this);
    }
    set_scene_aabb_as_dirty();
}

void PreviewScenePresenter::on_node_added(Scene::Node* node)
{
    if (node != nullptr && node->contains_raycast_component())
        set_scene_aabb_as_dirty();
}

void PreviewScenePresenter::on_node_removed(Scene::Node* node)
{
    if (node != nullptr && node->contains_raycast_component())
        set_scene_aabb_as_dirty();
}

void PreviewScenePresenter::on_node_changed(Scene::Node* node)
{
    if (node != nullptr && node->contains_raycast_component())
        set_scene_aabb_as_dirty();
}

void PreviewScenePresenter::remove_all_bed_instances()
{
    scene().remove_children([&](const Scene::Node* n) {
        return n->tag_of_type<Scene::BedNodeTag>() != nullptr;
    });  
}

void PreviewScenePresenter::add_bed_instances(const Domain::BedRefs& instances)
{
    auto& scn = scene();
    const auto& proj = m_workbench.project(m_selected_project_id);
    
    for (auto& instance : instances) {
        const Domain::ConfigContainer* cc =
            proj.find_config_container(instance.config_container_id); DEBUG_ASSERT(cc != nullptr);
        const Domain::BedInstance& inst = cc->find_bed_instance(instance.instance_id);

        Scene::BedNodeTag tag = { instance.config_container_id, instance.instance_id };

        Scene::NodeBuilder builder(scn);
        Scene::build_bed_node(builder, inst, tag, m_device,
            m_projects[m_selected_project_id], Scene::RenderLayerId(PreviewSceneLayer::Bed));

        scn.add_child(builder.build().release());
    }
}

void PreviewScenePresenter::update_bed_instances()
{
    m_bed_render_updater.update_all(scene().camera(), project_context().bed_error());
    const auto& scene_interactor = m_project_interactor.scene_interactor();
    const Biz::Scene::BedSelection selection{scene_interactor.bed_selection()};

    // update visibility of bed instances
    visit(
        scene().root(),
        [&](Scene::Node& n) {
            Scene::BedNodeTag* tag = n.tag_of_type<Scene::BedNodeTag>();
            if (tag != nullptr && tag->type == Scene::BedElementType::Undefined) {
                const auto& proj                  = m_workbench.project(m_selected_project_id);
                const Domain::ConfigContainer* cc = proj.find_config_container(
                    tag->config_container_id
                );
                const Domain::BedInstance* inst = Domain::find_by_id(
                    cc->bed_instances(),
                    tag->instance_id
                );
                if (inst == nullptr)
                    return;

                const Domain::BedRef bed_ref{cc->id().id, inst->id().id};
                n.set_enabled(selection.last_selected_bed() == bed_ref);
            }
        },
        true
    );
}

bool PreviewScenePresenter::update_bed_instance_error_state(const Domain::SlicingId& id, bool error)
{
    bool ret = error ? project_context().bed_error().add_bed_instance(id) : project_context().bed_error().remove_bed_instance(id);
    if (ret)
        update_bed_instances();
    return ret;
}

void PreviewScenePresenter::center_camera_on_selected_bed(bool animated)
{
    if (animated)
        animated_center_camera_on_bed(m_workbench.project(m_project_interactor.selected_project_id()),
            m_project_interactor.scene_interactor().bed_selection().last_selected_bed(), scene().camera_trackball(),
            m_animation_manager);
    else
        center_camera_on_bed(m_workbench.project(m_project_interactor.selected_project_id()),
            m_project_interactor.scene_interactor().bed_selection().last_selected_bed(), scene().camera_trackball());
}

void PreviewScenePresenter::remove_all_shells()
{
    scene().remove_children([&](const Scene::Node* n) {
        return n->tag_of_type<Scene::SceneNodeTag>() != nullptr;
    });
}

void PreviewScenePresenter::add_shells()
{
    auto& scn = scene();
    const Domain::Project& project = m_workbench.project(m_selected_project_id);
    const auto& model = project.model();
    const auto& ccs = project.config_containers();

    // collect all model instances on bed + map instance_id -> config container
    Domain::ModelInstanceList instances_on_bed;
    std::unordered_map<SelectionId, const ConfigContainer*> mi_to_cc_map;
    for (const auto& cc : ccs) {
        const auto& bis = cc->bed_instances();
        for (const auto& bi : bis) {
            instances_on_bed.reserve(instances_on_bed.size() + bi->model_instances.size());
            instances_on_bed.insert(instances_on_bed.end(), bi->model_instances.begin(), bi->model_instances.end());
            for (const auto* mi : bi->model_instances)
                mi_to_cc_map[mi->id().id] = cc.get();
        }
    }

    // collect all model part volumes
    std::map<size_t, std::vector<size_t>> volumes;
    for (const auto obj : model.objects) {
        auto it = volumes.insert(std::make_pair(obj->id().id, std::vector<size_t>())).first;
        for (const auto vol : obj->volumes) {
            if (vol->is_model_part())
                it->second.push_back(vol->id().id);
        }
    }

    auto& geom_mgr = project_context().model_geometry_manager();
    auto& trimesh_mgr = project_context().model_triangle_mesh_manager();

    // do not perform out of bed detection in shaders 
    Scene::PrintVolumeData print_volume_data;
    print_volume_data.type = Domain::BedType::Invalid;

    // add shells to scene
    for (const auto obj : model.objects) {
        auto it = volumes.find(obj->id().id);
        if (it != volumes.end()) {
            for (const auto inst : obj->instances) {
                if (!inst->printable)
                    continue;

                if (Domain::find_by_id<Domain::ModelInstance>(instances_on_bed, inst->id().id) != nullptr) {

                    Scene::NodeBuilder builder(scn);
                    builder.set_debug_name(fmt::format("obj: {} inst: {}", obj->id().id, inst->id().id))
                        .transform([inst](auto& t) { t = inst->get_matrix(); })
                        .set_tag(Scene::SceneNodeTag{ obj->id().id, 0, inst->id().id, Domain::ModelVolumeType::INVALID });

                    for (const auto& vol_id : it->second) {
                        const auto* vol = Domain::find_by_id<Domain::ModelVolume>(obj->volumes, vol_id);

                        Scene::AuxiliaryElementId id{Scene::AuxiliaryElementId::Type::Volume, vol_id};
                        const auto& trimesh = trimesh_mgr.get_or_create(id,
                            [&]() { return std::make_unique<Scene::TriangleMesh>(vol->mesh_ptr()); });
                        const auto* geom = geom_mgr.get_or_create(id, [&]() {
                            return Render::geometry_from_triangle_mesh(m_device, trimesh->triangles()); });

                        Domain::ColorRGBA clr = Domain::ColorRGBA::WHITE();
                        if (vol->is_model_part()) {
                            auto cc_it = mi_to_cc_map.find(inst->id().id);
                            if (cc_it != mi_to_cc_map.end()) {
                                const ConfigContainer& config_container = *cc_it->second;
                                const auto& slot_colors =
                                    m_project_interactor.project_settings_interactor().get_colors(
                                        config_container.id().id
                                    );

                                clr = Scene::color_from_extruder_slot(
                                          slot_colors,
                                          *vol,
                                          config_container
                                )
                                          .value_or(clr);
                            }
                        } else {
                            auto color_it = Scene::VOLUME_COLORS.find(vol->type());
                            if (color_it != Scene::VOLUME_COLORS.end())
                                clr = color_it->second;
                        }
                        clr.a(0.5f); // make shells semi-transparent

                        auto material = Render::Material{}
                            .set_shader(m_device.context().shader_manager().shader("gouraud_light"))
                            .set_uniform("uniform_color", clr)
                            .set_transparent(clr.is_transparent());

                        set_uniforms(print_volume_data, material);
                        
                        builder.child([&](Scene::NodeBuilder& bldr) {
                            bldr.set_debug_name(fmt::format("vol: {}", vol->id().id))
                                .transform([vol](auto& xform) { xform = vol->get_matrix(); })
                                .set_tag(Scene::SceneNodeTag{obj->id().id, vol->id().id, inst->id().id, vol->type()})
                                .set_mesh(geom, material, Scene::RenderLayerId(PreviewSceneLayer::Shell))
                                .set_shadows(Render::Shadows{false, false})
                                .set_aabb(trimesh->aabb_mesh())
                                .set_pbr(Scene::DEFAULT_VOLUME_PBRPARAMS);
                        });
                    }

                    scn.add_child(builder.build().release());
                }
            }
        }
    }
}

void PreviewScenePresenter::update_shells_visibility()
{
    const Domain::Project& project = m_workbench.project(m_selected_project_id);
    const auto& bed_selection = m_project_interactor.scene_interactor().bed_selection();
    if (bed_selection.empty())
        return;

    const Domain::BedRef& selected_bed = bed_selection.last_selected_bed();
    const auto bed_instance = project.find_bed_instance_by_id(selected_bed.instance_id);

    auto printer_technology = m_project_interactor.selected_config_container().print_technology();
    bool has_data = false;
    bool are_shells_visible = false;
    if (printer_technology == Domain::PrinterTechnology::FFF) {
        Domain::SlicingId slicing_id = { m_selected_project_id, selected_bed.instance_id };
        has_data = m_project_interactor.fdm_result_cache().get_result(slicing_id).has_value();
        are_shells_visible = m_shells_visible;
    }
    else {
        // currently, when the sla slice is invalidated by moving an object in the plater,
        // sla_result_cache().get_result() returns an empty result, but the sliced geometry is still
        // shown in the scene.
        // so for now we hide the shells only if there are no sla elements in the scene
        Scene::Node* node = scene().root().query_first([](const Scene::Node* n) {
            auto tag = n->tag_of_type<libvgcode::SlaObjectNodeTag>();
            return tag != nullptr && tag->object_id > 0;
        }, true);
        has_data = node != nullptr;
//        Domain::SlicingId slicing_id = { m_selected_project_id, selected_bed.instance_id };
//        has_data = m_project_interactor.sla_result_cache().get_result(slicing_id).has_value();
    }

    Scene::visit(scene().root(), [&](Scene::Node& n) {
        const auto tag = n.tag_of_type<Scene::SceneNodeTag>();
        if (tag != nullptr && n.has_render_component()) {
            bool enabled = Domain::find_by_id<Domain::ModelInstance>(bed_instance->model_instances, tag->instance_id) != nullptr;
            enabled &= !has_data || are_shells_visible;
            n.set_enabled(enabled);
        }
    }, true);
}

void PreviewScenePresenter::update_cameras(const std::function<void(Scene::Camera&)>& modifier)
{
    std::for_each(m_projects.begin(), m_projects.end(),
        [modifier](auto& p) { modifier(p.second.scene().camera()); });
}

} // namespace Slic3r::App::Preview
