#include "Slic3r/App/Plater/PaintOnGizmoBase.hpp"

#include "Slic3r/App/Plater/PaintOnGizmoHelper.hpp"
#include "Slic3r/App/Plater/PlaterScenePresenter.hpp"
#include "Slic3r/App/Scene/SceneNodeTag.hpp"
#include "Slic3r/App/Render/Device.hpp"
#include "Slic3r/App/Scene/Clipper.hpp"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Biz/Algorithms/Color.hpp"
#include "Slic3r/Biz/Algorithms/Polyline.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/Biz/Scene/Selection.hpp"
#include "Slic3r/Biz/Utils/MeshRaycaster.hpp"
#include "Slic3r/Domain/Polyline.hpp"
#include "Slic3r/Math.hpp"

#include <algorithm>
#include <imgui/imgui.h>
#include <magic_enum/magic_enum_flags.hpp>
#include <numeric>

#include "libslic3r/TriangleMeshSlicer.hpp"

using namespace Slic3r;
using namespace Slic3r::App::Yoga;
using namespace Slic3r::Biz;
using namespace Slic3r::Biz::Utils;
using namespace magic_enum::bitwise_operators;

using Slic3r::App::Plater::TriangleSelectorRenderWrapper;
using Slic3r::App::Scene::SceneNodeTag;
using Slic3r::Biz::Algorithms::TriangleSelector;
using Slic3r::Biz::Scene::ObjectSelection;
using Slic3r::Biz::Scene::SceneInteractor;
using Slic3r::Biz::Scene::SelectionState;
using Slic3r::Domain::ColorRGBA;
using Slic3r::Domain::ConfigContainer;
using Slic3r::Domain::ModelInstance;
using Slic3r::Domain::ModelObject;
using Slic3r::Domain::ModelVolume;
using Slic3r::Domain::Point;
using Slic3r::Domain::Polyline;
using Slic3r::Domain::PrinterTechnology;
using Slic3r::Domain::Project;
using Slic3r::Domain::SelectionId;
using Slic3r::Domain::Transform3d;
using Slic3r::Domain::Vec2d;
using Slic3r::Domain::Vec3d;
using Slic3r::Domain::Vec3f;

namespace Slic3r::App::Plater {

PaintOnGizmoBase::PaintOnGizmoBase(
    Render::Device& device,
    Scene::GeometryDataFactory& data_factory,
    ProjectInteractor& project_interactor,
    PlaterScenePresenter& scene_presenter
) :
    m_device(device),
    m_data_factory(data_factory),
    m_project_interactor(project_interactor),
    m_scene_interactor(project_interactor.scene_interactor()),
    m_scene_presenter(scene_presenter)
{
    m_clipping_plane_presenter =
        Scene::ClipperPresenter(&m_clipping_plane_clipper, &m_device, &m_scene_presenter);
    m_sinking_plane_presenter =
        Scene::ClipperPresenter(&m_sinking_plane_clipper, &m_device, &m_scene_presenter);
}

float PaintOnGizmoBase::get_cursor_radius_min() const
{
    return PaintOnGizmoBase::CursorRadiusMin;
}

float PaintOnGizmoBase::get_cursor_radius_max() const
{
    return PaintOnGizmoBase::CursorRadiusMax;
}

float PaintOnGizmoBase::get_cursor_radius_step() const
{
    return PaintOnGizmoBase::CursorRadiusStep;
}

float PaintOnGizmoBase::get_cursor_edge_limit() const
{
    return m_cursor_radius / 5.f;
}

bool PaintOnGizmoBase::disable_object_selection() const
{
    return true;
}

static Scene::Node::NodeList
collect_visible_volumes_nodes(const Project& project, Scene::Scene& scene)
{
    Scene::Node::NodeList visible_volumes_nodes;
    for (const ModelObject* model_object : project.model().objects) {
        scene.root().query(
            [&model_object](const Scene::Node* n) -> bool
            {
                const SceneNodeTag* tag = n->tag_of_type<SceneNodeTag>();
                return tag != nullptr && tag->object_id == model_object->id().id;
            },
            visible_volumes_nodes,
            false
        );
    }

    // Also, collect wipe tower nodes so they get hidden during the gizmo.
    scene.root().query(
        [](const Scene::Node* n) -> bool
        {
            const SceneNodeTag* tag = n->tag_of_type<SceneNodeTag>();
            return tag != nullptr && tag->is_wipe_tower();
        },
        visible_volumes_nodes,
        false
    );

    return visible_volumes_nodes;
}

static std::optional<PaintOnGizmoBase::PaintableVolumes> collect_paintable_volumes(
    const ObjectSelection& object_selection,
    const Project& project,
    const PlaterScenePresenter::MeshManager& mesh_manager
)
{
    std::set<std::pair<size_t, size_t>> paintable_objects_instances_ids;
    for (const Domain::ElementRef& selected_element : object_selection.elements) {
        paintable_objects_instances_ids.emplace(
            selected_element.object_id,
            selected_element.instance_id
        );
    }

    PaintOnGizmoBase::PaintableVolumes paintable_volumes;
    for (const std::pair<size_t, size_t> object_instance_id : paintable_objects_instances_ids) {
        const ModelObject* model_object = project.find_object_by_id(object_instance_id.first);
        const ModelInstance* model_instance =
            project.find_instance_by_id(object_instance_id.first, object_instance_id.second);

        if (model_object == nullptr || model_instance == nullptr) {
            return std::nullopt;
        }

        for (ModelVolume* model_volume : model_object->volumes) {
            if (!model_volume->is_model_part()) {
                continue;
            }

            const Scene::AuxiliaryElementId volume_id{
                Scene::AuxiliaryElementId::Type::Volume,
                model_volume->id().id
            };
            const Scene::TriangleMesh* mesh = mesh_manager.get(volume_id);
            if (mesh == nullptr) {
                return std::nullopt;
            }

            paintable_volumes.push_back(
                {*model_object,
                 *model_instance,
                 *model_volume,
                 mesh->aabb_mesh(),
                 model_instance->get_matrix() * model_volume->get_matrix(),
                 model_instance->get_matrix_no_offset() * model_volume->get_matrix_no_offset()}
            );
        }
    }

    return paintable_volumes;
}

static Domain::Vec4f get_clipping_plane_data(const Scene::Clipper& clipper)
{
    Domain::Vec4f clp_data_out(0.f, 0.f, 1.f, FLT_MAX);
    // Take care of the clipping plane. The normal of the clipping plane is
    // saved with opposite sign than we need to pass to OpenGL (FIXME)
    if (bool clipping_plane_active = clipper.get_position() != 0.; clipping_plane_active) {
        const ClippingPlane& clp = clipper.get_clipping_plane();
        for (size_t i = 0; i < 3; ++i) {
            clp_data_out[i] = -1.f * float(clp.get_data()[i]);
        }

        clp_data_out[3] = float(clp.get_data()[3]);
    }

    return clp_data_out;
}

static bool is_any_paintable_volume_sinking(const PaintOnGizmoBase::PaintableVolumes& volumes)
{
    for (const PaintOnGizmoBase::PaintableVolume& volume : volumes) {
        Domain::BoundingBox3d box = Algorithms::BoundingBox::transformed(
            volume.model_volume.mesh().bounding_box(),
            volume.world_trafo
        );

        if (box.min.z() < Domain::SINKING_Z_THRESHOLD && box.max.z() >= Domain::SINKING_Z_THRESHOLD)
        {
            return true;
        }
    }

    return false;
}

void PaintOnGizmoBase::seed_fill_unselect_all()
{
    for (TriangleSelectorRenderWrapper& triangle_selector_wrapper : m_triangle_selector_wrappers) {
        triangle_selector_wrapper.triangle_selector().seed_fill_unselect_all_triangles();
        triangle_selector_wrapper.update_painted_geometry(m_device);
    }

    m_seed_fill_last_mesh_id = -1;
}

void PaintOnGizmoBase::restore_visible_volumes()
{
    for (Scene::Node* node : m_visible_volumes_nodes) {
        node->set_enabled(true);
    }
}

void PaintOnGizmoBase::hide_visible_volumes()
{
    for (Scene::Node* node : m_visible_volumes_nodes) {
        node->set_enabled(false);
    }
}

void PaintOnGizmoBase::on_node_added(Scene::Node* node)
{
    const SceneNodeTag* tag = node->tag_of_type<SceneNodeTag>();
    if (tag == nullptr) {
        return;
    }

    m_visible_volumes_nodes.push_back(node);
    node->set_enabled(false);
}

void PaintOnGizmoBase::on_node_removed(Scene::Node* node)
{
    std::erase(m_visible_volumes_nodes, node);
}

void PaintOnGizmoBase::on_thumbnail_render_begin()
{
    // Before rendering thumbnail, hide gizmo nodes and show original model nodes.
    if (m_main_node != nullptr) {
        m_main_node->set_enabled(false);
    }

    this->restore_visible_volumes();
}

void PaintOnGizmoBase::on_thumbnail_render_end()
{
    // After rendering thumbnail, restore gizmo nodes and hide original model nodes,
    if (m_main_node != nullptr) {
        m_main_node->set_enabled(true);
    }

    this->hide_visible_volumes();
}

void PaintOnGizmoBase::on_model_reloaded(Domain::SelectionId project_id)
{
    if (project_id != m_project_interactor.selected_project_id()) {
        return;
    }

    rebuild_paintable_geometry();
}

void PaintOnGizmoBase::on_scene_selection_changed(
    const SelectionId project_id,
    const ObjectSelection& selection
)
{
    this->rebuild_paintable_geometry();
}

void PaintOnGizmoBase::apply_painting_to_model() const
{
    Domain::ElementRefs volume_refs;
    std::unordered_map<size_t, size_t> volume_id_to_idx;
    for (const PaintableVolume& paintable_volume : m_paintable_volumes) {
        const size_t volume_idx = &paintable_volume - &m_paintable_volumes.front();

        volume_refs.emplace_back(
            paintable_volume.model_object.id().id,
            paintable_volume.model_instance.id().id,
            paintable_volume.model_volume.id().id
        );

        volume_id_to_idx[paintable_volume.model_volume.id().id] = volume_idx;
    }

    const auto facets_annotations_modificator =
        [this, &volume_id_to_idx](const Domain::ElementRef& ref, ModelVolume& volume) -> bool
    {
        const size_t volume_idx = volume_id_to_idx.at(ref.volume_id);

        const TriangleSelectorRenderWrapper& triangle_selector_wrapper =
            m_triangle_selector_wrappers[volume_idx];
        const TriangleSelector& triangle_selector = triangle_selector_wrapper.triangle_selector();

        return this->set_facets_annotation(volume, triangle_selector);
    };

    m_scene_interactor.modify_facets_annotations(
        volume_refs,
        this->get_facets_annotation_kind(),
        facets_annotations_modificator
    );
}

void PaintOnGizmoBase::init_main_nodes()
{
    Scene::Scene& scene = m_scene_presenter.scene();

    Scene::NodeBuilder main_node_builder{scene};
    main_node_builder.set_debug_name("PaintOnGizmoBase - Main node");
    std::unique_ptr<Scene::Node> main_node = main_node_builder.build();
    m_main_node                            = main_node.get();
    scene.add_child(main_node.release(), &scene.root());

    Scene::NodeBuilder triangle_selectors_node_builder{scene};
    triangle_selectors_node_builder.set_debug_name("PaintOnGizmoBase - TriangleSelectors node");
    std::unique_ptr<Scene::Node> triangle_selectors_node = triangle_selectors_node_builder.build();
    m_triangle_selectors_node                            = triangle_selectors_node.get();
    scene.add_child(triangle_selectors_node.release(), m_main_node);

    Scene::NodeBuilder cursors_node_builder{scene};
    cursors_node_builder.set_debug_name("PaintOnGizmoBase - Cursors node");
    std::unique_ptr<Scene::Node> cursors_node = cursors_node_builder.build();
    m_cursors_node                            = cursors_node.get();
    scene.add_child(cursors_node.release(), m_main_node);

    Scene::NodeBuilder clipping_plane_presenter_node_builder{scene};
    clipping_plane_presenter_node_builder.set_debug_name("PaintOnGizmoBase - clipping_plane_presenter node");
    std::unique_ptr<Scene::Node> clipping_plane_presenter_node = clipping_plane_presenter_node_builder.build();
    m_clipping_plane_presenter_node                            = clipping_plane_presenter_node.get();
    scene.add_child(clipping_plane_presenter_node.release(), m_main_node);

    Scene::NodeBuilder sinking_plane_presenter_node_builder{scene};
    sinking_plane_presenter_node_builder.set_debug_name("PaintOnGizmoBase - sinking_plane_presenter node");
    std::unique_ptr<Scene::Node> sinking_plane_presenter_node = sinking_plane_presenter_node_builder.build();
    m_sinking_plane_presenter_node                            = sinking_plane_presenter_node.get();
    scene.add_child(sinking_plane_presenter_node.release(), m_main_node);
}

void PaintOnGizmoBase::init_cursors_nodes()
{
    ASSERT(m_cursors_node != nullptr);

    Scene::Scene& scene = m_scene_presenter.scene();

    m_sphere_cursor_render_wrapper
        .init_cursor_node(m_device, scene, *m_cursors_node, m_data_factory);
    m_circle_cursor_render_wrapper.init_cursor_node(m_device, scene, *m_cursors_node);
    m_height_range_cursor_render_wrapper.init_cursor_node(m_device, scene, *m_cursors_node);
}

void PaintOnGizmoBase::init_clipper_presenters()
{
    Biz::Scene::SceneInteractor& scene_interactor       = m_project_interactor.scene_interactor();
    const Biz::Scene::ObjectSelection& object_selection = scene_interactor.object_selection();
    const Domain::ElementRef& element                   = object_selection.elements.front();
    const Project& project                              = m_project_interactor.selected_project();

    ASSERT(element.volume_id == 0); // Is object.
    const ModelObject* selected_object = project.find_object_by_id(element.object_id);
    const ModelInstance* selected_instance =
        project.find_instance_by_id(element.object_id, element.instance_id);

    ASSERT(selected_instance && selected_object);
    m_clipping_plane_presenter
        .activate(selected_object, selected_instance, m_clipping_plane_presenter_node, 0., Scene::BuildMeshesNodes::No);
    m_clipping_plane_presenter.set_behavior(true, true, 0.);
    m_clipping_plane_presenter.set_position_by_ratio(m_clipping_plane_clipper.get_position(), true);

    m_sinking_plane_presenter
        .activate(selected_object, selected_instance, m_sinking_plane_presenter_node, 0., Scene::BuildMeshesNodes::No);
    m_sinking_plane_presenter.set_behavior(true, true, 0.);

    if (is_any_paintable_volume_sinking(m_paintable_volumes)) {
        m_sinking_plane_presenter
            .update_clipper(-Vec3d::UnitZ(), Domain::SINKING_Z_THRESHOLD, Domain::EPSILON, false);
    }
}

void PaintOnGizmoBase::set_tool_type(ToolType tool_type)
{
    if (m_tool_type == tool_type) {
        return;
    }

    m_tool_type = tool_type;

    // Deselect all triangles selected by the previous tool.
    this->seed_fill_unselect_all();
}

void PaintOnGizmoBase::set_cursor_type(TriangleSelector::CursorType cursor_type)
{
    if (m_cursor_type == cursor_type) {
        return;
    }

    m_cursor_type = cursor_type;

    // Deselect all triangles selected by the previous cursor.
    this->seed_fill_unselect_all();
}

void PaintOnGizmoBase::update_clipping_plane()
{
    const Vec4f clipping_plane = get_clipping_plane_data(m_clipping_plane_clipper);

    for (TriangleSelectorRenderWrapper& triangle_selector_wrapper : m_triangle_selector_wrappers) {
        triangle_selector_wrapper.set_clipping_plane(clipping_plane);
    }

    const ClippingPlane& original_clipping_plane =
        m_clipping_plane_clipper.get_clipping_plane(true);
    m_sinking_plane_presenter.set_limiting_plane(
        -original_clipping_plane.get_normal(),
        original_clipping_plane.get_offset()
    );
}

void PaintOnGizmoBase::update_overhang_detection()
{
    for (TriangleSelectorRenderWrapper& triangle_selector_wrapper : m_triangle_selector_wrappers) {
        triangle_selector_wrapper.set_overhang_slope_normal(m_highlight_by_angle_threshold_deg);
    }
}

void PaintOnGizmoBase::clear_all_paintings()
{
    for (TriangleSelectorRenderWrapper& triangle_selector_wrapper : m_triangle_selector_wrappers) {
        triangle_selector_wrapper.triangle_selector().reset();
        triangle_selector_wrapper.update_painted_geometry(m_device);
    }

    this->apply_painting_to_model();
}

void PaintOnGizmoBase::on_activated()
{
    using MeshManager = PlaterScenePresenter::MeshManager;

    const SceneInteractor& scene_interactor = m_project_interactor.scene_interactor();
    const ObjectSelection& object_selection = scene_interactor.object_selection();
    const Project& project                  = m_project_interactor.selected_project();
    const MeshManager& mesh_manager         = m_scene_presenter.model_triangle_mesh_manager();
    Scene::Scene& scene                     = m_scene_presenter.scene();

    if (object_selection.empty() || object_selection.mode != Biz::Scene::SelectionMode::Instance) {
        m_gizmo_controller->deactivate_current_tool();
        return;
    }

    std::optional<PaintableVolumes> paintable_volumes =
        collect_paintable_volumes(object_selection, project, mesh_manager);
    if (!paintable_volumes) {
        m_gizmo_controller->deactivate_current_tool();
        return;
    }

    m_visible_volumes_nodes = collect_visible_volumes_nodes(project, scene);
    m_paintable_volumes     = std::move(*paintable_volumes);
    m_painting_colors       = this->create_painting_colors();
    m_mouse_dragging        = false;

    this->hide_visible_volumes();

    this->init_main_nodes();
    this->init_cursors_nodes();

    m_triangle_selector_wrappers.clear();

    for (PaintableVolume& paintable_volume : m_paintable_volumes) {
        const ModelVolume& model_volume           = paintable_volume.model_volume;
        const Domain::TriangleMesh& triangle_mesh = model_volume.mesh();

        m_triangle_selector_wrappers.emplace_back(
            triangle_mesh,
            paintable_volume.aabb_mesh,
            m_painting_colors,
            this->create_default_painting_color(model_volume)
        );
        // Reset of TriangleSelector is done inside TriangleSelector's constructor, so we don't need it to perform it again in deserialize().
        m_triangle_selector_wrappers.back().triangle_selector().deserialize(
            this->get_facets_annotation(model_volume).get_data(),
            false
        );
        m_triangle_selector_wrappers.back().init_painted_mesh_node(
            m_device,
            scene,
            *m_triangle_selectors_node,
            paintable_volume.world_trafo
        );
        m_triangle_selector_wrappers.back().init_painted_contour_node(
            m_device,
            scene,
            *m_triangle_selectors_node,
            paintable_volume.world_trafo
        );
    }

    this->init_clipper_presenters();
    this->update_clipping_plane();
    this->update_overhang_detection();

    scene.add_listener<Scene::ISceneChangedListener>(this);
    m_scene_interactor.add_listener<Biz::Scene::ISceneChangedListener>(this);
    m_scene_interactor.add_listener<ISceneSelectionChangedListener>(this);
    scene.add_listener<IThumbnailRenderListener>(this);
}

void PaintOnGizmoBase::on_deactivated()
{
    Scene::Scene& scene = m_scene_presenter.scene();

    // Restore the originally visible nodes.
    this->restore_visible_volumes();

    // Remove all the scene nodes created by this gizmo.
    if (m_main_node != nullptr) {
        scene.remove_child(m_main_node);
        m_main_node               = nullptr;
        m_cursors_node            = nullptr;
        m_triangle_selectors_node = nullptr;
        m_clipping_plane_presenter_node = nullptr;
        m_sinking_plane_presenter_node = nullptr;
    }

    m_triangle_selector_wrappers.clear();
    m_visible_volumes_nodes.clear();

    scene.remove_listener<Scene::ISceneChangedListener>(this);
    m_scene_interactor.remove_listener<Biz::Scene::ISceneChangedListener>(this);
    m_scene_interactor.remove_listener<ISceneSelectionChangedListener>(this);
    scene.remove_listener<IThumbnailRenderListener>(this);
}

void PaintOnGizmoBase::on_project_activated(size_t new_project_id)
{
    on_activated();
}

void PaintOnGizmoBase::on_project_deactivated(size_t old_project_id)
{
    on_deactivated();
}

// Following function is called from GLCanvas3D to inform the gizmo about a mouse/keyboard event.
// The gizmo has an opportunity to react - if it does, it should return true so that the Canvas3D is
// aware that the event was reacted to and stops trying to make different sense of it. If the gizmo
// concludes that the event was not intended for it, it should return false.
bool PaintOnGizmoBase::process_gizmo_event(
    const PaintOnGizmoEvent& gizmo_event,
    const Scene::GizmoEventContext& ctx
)
{
    using namespace Slic3r::Biz::Algorithms;

    if (gizmo_event.type == PaintOnGizmoEvent::Type::MouseWheelUp
        || gizmo_event.type == PaintOnGizmoEvent::Type::MouseWheelDown)
    {
        // On Windows, Right ALT could be reported as Left ALT + Control.
        // In such cases, we want to prioritize ALT over Control.
        if (!gizmo_event.alt_down && gizmo_event.ctrl_down) {
            double pos = m_clipping_plane_clipper.get_position();
            pos        = gizmo_event.type == PaintOnGizmoEvent::Type::MouseWheelDown ?
                       std::max(0., pos - 0.01) :
                       std::min(1., pos + 0.01);
            m_clipping_plane_clipper.set_position_by_ratio(pos, true);

            this->on_clipping_of_view_changed(pos);
            return true;
        } else if (gizmo_event.alt_down) {
            if (m_tool_type == ToolType::BRUSH
                && (m_cursor_type == TriangleSelector::CursorType::SPHERE
                    || m_cursor_type == TriangleSelector::CursorType::CIRCLE))
            {
                m_cursor_radius = gizmo_event.type == PaintOnGizmoEvent::Type::MouseWheelDown ?
                    std::max(
                        m_cursor_radius - this->get_cursor_radius_step(),
                        this->get_cursor_radius_min()
                    ) :
                    std::min(
                        m_cursor_radius + this->get_cursor_radius_step(),
                        this->get_cursor_radius_max()
                    );
                this->on_cursor_radius_changed(m_cursor_radius);
                return true;
            } else if (m_tool_type == ToolType::SMART_FILL || m_tool_type == ToolType::BUCKET_FILL)
            {
                float& fill_angle = (m_tool_type == ToolType::SMART_FILL) ? m_smart_fill_angle :
                                                                            m_bucket_fill_angle;
                fill_angle        = (gizmo_event.type == PaintOnGizmoEvent::Type::MouseWheelDown) ?
                           std::max(fill_angle - SmartFillAngleStep, SmartFillAngleMin) :
                           std::min(fill_angle + SmartFillAngleStep, SmartFillAngleMax);

                if (m_tool_type == ToolType::SMART_FILL) {
                    this->on_smart_fill_angle_changed(m_smart_fill_angle);
                } else {
                    this->on_bucket_fill_angle_changed(m_bucket_fill_angle);
                }

                const VolumeHitPoint& hit = m_raycast_cache.hit;
                TriangleSelectorRenderWrapper& triangle_selector_wrapper =
                    m_triangle_selector_wrappers[hit.volume_idx];
                TriangleSelector& triangle_selector = triangle_selector_wrapper.triangle_selector();
                if (hit.volume_idx != -1) {
                    const PaintableVolume& paintable_volume =
                        this->m_paintable_volumes[hit.volume_idx];
                    const Transform3d& trafo_matrix = paintable_volume.world_trafo;
                    const TriangleSelector::ClippingPlane& clipping_plane =
                        this->get_clipping_plane_in_volume_coordinates(trafo_matrix);
                    const Vec3f hit_position = hit.volume_hit_position.cast<float>();

                    if (m_tool_type == ToolType::SMART_FILL) {
                        const Transform3d& trafo_matrix_no_translate =
                            paintable_volume.world_trafo_no_translate;
                        triangle_selector.seed_fill_select_triangles(
                            hit_position,
                            static_cast<int>(hit.facet_idx),
                            trafo_matrix_no_translate,
                            clipping_plane,
                            m_smart_fill_angle,
                            SmartFillGapArea,
                            m_paint_on_overhangs_only ? m_highlight_by_angle_threshold_deg : 0.f,
                            TriangleSelector::ForceReselection::YES
                        );
                    } else {
                        assert(m_tool_type == ToolType::BUCKET_FILL);
                        triangle_selector.bucket_fill_select_triangles(
                            hit_position,
                            static_cast<int>(hit.facet_idx),
                            clipping_plane,
                            m_bucket_fill_angle,
                            BucketFillGapArea,
                            TriangleSelector::BucketFillPropagate::YES,
                            TriangleSelector::ForceReselection::YES
                        );
                    }

                    triangle_selector_wrapper.update_painted_geometry(m_device);
                    m_seed_fill_last_mesh_id = hit.volume_idx;
                }

                return true;
            } else if (m_tool_type == ToolType::HEIGHT_RANGE) {
                m_height_range_z_range =
                    gizmo_event.type == PaintOnGizmoEvent::Type::MouseWheelDown ?
                    std::max(m_height_range_z_range - HeightRangeZRangeStep, HeightRangeZRangeMin) :
                    std::min(m_height_range_z_range + HeightRangeZRangeStep, HeightRangeZRangeMax);
                this->on_height_range_z_range_changed(m_height_range_z_range);
                return true;
            }

            return false;
        }
    }

    if (gizmo_event.type == PaintOnGizmoEvent::Type::LeftDown
        || gizmo_event.type == PaintOnGizmoEvent::Type::RightDown
        || (gizmo_event.type == PaintOnGizmoEvent::Type::Dragging && m_button_down != Button::None))
    {
        if (m_triangle_selector_wrappers.empty()) {
            return false;
        }

        TriangleStateType new_state = TriangleStateType::NONE;
        if (!gizmo_event.shift_down) {
            if (gizmo_event.type == PaintOnGizmoEvent::Type::Dragging) {
                new_state = (m_button_down == Button::Left) ? this->get_left_button_state_type() :
                                                              this->get_right_button_state_type();
            } else {
                new_state = (gizmo_event.type == PaintOnGizmoEvent::Type::LeftDown) ?
                    this->get_left_button_state_type() :
                    this->get_right_button_state_type();
            }
        }

        const std::vector<VolumeHitPoints> volume_hit_points_by_volume =
            this->get_projected_mouse_positions(
                gizmo_event.mouse_position,
                m_scene_presenter.scene().camera(),
                1.
            );
        m_last_mouse_click = Vec2d::Zero(); // Only actual hits should be saved.

        for (const VolumeHitPoints& volume_hit_points : volume_hit_points_by_volume) {
            assert(!volume_hit_points.empty());
            const int volume_idx = volume_hit_points.front().volume_idx;
            const bool dragging_while_painting =
                (gizmo_event.type == PaintOnGizmoEvent::Type::Dragging
                 && m_button_down != Button::None);

            // The mouse button click detection is enabled when there is a valid hit.
            // Missing the object entirely
            // shall not capture the mouse.
            if (volume_idx != -1 && m_button_down == Button::None) {
                m_button_down =
                    ((gizmo_event.type == PaintOnGizmoEvent::Type::LeftDown) ? Button::Left :
                                                                               Button::Right);
            }

            // In case we have no valid hit, we can return. The event will be stopped when
            // dragging while painting (to prevent scene rotations and moving the object)
            if (volume_idx == -1) {
                return dragging_while_painting;
            }

            const PaintableVolume& paintable_volume = m_paintable_volumes[volume_idx];
            const Transform3d& trafo_matrix         = paintable_volume.world_trafo;
            const Transform3d& trafo_matrix_no_translate =
                paintable_volume.world_trafo_no_translate;
            TriangleSelectorRenderWrapper& triangle_selector_wrapper =
                m_triangle_selector_wrappers[volume_idx];
            TriangleSelector& triangle_selector = triangle_selector_wrapper.triangle_selector();

            // Calculate the direction from camera to the hit (in volume coords):
            Vec3f camera_pos = (trafo_matrix.inverse() * ctx.pick_ray().origin).cast<float>();

            assert(volume_idx < int(m_triangle_selector_wrappers.size()));
            const TriangleSelector::ClippingPlane& clp =
                this->get_clipping_plane_in_volume_coordinates(trafo_matrix);
            if (m_tool_type == ToolType::SMART_FILL
                || m_tool_type == ToolType::BUCKET_FILL
                || (m_tool_type == ToolType::BRUSH
                    && m_cursor_type == TriangleSelector::CursorType::POINTER))
            {
                for (const VolumeHitPoint& volume_hit_point : volume_hit_points) {
                    assert(volume_hit_point.volume_idx == volume_idx);
                    const Vec3f hit_position = volume_hit_point.volume_hit_position.cast<float>();
                    const int facet_idx      = int(volume_hit_point.facet_idx);

                    triangle_selector.seed_fill_apply_on_triangles(new_state);

                    if (m_tool_type == ToolType::SMART_FILL) {
                        triangle_selector.seed_fill_select_triangles(
                            hit_position,
                            facet_idx,
                            trafo_matrix_no_translate,
                            clp,
                            m_smart_fill_angle,
                            SmartFillGapArea,
                            (m_paint_on_overhangs_only ? m_highlight_by_angle_threshold_deg : 0.f),
                            TriangleSelector::ForceReselection::YES
                        );
                    } else if (m_tool_type == ToolType::BRUSH
                               && m_cursor_type == TriangleSelector::CursorType::POINTER)
                    {
                        triangle_selector.bucket_fill_select_triangles(
                            hit_position,
                            facet_idx,
                            clp,
                            m_bucket_fill_angle,
                            BucketFillGapArea,
                            TriangleSelector::BucketFillPropagate::NO,
                            TriangleSelector::ForceReselection::YES
                        );
                    } else if (m_tool_type == ToolType::BUCKET_FILL) {
                        triangle_selector.bucket_fill_select_triangles(
                            hit_position,
                            facet_idx,
                            clp,
                            m_bucket_fill_angle,
                            BucketFillGapArea,
                            TriangleSelector::BucketFillPropagate::YES,
                            TriangleSelector::ForceReselection::YES
                        );
                    }

                    m_seed_fill_last_mesh_id = -1;
                }
            } else if (m_tool_type == ToolType::BRUSH) {
                assert(
                    m_cursor_type == TriangleSelector::CursorType::CIRCLE
                    || m_cursor_type == TriangleSelector::CursorType::SPHERE
                );

                if (volume_hit_points.size() == 1) {
                    const VolumeHitPoint& first_hit = volume_hit_points.front();
                    std::unique_ptr<TriangleSelector::Cursor> cursor =
                        TriangleSelector::SinglePointCursor::cursor_factory(
                            first_hit.volume_hit_position.cast<float>(),
                            camera_pos,
                            m_cursor_radius,
                            m_cursor_type,
                            trafo_matrix,
                            clp,
                            this->get_cursor_edge_limit()
                        );
                    triangle_selector.select_patch(
                        int(first_hit.facet_idx),
                        std::move(cursor),
                        new_state,
                        trafo_matrix_no_translate,
                        m_triangle_splitting_enabled,
                        m_paint_on_overhangs_only ? m_highlight_by_angle_threshold_deg : 0.f
                    );
                } else {
                    for (auto first_hit_it = volume_hit_points.cbegin();
                         first_hit_it != volume_hit_points.cend() - 1;
                         ++first_hit_it)
                    {
                        auto second_hit_it = first_hit_it + 1;
                        std::unique_ptr<TriangleSelector::Cursor> cursor =
                            TriangleSelector::DoublePointCursor::cursor_factory(
                                first_hit_it->volume_hit_position.cast<float>(),
                                second_hit_it->volume_hit_position.cast<float>(),
                                camera_pos,
                                m_cursor_radius,
                                m_cursor_type,
                                trafo_matrix,
                                clp,
                                this->get_cursor_edge_limit()
                            );
                        triangle_selector.select_patch(
                            int(first_hit_it->facet_idx),
                            std::move(cursor),
                            new_state,
                            trafo_matrix_no_translate,
                            m_triangle_splitting_enabled,
                            m_paint_on_overhangs_only ? m_highlight_by_angle_threshold_deg : 0.f
                        );
                    }
                }
            } else if (m_tool_type == ToolType::HEIGHT_RANGE) {
                for (const VolumeHitPoint& volume_hit_point : volume_hit_points) {
                    const Vec3d& hit_position     = volume_hit_point.volume_hit_position;
                    const int facet_idx           = int(volume_hit_point.facet_idx);
                    const BoundingBoxf3 mesh_bbox = m_paintable_volumes[volume_hit_point.volume_idx]
                                                        .model_volume.mesh()
                                                        .bounding_box();

                    std::unique_ptr<TriangleSelector::Cursor> cursor =
                        std::make_unique<TriangleSelector::HeightRange>(
                            hit_position.cast<float>(),
                            mesh_bbox,
                            m_height_range_z_range,
                            trafo_matrix,
                            clp
                        );
                    triangle_selector.select_patch(
                        facet_idx,
                        std::move(cursor),
                        new_state,
                        trafo_matrix_no_translate,
                        m_triangle_splitting_enabled,
                        m_paint_on_overhangs_only ? m_highlight_by_angle_threshold_deg : 0.f
                    );
                }
            } else if (m_tool_type == ToolType::COLOR_REPLACE) {
                for (const VolumeHitPoint& volume_hit_point : volume_hit_points) {
                    assert(volume_hit_point.volume_idx == volume_idx);
                    const Vec3f hit_position = volume_hit_point.volume_hit_position.cast<float>();
                    const int facet_idx      = int(volume_hit_point.facet_idx);

                    for (TriangleSelectorRenderWrapper& wrapper : m_triangle_selector_wrappers) {
                        wrapper.triangle_selector().seed_fill_apply_on_triangles(new_state);
                    }

                    ASSERT(m_tool_type == ToolType::COLOR_REPLACE);
                    const std::optional<TriangleStateType> selected_state =
                        triangle_selector.color_replace_select_triangles(
                            hit_position,
                            facet_idx,
                            clp,
                            TriangleSelector::ForceReselection::YES
                        );

                    if (selected_state.has_value()) {
                        for (TriangleSelectorRenderWrapper& wrapper : m_triangle_selector_wrappers)
                        {
                            const size_t idx = &wrapper - m_triangle_selector_wrappers.data();
                            const Transform3d& volume_trafo_matrix =
                                m_paintable_volumes[idx].world_trafo;
                            const TriangleSelector::ClippingPlane& volume_clp =
                                this->get_clipping_plane_in_volume_coordinates(volume_trafo_matrix);
                            wrapper.triangle_selector().select_triangles_by_state_type(
                                selected_state.value(),
                                volume_clp
                            );
                        }
                    }

                    m_seed_fill_last_mesh_id = -1;
                }
            }

            if (m_tool_type == ToolType::COLOR_REPLACE) {
                for (TriangleSelectorRenderWrapper& wrapper : m_triangle_selector_wrappers) {
                    wrapper.update_painted_geometry(m_device);
                }
            } else {
                triangle_selector_wrapper.update_painted_geometry(m_device);
            }

            m_last_mouse_click = gizmo_event.mouse_position;
        }

        return true;
    }

    if (gizmo_event.type == PaintOnGizmoEvent::Type::Moving
        && (m_tool_type == ToolType::SMART_FILL
            || m_tool_type == ToolType::BUCKET_FILL
            || m_tool_type == ToolType::COLOR_REPLACE
            || (m_tool_type == ToolType::BRUSH
                && m_cursor_type == TriangleSelector::CursorType::POINTER)))
    {
        if (m_triangle_selector_wrappers.empty()) {
            return false;
        }

        // Now "click" into all the prepared points and spill paint around them.
        this->update_raycast_cache(gizmo_event.mouse_position, m_scene_presenter.scene().camera());

        const VolumeHitPoint& hit = m_raycast_cache.hit;
        if (hit.volume_idx == -1) {
            // Clean selected by seed fill for all triangles in all volumes when a mouse isn't pointing on any volume.
            this->seed_fill_unselect_all();

            // In case we have no valid hit, we can return.
            return false;
        }

        // The mouse moved from one object's volume to another one. So it is needed to unselect all triangles selected by seed fill.
        if (hit.volume_idx != m_seed_fill_last_mesh_id) {
            this->seed_fill_unselect_all();
        }

        const PaintableVolume& paintable_volume      = m_paintable_volumes[hit.volume_idx];
        const Transform3d& trafo_matrix              = paintable_volume.world_trafo;
        const Transform3d& trafo_matrix_no_translate = paintable_volume.world_trafo_no_translate;
        TriangleSelectorRenderWrapper& triangle_selector_wrapper =
            m_triangle_selector_wrappers[hit.volume_idx];
        TriangleSelector& triangle_selector = triangle_selector_wrapper.triangle_selector();

        assert(hit.volume_idx < int(m_triangle_selector_wrappers.size()));
        const TriangleSelector::ClippingPlane& clp =
            this->get_clipping_plane_in_volume_coordinates(trafo_matrix);
        const Vec3f hit_position = hit.volume_hit_position.cast<float>();
        if (m_tool_type == ToolType::SMART_FILL) {
            triangle_selector.seed_fill_select_triangles(
                hit_position,
                static_cast<int>(hit.facet_idx),
                trafo_matrix_no_translate,
                clp,
                m_smart_fill_angle,
                SmartFillGapArea,
                m_paint_on_overhangs_only ? m_highlight_by_angle_threshold_deg : 0.f
            );
        } else if (m_tool_type == ToolType::BRUSH
                   && m_cursor_type == TriangleSelector::CursorType::POINTER)
        {
            triangle_selector.bucket_fill_select_triangles(
                hit_position,
                static_cast<int>(hit.facet_idx),
                clp,
                m_bucket_fill_angle,
                BucketFillGapArea,
                TriangleSelector::BucketFillPropagate::NO
            );
        } else if (m_tool_type == ToolType::BUCKET_FILL) {
            triangle_selector.bucket_fill_select_triangles(
                hit_position,
                static_cast<int>(hit.facet_idx),
                clp,
                m_bucket_fill_angle,
                BucketFillGapArea,
                TriangleSelector::BucketFillPropagate::YES
            );
        } else if (m_tool_type == ToolType::COLOR_REPLACE) {
            const std::optional<TriangleStateType> selected_state =
                triangle_selector.color_replace_select_triangles(
                    hit_position,
                    static_cast<int>(hit.facet_idx),
                    clp
                );

            if (selected_state.has_value()) {
                for (TriangleSelectorRenderWrapper& wrapper : m_triangle_selector_wrappers) {
                    const size_t idx = &wrapper - m_triangle_selector_wrappers.data();
                    const Transform3d& volume_trafo_matrix = m_paintable_volumes[idx].world_trafo;
                    const TriangleSelector::ClippingPlane& volume_clp =
                        this->get_clipping_plane_in_volume_coordinates(volume_trafo_matrix);
                    wrapper.triangle_selector().select_triangles_by_state_type(
                        selected_state.value(),
                        volume_clp
                    );
                }
            }
        }

        if (m_tool_type == ToolType::COLOR_REPLACE) {
            for (TriangleSelectorRenderWrapper& wrapper : m_triangle_selector_wrappers) {
                wrapper.update_painted_geometry(m_device);
            }
        } else {
            triangle_selector_wrapper.update_painted_geometry(m_device);
        }

        m_seed_fill_last_mesh_id = hit.volume_idx;
        return true;
    }

    if ((gizmo_event.type == PaintOnGizmoEvent::Type::LeftUp
         || gizmo_event.type == PaintOnGizmoEvent::Type::RightUp)
        && m_button_down != Button::None)
    {
        this->apply_painting_to_model();
        this->on_painting_stroke_applied();

        m_button_down      = Button::None;
        m_last_mouse_click = Vec2d::Zero();
        return true;
    }

    return false;
}

Scene::GizmoActivationState
PaintOnGizmoBase::on_mouse(Scene::GizmoEventContext& ctx, bool only_active)
{
    using namespace Slic3r::App::Platform;
    using namespace Slic3r::App::Scene;

    const MouseEvent& mouse_event = ctx.mouse_event();
    const Vec2d mouse_position = Vec2f(ctx.screen_mouse_x(), ctx.screen_mouse_y()).cast<double>();

    const bool is_left_button_event =
        (mouse_event.button() & MouseButton::Left) == MouseButton::Left;
    const bool is_right_button_event =
        (mouse_event.button() & MouseButton::Right) == MouseButton::Right;

    const bool alt_down   = (mouse_event.key_modifiers() & KeyModifiers(KeyModifier::Alt)) != 0;
    const bool ctrl_down  = (mouse_event.key_modifiers() & KeyModifiers(KeyModifier::Ctrl)) != 0;
    const bool shift_down = (mouse_event.key_modifiers() & KeyModifiers(KeyModifier::Shift)) != 0;

    if (!m_paintable_volumes.empty()) {
        this->update_raycast_cache(mouse_position, m_scene_presenter.scene().camera());
    }

    if (mouse_event.type() == MouseEvent::Type::Wheel) {
        const float wheel_rotation =
            mouse_event.wheel_delta_y() / std::abs(mouse_event.wheel_delta_y());
        const PaintOnGizmoEvent::Type wheel_event_type =
            (wheel_rotation > 0.f ? PaintOnGizmoEvent::Type::MouseWheelUp :
                                    PaintOnGizmoEvent::Type::MouseWheelDown);

        if (this->process_gizmo_event(
                {wheel_event_type, mouse_position, shift_down, alt_down, ctrl_down},
                ctx
            ))
        {
            return GizmoActivationState::Done;
        }
    }

    if (mouse_event.type() == MouseEvent::Type::Move && !m_mouse_dragging) {
        this->process_gizmo_event(
            {PaintOnGizmoEvent::Type::Moving, mouse_position, shift_down, alt_down, false},
            ctx
        );
        return GizmoActivationState::Inactive;
    }

    if (is_left_button_event && mouse_event.type() == MouseEvent::Type::ButtonDown) {
        if (!ctrl_down
            && this->process_gizmo_event(
                {PaintOnGizmoEvent::Type::LeftDown,
                 mouse_position,
                 shift_down,
                 alt_down,
                 ctrl_down},
                ctx
            ))
        {
            m_mouse_dragging = true;
            return GizmoActivationState::Active;
        }
    } else if (is_right_button_event && mouse_event.type() == MouseEvent::Type::ButtonDown) {
        if (!ctrl_down
            && this->process_gizmo_event(
                {PaintOnGizmoEvent::Type::RightDown, mouse_position, false, false, ctrl_down},
                ctx
            ))
        {
            m_mouse_dragging = true;
            return GizmoActivationState::Active;
        }
    } else if (mouse_event.type() == MouseEvent::Type::Move && m_mouse_dragging) {
        if (!ctrl_down
            && this->process_gizmo_event(
                {PaintOnGizmoEvent::Type::Dragging,
                 mouse_position,
                 shift_down,
                 alt_down,
                 ctrl_down},
                ctx
            ))
        {
            return GizmoActivationState::Active;
        }

        if (ctrl_down && mouse_event.type() == MouseEvent::Type::ButtonDown) {
            // CTRL has been pressed while already dragging, so stop the current action.
            if (is_left_button_event) {
                this->process_gizmo_event(
                    {PaintOnGizmoEvent::Type::LeftUp,
                     mouse_position,
                     shift_down,
                     alt_down,
                     ctrl_down},
                    ctx
                );
                m_mouse_dragging = false;
            } else if (is_right_button_event) {
                this->process_gizmo_event(
                    {PaintOnGizmoEvent::Type::RightUp,
                     mouse_position,
                     shift_down,
                     alt_down,
                     ctrl_down},
                    ctx
                );
                m_mouse_dragging = false;
            }

            return GizmoActivationState::Inactive;
        }
    } else if (is_left_button_event && mouse_event.type() == MouseEvent::Type::ButtonUp) {
        this->process_gizmo_event(
            {PaintOnGizmoEvent::Type::LeftUp, mouse_position, shift_down, alt_down, ctrl_down},
            ctx
        );
        m_mouse_dragging = false;
        return GizmoActivationState::Active;
    } else if (is_right_button_event && mouse_event.type() == MouseEvent::Type::ButtonUp) {
        this->process_gizmo_event(
            {PaintOnGizmoEvent::Type::RightUp, mouse_position, shift_down, alt_down, ctrl_down},
            ctx
        );
        m_mouse_dragging = false;
        return GizmoActivationState::Active;
    }

    return GizmoActivationState::Inactive;
}

ColorRGBA PaintOnGizmoBase::create_default_painting_color(const ModelVolume& model_volume) const
{
    return Algorithms::Color::saturate(ColorRGBA::WHITE(), 0.25f);
}

PaintingPalette PaintOnGizmoBase::create_painting_colors() const
{
    return {
        {ColorRGBA(0.47f, 0.47f, 1.f, 1.f), 1},
        {ColorRGBA(1.f, 0.44f, 0.44f, 1.f), 2},
    };
}

ColorRGBA PaintOnGizmoBase::get_cursor_sphere_left_button_color() const
{
    return {0.f, 0.f, 1.f, 0.25f};
}

ColorRGBA PaintOnGizmoBase::get_cursor_sphere_right_button_color() const
{
    return {1.f, 0.f, 0.f, 0.25f};
}

ColorRGBA PaintOnGizmoBase::get_sphere_cursor_color() const
{
    if (m_button_down == Button::Left) {
        return this->get_cursor_sphere_left_button_color();
    } else if (m_button_down == Button::Right) {
        return this->get_cursor_sphere_right_button_color();
    }

    return {0.f, 0.f, 0.f, 0.25f};
}

void PaintOnGizmoBase::update_cursors()
{
    m_sphere_cursor_render_wrapper.set_enabled(
        m_tool_type == ToolType::BRUSH && m_cursor_type == TriangleSelector::CursorType::SPHERE
    );
    m_circle_cursor_render_wrapper.set_enabled(
        m_tool_type == ToolType::BRUSH && m_cursor_type == TriangleSelector::CursorType::CIRCLE
    );
    m_height_range_cursor_render_wrapper.set_enabled(m_tool_type == ToolType::HEIGHT_RANGE);

    const VolumeHitPoint& hit = m_raycast_cache.hit;
    if (!m_paintable_volumes.empty() && hit.volume_idx != -1) {
        const Scene::Camera& camera             = m_scene_presenter.scene().camera();
        const PaintableVolume& paintable_volume = m_paintable_volumes[hit.volume_idx];
        const Vec3d cursor_position_world = paintable_volume.world_trafo * hit.volume_hit_position;

        if (m_tool_type == ToolType::BRUSH) {
            if (m_cursor_type == TriangleSelector::CursorType::SPHERE) {
                const ColorRGBA sphere_cursor_color = this->get_sphere_cursor_color();

                m_sphere_cursor_render_wrapper.update_cursor_geometry(
                    cursor_position_world,
                    m_cursor_radius,
                    sphere_cursor_color
                );
            } else if (m_cursor_type == TriangleSelector::CursorType::CIRCLE) {
                m_circle_cursor_render_wrapper.update_cursor_geometry(
                    m_device,
                    camera,
                    cursor_position_world,
                    m_cursor_radius
                );
            }
        } else if (m_tool_type == ToolType::HEIGHT_RANGE) {
            m_height_range_cursor_render_wrapper.update_cursor_geometry(
                m_device,
                hit.volume_hit_position,
                m_height_range_z_range,
                paintable_volume.model_volume.mesh(),
                paintable_volume.world_trafo
            );
        }
    } else {
        m_sphere_cursor_render_wrapper.set_enabled(false);
        m_circle_cursor_render_wrapper.set_enabled(false);
        m_height_range_cursor_render_wrapper.set_enabled(false);
    }
}

void PaintOnGizmoBase::rebuild_paintable_geometry()
{
    using MeshManager = PlaterScenePresenter::MeshManager;

    const SceneInteractor& scene_interactor = m_project_interactor.scene_interactor();
    const ObjectSelection& object_selection = scene_interactor.object_selection();
    const Project& project                  = m_project_interactor.selected_project();
    const MeshManager& mesh_manager         = m_scene_presenter.model_triangle_mesh_manager();
    Scene::Scene& scene                     = m_scene_presenter.scene();

    this->restore_visible_volumes();

    // Remove all the scene nodes created by this gizmo.
    if (m_main_node != nullptr) {
        scene.remove_child(m_main_node);
        m_main_node                     = nullptr;
        m_cursors_node                  = nullptr;
        m_triangle_selectors_node       = nullptr;
        m_clipping_plane_presenter_node = nullptr;
        m_sinking_plane_presenter_node  = nullptr;
    }

    m_visible_volumes_nodes.clear();

    if (object_selection.empty() || object_selection.mode != Biz::Scene::SelectionMode::Instance) {
        m_gizmo_controller->deactivate_current_tool();
        return;
    }

    std::optional<PaintableVolumes> paintable_volumes =
        collect_paintable_volumes(object_selection, project, mesh_manager);
    if (!paintable_volumes) {
        m_gizmo_controller->deactivate_current_tool();
        return;
    }

    m_visible_volumes_nodes = collect_visible_volumes_nodes(project, scene);
    m_paintable_volumes     = std::move(*paintable_volumes);

    this->hide_visible_volumes();
    this->init_main_nodes();
    this->init_cursors_nodes();

    m_triangle_selector_wrappers.clear();

    for (PaintableVolume& paintable_volume : m_paintable_volumes) {
        const ModelVolume& model_volume           = paintable_volume.model_volume;
        const Domain::TriangleMesh& triangle_mesh = model_volume.mesh();

        m_triangle_selector_wrappers.emplace_back(
            triangle_mesh,
            paintable_volume.aabb_mesh,
            m_painting_colors,
            this->create_default_painting_color(model_volume)
        );
        // Reset of TriangleSelector is done inside TriangleSelector's constructor, so we don't need it to perform it again in deserialize().
        m_triangle_selector_wrappers.back().triangle_selector().deserialize(
            this->get_facets_annotation(model_volume).get_data(),
            false
        );
        m_triangle_selector_wrappers.back().init_painted_mesh_node(
            m_device,
            scene,
            *m_triangle_selectors_node,
            paintable_volume.world_trafo
        );
        m_triangle_selector_wrappers.back().init_painted_contour_node(
            m_device,
            scene,
            *m_triangle_selectors_node,
            paintable_volume.world_trafo
        );
    }

    this->init_clipper_presenters();

    this->update_clipping_plane();
    this->update_overhang_detection();
}

void PaintOnGizmoBase::render_scene(Render::CommandBuffer& cmd_buffer)
{
    this->update_cursors();
}

bool PaintOnGizmoBase::enabled() const
{
    const ObjectSelection& selection = m_scene_interactor.object_selection();
    if (selection.state() != SelectionState::WholeInstance) {
        return false;
    }

    const ConfigContainer* config_container =
        m_project_interactor.selected_project()
            .find_config_container(m_project_interactor.selected_config_container_id());

    return config_container != nullptr
        && config_container->print_technology() == PrinterTechnology::FFF;
}

void PaintOnGizmoBase::provide_gizmo_controller(Scene::IGizmoController& gizmo_controller)
{
    m_gizmo_controller = &gizmo_controller;
}

TriangleSelector::ClippingPlane PaintOnGizmoBase::get_clipping_plane_in_volume_coordinates(
    const Transform3d& trafo
) const
{
    const ClippingPlane& clipping_plane = m_clipping_plane_clipper.get_clipping_plane();
    if (!clipping_plane.is_active()) {
        return {};
    }

    const Vec3d clp_normal  = clipping_plane.get_normal();
    const double clp_offset = clipping_plane.get_offset();

    const Transform3d trafo_normal = Transform3d(trafo.linear().transpose());
    const Transform3d trafo_inv    = trafo.inverse();

    Vec3d point_on_plane             = clp_normal * clp_offset;
    Vec3d point_on_plane_transformed = trafo_inv * point_on_plane;
    Vec3d normal_transformed         = trafo_normal * clp_normal;
    auto offset_transformed          = float(point_on_plane_transformed.dot(normal_transformed));

    return TriangleSelector::ClippingPlane(
        {float(normal_transformed.x()),
         float(normal_transformed.y()),
         float(normal_transformed.z()),
         offset_transformed}
    );
}

// Interpolate points between the previous and current mouse positions, which are then projected onto the object.
// Returned hit points are grouped by volume_index. It may contain multiple VolumeHitPoints
// with the same volume_index, but all items in VolumeHitPoints always have the same volume_index.
std::vector<PaintOnGizmoBase::VolumeHitPoints> PaintOnGizmoBase::get_projected_mouse_positions(
    const Vec2d& mouse_position,
    const Scene::Camera& camera,
    const double resolution
) const
{
    // List of mouse positions that will be used as seeds for painting.
    std::vector<Vec2d> mouse_positions{mouse_position};
    if (m_last_mouse_click != Vec2d::Zero()) {
        // In case current mouse position is far from the last one,
        // add several positions from between into the list, so there
        // are no gaps in the painted region.
        if (size_t patches_in_between =
                size_t((mouse_position - m_last_mouse_click).norm() / resolution);
            patches_in_between > 0)
        {
            const Vec2d diff = (m_last_mouse_click - mouse_position) / (patches_in_between + 1);
            for (size_t patch_idx = 1; patch_idx <= patches_in_between; ++patch_idx) {
                mouse_positions.emplace_back(mouse_position + patch_idx * diff);
            }

            mouse_positions.emplace_back(m_last_mouse_click);
        }
    }

    std::vector<VolumeHitPoint> volume_hit_points;
    volume_hit_points.reserve(mouse_positions.size());

    // In volume_hit_points only the last item could have volume_index == -1, any other items mustn't.
    for (const Vec2d& mp : mouse_positions) {
        this->update_raycast_cache(mp, camera);
        volume_hit_points.push_back(m_raycast_cache.hit);
        if (m_raycast_cache.hit.volume_idx == -1) {
            break;
        }
    }

    // Divide volume_hit_points into groups with the same volume_index. It may contain multiple groups with the same volume_index.
    std::vector<VolumeHitPoints> volume_hit_points_by_volume;
    for (size_t prev_hit_point = 0, curr_hit_point = 0; curr_hit_point < volume_hit_points.size();
         ++curr_hit_point)
    {
        size_t next_hit_point = curr_hit_point + 1;
        if (next_hit_point >= volume_hit_points.size()
            || volume_hit_points[curr_hit_point].volume_idx
                != volume_hit_points[next_hit_point].volume_idx)
        {
            volume_hit_points_by_volume.emplace_back();
            volume_hit_points_by_volume.back().insert(
                volume_hit_points_by_volume.back().end(),
                volume_hit_points.begin() + int(prev_hit_point),
                volume_hit_points.begin() + int(next_hit_point)
            );
            prev_hit_point = next_hit_point;
        }
    }

    auto on_same_facet = [](const VolumeHitPoints& hit_points) -> bool
    {
        for (const VolumeHitPoint& hit_point : hit_points) {
            if (hit_point.facet_idx != hit_points.front().facet_idx) {
                return false;
            }
        }

        return true;
    };

    struct Plane
    {
        Vec3d origin;
        Vec3d first_axis;
        Vec3d second_axis;
    };

    auto find_plane = [](const VolumeHitPoints& hit_points) -> std::optional<Plane>
    {
        assert(hit_points.size() >= 3);
        for (size_t third_idx = 2; third_idx < hit_points.size(); ++third_idx) {
            const Vec3d& first_point = hit_points[third_idx - 2].volume_hit_position.cast<double>();
            const Vec3d& second_point =
                hit_points[third_idx - 1].volume_hit_position.cast<double>();
            const Vec3d& third_point = hit_points[third_idx].volume_hit_position.cast<double>();

            const Vec3d first_vec  = first_point - second_point;
            const Vec3d second_vec = third_point - second_point;

            // If three points aren't collinear, then there exists only one plane going through all points.
            if (first_vec.cross(second_vec).squaredNorm() > Slic3r::sqr(Domain::EPSILON)) {
                const Vec3d first_axis_vec_n = first_vec.normalized();
                // Make second_vec perpendicular to first_axis_vec_n using Gram–Schmidt orthogonalization process
                const Vec3d second_axis_vec_n =
                    (second_vec
                     - (first_vec.dot(second_vec) / first_vec.dot(first_vec)) * first_vec)
                        .normalized();
                return Plane{second_point, first_axis_vec_n, second_axis_vec_n};
            }
        }

        return std::nullopt;
    };

    for (VolumeHitPoints& hit_points : volume_hit_points_by_volume) {
        assert(!hit_points.empty());
        if (hit_points.back().volume_idx == -1) {
            break;
        }

        if (hit_points.size() <= 2) {
            continue;
        }

        if (on_same_facet(hit_points)) {
            hit_points = {hit_points.front(), hit_points.back()};
        } else if (std::optional<Plane> plane = find_plane(hit_points); plane) {
            Polyline polyline;
            polyline.points.reserve(hit_points.size());
            // Project hit_points into its plane to simplify them in the next step.
            for (const VolumeHitPoint& hit_point : hit_points) {
                const Vec3d& point  = hit_point.volume_hit_position.cast<double>();
                const double x_cord = plane->first_axis.dot(point - plane->origin);
                const double y_cord = plane->second_axis.dot(point - plane->origin);
                polyline.points.emplace_back(scale_(x_cord), scale_(y_cord));
            }

            Algorithms::Polyline::simplify(polyline, scale_(m_cursor_radius) / 10.);

            const int volume_idx = hit_points.front().volume_idx;
            VolumeHitPoints new_hit_points;
            new_hit_points.reserve(polyline.points.size());
            // Project 2D simplified hit_points back to 3D.
            for (const Point& point : polyline.points) {
                const double x_cord = Algorithms::Scaling::unscaled<double>(point.x());
                const double y_cord = Algorithms::Scaling::unscaled<double>(point.y());
                const Vec3d new_hit_point =
                    plane->origin + x_cord * plane->first_axis + y_cord * plane->second_axis;
                const int facet_idx = MeshRaycaster::get_closest_facet(
                    m_paintable_volumes[volume_idx].aabb_mesh,
                    new_hit_point.cast<float>()
                );
                new_hit_points.push_back({new_hit_point, volume_idx, size_t(facet_idx)});
            }

            hit_points = new_hit_points;
        } else {
            hit_points = {hit_points.front(), hit_points.back()};
        }
    }

    return volume_hit_points_by_volume;
}

bool PaintOnGizmoBase::is_mesh_point_clipped(const Vec3d& point, const Transform3d& trafo) const
{
    if (m_clipping_plane_clipper.get_position() == 0.) {
        return false;
    }

    Vec3d transformed_point = trafo * point;

    // TODO: SLA shift isn't implemented yet, resolve it later.
    // transformed_point.z() += m_c->selection_info()->get_sla_shift();

    return m_clipping_plane_clipper.get_clipping_plane().is_point_clipped(transformed_point);
}

void PaintOnGizmoBase::update_raycast_cache(
    const Vec2d& mouse_position,
    const Scene::Camera& camera
) const
{
    // Check if we have a cached result for this mouse position.
    if (m_raycast_cache.mouse_position == mouse_position) {
        return;
    }

    // Cache is not valid, perform a new raycast and store it.
    const VolumeHitPoint hit = perform_raycast(mouse_position, camera);
    m_raycast_cache          = {mouse_position, hit};
}

PaintOnGizmoBase::VolumeHitPoint
PaintOnGizmoBase::perform_raycast(const Vec2d& mouse_position, const Scene::Camera& camera) const
{
    Vec3d closest_hit_position          = Vec3d::Zero();
    double closest_hit_squared_distance = std::numeric_limits<double>::max();
    size_t closest_facet_idx            = 0;
    int closest_volume_idx              = -1;

    // Cast a ray on all PaintableVolumes, pick the closest hit.
    for (const PaintableVolume& paintable_volume : m_paintable_volumes) {
        const size_t volume_idx = &paintable_volume - &m_paintable_volumes.front();

        const Scene::Ray ray = camera.ray_at(mouse_position.x(), mouse_position.y());

        const std::optional<MeshRaycaster::UnprojectResult> unproject_result =
            MeshRaycaster::unproject_on_mesh(
                paintable_volume.aabb_mesh,
                ray,
                paintable_volume.world_trafo,
                m_clipping_plane_clipper.get_clipping_plane(),
                false
            );
        if (!unproject_result.has_value()) {
            continue;
        }

        // In case this hit is clipped, skip it.
        if (is_mesh_point_clipped(unproject_result->position, paintable_volume.world_trafo)) {
            continue;
        }

        // Is this hit the closest to the camera so far?
        double hit_squared_distance =
            (ray.origin - paintable_volume.world_trafo * unproject_result->position).squaredNorm();
        if (hit_squared_distance < closest_hit_squared_distance) {
            closest_hit_squared_distance = hit_squared_distance;
            closest_facet_idx            = unproject_result->facet_idx;
            closest_volume_idx           = static_cast<int>(volume_idx);
            closest_hit_position         = unproject_result->position;
        }
    }

    return {closest_hit_position, closest_volume_idx, closest_facet_idx};
}

} // namespace Slic3r::App::Plater
