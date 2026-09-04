#include "Slic3r/App/Plater/VariableLayerHeightGizmo.hpp"

#include "Slic3r/App/Plater/PlaterSceneLayer.hpp"
#include "Slic3r/App/Plater/PlaterScenePresenter.hpp"
#include "Slic3r/App/Plater/VariableLayerHeightDialog.hpp"
#include "Slic3r/App/Render/Device.hpp"
#include "Slic3r/App/Scene/AuxiliaryElementId.hpp"
#include "Slic3r/App/Scene/MeshRenderNodeComponent.hpp"
#include "Slic3r/App/Scene/ModelGeometryProvider.hpp"
#include "Slic3r/App/Scene/NodeBuilder.hpp"
#include "Slic3r/Biz/Algorithms/LayerHeight.hpp"
#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Utils/MeshRaycaster.hpp"
#include "Slic3r/Domain/FullConfigFDM.hpp"
#include "Slic3r/Domain/Project.hpp"
#include "Slic3r/Domain/Types.hpp"

#include <algorithm>
#include <magic_enum/magic_enum_flags.hpp>

using namespace Slic3r;
using namespace Slic3r::App::Yoga;
using namespace Slic3r::Biz;
using namespace Slic3r::Biz::Utils;
using namespace magic_enum::bitwise_operators;

using Slic3r::App::Scene::SceneNodeTag;
using Slic3r::App::Scene::ToolType;
using Slic3r::Biz::Algorithms::LayerHeight::AdaptiveParams;
using Slic3r::Biz::Algorithms::LayerHeight::AdjustAction;
using Slic3r::Biz::Algorithms::LayerHeight::AdjustParams;
using Slic3r::Biz::Algorithms::LayerHeight::GenerateLayersParams;
using Slic3r::Biz::Algorithms::LayerHeight::SmoothParams;
using Slic3r::Biz::Scene::ObjectSelection;
using Slic3r::Biz::Scene::SceneInteractor;
using Slic3r::Domain::ConfigContainer;
using Slic3r::Domain::ConfigPack;
using Slic3r::Domain::ConfigPackFDM;
using Slic3r::Domain::ConstFindResult;
using Slic3r::Domain::FullConfigFDM;
using Slic3r::Domain::FullConfigFDMPtr;
using Slic3r::Domain::LayerConfigRanges;
using Slic3r::Domain::LayerHeightProfile;
using Slic3r::Domain::LayerZRanges;
using Slic3r::Domain::ModelInstance;
using Slic3r::Domain::ModelObject;
using Slic3r::Domain::ModelVolume;
using Slic3r::Domain::PrinterTechnology;
using Slic3r::Domain::Project;
using Slic3r::Domain::SelectionId;
using Slic3r::Domain::SquareMatrix3f;
using Slic3r::Domain::Transform3d;
using Slic3r::Domain::Vec2d;
using Slic3r::Domain::Vec3d;
using Slic3r::Domain::ZHeightPairs;

namespace Slic3r::App::Plater {

const constexpr double LAYER_HEIGHT_ADJUST_STRENGTH = 0.005;

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

static std::optional<VariableLayerHeightGizmo::SelectedObjectData> collect_selected_object_data(
    const ObjectSelection& object_selection,
    Project& project,
    const PlaterScenePresenter::MeshManager& mesh_manager
)
{
    if (object_selection.elements.size() != 1) {
        return std::nullopt;
    }

    const Domain::ElementRef& first_element = object_selection.elements.front();
    ModelObject* model_object               = project.find_object_by_id(first_element.object_id);
    const ModelInstance* model_instance =
        project.find_instance_by_id(first_element.object_id, first_element.instance_id);

    if (model_object == nullptr || model_instance == nullptr) {
        return std::nullopt;
    }

    VariableLayerHeightGizmo::SelectedObjectData::Volumes volumes;
    for (const ModelVolume* model_volume : model_object->volumes) {
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

        volumes.push_back(
            {.model_volume = *model_volume,
             .aabb_mesh    = mesh->aabb_mesh(),
             .world_trafo  = model_instance->get_matrix() * model_volume->get_matrix()}
        );
    }

    return VariableLayerHeightGizmo::SelectedObjectData{
        .model_object   = model_object,
        .model_instance = model_instance,
        .volumes        = std::move(volumes)
    };
}

static AdjustAction determine_adjust_action(bool left_button_down, bool shift_down)
{
    if (left_button_down) {
        return shift_down ? AdjustAction::Reduce : AdjustAction::Decrease;
    } else {
        return shift_down ? AdjustAction::Smooth : AdjustAction::Increase;
    }
}

VariableLayerHeightGizmo::VariableLayerHeightGizmo(
    Render::Device& device,
    ProjectInteractor& project_interactor,
    PlaterScenePresenter& scene_presenter
) :
    m_device(device),
    m_project_interactor(project_interactor),
    m_scene_interactor(project_interactor.scene_interactor()),
    m_scene_presenter(scene_presenter)
{
    m_dialog = std::make_unique<VariableLayerHeightDialog>();
    m_dialog->set_smart_resolution(m_smart_resolution);
    m_dialog->set_blend_distance(m_blend_distance);
    m_dialog->set_lock_high_detail(m_lock_high_detail);

    m_dialog->callbacks().smart_resolution_changed = [this](const double smart_resolution)
    { m_smart_resolution = smart_resolution; };

    m_dialog->callbacks().blend_distance_changed = [this](const int blend_distance)
    { m_blend_distance = blend_distance; };

    m_dialog->callbacks().lock_high_detail_changed = [this](const bool lock_high_detail)
    { m_lock_high_detail = lock_high_detail; };

    m_dialog->callbacks().auto_calculate_clicked = [this]()
    {
        this->generate_adaptive_layer_height_profile();
        m_project_interactor.undo_provider().take_snapshot(
            UndoSnapshotType::VariableLayerHeightAdaptive
        );
    };

    m_dialog->callbacks().smooth_clicked = [this]()
    {
        this->perform_layer_height_profile_smoothing();
        m_project_interactor.undo_provider().take_snapshot(
            UndoSnapshotType::VariableLayerHeightSmooth
        );
    };

    m_dialog->callbacks().reset_clicked = [this]()
    {
        this->perform_layer_height_profile_reset();
        m_project_interactor.undo_provider().take_snapshot(
            UndoSnapshotType::VariableLayerHeightReset
        );
    };

    m_dialog->callbacks().layer_profile_mouse_move =
        [this](const std::optional<float> cursor_normalized_position)
    {
        GizmoEvent gizmo_event{.type = GizmoEvent::Type::Moving};
        if (cursor_normalized_position.has_value()) {
            gizmo_event.cursor_z = cursor_normalized_position.value()
                * static_cast<float>(m_layer_height_params.object_print_z_uncompensated_height);
        }
        this->process_gizmo_event(gizmo_event);
    };

    m_dialog->callbacks().layer_profile_mouse_down =
        [this](
            const float cursor_normalized_position,
            const bool shift_down,
            const bool ctrl_down,
            const VariableLayerHeightControl::Button mouse_button
        )
    {
        const GizmoEvent gizmo_event{
            .type     = (mouse_button == VariableLayerHeightControl::Button::Left) ?
                    GizmoEvent::Type::LeftDown :
                    GizmoEvent::Type::RightDown,
            .cursor_z = cursor_normalized_position
                * static_cast<float>(m_layer_height_params.object_print_z_uncompensated_height),
            .shift_down = shift_down,
            .ctrl_down  = ctrl_down
        };
        this->process_gizmo_event(gizmo_event);
    };

    m_dialog->callbacks().layer_profile_mouse_drag =
        [this](
            const float cursor_normalized_position,
            const bool shift_down,
            const bool ctrl_down,
            const VariableLayerHeightControl::Button mouse_button
        )
    {
        const GizmoEvent gizmo_event{
            .type     = GizmoEvent::Type::Dragging,
            .cursor_z = cursor_normalized_position
                * static_cast<float>(m_layer_height_params.object_print_z_uncompensated_height),
            .shift_down       = shift_down,
            .ctrl_down        = ctrl_down,
            .left_button_down = (mouse_button == VariableLayerHeightControl::Button::Left)
        };
        this->process_gizmo_event(gizmo_event);
    };

    m_dialog->callbacks().layer_profile_mouse_up =
        [this](const VariableLayerHeightControl::Button mouse_button)
    {
        const GizmoEvent gizmo_event{
            .type = (mouse_button == VariableLayerHeightControl::Button::Left) ?
                GizmoEvent::Type::LeftUp :
                GizmoEvent::Type::RightUp
        };
        this->process_gizmo_event(gizmo_event);
    };

    m_dialog->callbacks().layer_profile_mouse_wheel =
        [this](const float mouse_wheel_delta, const bool ctrl_down)
    {
        const GizmoEvent gizmo_event{
            .type        = GizmoEvent::Type::Wheel,
            .ctrl_down   = ctrl_down,
            .wheel_delta = mouse_wheel_delta
        };
        this->process_gizmo_event(gizmo_event);
    };

    m_dialog->callbacks().on_height_range_click = [this]()
    {
        ASSERT(m_gizmo_controller != nullptr);
        m_gizmo_controller->activate_tool(ToolType::HeightRangeGizmo);
    };
}

VariableLayerHeightGizmo::~VariableLayerHeightGizmo() = default;

ToolType VariableLayerHeightGizmo::type() const
{
    return ToolType::VariableLayerHeightGizmo;
}

bool VariableLayerHeightGizmo::disable_object_selection() const
{
    return true;
}

bool VariableLayerHeightGizmo::enabled() const
{
    const ObjectSelection& selection = m_project_interactor.scene_interactor().object_selection();
    const bool whole_instance = selection.state() == Biz::Scene::SelectionState::WholeInstance;
    const SelectionId config_container_id = m_project_interactor.selected_config_container_id();
    const Project& project =
        m_project_interactor.workbench().project(m_project_interactor.selected_project_id());
    const ConfigContainer* config_container = project.find_config_container(config_container_id);
    if (config_container == nullptr) {
        return false;
    }

    const bool is_fdm = config_container->print_technology() == PrinterTechnology::FFF;
    return whole_instance && is_fdm;
}

std::unique_ptr<GizmoWindow> VariableLayerHeightGizmo::release_ui_window()
{
    return m_dialog.release();
}

void VariableLayerHeightGizmo::provide_gizmo_controller(Scene::IGizmoController& gizmo_controller)
{
    m_gizmo_controller = &gizmo_controller;
}

void VariableLayerHeightGizmo::on_activated()
{
    using MeshManager = PlaterScenePresenter::MeshManager;

    const SceneInteractor& scene_interactor = m_project_interactor.scene_interactor();
    const ObjectSelection& object_selection = scene_interactor.object_selection();
    const ConfigContainer& config_container = m_project_interactor.selected_config_container();
    const MeshManager& mesh_manager         = m_scene_presenter.model_triangle_mesh_manager();
    Project& project                        = m_project_interactor.selected_project();
    Scene::Scene& scene                     = m_scene_presenter.scene();

    if (object_selection.empty() || object_selection.mode != Biz::Scene::SelectionMode::Instance) {
        m_gizmo_controller->deactivate_current_tool();
        return;
    }

    std::optional<SelectedObjectData> selected_object_data =
        collect_selected_object_data(object_selection, project, mesh_manager);
    if (!selected_object_data) {
        m_gizmo_controller->deactivate_current_tool();
        return;
    }

    m_selected_object_data  = std::move(*selected_object_data);
    m_visible_volumes_nodes = collect_visible_volumes_nodes(project, scene);
    m_layer_height_params   = compute_layer_height_params(
        object_selection,
        project,
        config_container,
        m_project_interactor.scene_interactor().bed_selection().last_selected_bed()
    );

    m_mouse_button_down = Button::None;
    m_mouse_dragging    = false;

    this->perform_layer_height_profile_clamping();
    this->set_dialog_layer_heights_profile_parameters();
    this->update_side_panel_layer_height_profile();
    this->update_side_panel_height_ranges();
    this->hide_visible_volumes();
    this->init_main_nodes();
    this->init_mesh_nodes();

    scene.add_listener<Scene::ISceneChangedListener>(this);
    scene.add_listener<IThumbnailRenderListener>(this);
    m_scene_interactor.add_listener<Biz::Scene::ISceneChangedListener>(this);
    m_scene_interactor.add_listener<ISceneSelectionChangedListener>(this);
}

void VariableLayerHeightGizmo::on_deactivated()
{
    Scene::Scene& scene = m_scene_presenter.scene();

    // Restore the originally visible nodes.
    this->restore_visible_volumes();

    // Remove all the scene nodes created by this gizmo.
    if (m_main_node != nullptr) {
        scene.remove_child(m_main_node);
        m_main_node = nullptr;
        m_mesh_node = nullptr;
    }

    m_visible_volumes_nodes.clear();
    m_material_wrapper.reset();

    scene.remove_listener<Scene::ISceneChangedListener>(this);
    scene.remove_listener<IThumbnailRenderListener>(this);
    m_scene_interactor.remove_listener<Biz::Scene::ISceneChangedListener>(this);
    m_scene_interactor.remove_listener<ISceneSelectionChangedListener>(this);
}

void VariableLayerHeightGizmo::on_project_activated(size_t new_project_id)
{
    this->on_activated();
}

void VariableLayerHeightGizmo::on_project_deactivated(size_t old_project_id)
{
    this->on_deactivated();
}

void VariableLayerHeightGizmo::on_node_added(Scene::Node* node)
{
    const SceneNodeTag* tag = node->tag_of_type<SceneNodeTag>();
    if (tag == nullptr) {
        return;
    }

    m_visible_volumes_nodes.push_back(node);
    node->set_enabled(false);
}

void VariableLayerHeightGizmo::on_node_removed(Scene::Node* node)
{
    std::erase(m_visible_volumes_nodes, node);
}

void VariableLayerHeightGizmo::on_thumbnail_render_begin()
{
    // Before rendering the thumbnail, hide gizmo nodes and show original model nodes.
    if (m_main_node != nullptr) {
        m_main_node->set_enabled(false);
    }

    this->restore_visible_volumes();
}

void VariableLayerHeightGizmo::on_thumbnail_render_end()
{
    // After rendering the thumbnail, restore gizmo nodes and hide original model nodes,
    if (m_main_node != nullptr) {
        m_main_node->set_enabled(true);
    }

    this->hide_visible_volumes();
}

void VariableLayerHeightGizmo::on_model_reloaded(SelectionId project_id)
{
    if (project_id != m_project_interactor.selected_project_id()) {
        return;
    }

    this->rebuild_gizmo_state();
}

void VariableLayerHeightGizmo::on_scene_selection_changed(
    const SelectionId project_id,
    const ObjectSelection& selection
)
{
    this->rebuild_gizmo_state();
}

Scene::GizmoActivationState
VariableLayerHeightGizmo::on_mouse(Scene::GizmoEventContext& ctx, bool only_active)
{
    using namespace Slic3r::App::Platform;
    using namespace Slic3r::App::Scene;

    const MouseEvent& mouse_event = ctx.mouse_event();
    const Vec2d mouse_position = Vec2f(ctx.screen_mouse_x(), ctx.screen_mouse_y()).cast<double>();

    const bool is_left_button_event =
        (mouse_event.button() & MouseButton::Left) == MouseButton::Left;
    const bool is_right_button_event =
        (mouse_event.button() & MouseButton::Right) == MouseButton::Right;

    const bool ctrl_down =
        (mouse_event.key_modifiers() & static_cast<KeyModifiers>(KeyModifier::Ctrl)) != 0;
    const bool shift_down =
        (mouse_event.key_modifiers() & static_cast<KeyModifiers>(KeyModifier::Shift)) != 0;

    if (mouse_event.type() == MouseEvent::Type::Wheel) {
        const GizmoEvent gizmo_event{
            .type        = GizmoEvent::Type::Wheel,
            .ctrl_down   = ctrl_down,
            .wheel_delta = mouse_event.wheel_delta_y()
        };
        return this->process_gizmo_event(gizmo_event) ? GizmoActivationState::Done :
                                                        GizmoActivationState::Inactive;
    }

    const VolumeHitPoint hit =
        this->perform_raycast(mouse_position, m_scene_presenter.scene().camera());
    const bool hit_volume               = hit.volume_idx >= 0;
    const std::optional<float> cursor_z = hit_volume ?
        std::make_optional(static_cast<float>(hit.world_hit_position.z())) :
        std::nullopt;

    // Store SHIFT, CTRL, and the cursor position to use inside render_scene() when the mouse button is pressed.
    m_last_shift_down = shift_down;
    m_last_ctrl_down  = ctrl_down;
    m_last_cursor_z   = cursor_z;

    GizmoEvent gizmo_event{.cursor_z = cursor_z, .shift_down = shift_down, .ctrl_down = ctrl_down};
    if (mouse_event.type() == MouseEvent::Type::Move && !m_mouse_dragging) {
        gizmo_event.type = GizmoEvent::Type::Moving;
        this->process_gizmo_event(gizmo_event);
        return GizmoActivationState::Inactive;
    }

    if ((is_left_button_event || is_right_button_event)
        && mouse_event.type() == MouseEvent::Type::ButtonDown)
    {
        if (!ctrl_down && hit_volume) {
            gizmo_event.type =
                is_left_button_event ? GizmoEvent::Type::LeftDown : GizmoEvent::Type::RightDown;
            if (this->process_gizmo_event(gizmo_event)) {
                m_mouse_dragging    = true;
                m_mouse_button_down = is_left_button_event ? Button::Left : Button::Right;
                return GizmoActivationState::Active;
            }
        }
    } else if (mouse_event.type() == MouseEvent::Type::Move && m_mouse_dragging) {
        if (hit_volume) {
            gizmo_event.type             = GizmoEvent::Type::Dragging;
            gizmo_event.left_button_down = (m_mouse_button_down == Button::Left);
            if (this->process_gizmo_event(gizmo_event)) {
                return GizmoActivationState::Active;
            }
        }
    } else if ((is_left_button_event || is_right_button_event)
               && mouse_event.type() == MouseEvent::Type::ButtonUp)
    {
        gizmo_event.type =
            is_left_button_event ? GizmoEvent::Type::LeftUp : GizmoEvent::Type::RightUp;
        this->process_gizmo_event(gizmo_event);
        m_mouse_button_down = Button::None;
        m_mouse_dragging    = false;
        m_last_cursor_z     = std::nullopt;
        return GizmoActivationState::Active;
    }

    return GizmoActivationState::Inactive;
}

void VariableLayerHeightGizmo::render_scene(Render::CommandBuffer& cmd_buffer)
{
    if (m_mouse_dragging && m_last_cursor_z.has_value() && m_mouse_button_down != Button::None) {
        const GizmoEvent gizmo_event{
            .type             = GizmoEvent::Type::Dragging,
            .cursor_z         = m_last_cursor_z,
            .shift_down       = m_last_shift_down,
            .ctrl_down        = m_last_ctrl_down,
            .left_button_down = (m_mouse_button_down == Button::Left)
        };
        this->process_gizmo_event(gizmo_event);
    }
}

void VariableLayerHeightGizmo::restore_visible_volumes()
{
    for (Scene::Node* node : m_visible_volumes_nodes) {
        node->set_enabled(true);
    }
}

void VariableLayerHeightGizmo::hide_visible_volumes()
{
    for (Scene::Node* node : m_visible_volumes_nodes) {
        node->set_enabled(false);
    }
}

void VariableLayerHeightGizmo::set_dialog_layer_heights_profile_parameters()
{
    m_dialog->set_object_max_z(
        static_cast<float>(m_layer_height_params.object_print_z_uncompensated_height)
    );
    m_dialog->set_min_layer_height(static_cast<float>(m_layer_height_params.min_layer_height));
    m_dialog->set_max_layer_height(static_cast<float>(m_layer_height_params.max_layer_height));
    m_dialog->set_default_layer_height(static_cast<float>(m_layer_height_params.layer_height));
    m_dialog->set_layer_height_title(m_layer_height_params.layer_height);
    m_dialog->set_cursor_band_width(static_cast<float>(m_band_width));
    m_dialog->reset_cursor_position();
}

void VariableLayerHeightGizmo::init_main_nodes()
{
    Scene::Scene& scene = m_scene_presenter.scene();

    Scene::NodeBuilder main_node_builder{scene};
    main_node_builder.set_debug_name("VariableLayerHeightGizmo - Main node");
    std::unique_ptr<Scene::Node> main_node = main_node_builder.build();
    m_main_node                            = main_node.get();
    scene.add_child(main_node.release(), &scene.root());

    Scene::NodeBuilder mesh_node_builder{scene};
    mesh_node_builder.set_debug_name("VariableLayerHeightGizmo - Mesh node");
    std::unique_ptr<Scene::Node> mesh_node = mesh_node_builder.build();
    m_mesh_node                            = mesh_node.get();
    scene.add_child(mesh_node.release(), m_main_node);
}

void VariableLayerHeightGizmo::init_mesh_nodes()
{
    Scene::Scene& scene = m_scene_presenter.scene();

    m_material_wrapper.init(m_device);
    m_material_wrapper.set_cursor_band_width(static_cast<float>(m_band_width));
    this->update_variable_layer_height_texture();

    ASSERT(m_scene_presenter.model_geometry_provider() != nullptr);
    const Scene::ModelGeometryProvider& geometry_provider =
        *m_scene_presenter.model_geometry_provider();

    for (const SelectedObjectData::Volume& volume : m_selected_object_data.volumes) {
        Scene::AuxiliaryElementId geometry_id = {
            Scene::AuxiliaryElementId::Type::Volume,
            volume.model_volume.id().id
        };
        const Render::Geometry* geometry = geometry_provider.geometry_manager.get(geometry_id);

        ASSERT(geometry != nullptr);

        Scene::NodeBuilder variable_layer_height_mesh_node_builder{scene};
        variable_layer_height_mesh_node_builder
            .set_debug_name("VariableLayerHeightGizmo - Mesh node")
            .set_transform(volume.world_trafo)
            .set_mesh(
                geometry,
                m_material_wrapper.material(),
                static_cast<int>(PlaterSceneLayer::DocumentObjects)
            )
            .set_aabb(volume.aabb_mesh)
            .set_shadows(Render::Shadows{false, false});

        std::unique_ptr<Scene::Node> variable_layer_height_mesh_node =
            variable_layer_height_mesh_node_builder.build();
        scene.add_child(variable_layer_height_mesh_node.release(), m_mesh_node);
    }
}

void VariableLayerHeightGizmo::update_variable_layer_height_texture()
{
    const GenerateLayersParams generate_layers_params{
        .min_layer_height                = m_layer_height_params.min_layer_height,
        .max_layer_height                = m_layer_height_params.max_layer_height,
        .first_object_layer_height       = m_layer_height_params.first_object_layer_height,
        .object_print_z_height           = m_layer_height_params.object_print_z_height,
        .object_shrinkage_compensation_z = m_layer_height_params.object_shrinkage_compensation_z,
        .first_object_layer_height_fixed = m_layer_height_params.first_object_layer_height_fixed
    };

    const LayerZRanges color_layers = Algorithms::LayerHeight::generate_object_layers(
        generate_layers_params,
        m_layer_height_params.layer_height_profile
    );

    const ZHeightPairs& stripe_profile = m_baseline_layer_height_profile.empty() ?
        m_layer_height_params.layer_height_profile :
        m_baseline_layer_height_profile;
    const LayerZRanges stripe_layers =
        Algorithms::LayerHeight::generate_object_layers(generate_layers_params, stripe_profile);

    m_material_wrapper.set_layers(
        color_layers,
        stripe_layers,
        m_layer_height_params.min_layer_height,
        m_layer_height_params.max_layer_height,
        m_layer_height_params.layer_height,
        m_layer_height_params.object_print_z_height,
        static_cast<float>(m_layer_height_params.object_print_z_uncompensated_height)
    );
}

void VariableLayerHeightGizmo::refresh_mesh_nodes_material()
{
    ASSERT(m_mesh_node != nullptr);
    for (const auto& child : m_mesh_node->children()) {
        ASSERT(child->has_render_component());
        Scene::MeshRenderNodeComponent* render_component =
            dynamic_cast<Scene::MeshRenderNodeComponent*>(child->render_component());
        render_component->replace_material(m_material_wrapper.material());
    }

    Biz::Platform::PlatformServices::instance().render_request_handler().request_render();
}

void VariableLayerHeightGizmo::perform_layer_height_profile_adjustment(
    const float layer_z_abs,
    const AdjustAction adjust_action
)
{
    const AdjustParams adjust_params{
        .min_layer_height          = m_layer_height_params.min_layer_height,
        .max_layer_height          = m_layer_height_params.max_layer_height,
        .layer_height              = m_layer_height_params.layer_height,
        .first_object_layer_height = m_layer_height_params.first_object_layer_height,
        .object_print_z_uncompensated_height =
            m_layer_height_params.object_print_z_uncompensated_height,
        .first_object_layer_height_fixed = m_layer_height_params.first_object_layer_height_fixed
    };

    Algorithms::LayerHeight::adjust_layer_height_profile(
        adjust_params,
        m_layer_height_params.layer_height_profile,
        layer_z_abs,
        LAYER_HEIGHT_ADJUST_STRENGTH,
        m_band_width,
        adjust_action
    );

    this->update_variable_layer_height_texture();
    this->refresh_mesh_nodes_material();
    this->update_side_panel_layer_height_profile();
}

void VariableLayerHeightGizmo::perform_layer_height_profile_smoothing()
{
    const SmoothParams smooth_params{
        .min_layer_height                = m_layer_height_params.min_layer_height,
        .max_layer_height                = m_layer_height_params.max_layer_height,
        .layer_height                    = m_layer_height_params.layer_height,
        .first_object_layer_height_fixed = m_layer_height_params.first_object_layer_height_fixed,
        .radius   = std::max<unsigned int>(1, static_cast<unsigned int>(m_blend_distance)),
        .keep_min = m_lock_high_detail
    };

    m_layer_height_params.layer_height_profile = Algorithms::LayerHeight::smooth_height_profile(
        m_layer_height_params.layer_height_profile,
        smooth_params
    );

    this->update_variable_layer_height_texture();
    this->refresh_mesh_nodes_material();
    this->update_side_panel_layer_height_profile();
    this->apply_layer_height_profile_to_model();
}

void VariableLayerHeightGizmo::perform_layer_height_profile_reset()
{
    ASSERT(m_selected_object_data.model_object != nullptr);
    const ModelObject& model_object              = *m_selected_object_data.model_object;
    const LayerConfigRanges& layer_config_ranges = model_object.layer_config_ranges;

    m_layer_height_params.layer_height_profile =
        compute_layer_height_profile(m_layer_height_params, layer_config_ranges);

    this->update_variable_layer_height_texture();
    this->refresh_mesh_nodes_material();
    this->update_side_panel_layer_height_profile();
    this->clear_layer_height_profile_on_model();
}

void VariableLayerHeightGizmo::perform_layer_height_profile_clamping()
{
    const ZHeightPairs original_profile = m_layer_height_params.layer_height_profile;
    ZHeightPairs& clamped_profile       = m_layer_height_params.layer_height_profile;
    Algorithms::LayerHeight::clamp_layer_height_profile(
        clamped_profile,
        m_layer_height_params.min_layer_height,
        m_layer_height_params.max_layer_height
    );

    if (clamped_profile != original_profile) {
        this->apply_layer_height_profile_to_model();
    }
}

void VariableLayerHeightGizmo::generate_adaptive_layer_height_profile()
{
    const float quality_factor = std::clamp(static_cast<float>(m_smart_resolution), 0.f, 1.f);

    const AdaptiveParams adaptive_params{
        .min_layer_height          = m_layer_height_params.min_layer_height,
        .max_layer_height          = m_layer_height_params.max_layer_height,
        .first_object_layer_height = m_layer_height_params.first_object_layer_height,
        .layer_height              = m_layer_height_params.layer_height,
        .object_print_z_uncompensated_height =
            m_layer_height_params.object_print_z_uncompensated_height,
        .first_object_layer_height_fixed = m_layer_height_params.first_object_layer_height_fixed,
    };

    const ModelObject& model_object = *m_selected_object_data.model_object;
    m_layer_height_params.layer_height_profile =
        Algorithms::LayerHeight::layer_height_profile_adaptive(
            adaptive_params,
            model_object,
            quality_factor
        );

    this->update_variable_layer_height_texture();
    this->refresh_mesh_nodes_material();
    this->update_side_panel_layer_height_profile();
    this->apply_layer_height_profile_to_model();
}

void VariableLayerHeightGizmo::apply_layer_height_profile_to_model() const
{
    ASSERT(m_selected_object_data.model_object != nullptr);
    ASSERT(m_selected_object_data.model_instance != nullptr);
    const ModelInstance& model_instance = *m_selected_object_data.model_instance;
    const ModelObject& model_object     = *m_selected_object_data.model_object;

    const Domain::ElementRef object_ref{model_object.id().id, model_instance.id().id};

    const auto layer_height_profile_modificator = [this](ModelObject& object) -> void
    { object.layer_height_profile.set(m_layer_height_params.layer_height_profile); };

    m_scene_interactor.modify_layer_height_profile(object_ref, layer_height_profile_modificator);
}

void VariableLayerHeightGizmo::clear_layer_height_profile_on_model() const
{
    ASSERT(m_selected_object_data.model_object != nullptr);
    ASSERT(m_selected_object_data.model_instance != nullptr);
    const ModelInstance& model_instance = *m_selected_object_data.model_instance;
    const ModelObject& model_object     = *m_selected_object_data.model_object;

    const Domain::ElementRef object_ref{model_object.id().id, model_instance.id().id};

    const auto layer_height_profile_modificator = [](ModelObject& object) -> void
    { object.layer_height_profile.clear(); };

    m_scene_interactor.modify_layer_height_profile(object_ref, layer_height_profile_modificator);
}

void VariableLayerHeightGizmo::update_side_panel_layer_height_profile()
{
    m_dialog->set_layer_height_profile(m_layer_height_params.layer_height_profile);
}

void VariableLayerHeightGizmo::update_side_panel_height_ranges()
{
    const double default_layer_height = m_layer_height_params.layer_height;
    const LayerConfigRanges& layer_config_ranges =
        m_selected_object_data.model_object->layer_config_ranges;

    const HeightRangeEntries height_ranges =
        create_height_ranges_from_config(layer_config_ranges, default_layer_height);
    m_dialog->set_height_ranges(height_ranges);
}

void VariableLayerHeightGizmo::set_cursor_z(const std::optional<float> cursor_z)
{
    if (!cursor_z.has_value() || m_layer_height_params.object_print_z_uncompensated_height <= 0.) {
        m_material_wrapper.set_cursor_z(-1.f);
        m_dialog->reset_cursor_position();
        m_dialog->set_layer_height_title(m_layer_height_params.layer_height);
        return;
    }

    const float layer_height        = this->get_layer_height_at_z(cursor_z.value());
    const float cursor_z_normalized = std::clamp(
        cursor_z.value()
            / static_cast<float>(m_layer_height_params.object_print_z_uncompensated_height),
        0.f,
        1.f
    );

    m_material_wrapper.set_cursor_z(cursor_z.value());
    m_dialog->set_cursor_normalized_position(cursor_z_normalized);
    m_dialog->set_layer_height_title(layer_height);
}

float VariableLayerHeightGizmo::get_layer_height_at_z(float z) const
{
    const ZHeightPairs& profile = m_layer_height_params.layer_height_profile;

    if (profile.size() < 2) {
        // Fallback: Return the layer height from the print profile.
        return static_cast<float>(m_layer_height_params.layer_height);
    }

    for (size_t i = profile.size() - 1; i >= 1; --i) {
        const float layer_z_prev = static_cast<float>(profile[i - 1].z);
        const float layer_z      = static_cast<float>(profile[i].z);
        if (layer_z_prev <= z && z <= layer_z) {
            if (const float layer_z_diff = layer_z - layer_z_prev; layer_z_diff != 0.f) {
                float layer_height_prev = static_cast<float>(profile[i - 1].layer_height);
                float layer_height      = static_cast<float>(profile[i].layer_height);
                return layer_height_prev
                    + (layer_height - layer_height_prev) * (z - layer_z_prev) / layer_z_diff;
            }

            return static_cast<float>(profile[i].layer_height);
        }
    }

    // Fallback: Return the first layer height.
    return static_cast<float>(profile.front().layer_height);
}

bool VariableLayerHeightGizmo::process_gizmo_event(const GizmoEvent& event)
{
    if (event.type == GizmoEvent::Type::Wheel) {
        if (!event.ctrl_down) {
            return false;
        }

        // On macOS with trackpad, wheel_delta can be 0.
        if (event.wheel_delta == 0.f) {
            return true;
        }

        const double wheel_rotation = event.wheel_delta / std::abs(event.wheel_delta);
        m_band_width = std::clamp(m_band_width * (1. + 0.1 * wheel_rotation), 1.5, 10.);

        m_material_wrapper.set_cursor_band_width(static_cast<float>(m_band_width));
        m_dialog->set_cursor_band_width(static_cast<float>(m_band_width));
        this->refresh_mesh_nodes_material();
        return true;
    } else if (event.type == GizmoEvent::Type::Moving) {
        this->set_cursor_z(event.cursor_z);
        this->refresh_mesh_nodes_material();
        return false;
    } else if (event.type == GizmoEvent::Type::LeftDown
               || event.type == GizmoEvent::Type::RightDown)
    {
        if (event.ctrl_down) {
            return false;
        }

        m_baseline_layer_height_profile = m_layer_height_params.layer_height_profile;

        const bool is_left_down   = (event.type == GizmoEvent::Type::LeftDown);
        const AdjustAction action = determine_adjust_action(is_left_down, event.shift_down);
        this->perform_layer_height_profile_adjustment(event.cursor_z.value(), action);
        this->set_cursor_z(event.cursor_z);
        this->refresh_mesh_nodes_material();
        return true;
    } else if (event.type == GizmoEvent::Type::Dragging) {
        if (!event.ctrl_down) {
            const AdjustAction adjust_action =
                determine_adjust_action(event.left_button_down, event.shift_down);
            this->perform_layer_height_profile_adjustment(event.cursor_z.value(), adjust_action);
        }

        this->set_cursor_z(event.cursor_z);
        this->refresh_mesh_nodes_material();
        return true;
    } else if (event.type == GizmoEvent::Type::LeftUp || event.type == GizmoEvent::Type::RightUp) {
        m_baseline_layer_height_profile.clear();

        this->apply_layer_height_profile_to_model();
        m_project_interactor.undo_provider().take_snapshot(
            UndoSnapshotType::VariableLayerHeightStroke
        );
        this->update_variable_layer_height_texture();
        this->set_cursor_z(std::nullopt);
        this->refresh_mesh_nodes_material();
        return true;
    }

    return false;
}

VariableLayerHeightGizmo::VolumeHitPoint VariableLayerHeightGizmo::perform_raycast(
    const Vec2d& mouse_position,
    const Scene::Camera& camera
) const
{
    VolumeHitPoint closest_hit          = {Vec3d::Zero(), -1};
    double closest_hit_squared_distance = std::numeric_limits<double>::max();

    const Scene::Ray ray = camera.ray_at(mouse_position.x(), mouse_position.y());

    // Cast a ray on all SelectedObjectData::Vo, pick the closest hit.
    for (const SelectedObjectData::Volume& volume : m_selected_object_data.volumes) {
        const size_t volume_idx = &volume - &m_selected_object_data.volumes.front();

        const std::optional<MeshRaycaster::UnprojectResult> unproject_result =
            MeshRaycaster::unproject_on_mesh(
                volume.aabb_mesh,
                ray,
                volume.world_trafo,
                std::nullopt,
                false
            );

        if (!unproject_result.has_value()) {
            continue;
        }

        // Is this hit the closest to the camera so far?
        Vec3d world_hit_position          = volume.world_trafo * unproject_result->position;
        const double hit_squared_distance = (ray.origin - world_hit_position).squaredNorm();
        if (hit_squared_distance < closest_hit_squared_distance) {
            closest_hit_squared_distance   = hit_squared_distance;
            closest_hit.volume_idx         = static_cast<int>(volume_idx);
            closest_hit.world_hit_position = world_hit_position;
        }
    }

    return closest_hit;
}

void VariableLayerHeightGizmo::rebuild_gizmo_state()
{
    using MeshManager = PlaterScenePresenter::MeshManager;

    const SceneInteractor& scene_interactor = m_project_interactor.scene_interactor();
    const ObjectSelection& object_selection = scene_interactor.object_selection();
    const ConfigContainer& config_container = m_project_interactor.selected_config_container();
    const MeshManager& mesh_manager         = m_scene_presenter.model_triangle_mesh_manager();
    Project& project                        = m_project_interactor.selected_project();
    Scene::Scene& scene                     = m_scene_presenter.scene();

    // Restore the originally visible nodes.
    this->restore_visible_volumes();

    // Remove all the scene nodes created by this gizmo.
    if (m_main_node != nullptr) {
        scene.remove_child(m_main_node);
        m_main_node = nullptr;
        m_mesh_node = nullptr;
    }

    m_visible_volumes_nodes.clear();
    m_material_wrapper.reset();

    if (object_selection.empty() || object_selection.mode != Biz::Scene::SelectionMode::Instance) {
        m_gizmo_controller->deactivate_current_tool();
        return;
    }

    std::optional<SelectedObjectData> selected_object_data =
        collect_selected_object_data(object_selection, project, mesh_manager);
    if (!selected_object_data) {
        m_gizmo_controller->deactivate_current_tool();
        return;
    }

    m_selected_object_data  = std::move(*selected_object_data);
    m_visible_volumes_nodes = collect_visible_volumes_nodes(project, scene);
    m_layer_height_params   = compute_layer_height_params(
        object_selection,
        project,
        config_container,
        m_project_interactor.scene_interactor().bed_selection().last_selected_bed()
    );

    m_mouse_button_down = Button::None;
    m_mouse_dragging    = false;

    this->perform_layer_height_profile_clamping();
    this->set_dialog_layer_heights_profile_parameters();
    this->update_side_panel_layer_height_profile();
    this->update_side_panel_height_ranges();
    this->hide_visible_volumes();
    this->init_main_nodes();
    this->init_mesh_nodes();
}

} // namespace Slic3r::App::Plater
