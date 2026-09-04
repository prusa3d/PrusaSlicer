#include "Slic3r/App/Plater/HeightRangeGizmo.hpp"

#include "Slic3r/App/Plater/HeightRangeDialog.hpp"
#include "Slic3r/App/Plater/PlaterScenePresenter.hpp"
#include "Slic3r/App/Render/Device.hpp"
#include "Slic3r/App/Scene/Node.hpp"
#include "Slic3r/App/Scene/Scene.hpp"
#include "Slic3r/Biz/Algorithms/LayerHeight.hpp"
#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Biz/IConfigBoxSetter.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Domain/Config.hpp"
#include "Slic3r/Domain/ConfigBoxesFDM.hpp"
#include "Slic3r/Domain/FullConfigFDM.hpp"
#include "Slic3r/Domain/Project.hpp"

#include <algorithm>

using namespace Slic3r;
using namespace Slic3r::App::Yoga;
using namespace Slic3r::Biz;

using Slic3r::App::Platform::FuncCommand;
using Slic3r::App::Platform::FuncCommandExtraOpts;
using Slic3r::App::Platform::KeyboardShortcut;
using Slic3r::App::Platform::KeyboardShortcuts;
using Slic3r::App::Platform::KeyCode;
using Slic3r::App::Platform::KeyModifier;
using Slic3r::App::Platform::KeyModifiers;
using Slic3r::App::Scene::Node;
using Slic3r::App::Scene::SceneNodeTag;
using Slic3r::Biz::Scene::ObjectSelection;
using Slic3r::Biz::Scene::SceneInteractor;
using Slic3r::Domain::BoundingBox3d;
using Slic3r::Domain::ConfigBox;
using Slic3r::Domain::ConfigContainer;
using Slic3r::Domain::ConfigItem;
using Slic3r::Domain::ConfigPack;
using Slic3r::Domain::ConfigPackFDM;
using Slic3r::Domain::ConfigValue;
using Slic3r::Domain::FullConfigFDM;
using Slic3r::Domain::FullConfigFDMPtr;
using Slic3r::Domain::LayerConfigRanges;
using Slic3r::Domain::LayerHeightRange;
using Slic3r::Domain::ModelInstance;
using Slic3r::Domain::ModelObject;
using Slic3r::Domain::PrinterTechnology;
using Slic3r::Domain::Project;
using Slic3r::Domain::SelectionId;
using Slic3r::Domain::Vec3d;
using Slic3r::Domain::VolumeSettings;
using Slic3r::Domain::ZHeightPairs;

namespace Slic3r::App::Plater {

struct HeightRangeGizmo::ConfigBoxSetterImpl : public IConfigBoxSetter
{
    ConfigBoxSetterImpl() = delete;

    explicit ConfigBoxSetterImpl(HeightRangeGizmo& gizmo) : m_gizmo(gizmo) {}

    ~ConfigBoxSetterImpl() override = default;

    const ConfigValue* get_override_original_value(const ConfigItem& item, size_t) const override
    {
        const ConfigBox* settings = m_gizmo.selected_height_range_config_box();
        ASSERT(settings != nullptr);

        const ConfigItem* found = settings->items.find(item.name());
        return found ? &found->value() : nullptr;
    }

    void set_item_value(const ConfigItem& item, const ConfigValue& value, const std::vector<size_t>&) override
    {
        ConfigBox* settings = m_gizmo.selected_height_range_config_box();
        ASSERT(settings != nullptr);

        settings->overrides.set(item.name(), value);
        m_gizmo.perform_override_change();
    }

    void set_item_override(const ConfigItem& item, const bool enable, size_t) override
    {
        ConfigBox* settings = m_gizmo.selected_height_range_config_box();
        ASSERT(settings != nullptr);

        if (enable) {
            settings->overrides.enable(item.name());
        } else {
            settings->overrides.disable(item.name());
        }

        m_gizmo.perform_override_change();
    }

private:
    HeightRangeGizmo& m_gizmo;
};

static Node::NodeList
collect_non_selected_volumes_nodes(Scene::Scene& scene, const ObjectSelection& object_selection)
{
    Node::NodeList non_selected_volumes_nodes;
    scene.root().query(
        [&object_selection](const Node* n) -> bool
        {
            const SceneNodeTag* tag = n->tag_of_type<SceneNodeTag>();
            if (tag == nullptr) {
                return false;
            }

            return std::none_of(
                object_selection.elements.begin(),
                object_selection.elements.end(),
                [&tag](const Domain::ElementRef& el) { return el.object_id == tag->object_id; }
            );
        },
        non_selected_volumes_nodes,
        false
    );

    return non_selected_volumes_nodes;
}

static HeightRangeGizmo::SelectedObjectData
collect_selected_object_data(const ObjectSelection& object_selection, Project& project)
{
    const Domain::ElementRef& first_element = object_selection.elements.front();
    ModelObject* model_object               = project.find_object_by_id(first_element.object_id);
    const ModelInstance* model_instance =
        project.find_instance_by_id(first_element.object_id, first_element.instance_id);

    const BoundingBox3d& bounding_box = Algorithms::ModelObject::bounding_box_approx(*model_object);
    const Vec3d model_object_center   = (bounding_box.min + bounding_box.max) * 0.5;

    return {model_object, model_instance, model_object_center};
}

HeightRangeGizmo::HeightRangeGizmo(
    Render::Device& device,
    ProjectInteractor& project_interactor,
    PlaterScenePresenter& scene_presenter
) :
    m_device(device),
    m_project_interactor(project_interactor),
    m_scene_interactor(project_interactor.scene_interactor()),
    m_scene_presenter(scene_presenter)
{
    m_config_setter = std::make_unique<ConfigBoxSetterImpl>(*this);
    m_dialog        = std::make_unique<HeightRangeDialog>(m_config_setter.get());

    m_dialog->callbacks().revert_clicked = [this]() { this->perform_height_ranges_restart(); };

    m_dialog->callbacks().add_range_clicked = [this]() { this->perform_height_range_addition(); };

    m_dialog->callbacks().delete_range_clicked = [this](const LayerHeightRange& range_to_delete)
    { this->perform_height_range_deletion(range_to_delete); };

    m_dialog->callbacks().height_range_selected = [this](const LayerHeightRange& range_to_select)
    { this->perform_height_range_selection(range_to_select); };

    m_dialog->callbacks().height_range_deselected = [this]()
    { this->perform_height_range_deselection(); };

    m_dialog->callbacks().min_z_changed = [this](const double min_z)
    { this->perform_height_range_value_change(min_z, std::nullopt); };

    m_dialog->callbacks().max_z_changed = [this](const double max_z)
    { this->perform_height_range_value_change(std::nullopt, max_z); };

    m_dialog->callbacks().override_removed = [this](const std::string& removed_override_name)
    {
        ConfigBox* selected_config_box = this->selected_height_range_config_box();
        if (selected_config_box == nullptr) {
            return;
        }

        selected_config_box->overrides.disable(removed_override_name);
        this->perform_override_change();
    };

    m_dialog->callbacks().undo_overrides_clicked = [this](const LayerHeightRange& range_to_clear)
    { this->perform_single_height_range_restart(range_to_clear); };

    m_dialog->callbacks().height_range_dragging = [this](
                                                      const LayerHeightRange& range_to_drag,
                                                      const double new_min_z,
                                                      const double new_max_z
                                                  )
    { m_planes_wrapper.set_positions(new_min_z, new_max_z, *m_selected_object_data.model_object); };

    m_dialog->callbacks().height_range_drag_ended = [this](
                                                        const LayerHeightRange& original_range,
                                                        const double new_min_z,
                                                        const double new_max_z
                                                    )
    {
        if (!m_selected_layer_height_range.has_value()
            || m_selected_layer_height_range.value() != original_range)
        {
            this->perform_height_range_selection(original_range);
        }

        this->perform_height_range_value_change(new_min_z, new_max_z);
    };

    m_dialog->callbacks().height_range_hovered =
        [this](const std::optional<LayerHeightRange>& hovered_range)
    {
        this->update_layer_height_title(hovered_range);
        m_dialog->highlight_range(hovered_range);
    };
}

HeightRangeGizmo::~HeightRangeGizmo() = default;

Scene::ToolType HeightRangeGizmo::type() const
{
    return Scene::ToolType::HeightRangeGizmo;
}

bool HeightRangeGizmo::disable_object_selection() const
{
    return true;
}

bool HeightRangeGizmo::enabled() const
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

GizmoWindowPtr HeightRangeGizmo::release_ui_window()
{
    return m_dialog.release();
}

void HeightRangeGizmo::on_activated()
{
    const SceneInteractor& scene_interactor = m_project_interactor.scene_interactor();
    const ObjectSelection& object_selection = scene_interactor.object_selection();
    const ConfigContainer& config_container = m_project_interactor.selected_config_container();
    Project& project                        = m_project_interactor.selected_project();
    Scene::Scene& scene                     = m_scene_presenter.scene();

    if (object_selection.empty() || object_selection.mode != Biz::Scene::SelectionMode::Instance) {
        on_deactivated();
        return;
    }

    m_non_selected_volumes_nodes = collect_non_selected_volumes_nodes(scene, object_selection);
    m_selected_object_data       = collect_selected_object_data(object_selection, project);
    m_layer_height_params        = compute_layer_height_params(
        object_selection,
        project,
        config_container,
        m_project_interactor.scene_interactor().bed_selection().last_selected_bed()
    );
    m_layer_config_ranges               = m_selected_object_data.model_object->layer_config_ranges;
    m_has_variable_layer_height_profile = is_valid_layer_height_profile(
        m_selected_object_data.model_object->layer_height_profile.get(),
        m_layer_height_params.object_print_z_uncompensated_height
    );

    this->set_dialog_layer_heights_profile_parameters();
    this->update_side_panel_layer_height_profile();
    this->update_side_panel_height_ranges();
    this->update_layer_height_title();
    this->init_main_nodes();
    this->hide_non_selected_volumes();

    scene.add_listener<App::Scene::ISceneChangedListener>(this);
    scene.add_listener<IThumbnailRenderListener>(this);
    m_scene_interactor.add_listener<Biz::Scene::ISceneChangedListener>(this);
}

void HeightRangeGizmo::on_deactivated()
{
    this->restore_non_selected_volumes();

    Scene::Scene& scene = m_scene_presenter.scene();

    m_dialog->clear_selection();
    m_dialog->set_selected_height_range_config_box(nullptr);
    m_dialog->clear_overrides();

    m_planes_wrapper.release(scene);
    m_drag_state.reset();
    m_hovered_plane.reset();
    m_layer_config_ranges.clear();
    m_selected_layer_height_range.reset();
    m_clipboard_height_range_settings.reset();
    m_selected_object_data = {};
    m_non_selected_volumes_nodes.clear();
    m_has_variable_layer_height_profile = false;

    scene.remove_listener<App::Scene::ISceneChangedListener>(this);
    scene.remove_listener<IThumbnailRenderListener>(this);
    m_scene_interactor.remove_listener<Biz::Scene::ISceneChangedListener>(this);
}

void HeightRangeGizmo::on_project_activated(size_t new_project_id)
{
    this->on_activated();
}

void HeightRangeGizmo::on_project_deactivated(size_t old_project_id)
{
    this->on_deactivated();
}

void HeightRangeGizmo::on_scene_selection_changed(
    SelectionId project_id,
    const ObjectSelection& selection
)
{
    if (m_selected_object_data.model_object == nullptr) {
        return;
    }

    this->on_deactivated();
    this->on_activated();
}

void HeightRangeGizmo::on_thumbnail_render_begin()
{
    // Before rendering a thumbnail, hide planes.
    m_planes_wrapper.set_enabled(false);
    this->restore_non_selected_volumes();
}

void HeightRangeGizmo::on_thumbnail_render_end()
{
    // After rendering a thumbnail, restore planes.
    m_planes_wrapper.set_enabled(true);
    this->hide_non_selected_volumes();
}

void HeightRangeGizmo::on_model_reloaded(SelectionId project_id)
{
    if (project_id != m_project_interactor.selected_project_id()) {
        return;
    }

    this->rebuild_gizmo_state();
}

void HeightRangeGizmo::on_node_added(Node* node)
{
    const SceneNodeTag* tag = node->tag_of_type<SceneNodeTag>();
    if (tag == nullptr) {
        return;
    }

    m_non_selected_volumes_nodes.push_back(node);
    node->set_enabled(false);
}

void HeightRangeGizmo::on_node_removed(Node* node)
{
    std::erase(m_non_selected_volumes_nodes, node);
}

void HeightRangeGizmo::on_transient_mouse(Scene::GizmoEventContext& ctx)
{
    using PlaneType = HeightRangePlaneNodeTag::PlaneType;

    if (!m_selected_layer_height_range.has_value() && m_hovered_plane) {
        m_planes_wrapper.set_plane_default_color(m_hovered_plane.value());
        m_hovered_plane.reset();

        return;
    }

    std::optional<PlaneType> new_hovered_plane;
    if (Node* node = ctx.pick_result_node_with_tag_of_type<HeightRangePlaneNodeTag>()) {
        new_hovered_plane = node->tag_of_type<HeightRangePlaneNodeTag>()->plane_type;
    }

    if (new_hovered_plane == m_hovered_plane) {
        return;
    }

    if (m_hovered_plane) {
        m_planes_wrapper.set_plane_default_color(m_hovered_plane.value());
    }

    m_hovered_plane = new_hovered_plane;

    if (m_hovered_plane) {
        m_planes_wrapper.set_plane_hover_color(m_hovered_plane.value());
    }
}

Scene::GizmoActivationState
HeightRangeGizmo::on_mouse(Scene::GizmoEventContext& ctx, bool only_active)
{
    if (!m_selected_object_data.model_object || !m_selected_layer_height_range.has_value()) {
        return Scene::GizmoActivationState::Inactive;
    }

    const Platform::MouseEvent::Type event_type = ctx.mouse_event().type();
    const Platform::MouseButton event_button    = ctx.mouse_event().button();
    const Scene::Ray& pick_ray                  = ctx.pick_ray();

    if (event_type == Platform::MouseEvent::Type::ButtonDown
        && event_button == Platform::MouseButton::Left)
    {
        Node* node = ctx.pick_result_node_with_tag_of_type<HeightRangePlaneNodeTag>();
        if (node) {
            const HeightRangePlaneNodeTag* tag = node->tag_of_type<HeightRangePlaneNodeTag>();
            const GizmoEvent gizmo_event{
                .type         = GizmoEvent::Type::LeftDown,
                .pick_ray     = pick_ray,
                .picked_plane = tag->plane_type
            };
            this->process_gizmo_event(gizmo_event);
            return Scene::GizmoActivationState::Active;
        }
        return Scene::GizmoActivationState::Inactive;
    }

    if (event_type == Platform::MouseEvent::Type::Move && m_drag_state.has_value()) {
        const GizmoEvent gizmo_event{.type = GizmoEvent::Type::Dragging, .pick_ray = pick_ray};
        this->process_gizmo_event(gizmo_event);
        return Scene::GizmoActivationState::Active;
    }

    if (event_type == Platform::MouseEvent::Type::ButtonUp && m_drag_state.has_value()) {
        const GizmoEvent gizmo_event{.type = GizmoEvent::Type::LeftUp, .pick_ray = pick_ray};
        this->process_gizmo_event(gizmo_event);
        return Scene::GizmoActivationState::Done;
    }

    return Scene::GizmoActivationState::Inactive;
}

void HeightRangeGizmo::register_commands(Platform::CommandRegistry& registry)
{
    registry
        .register_command(
            std::make_unique<FuncCommand>(
                "height-range-gizmo-add-layer-height",
                [this]() { this->add_layer_height_override(); },
                FuncCommandExtraOpts{
                    .keyboard_shortcuts = KeyboardShortcuts{KeyboardShortcut{0, KeyCode::H}}
                }
            )
        )
        .register_command(
            std::make_unique<FuncCommand>(
                "height-range-gizmo-copy-overrides",
                [this]() { this->copy_height_range_overrides(); },
                FuncCommandExtraOpts{
                    .keyboard_shortcuts =
                        KeyboardShortcuts{
                            KeyboardShortcut{KeyModifiers(KeyModifier::Ctrl), KeyCode::C}
                        }
                }
            )
        )
        .register_command(
            std::make_unique<FuncCommand>(
                "height-range-gizmo-paste-overrides",
                [this]() { this->paste_height_range_overrides(); },
                FuncCommandExtraOpts{
                    .keyboard_shortcuts = KeyboardShortcuts{
                        KeyboardShortcut{KeyModifiers(KeyModifier::Ctrl), KeyCode::V}
                    }
                }
            )
        );
}

std::optional<Undo::ToolState> HeightRangeGizmo::get_tool_state() const
{
    return Undo::HeightRangeGizmoState{m_selected_layer_height_range};
}

void HeightRangeGizmo::set_tool_state(const Undo::ToolState& tool_state)
{
    ASSERT(std::holds_alternative<Undo::HeightRangeGizmoState>(tool_state));
    const Undo::HeightRangeGizmoState& gizmo_state =
        std::get<Undo::HeightRangeGizmoState>(tool_state);

    if (!gizmo_state.selected_height_range.has_value()) {
        if (m_selected_layer_height_range.has_value()) {
            this->perform_height_range_deselection();
        }

        return;
    }

    const LayerHeightRange& height_range = gizmo_state.selected_height_range.value();
    ASSERT(m_layer_config_ranges.find(height_range) != m_layer_config_ranges.end());
    this->perform_height_range_selection(height_range);
}

void HeightRangeGizmo::hide_non_selected_volumes()
{
    for (Node* node : m_non_selected_volumes_nodes) {
        node->set_enabled(false);
    }
}

void HeightRangeGizmo::restore_non_selected_volumes()
{
    for (Node* node : m_non_selected_volumes_nodes) {
        node->set_enabled(true);
    }
}

double HeightRangeGizmo::get_range_layer_height(const VolumeSettings& settings) const
{
    const auto layer_height = settings.overrides.get("layer_height");
    return layer_height.has_value() ? layer_height->get<double>() :
                                      m_layer_height_params.layer_height;
}

ConfigBox* HeightRangeGizmo::selected_height_range_config_box()
{
    if (!m_selected_layer_height_range.has_value()) {
        return nullptr;
    }

    const auto selected_range_it =
        m_layer_config_ranges.find(m_selected_layer_height_range.value());
    if (selected_range_it == m_layer_config_ranges.end()) {
        return nullptr;
    }

    return &selected_range_it->second;
}

bool HeightRangeGizmo::process_gizmo_event(const GizmoEvent& event)
{
    using PlaneType = HeightRangePlaneNodeTag::PlaneType;
    using DragType  = DragState::DragType;

    const auto make_projection_axis = [this](const double z) -> Scene::Ray
    {
        return Scene::Ray{
            Vec3d{
                m_selected_object_data.model_object_center.x(),
                m_selected_object_data.model_object_center.y(),
                z
            },
            Vec3d::UnitZ()
        };
    };

    if (event.type == GizmoEvent::Type::LeftDown) {
        ASSERT(m_selected_layer_height_range.has_value());

        const DragType drag_type =
            (event.picked_plane == PlaneType::Min) ? DragType::MinPlane : DragType::MaxPlane;

        const auto selected_range_it =
            m_layer_config_ranges.find(m_selected_layer_height_range.value());
        ASSERT(selected_range_it != m_layer_config_ranges.end());

        const double initial_min_z = selected_range_it->first.first;
        const double initial_max_z = selected_range_it->first.second;
        const double initial_plane_z =
            (drag_type == DragType::MinPlane) ? initial_min_z : initial_max_z;

        double projection_offset = 0.;
        make_projection_axis(initial_plane_z)
            .closest_point_from_ray(event.pick_ray, projection_offset);

        m_drag_state.emplace(
            DragState{drag_type, initial_min_z, initial_max_z, initial_plane_z + projection_offset}
        );
        return true;
    }

    if (event.type == GizmoEvent::Type::Dragging) {
        ASSERT(m_drag_state.has_value());

        double projection_offset;
        if (!make_projection_axis(m_drag_state->initial_plane_z())
                 .closest_point_from_ray(event.pick_ray, projection_offset))
        {
            return true;
        }

        const double current_projected_z = m_drag_state->initial_plane_z() + projection_offset;
        double new_z                     = m_drag_state->initial_plane_z()
            + (current_projected_z - m_drag_state->initial_projected_z);
        new_z = std::clamp(new_z, 0., m_layer_height_params.object_print_z_uncompensated_height);

        const auto selected_range_it =
            m_layer_config_ranges.find(m_selected_layer_height_range.value());
        ASSERT(selected_range_it != m_layer_config_ranges.end());

        HeightRangeEntry dragged_entry{
            m_drag_state->initial_min_z,
            m_drag_state->initial_max_z,
            this->get_range_layer_height(selected_range_it->second)
        };
        if (m_drag_state->drag_type == DragType::MinPlane) {
            dragged_entry.min_z = std::min(new_z, dragged_entry.max_z);
            m_dialog->set_height_range_min_z(dragged_entry.min_z);
        } else {
            ASSERT(m_drag_state->drag_type == DragType::MaxPlane);
            dragged_entry.max_z = std::max(new_z, dragged_entry.min_z);
            m_dialog->set_height_range_max_z(dragged_entry.max_z);
        }

        m_dialog->update_single_height_range(m_selected_layer_height_range.value(), dragged_entry);
        m_planes_wrapper.set_positions(
            dragged_entry.min_z,
            dragged_entry.max_z,
            *m_selected_object_data.model_object
        );
        return true;
    }

    if (event.type == GizmoEvent::Type::LeftUp) {
        ASSERT(m_drag_state.has_value());

        double final_min_z = m_drag_state->initial_min_z;
        double final_max_z = m_drag_state->initial_max_z;

        double projection_offset;
        if (make_projection_axis(m_drag_state->initial_plane_z())
                .closest_point_from_ray(event.pick_ray, projection_offset))
        {
            const double current_projected_z = m_drag_state->initial_plane_z() + projection_offset;
            double new_z                     = m_drag_state->initial_plane_z()
                + (current_projected_z - m_drag_state->initial_projected_z);
            new_z =
                std::clamp(new_z, 0., m_layer_height_params.object_print_z_uncompensated_height);

            if (m_drag_state->drag_type == DragType::MinPlane) {
                final_min_z = std::min(new_z, final_max_z);
            } else {
                final_max_z = std::max(new_z, final_min_z);
            }
        }

        m_drag_state.reset();
        this->perform_height_range_value_change(final_min_z, final_max_z);
        return true;
    }

    return false;
}

void HeightRangeGizmo::init_main_nodes()
{
    Scene::Scene& scene = m_scene_presenter.scene();
    m_planes_wrapper.init(m_device, scene, *m_selected_object_data.model_object);
}

void HeightRangeGizmo::set_dialog_layer_heights_profile_parameters()
{
    m_dialog->set_object_max_z(
        static_cast<float>(m_layer_height_params.object_print_z_uncompensated_height)
    );
    m_dialog->set_min_layer_height(static_cast<float>(m_layer_height_params.min_layer_height));
    m_dialog->set_max_layer_height(static_cast<float>(m_layer_height_params.max_layer_height));
    m_dialog->set_default_layer_height(static_cast<float>(m_layer_height_params.layer_height));
}

void HeightRangeGizmo::update_side_panel_layer_height_profile()
{
    m_dialog->set_layer_height_profile(m_layer_height_params.layer_height_profile);
}

void HeightRangeGizmo::update_side_panel_height_ranges()
{
    const double default_layer_height = m_layer_height_params.layer_height;
    const HeightRangeEntries height_ranges =
        create_height_ranges_from_config(m_layer_config_ranges, default_layer_height);
    m_dialog->update_height_ranges(height_ranges, m_layer_config_ranges);
}

void HeightRangeGizmo::update_layer_height_profile()
{
    if (m_has_variable_layer_height_profile) {
        // Variable layer height profile has priority, so we keep showing the painted profile.
        return;
    }

    // No variable layer height profile, so we regenerate from ranges to show height range overrides.
    m_layer_height_params.layer_height_profile =
        compute_layer_height_profile(m_layer_height_params, m_layer_config_ranges);
}

void HeightRangeGizmo::update_layer_height_title(
    const std::optional<LayerHeightRange>& range_for_title
)
{
    if (range_for_title.has_value()) {
        auto range_it = m_layer_config_ranges.find(range_for_title.value());
        if (range_it != m_layer_config_ranges.end()) {
            m_dialog->set_layer_height_title(this->get_range_layer_height(range_it->second));
            return;
        }
    }

    if (m_selected_layer_height_range.has_value()) {
        auto selected_range_it = m_layer_config_ranges.find(m_selected_layer_height_range.value());
        if (selected_range_it != m_layer_config_ranges.end()) {
            m_dialog->set_layer_height_title(
                this->get_range_layer_height(selected_range_it->second)
            );
            return;
        }
    }

    m_dialog->set_layer_height_title(m_layer_height_params.layer_height);
}

void HeightRangeGizmo::perform_height_range_addition()
{
    const std::optional<LayerHeightRange> next_range = [&]() -> std::optional<LayerHeightRange>
    {
        if (m_selected_layer_height_range.has_value()) {
            const auto selected_range_it =
                m_layer_config_ranges.find(m_selected_layer_height_range.value());
            ASSERT(selected_range_it != m_layer_config_ranges.end());

            const auto next_range_it = std::next(selected_range_it);
            if (next_range_it != m_layer_config_ranges.end()) {
                return next_range_it->first;
            }
        }

        return std::nullopt;
    }();

    const double last_max_z =
        m_layer_config_ranges.empty() ? 0. : m_layer_config_ranges.rbegin()->first.second;
    const std::optional<LayerHeightRange> new_height_range = compute_new_height_range(
        m_selected_layer_height_range,
        next_range,
        last_max_z,
        m_layer_height_params.object_print_z_uncompensated_height,
        m_layer_height_params.min_layer_height
    );

    if (!new_height_range.has_value()) {
        return;
    }

    if (next_range.has_value() && new_height_range->second > next_range->first) {
        const auto next_range_it = m_layer_config_ranges.find(next_range.value());
        const LayerHeightRange new_key{new_height_range->second, next_range_it->first.second};
        VolumeSettings config = std::move(next_range_it->second);

        m_layer_config_ranges.erase(next_range_it);
        m_layer_config_ranges[new_key] = std::move(config);
    }

    m_layer_config_ranges.try_emplace(new_height_range.value());

    this->apply_layer_config_ranges_to_model();
    this->update_layer_height_profile();
    this->update_side_panel_layer_height_profile();
    this->update_side_panel_height_ranges();
    this->perform_height_range_selection(new_height_range.value());
    m_project_interactor.undo_provider().take_snapshot(UndoSnapshotType::HeightRangeAdd);
}

void HeightRangeGizmo::perform_height_range_deletion(const LayerHeightRange& range_to_delete)
{
    const auto range_to_delete_it = m_layer_config_ranges.find(range_to_delete);
    ASSERT(range_to_delete_it != m_layer_config_ranges.end());

    if (m_selected_layer_height_range.has_value()
        && m_selected_layer_height_range.value() == range_to_delete)
    {
        m_dialog->clear_selection();
        m_selected_layer_height_range.reset();
        m_planes_wrapper.set_planes_visible(false);
    }

    m_layer_config_ranges.erase(range_to_delete_it);

    this->apply_layer_config_ranges_to_model();
    m_project_interactor.undo_provider().take_snapshot(UndoSnapshotType::HeightRangeDelete);
    this->update_layer_height_profile();
    this->update_side_panel_layer_height_profile();
    this->update_side_panel_height_ranges();
}

void HeightRangeGizmo::perform_height_range_selection(const LayerHeightRange& range_to_select)
{
    const auto range_to_select_it = m_layer_config_ranges.find(range_to_select);
    ASSERT(range_to_select_it != m_layer_config_ranges.end());

    m_selected_layer_height_range = range_to_select;

    m_dialog->select_range(range_to_select);
    m_dialog->set_height_range_min_z(range_to_select_it->first.first);
    m_dialog->set_height_range_max_z(range_to_select_it->first.second);
    m_dialog->set_selected_height_range_config_box(&range_to_select_it->second);
    m_dialog->update_overrides_section();
    this->update_layer_height_title(m_selected_layer_height_range);

    m_planes_wrapper.set_planes_visible(true);
    m_planes_wrapper.set_positions(
        range_to_select.first,
        range_to_select.second,
        *m_selected_object_data.model_object
    );
}

void HeightRangeGizmo::perform_height_range_deselection()
{
    m_selected_layer_height_range.reset();
    m_dialog->clear_selection();
    m_dialog->set_selected_height_range_config_box(nullptr);
    m_dialog->update_overrides_section();
    this->update_layer_height_title();
    m_planes_wrapper.set_planes_visible(false);
}

void HeightRangeGizmo::perform_height_range_value_change(
    const std::optional<double> min_z,
    const std::optional<double> max_z
)
{
    ASSERT(m_selected_layer_height_range.has_value());

    const auto selected_range_it =
        m_layer_config_ranges.find(m_selected_layer_height_range.value());
    ASSERT(selected_range_it != m_layer_config_ranges.end());

    const double object_max_z = m_layer_height_params.object_print_z_uncompensated_height;
    const LayerHeightRange new_range{
        std::clamp(min_z.value_or(selected_range_it->first.first), 0., object_max_z),
        std::clamp(max_z.value_or(selected_range_it->first.second), 0., object_max_z)
    };

    if (m_selected_layer_height_range.value() == new_range) {
        return;
    }

    VolumeSettings volume_settings = std::move(selected_range_it->second);
    m_layer_config_ranges.erase(selected_range_it);
    m_layer_config_ranges[new_range] = std::move(volume_settings);
    m_selected_layer_height_range    = new_range;

    this->apply_layer_config_ranges_to_model();
    m_project_interactor.undo_provider().take_snapshot(UndoSnapshotType::HeightRangeValueChange);
    this->update_layer_height_profile();
    this->update_side_panel_layer_height_profile();
    this->update_side_panel_height_ranges();

    const auto new_range_it = m_layer_config_ranges.find(new_range);
    ASSERT(new_range_it != m_layer_config_ranges.end());

    m_dialog->select_range(new_range);
    m_dialog->set_height_range_min_z(new_range.first);
    m_dialog->set_height_range_max_z(new_range.second);
    m_dialog->set_selected_height_range_config_box(&new_range_it->second);

    m_planes_wrapper.set_planes_visible(true);
    m_planes_wrapper
        .set_positions(new_range.first, new_range.second, *m_selected_object_data.model_object);
}

void HeightRangeGizmo::perform_override_change()
{
    ASSERT(m_selected_layer_height_range.has_value());

    const auto selected_range_it =
        m_layer_config_ranges.find(m_selected_layer_height_range.value());
    ASSERT(selected_range_it != m_layer_config_ranges.end());

    this->apply_layer_config_ranges_to_model();
    m_project_interactor.undo_provider().take_snapshot(UndoSnapshotType::HeightRangeOverrideChange);

    this->update_layer_height_profile();
    this->update_side_panel_layer_height_profile();
    this->update_side_panel_height_ranges();
    m_dialog->select_range(m_selected_layer_height_range.value());
    m_dialog->update_overrides_section();
}

void HeightRangeGizmo::perform_height_ranges_restart()
{
    m_layer_config_ranges.clear();
    m_selected_layer_height_range.reset();

    this->apply_layer_config_ranges_to_model();
    m_project_interactor.undo_provider().take_snapshot(UndoSnapshotType::HeightRangeRestart);
    this->update_layer_height_profile();
    this->update_side_panel_layer_height_profile();
    this->update_side_panel_height_ranges();
    m_dialog->clear_selection();

    m_planes_wrapper.set_planes_visible(false);
}

void HeightRangeGizmo::perform_single_height_range_restart(const LayerHeightRange& range_to_undo)
{
    using ConfigItemRef = std::reference_wrapper<const ConfigItem>;

    const auto range_to_undo_it = m_layer_config_ranges.find(range_to_undo);
    if (range_to_undo_it == m_layer_config_ranges.end()) {
        return;
    }

    VolumeSettings& settings = range_to_undo_it->second;
    for (const ConfigItemRef& config_item_ref : settings.overrides.overridden_items()) {
        settings.overrides.disable(config_item_ref.get().name());
    }

    this->apply_layer_config_ranges_to_model();
    m_project_interactor.undo_provider().take_snapshot(UndoSnapshotType::HeightRangeRestart);
    this->update_layer_height_profile();
    this->update_side_panel_layer_height_profile();
    this->update_side_panel_height_ranges();
    this->update_layer_height_title();

    if (m_selected_layer_height_range.has_value()
        && m_selected_layer_height_range.value() == range_to_undo)
    {
        this->perform_height_range_selection(range_to_undo);
    }
}

void HeightRangeGizmo::add_layer_height_override()
{
    if (!m_selected_layer_height_range.has_value()) {
        return;
    }

    const auto selected_layer_config_range_it =
        m_layer_config_ranges.find(m_selected_layer_height_range.value());
    if (selected_layer_config_range_it == m_layer_config_ranges.end()) {
        return;
    }

    VolumeSettings& settings = selected_layer_config_range_it->second;
    const auto layer_height  = settings.overrides.get("layer_height");

    if (layer_height.has_value()) {
        return;
    }

    settings.overrides.set("layer_height", m_layer_height_params.layer_height);

    this->apply_layer_config_ranges_to_model();
    m_project_interactor.undo_provider().take_snapshot(
        UndoSnapshotType::HeightRangeLayerHeightOverride
    );
    this->update_layer_height_profile();
    this->update_side_panel_layer_height_profile();
    this->update_side_panel_height_ranges();
    this->update_layer_height_title();
    this->perform_height_range_selection(m_selected_layer_height_range.value());
}

void HeightRangeGizmo::copy_height_range_overrides()
{
    if (m_selected_layer_height_range.has_value()) {
        const auto selected_layer_config_range_it =
            m_layer_config_ranges.find(m_selected_layer_height_range.value());
        if (selected_layer_config_range_it != m_layer_config_ranges.end()) {
            m_clipboard_height_range_settings = selected_layer_config_range_it->second;
        }
    } else if (!m_layer_config_ranges.empty()) {
        m_clipboard_layer_config_ranges = m_layer_config_ranges;
    }
}

void HeightRangeGizmo::paste_height_range_overrides()
{
    bool pasted = false;

    if (m_selected_layer_height_range.has_value() && m_clipboard_height_range_settings.has_value())
    {
        const auto selected_layer_config_range_it =
            m_layer_config_ranges.find(m_selected_layer_height_range.value());
        if (selected_layer_config_range_it == m_layer_config_ranges.end()) {
            return;
        }

        selected_layer_config_range_it->second = m_clipboard_height_range_settings.value();
        pasted                                 = true;
    } else if (!m_selected_layer_height_range.has_value()
               && m_clipboard_layer_config_ranges.has_value())
    {
        m_layer_config_ranges = m_clipboard_layer_config_ranges.value();
        pasted                = true;
    }

    if (!pasted) {
        return;
    }

    this->apply_layer_config_ranges_to_model();
    m_project_interactor.undo_provider().take_snapshot(UndoSnapshotType::HeightRangePaste);
    this->update_layer_height_profile();
    this->update_side_panel_layer_height_profile();
    this->update_side_panel_height_ranges();
    this->update_layer_height_title();

    if (m_selected_layer_height_range.has_value()) {
        this->perform_height_range_selection(m_selected_layer_height_range.value());
    } else {
        m_dialog->clear_selection();
        m_planes_wrapper.set_planes_visible(false);
    }
}

void HeightRangeGizmo::apply_layer_config_ranges_to_model()
{
    ASSERT(m_selected_object_data.model_object != nullptr);
    ASSERT(m_selected_object_data.model_instance != nullptr);

    const Domain::ElementRef object_ref{
        m_selected_object_data.model_object->id().id,
        m_selected_object_data.model_instance->id().id
    };

    const auto modifier = [this](ModelObject& object)
    { object.layer_config_ranges = m_layer_config_ranges; };

    m_scene_interactor.modify_layer_config_ranges(object_ref, modifier);
}

void HeightRangeGizmo::rebuild_gizmo_state()
{
    const SceneInteractor& scene_interactor = m_project_interactor.scene_interactor();
    const ObjectSelection& object_selection = scene_interactor.object_selection();
    const ConfigContainer& config_container = m_project_interactor.selected_config_container();
    Project& project                        = m_project_interactor.selected_project();
    Scene::Scene& scene                     = m_scene_presenter.scene();

    this->restore_non_selected_volumes();

    m_dialog->clear_selection();
    m_dialog->set_selected_height_range_config_box(nullptr);

    m_planes_wrapper.release(scene);
    m_drag_state.reset();
    m_hovered_plane.reset();
    m_layer_config_ranges.clear();
    m_selected_layer_height_range.reset();
    m_selected_object_data = {};
    m_non_selected_volumes_nodes.clear();
    m_has_variable_layer_height_profile = false;

    if (object_selection.empty() || object_selection.mode != Biz::Scene::SelectionMode::Instance) {
        this->on_deactivated();
        return;
    }

    m_non_selected_volumes_nodes = collect_non_selected_volumes_nodes(scene, object_selection);
    m_selected_object_data       = collect_selected_object_data(object_selection, project);
    m_layer_height_params        = compute_layer_height_params(
        object_selection,
        project,
        config_container,
        m_project_interactor.scene_interactor().bed_selection().last_selected_bed()
    );
    m_layer_config_ranges               = m_selected_object_data.model_object->layer_config_ranges;
    m_has_variable_layer_height_profile = is_valid_layer_height_profile(
        m_selected_object_data.model_object->layer_height_profile.get(),
        m_layer_height_params.object_print_z_uncompensated_height
    );

    this->set_dialog_layer_heights_profile_parameters();
    this->update_side_panel_layer_height_profile();
    this->update_side_panel_height_ranges();
    this->update_layer_height_title();
    this->init_main_nodes();
    this->hide_non_selected_volumes();
}

} // namespace Slic3r::App::Plater
