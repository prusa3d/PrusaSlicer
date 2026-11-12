
#include "Slic3r/App/Plater/MeasureGizmo.hpp"
#include "Slic3r/App/Plater/MeasureDialog.hpp"
#include "Slic3r/Biz/Scene/Selection.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/App/Plater/PlaterScenePresenter.hpp"
#include "Slic3r/App/Scene/SceneNodeTag.hpp"
#include "Slic3r/App/Scene/AabbRaycastNodeComponent.hpp"
#include "Slic3r/App/Render/Device.hpp"
#include "Slic3r/App/Render/GeometryBuilder.hpp"
#include "Slic3r/App/Plater/PlaterSceneLayer.hpp"
#include "Slic3r/App/Scene/NodeVisitor.hpp"
#include "Slic3r/App/Scene/MeshRenderNodeComponent.hpp"
#include "Slic3r/App/Render/GeometryBuilder.hpp"
#include "Slic3r/Math.hpp"
#include "Slic3r/App/Plater/QuickSelectGizmo.hpp"


#include <magic_enum/magic_enum_flags.hpp>

#include <imgui/imgui.h>

using namespace Slic3r::App::Yoga;
using namespace Slic3r::App::Plater::Measure;
using namespace magic_enum::bitwise_operators;

using Slic3r::App::Scene::SceneNodeTag;

namespace Slic3r::App::Plater {

using FuncCommandExtraOpts = Platform::FuncCommandExtraOpts;

static constexpr double SPHERE_RADIUS                    = 0.75;
static constexpr double SPHERE_RESOLUTION_ANGLE          = Slic3r::deg2rad(360.0 / 64.0);
static constexpr double CYLINDER_RADIUS                  = 0.5;
static constexpr double CYLINDER_RESOLUTION_ANGLE        = Slic3r::deg2rad(360.0 / 64.0);
static constexpr double TORUS_RADIUS                     = 0.5;
static constexpr double TORUS_MAIN_RESOLUTION_ANGLE      = Slic3r::deg2rad(360.0 / 64.0);
static constexpr double TORUS_SECONDARY_RESOLUTION_ANGLE = Slic3r::deg2rad(360.0 / 16.0);

static const Domain::ColorRGBA FIRST_FEATURE_COLOR          = {0.25f, 0.75f, 0.75f, 1.0f};
static const Domain::ColorRGBA SECOND_FEATURE_COLOR         = {0.75f, 0.25f, 0.75f, 1.0f};
static const Domain::ColorRGBA FIRST_FEATURE_HOVERED_COLOR  = {0.35f, 0.85f, 0.85f, 1.0f};
static const Domain::ColorRGBA SECOND_FEATURE_HOVERED_COLOR = {0.85f, 0.35f, 0.85f, 1.0f};
static const Domain::ColorRGBA NEUTRAL_COLOR                = {0.50f, 0.50f, 0.50f, 1.0f};

static const std::string CURRENT_FEATURE_NAME       = "current feature";
static const std::string FIRST_FEATURE_NAME         = "first selected feature";
static const std::string SECOND_FEATURE_NAME        = "second selected feature";
static const std::string AUXILIARY_FEATURE_NAME     = "auxiliary feature";
static const std::string CIRCLE_CENTER_FEATURE_NAME = "circle center feature";

static constexpr float TRIANGLE_BASE   = 10.0f;
static constexpr float TRIANGLE_HEIGHT = TRIANGLE_BASE * 1.618033f; // golden ratio

static void replace_color(Scene::Node& node, const Domain::ColorRGBA& color)
{
    DEBUG_ASSERT(node.render_component() != nullptr);
    Render::Material material = node.render_component()->material();
    material.set_uniform("uniform_color", color);
    node.render_component()->replace_material(material);
}

static void set_override_color(Scene::Node& node, const Domain::ColorRGBA& color)
{
    DEBUG_ASSERT(node.render_component() != nullptr);
    Render::Material material = node.render_component()->material();
    material.set_uniform("uniform_color", color);
    node.set_material_override(material);
}

MeasureGizmo::MeasureGizmo(
    Render::Device& device,
    Biz::ProjectInteractor& project_interactor,
    PlaterScenePresenter& scene_presenter
) :
    m_device(device),
    m_project_interactor(project_interactor),
    m_scene_interactor(project_interactor.scene_interactor()),
    m_scene_presenter(scene_presenter),
    m_projects(project_interactor)
{
    m_dialog = std::make_unique<MeasureDialog>();
}

void MeasureGizmo::on_activated()
{
    m_current_project            = &m_projects.project(m_project_interactor.selected_project_id());
    m_current_project->id        = m_project_interactor.selected_project_id();
    m_current_project->activated = true;

    DEBUG_ASSERT(m_current_project->main_node == nullptr);

    auto& scene = m_scene_presenter.scene();

    // builds the following hierarchy of scene nodes:
    // [measure gizmo main] - [features]
    // - [dimensionings] - [linear] - [stem]
    // - [arrow 1]
    // - [arrow 2]
    // - [angular] - [arc]
    //
    // Children of [features] are built on demand

    Scene::NodeBuilder builder{scene};
    builder.set_debug_name("measure gizmo main");
    builder.set_tag(MeasureGizmoNodeTag{});
    auto main_node               = builder.build();
    m_current_project->main_node = main_node.get();

    // set as a child of the scene root and not selection root to avoid automatic screen space rescaling
    scene.add_child(main_node.release(), &scene.root());

    Scene::NodeBuilder features_builder{scene};
    features_builder.set_debug_name("features");
    features_builder.set_tag(MeasureGizmoNodeTag{MeasureGizmoElementType::Features});
    auto features_node               = features_builder.build();
    m_current_project->features_node = features_node.get();
    scene.add_child(features_node.release(), m_current_project->main_node);

    build_dimensioning_node();

    on_scene_selection_changed(
        m_project_interactor.selected_project_id(),
        m_scene_interactor.object_selection()
    );

    update_ui_dialog();
}

void MeasureGizmo::on_deactivated()
{
    reset();
    clear_scene();
    m_current_project->activated = false;
}

Scene::ToolType MeasureGizmo::type() const
{
    return Scene::ToolType::MeasureGizmo;
}

bool MeasureGizmo::enabled() const
{
    const Biz::Scene::ObjectSelection& selection{
        m_project_interactor.scene_interactor().object_selection()
    };
    return !selection.empty() && !selection.contains_wipe_tower();
}

GizmoWindowPtr MeasureGizmo::release_ui_window()
{
    return m_dialog.release();
}

static std::optional<Domain::Vec3d> position_on_feature(
    const SurfaceFeature& feature,
    const Domain::Vec3d& hit_position
)
{
    std::optional<Domain::Vec3d> ret;

    switch (feature.type()) {
    case SurfaceFeatureType::Point: {
        ret = feature.point();
        break;
    }
    case SurfaceFeatureType::Edge: {
        auto [from, to] = feature.edge();
        Domain::Vec3d v = hit_position - from;
        double t        = std::clamp(v.dot(to - from) / (to - from).squaredNorm(), 0.0, 1.0);
        ret             = Domain::Vec3d(
            std::lerp(from.x(), to.x(), t),
            std::lerp(from.y(), to.y(), t),
            std::lerp(from.z(), to.z(), t)
        );
        break;
    }
    case SurfaceFeatureType::Circle: {
        auto [center, radius, normal] = feature.circle();
        Domain::Vec3d v               = hit_position - center;
        ret                           = center + radius * v.normalized() - v.dot(normal) * normal;
        break;
    }
    case SurfaceFeatureType::Plane: {
        ret = hit_position;
        break;
    }
    case SurfaceFeatureType::Undefined:
        break;
    }

    return ret;
}

std::optional<FeatureItem> MeasureGizmo::detect_current_feature()
{
    std::optional<FeatureItem> ret;
    if (m_feature_detection_data.has_value()) {
        auto inst = m_feature_detection_data->hovered_instance;
        if (inst->measuring == nullptr)
            return std::nullopt;

        const Scene::AabbRaycastNodeComponent*
            raycast_component = dynamic_cast<const Scene::AabbRaycastNodeComponent*>(
                m_feature_detection_data->node->raycast_component()
            );
        DEBUG_ASSERT(raycast_component != nullptr);
        AABBMesh::hit_result hit_result = raycast_component->hit_result(
            m_feature_detection_data->hovered_volume->world_trafo.matrix(),
            m_feature_detection_data->pick_ray
        );
        if (hit_result.is_hit()) {
            Domain::Vec3d hit_position = m_feature_detection_data->pick_ray.origin
                + m_feature_detection_data->pick_ray.direction * m_feature_detection_data->hit_coord;
            int hit_face_id = hit_result.face()
                + m_feature_detection_data->hovered_volume->face_offset;
            std::optional<SurfaceFeature> feature = m_feature_detection_data->hovered_instance
                                                        ->measuring->feature(hit_face_id, hit_position);
            if (feature.has_value()) {
                // for point on feature selection, force the point to lie on the feature
                if (m_selection_mode == SelectionMode::Point) {
                    std::optional<Domain::Vec3d> pos_on_feature = position_on_feature(
                        *feature,
                        hit_position
                    );
                    if (pos_on_feature.has_value())
                        hit_position = *pos_on_feature;
                    ret = {
                        m_feature_detection_data->hovered_instance->ref,
                        SurfaceFeature(hit_position),
                        feature->type() == SurfaceFeatureType::Point ? std::nullopt : feature
                    };
                } else
                    ret = {m_feature_detection_data->hovered_instance->ref, *feature};
            }
        }
    }

    return ret;
}

Scene::GizmoActivationState MeasureGizmo::on_mouse(Scene::GizmoEventContext& ctx, bool only_active)
{
    Scene::GizmoActivationState return_value = (m_selection_mode == SelectionMode::Point) ?
        Scene::GizmoActivationState::Active :
        Scene::GizmoActivationState::Inactive;

    if (m_current_project->scene_selection_cache.volumes.empty())
        // selection is empty
        return return_value;

    bool is_left_button = (ctx.mouse_event().button() & Platform::MouseButton::Left)
        == Platform::MouseButton::Left;
    auto event_type = ctx.mouse_event().type();
    const bool button_down{event_type == Platform::MouseEvent::Type::ButtonDown && is_left_button};
    if (button_down) {
        m_mouse_left_down = true;
    }
    else if (event_type == Platform::MouseEvent::Type::ButtonUp && is_left_button)
        m_mouse_left_down = false;

    auto& feature_cache = m_current_project->feature_cache;

    Scene::Node* hovered_feature_node = ctx.pick_result_node_with_tag_of_type<MeasureGizmoNodeTag>();
    Scene::Node* hovered_scene_node = ctx.pick_result_node_with_tag_of_type<SceneNodeTag>();

    update_feature_detection_data(hovered_scene_node, ctx);

    if (hovered_scene_node != nullptr && !m_feature_detection_data.has_value()) {
        // hovering an unselected volume
        feature_cache.current.reset();
        update_current_feature_on_scene();
        update_ui_dialog();

        if (button_down) {
            Biz::Scene::ObjectSelection selection;
            const SceneNodeTag* tag{hovered_scene_node->tag_of_type<SceneNodeTag>()};
            ASSERT(tag);
            if (tag->instance_id != 0) {
                ASSERT(tag->object_id != 0);
                selection.elements.push_back(Domain::ElementRef{tag->object_id, tag->instance_id});
            }
            m_scene_interactor.set_object_selection(selection);
            update_feature_detection_data(hovered_scene_node, ctx);
        }
        return Scene::GizmoActivationState::Inactive;
    }

    feature_cache.hover_id = HoverID::None;

    if (hovered_feature_node != nullptr) {
        // hovering a feature
        if (button_down) {
            const MeasureGizmoNodeTag& tag = *hovered_feature_node->tag_of_type<MeasureGizmoNodeTag>();
            switch (tag.type) {
            case MeasureGizmoElementType::CurrentFeature: {
                handle_left_click_on_current_feature(*hovered_feature_node);
                break;
            }
            case MeasureGizmoElementType::FirstSelectedFeature: {
                handle_left_click_on_first_selected_feature(*hovered_feature_node);
                break;
            }
            case MeasureGizmoElementType::SecondSelectedFeature: {
                handle_left_click_on_second_selected_feature(*hovered_feature_node);
                break;
            }
            case MeasureGizmoElementType::FirstCircleCenterFeature: {
                handle_left_click_on_first_circle_center_feature(*hovered_feature_node);
                break;
            }
            case MeasureGizmoElementType::SecondCircleCenterFeature: {
                handle_left_click_on_second_circle_center_feature(*hovered_feature_node);
                break;
            }
            case MeasureGizmoElementType::AuxiliaryFeature: {
                handle_left_click_on_current_feature(*hovered_feature_node->parent());
                break;
            }
            case MeasureGizmoElementType::Undefined:
            case MeasureGizmoElementType::Features:
            case MeasureGizmoElementType::Dimensionings:
            case MeasureGizmoElementType::DimensioningLinear:
            case MeasureGizmoElementType::DimensioningLinearStem:
            case MeasureGizmoElementType::DimensioningLinearArrow1:
            case MeasureGizmoElementType::DimensioningLinearArrow2:
            case MeasureGizmoElementType::DimensioningAngularArc:
            case MeasureGizmoElementType::DimensioningAngular:
                break;
            }
            update_measurement();
        } else {
            const MeasureGizmoNodeTag& tag = *hovered_feature_node->tag_of_type<MeasureGizmoNodeTag>();
            if (tag.type == MeasureGizmoElementType::FirstSelectedFeature)
                handle_hover_first_selected_feature(*hovered_feature_node);
            else if (tag.type == MeasureGizmoElementType::SecondSelectedFeature)
                handle_hover_second_selected_feature(*hovered_feature_node);
            else if (tag.type == MeasureGizmoElementType::FirstCircleCenterFeature)
                handle_hover_first_circle_center_feature(*hovered_feature_node);
            else if (tag.type == MeasureGizmoElementType::SecondCircleCenterFeature)
                handle_hover_second_circle_center_feature(*hovered_feature_node);
            else
                feature_cache.current = detect_current_feature();

            update_current_feature_on_scene();
            update_ui_dialog();
        }
    } else {
        if (hovered_scene_node == nullptr) {
            // nothing is hovered
            if (feature_cache.current.has_value())
                feature_cache.current.reset();
        } else
            // hovering a scene volume
            feature_cache.current = detect_current_feature();

        update_current_feature_on_scene();
        update_ui_dialog();
    }

    return return_value;
}

void MeasureGizmo::on_transient_mouse(Scene::GizmoEventContext& ctx)
{
    // this is a hack to cope with the on_mouse() method not being called, until the first mouse move event, while the Shift key is pressed
    if (m_current_project != nullptr
        && m_current_project->main_node != nullptr
        && !m_mouse_left_down
        && ctx.mouse_event().type() == Platform::MouseEvent::Type::ButtonDown
        && (ctx.mouse_event().button() & Platform::MouseButton::Left) == Platform::MouseButton::Left)
        on_mouse(ctx, false);
}

void MeasureGizmo::on_keyboard(Scene::GizmoKeyEventContext& ctx)
{
    const Platform::KeyboardEvent& evt = ctx.keyboard_event();
    if (m_current_project == nullptr || m_current_project->main_node == nullptr || evt.is_repeat())
        return;

    Platform::KeyCode code = evt.code();
    if (code == Platform::KeyCode::RShift || code == Platform::KeyCode::LShift) {
        m_selection_mode = (evt.type() == Platform::KeyboardEvent::Type::KeyDown) ?
            SelectionMode::Point :
            SelectionMode::Feature;

        m_current_project->feature_cache.current = detect_current_feature();
        update_current_feature_on_scene();
        Biz::Platform::PlatformServices::instance().render_request_handler().request_render();
    }
}

void MeasureGizmo::on_project_activated(size_t new_project_id)
{
    m_current_project     = &m_projects.project(new_project_id);
    m_current_project->id = new_project_id;

    on_scene_selection_changed(
        m_project_interactor.selected_project_id(),
        m_scene_interactor.object_selection()
    );

    // restore selected features to scene
    const auto& feature_cache = m_current_project->feature_cache;
    if (feature_cache.first_selected().has_value()) {
        const FeatureItem& feature = *feature_cache.first_selected();

        const auto& instances = m_current_project->scene_selection_cache.instances;
        auto inst_it          = std::find_if(
            instances.begin(),
            instances.end(),
            [&feature](const InstanceCacheItem& item) {
                return item.ref.object_id == feature.ref.object_id
                    && item.ref.instance_id == feature.ref.instance_id;
            }
        );
        DEBUG_ASSERT(inst_it != instances.end());

        add_feature_to_scene(
            feature.feature,
            MeasureGizmoElementType::FirstSelectedFeature,
            feature.ref,
            FIRST_FEATURE_NAME,
            FIRST_FEATURE_COLOR,
            *inst_it->measuring,
            *m_current_project->features_node
        );
    }

    if (feature_cache.second_selected().has_value()) {
        const FeatureItem& feature = *feature_cache.second_selected();

        const auto& instances = m_current_project->scene_selection_cache.instances;
        auto inst_it          = std::find_if(
            instances.begin(),
            instances.end(),
            [&feature](const InstanceCacheItem& item) {
                return item.ref.object_id == feature.ref.object_id
                    && item.ref.instance_id == feature.ref.instance_id;
            }
        );
        DEBUG_ASSERT(inst_it != instances.end());

        add_feature_to_scene(
            feature.feature,
            MeasureGizmoElementType::SecondSelectedFeature,
            feature.ref,
            SECOND_FEATURE_NAME,
            SECOND_FEATURE_COLOR,
            *inst_it->measuring,
            *m_current_project->features_node
        );
    }

    update_measurement();
    update_ui_dialog();

    m_selection_mode = ImGui::GetIO().KeyShift ? SelectionMode::Point : SelectionMode::Feature;
    m_dialog->show_measure(!m_current_project->scene_selection_cache.volumes.empty());
}

void MeasureGizmo::on_project_deactivated(size_t old_project_id)
{
    clear_features();
}

void MeasureGizmo::render_scene(Render::CommandBuffer& cmd_buffer)
{
    // recover state when the mouse left button is released while the scene has no focus
    // it also detect mouse left button up events not passed to the on_mouse() method
    // while the gizmo is set as inactive
    if (m_mouse_left_down && !ImGui::GetIO().MouseDown[0])
        m_mouse_left_down = false;

    // recover state when the Shift key is released while the scene has no focus
    if (m_selection_mode == SelectionMode::Point && !ImGui::GetIO().KeyShift)
        m_selection_mode = SelectionMode::Feature;

    update_highlight();
    // required to properly orient arrow billboards
    update_linear_dimensioning();

    if (!m_mouse_left_down)
        update_scene_selection_cache_measuring_geometry();
}

void MeasureGizmo::register_commands(Platform::CommandRegistry& registry)
{
    registry
        .register_command(
            std::make_unique<Platform::FuncCommand>(
                "measure-gizmo-unselect-feature",
                [this]() {
                    if (m_current_project->feature_cache.second_selected().has_value()) {
                        // remove second selected feature
                        m_current_project->feature_cache.second_selected().reset();
                        remove_feature_from_scene(MeasureGizmoElementType::SecondSelectedFeature);
                        m_current_project->dimensioning_nodes.main->set_enabled(false);
                        m_current_project->feature_cache.hover_id = HoverID::None;
                    } else {
                        // remove first selected feature
                        m_current_project->feature_cache.first_selected().reset();
                        remove_feature_from_scene(MeasureGizmoElementType::FirstSelectedFeature);
                        m_current_project->feature_cache.hover_id = HoverID::None;
                    }

                    update_measurement();
                },
                FuncCommandExtraOpts{
                    .keyboard_shortcuts =
                        Platform::KeyboardShortcuts{
                            Platform::KeyboardShortcut{0, Platform::KeyCode::Backspace}
                        }
                }
            )
        );
}

static Domain::ElementRefs extract_volume_ids_from_selection(
    const Biz::Scene::ObjectSelection& selection,
    const Domain::Project& project
)
{
    Domain::ElementRefs ret;
    for (const auto& ref : selection.elements) {
        if (ref.volume_id == 0) {
            const Domain::ModelObject* object = project.find_object_by_id(ref.object_id);
            for (const auto volume : object->volumes) {
                ret.push_back({ref.object_id, ref.instance_id, volume->id().id});
            }
        } else
            ret.push_back(ref);
    }
    return ret;
}

// returns a pair of removed and added volumes
static std::pair<Domain::ElementRefs, Domain::ElementRefs> compare_volume_ids(
    Domain::ElementRefs old_ids,
    Domain::ElementRefs new_ids
)
{
    std::sort(old_ids.begin(), old_ids.end());
    std::sort(new_ids.begin(), new_ids.end());

    std::pair<Domain::ElementRefs, Domain::ElementRefs> ret;
    auto& [removed, added] = ret;
    std::set_difference(
        old_ids.begin(),
        old_ids.end(),
        new_ids.begin(),
        new_ids.end(),
        std::back_inserter(removed)
    );
    std::set_difference(
        new_ids.begin(),
        new_ids.end(),
        old_ids.begin(),
        old_ids.end(),
        std::back_inserter(added)
    );
    return ret;
}

void MeasureGizmo::on_scene_selection_changed(
    Domain::SelectionId project_id,
    const Biz::Scene::ObjectSelection& selection
)
{
    // the gizmo is not active
    if (!enabled()
        || m_current_project == nullptr
        || m_current_project->main_node == nullptr
        || m_current_project->id != project_id
        || !m_current_project->activated)
        return;

    if (selection.empty()) {
        reset();
        return;
    }

    m_current_project->feature_cache.reset();
    update_measurement();
    clear_features();

    m_dialog->show_measure(true);

    Domain::ElementRefs& volumes_ids = m_current_project->scene_selection_cache.volume_ids;
    Domain::ElementRefs volumes_from_selection = extract_volume_ids_from_selection(
        selection,
        m_project_interactor.selected_project()
    );
    if (volumes_ids != volumes_from_selection) {
        auto [removed, added] = compare_volume_ids(volumes_ids, volumes_from_selection);
        update_scene_selection_cache_state(removed, added);
        volumes_ids = volumes_from_selection;
        Biz::Platform::PlatformServices::instance().render_request_handler().request_render();
    }
}

void MeasureGizmo::on_scene_selection_transformed(
    Domain::SelectionId project_id,
    const Biz::Scene::ObjectSelection& selection
)
{
    // the gizmo is not active
    if (m_current_project == nullptr
        || m_current_project->main_node == nullptr
        || m_current_project->id != project_id
        || !m_current_project->activated)
        return;

    m_current_project->feature_cache.reset();
    update_measurement();
    clear_features();

    m_current_project->geometry_manager.release_if([](const std::string& name,
                                                      const Render::Geometry& geom) {
        return name.starts_with("feature_plane");
    });

    m_current_project->triangle_mesh_manager.release_if([](const std::string& name,
                                                           const Scene::TriangleMesh& mesh) {
        return name.starts_with("feature_plane");
    });

    // detect instances to update
    auto& instances = m_current_project->scene_selection_cache.instances;
    for (const auto& elem : selection.elements) {
        auto inst_it = std::find_if(
            instances.begin(),
            instances.end(),
            [&elem](const InstanceCacheItem& item) {
                return item.ref.object_id == elem.object_id
                    && item.ref.instance_id == elem.instance_id;
            }
        );
        DEBUG_ASSERT(inst_it != instances.end());
        inst_it->modified = true;
    }
    Biz::Platform::PlatformServices::instance().render_request_handler().request_render();
}

void MeasureGizmo::render_imgui()
{
#if MEASURE_GIZMO_DEBUG
    const auto& scene_selection_cache = m_current_project->scene_selection_cache;
    const auto& feature_cache         = m_current_project->feature_cache;

    if (ImGui::Begin("Measure gizmo debug", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::BeginTable("Project", 2, ImGuiTableFlags_Borders)) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Project ID");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%d", m_current_project->id);
            ImGui::EndTable();
        }
        if (ImGui::BeginTable("SelectionMode", 2, ImGuiTableFlags_Borders)) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Selection mode");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", (m_selection_mode == SelectionMode::Feature) ? "Feature" : "Point");
            ImGui::EndTable();
        }
        if (ImGui::BeginTable("MouseLeftDown", 2, ImGuiTableFlags_Borders)) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Mouse left down");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", m_mouse_left_down ? "True" : "False");
            ImGui::EndTable();
        }
        if (ImGui::BeginTable("HoverID", 2, ImGuiTableFlags_Borders)) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Hover ID");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%d", int(m_current_project->feature_cache.hover_id));
            ImGui::EndTable();
        }

        ImGui::Text("Scene selection cache");
        if (scene_selection_cache.volume_ids.empty())
            ImGui::Text("EMPTY");
        else {
            ImGui::Text("Selected volumes ids");
            if (ImGui::BeginTable("SelectedVolumesIds", 2, ImGuiTableFlags_Borders)) {
                for (size_t i = 0; i < scene_selection_cache.volume_ids.size(); ++i) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("Volume %d", i + 1);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text(
                        "o:%d, i:%d, v:%d",
                        scene_selection_cache.volume_ids[i].object_id,
                        scene_selection_cache.volume_ids[i].instance_id,
                        scene_selection_cache.volume_ids[i].volume_id
                    );
                }
                ImGui::EndTable();
            }
            ImGui::Text("Volume cache");
            if (ImGui::BeginTable("VolumeCache", 2, ImGuiTableFlags_Borders)) {
                ImGui::TableSetupColumn("Volume");
                ImGui::TableSetupColumn("Offset");
                ImGui::TableHeadersRow();

                for (size_t i = 0; i < scene_selection_cache.volumes.size(); ++i) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text(
                        "o:%d, i:%d, v:%d",
                        scene_selection_cache.volumes[i].ref.object_id,
                        scene_selection_cache.volumes[i].ref.instance_id,
                        scene_selection_cache.volumes[i].ref.volume_id
                    );
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%d", scene_selection_cache.volumes[i].face_offset);
                }
                ImGui::EndTable();
            }
            ImGui::Text("Instance cache");
            if (ImGui::BeginTable("InstanceCache", 2, ImGuiTableFlags_Borders)) {
                ImGui::TableSetupColumn("Instance");
                ImGui::TableSetupColumn("Modified");
                ImGui::TableHeadersRow();

                for (size_t i = 0; i < scene_selection_cache.instances.size(); ++i) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text(
                        "o:%d, i:%d",
                        scene_selection_cache.instances[i].ref.object_id,
                        scene_selection_cache.instances[i].ref.instance_id
                    );
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%s", scene_selection_cache.instances[i].modified ? "true" : "false");
                }
                ImGui::EndTable();
            }
        }
        ImGui::Separator();
        ImGui::Text("Current feature");
        if (feature_cache.current.has_value()) {
            std::string text;
            switch (feature_cache.current->feature.type()) {
            default:
            case SurfaceFeatureType::Undefined: {
                text = "Undefined";
                break;
            }
            case SurfaceFeatureType::Point: {
                if (feature_cache.current->parent.has_value()) {
                    text = "Point on ";
                    switch (feature_cache.current->parent->type()) {
                    default:
                    case SurfaceFeatureType::Undefined: {
                        text += "undefined";
                        break;
                    }
                    case SurfaceFeatureType::Point: {
                        text += "point";
                        break;
                    }
                    case SurfaceFeatureType::Edge: {
                        text += "edge";
                        break;
                    }
                    case SurfaceFeatureType::Circle: {
                        text += "circle";
                        break;
                    }
                    case SurfaceFeatureType::Plane: {
                        text += "plane";
                        break;
                    }
                    }
                } else
                    text = "Vertex";
                break;
            }
            case SurfaceFeatureType::Edge: {
                text = "Edge";
                break;
            }
            case SurfaceFeatureType::Circle: {
                text = "Circle";
                break;
            }
            case SurfaceFeatureType::Plane: {
                text = "Plane";
                break;
            }
            }
            ImGui::Text("%s", text.c_str());
        } else
            ImGui::Text("EMPTY");
    }
    ImGui::End();
#endif // MEASURE_GIZMO_DEBUG
}

void MeasureGizmo::reset()
{
    m_current_project->scene_selection_cache.reset();
    m_current_project->feature_cache.reset();
    update_measurement();
    clear_features();
    m_dialog->show_measure(false);
}

void MeasureGizmo::update_scene_selection_cache_state(
    const Domain::ElementRefs& removed_volumes,
    const Domain::ElementRefs& added_volumes
)
{
    auto& volumes   = m_current_project->scene_selection_cache.volumes;
    auto& instances = m_current_project->scene_selection_cache.instances;

    const Domain::Project& project = m_project_interactor.selected_project();

    // update volume cache and detect instances to update
    for (const auto& v : removed_volumes) {
        auto vol_it = std::find_if(volumes.begin(), volumes.end(), [&v](const VolumeCacheItem& item) {
            return item.ref == v;
        });
        DEBUG_ASSERT(vol_it != volumes.end());
        volumes.erase(vol_it);

        auto inst_it = std::find_if(
            instances.begin(),
            instances.end(),
            [&v](const InstanceCacheItem& item) {
                return item.ref.object_id == v.object_id && item.ref.instance_id == v.instance_id;
            }
        );
        DEBUG_ASSERT(inst_it != instances.end());
        inst_it->modified = true;
    }

    for (const auto& v : added_volumes) {
        const Domain::ModelObject* object = project.find_object_by_id(v.object_id);
        const Domain::ModelInstance* instance = project.find_instance_by_id(v.object_id, v.instance_id);
        if (v.has_volume()) {
            const Domain::ModelVolume* volume = project.find_volume_by_id(v.object_id, v.volume_id);
            VolumeCacheItem item;
            item.ref         = v;
            item.mesh        = &volume->mesh();
            item.world_trafo = instance->get_matrix() * volume->get_matrix();
            volumes.emplace_back(item);
        } else {
            for (const auto volume : object->volumes) {
                VolumeCacheItem item;
                item.ref         = {v.object_id, v.instance_id, volume->id().id};
                item.mesh        = &volume->mesh();
                item.world_trafo = instance->get_matrix() * volume->get_matrix();
                volumes.emplace_back(item);
            }
        }

        auto inst_it = std::find_if(
            instances.begin(),
            instances.end(),
            [&v](const InstanceCacheItem& item) {
                return item.ref.object_id == v.object_id && item.ref.instance_id == v.instance_id;
            }
        );
        if (inst_it == instances.end()) {
            InstanceCacheItem item;
            item.ref = {v.object_id, v.instance_id, 0};
            instances.push_back(std::move(item));
            inst_it = std::prev(instances.end());
        }
        inst_it->modified = true;
    }
}

void MeasureGizmo::update_scene_selection_cache_measuring_geometry()
{
    const Domain::Project& project = m_project_interactor.selected_project();
    auto& volumes                  = m_current_project->scene_selection_cache.volumes;
    auto& instances                = m_current_project->scene_selection_cache.instances;

    for (size_t i = 0; i < instances.size(); ++i) {
        InstanceCacheItem& inst_item = instances[i];
        if (inst_item.modified) {
            // removes instance render geometry from the scene
            auto& scene = m_scene_presenter.scene();
            if (inst_item.node != nullptr)
                scene.remove_child(inst_item.node);

            // check if all the volumes of the instance have been removed
            size_t vol_count = std::count_if(
                volumes.begin(),
                volumes.end(),
                [&inst_item](const VolumeCacheItem& vol_item) {
                    return vol_item.ref.object_id == inst_item.ref.object_id
                        && vol_item.ref.instance_id == inst_item.ref.instance_id;
                }
            );
            // and if so, remove the instance too
            if (vol_count == 0) {
                instances.erase(instances.begin() + i);
                --i;
            } else {
                // generate new measuring object for the instance by composing the volume meshes
                const Domain::ModelInstance* instance = project.find_instance_by_id(
                    inst_item.ref.object_id,
                    inst_item.ref.instance_id
                );
                Domain::Transform3d inst_matrix = instance->get_matrix();
                Domain::TriangleMesh composite_mesh;
                for (auto& vol_item : volumes) {
                    if (vol_item.ref.object_id == inst_item.ref.object_id
                        && vol_item.ref.instance_id == inst_item.ref.instance_id)
                    {
                        vol_item.world_trafo = inst_matrix
                            * project
                                  .find_volume_by_id(vol_item.ref.object_id, vol_item.ref.volume_id)
                                  ->get_matrix();
                        vol_item.face_offset = int(composite_mesh.its.indices.size());
                        Domain::TriangleMesh volume_mesh = *vol_item.mesh;
                        volume_mesh.transform(vol_item.world_trafo);
                        composite_mesh.merge(volume_mesh);
                    }
                }
                inst_item.measuring.reset(new Measuring(composite_mesh.its));
                inst_item.modified = false;
            }
        }
    }
}

void MeasureGizmo::update_feature_detection_data(
    const Scene::Node* scene_node,
    const Scene::GizmoEventContext& ctx
)
{
    if (scene_node == nullptr) {
        // nothing is hovered
        m_feature_detection_data.reset();
        return;
    }

    const SceneSelectionCache& cache = m_current_project->scene_selection_cache;
    const SceneNodeTag* tag          = scene_node->tag_of_type<SceneNodeTag>();
    DEBUG_ASSERT(tag != nullptr);

    auto vol_it = std::find_if(
        cache.volumes.begin(),
        cache.volumes.end(),
        [tag](const VolumeCacheItem& item) {
            return item.ref.object_id == tag->object_id
                && item.ref.instance_id == tag->instance_id
                && item.ref.volume_id == tag->volume_id;
        }
    );

    if (vol_it == cache.volumes.end()) {
        // hovering an unselected volume
        m_feature_detection_data.reset();
        return;
    }

    auto inst_it = std::find_if(
        cache.instances.begin(),
        cache.instances.end(),
        [&vol_it](const InstanceCacheItem& item) {
            return item.ref.object_id == vol_it->ref.object_id
                && item.ref.instance_id == vol_it->ref.instance_id;
        }
    );
    DEBUG_ASSERT(inst_it != cache.instances.end());

    Scene::NodePickResults pick_results = ctx.pick_results();
    if (!pick_results.empty()) {
        const Scene::NodePickResult& first_pick = pick_results.front();
        const MeasureGizmoNodeTag* first_pick_tag = first_pick.node->tag_of_type<MeasureGizmoNodeTag>();
        if (first_pick_tag != nullptr
            && (first_pick_tag->type == MeasureGizmoElementType::FirstCircleCenterFeature
                || first_pick_tag->type == MeasureGizmoElementType::SecondCircleCenterFeature))
        {
            // hovering a circle center feature
            m_feature_detection_data = FeatureDetectionData{
                &(*vol_it),
                &(*inst_it),
                first_pick.node,
                ctx.pick_ray(),
                first_pick.cast.distance
            };
            return;
        }
    }

    double t = 0.0;
    for (size_t i = 0; i < pick_results.size(); ++i) {
        if (pick_results[i].node == scene_node) {
            t = pick_results[i].cast.distance;
            break;
        }
    }

    m_feature_detection_data = FeatureDetectionData{
        &(*vol_it),
        &(*inst_it),
        scene_node,
        ctx.pick_ray(),
        t
    };
}

void MeasureGizmo::update_current_feature_on_scene()
{
    remove_feature_from_scene(MeasureGizmoElementType::CurrentFeature);
    if (m_current_project->feature_cache.current.has_value()) {
        const FeatureItem& current_feature = *m_current_project->feature_cache.current;
        const auto& instances              = m_current_project->scene_selection_cache.instances;
        auto inst_it                       = std::find_if(
            instances.begin(),
            instances.end(),
            [&current_feature](const InstanceCacheItem& item) {
                return item.ref.object_id == current_feature.ref.object_id
                    && item.ref.instance_id == current_feature.ref.instance_id;
            }
        );
        DEBUG_ASSERT(inst_it != instances.end());

        add_feature_to_scene(
            current_feature.feature,
            MeasureGizmoElementType::CurrentFeature,
            current_feature.ref,
            CURRENT_FEATURE_NAME,
            m_current_project->feature_cache.first_selected().has_value() ?
                SECOND_FEATURE_HOVERED_COLOR :
                FIRST_FEATURE_HOVERED_COLOR,
            *inst_it->measuring,
            *m_current_project->features_node
        );

        // add center to circle features
        if (current_feature.feature.type() == SurfaceFeatureType::Circle) {
            Scene::Node* node = m_current_project->features_node->query_first([](const Scene::Node* n) {
                return n->tag_of_type<MeasureGizmoNodeTag>()->type
                    == MeasureGizmoElementType::CurrentFeature;
            });
            DEBUG_ASSERT(node != nullptr);

            add_feature_to_scene(
                SurfaceFeature(Domain::Vec3d::Zero()),
                m_current_project->feature_cache.first_selected().has_value() ?
                    MeasureGizmoElementType::SecondCircleCenterFeature :
                    MeasureGizmoElementType::FirstCircleCenterFeature,
                current_feature.ref,
                CIRCLE_CENTER_FEATURE_NAME,
                NEUTRAL_COLOR,
                *inst_it->measuring,
                *node
            );
        }

        // add auxiliary feature for point on edge and point on circle
        else if (current_feature.feature.type() == SurfaceFeatureType::Point
                 && current_feature.parent.has_value()
                 && (current_feature.parent->type() == SurfaceFeatureType::Edge
                     || current_feature.parent->type() == SurfaceFeatureType::Circle))
        {
            Scene::Node* node = m_current_project->features_node->query_first([](const Scene::Node* n) {
                return n->tag_of_type<MeasureGizmoNodeTag>()->type
                    == MeasureGizmoElementType::CurrentFeature;
            });
            DEBUG_ASSERT(node != nullptr);

            Scene::Transform trafo = node->local_transform();

            add_feature_to_scene(
                *current_feature.parent,
                MeasureGizmoElementType::AuxiliaryFeature,
                current_feature.ref,
                AUXILIARY_FEATURE_NAME,
                NEUTRAL_COLOR,
                *inst_it->measuring,
                *node
            );

            node = m_current_project->features_node->query_first([](const Scene::Node* n) {
                return n->tag_of_type<MeasureGizmoNodeTag>()->type
                    == MeasureGizmoElementType::AuxiliaryFeature;
            });
            DEBUG_ASSERT(node != nullptr);

            node->set_local_transform(trafo.inverse() * node->local_transform());
        }
    }
}

void MeasureGizmo::update_measurement()
{
    update_measurement_result();
    update_dimensioning();
    update_ui_dialog();
}

void MeasureGizmo::update_measurement_result()
{
    m_measurement_result = MeasurementResult();
    auto& feature_cache  = m_current_project->feature_cache;
    if (feature_cache.second_selected().has_value()) {
        const auto& instances = m_current_project->scene_selection_cache.instances;

        //
        // measuring is needed only in case of edge-plane or circle-plane measurements
        // it must come from the feature containing the plane
        //
        SurfaceFeatureType f0 = feature_cache.first_selected()->feature.type();
        SurfaceFeatureType f1 = feature_cache.second_selected()->feature.type();
        Measuring* measuring  = nullptr;
        int id                = -1;
        if (f0 == SurfaceFeatureType::Plane
            && (f1 == SurfaceFeatureType::Edge || f1 == SurfaceFeatureType::Circle))
            id = 0;
        else if (f1 == SurfaceFeatureType::Plane
                 && (f0 == SurfaceFeatureType::Edge || f0 == SurfaceFeatureType::Circle))
            id = 1;
        if (id != -1) {
            auto inst_it = std::find_if(
                instances.begin(),
                instances.end(),
                [&feature_cache, id](const InstanceCacheItem& item) {
                    return item.ref.object_id == feature_cache.selected[id]->ref.object_id
                        && item.ref.instance_id == feature_cache.selected[id]->ref.instance_id;
                }
            );
            DEBUG_ASSERT(inst_it != instances.end());
            measuring = inst_it->measuring.get();
        }
        m_measurement_result = measurement(
            feature_cache.first_selected()->feature,
            feature_cache.second_selected()->feature,
            measuring
        );
    }

    Biz::Platform::PlatformServices::instance().render_request_handler().request_render();
}

void MeasureGizmo::update_dimensioning()
{
    if (!m_measurement_result.has_any_data()) {
        m_current_project->dimensioning_nodes.main->set_enabled(false);
        return;
    }

    update_linear_dimensioning();
    update_angular_dimensioning();

    m_current_project->dimensioning_nodes.main->set_enabled(true);
}

void MeasureGizmo::update_linear_dimensioning()
{
    if (!m_measurement_result.has_distance_data()) {
        // no linear result available
        m_current_project->dimensioning_nodes.linear->set_enabled(false);
        return;
    }

    const DistAndPoints& dap = m_measurement_result.distance_infinite.has_value() ?
        *m_measurement_result.distance_infinite :
        *m_measurement_result.distance_strict;

    const Scene::Camera& camera = m_scene_presenter.scene().camera();
    Domain::Vec2d v1_ss         = camera.project_to_screen_space(dap.from);
    Domain::Vec2d v2_ss         = camera.project_to_screen_space(dap.to);
    Domain::Vec2d v12_ss        = v2_ss - v1_ss;
    if (v12_ss.norm() < 2.0) {
        // linear result almost parallel to view direction
        m_current_project->dimensioning_nodes.linear->set_enabled(false);
        return;
    }

    m_current_project->dimensioning_nodes.linear->set_enabled(true);

    Domain::Vec3d v12 = dap.to - dap.from;
    double v12_len    = v12.norm();

    // stem child
    std::string id = fmt::format("dimensioning_line_{}", v12_len);

    std::vector<Domain::Vec3f> lines = {Domain::Vec3f::Zero(), v12_len * Domain::Vec3f::UnitX()};
    const auto* stem_geom            = m_current_project->geometry_manager.get_or_create(id, [&]() {
        return Render::geometry_from_lines(m_device, lines);
    });

    auto q_x_to_v12 = Eigen::Quaternion<double>::FromTwoVectors(Domain::Vec3d::UnitX(), v12);
    Domain::Transform3d stem_trafo = Domain::Transform3d::Identity();
    stem_trafo.translate(dap.from).rotate(q_x_to_v12);

    Scene::Node* stem_node = m_current_project->dimensioning_nodes.linear->query_first(
        [](const Scene::Node* n) {
            return n->tag_of_type<MeasureGizmoNodeTag>()->type
                == MeasureGizmoElementType::DimensioningLinearStem;
        }
    );
    DEBUG_ASSERT(stem_node != nullptr);
    stem_node->set_local_transform(stem_trafo);

    Scene::MeshRenderNodeComponent* stem_render_component = nullptr;
    if (stem_node->has_render_component()) {
        stem_render_component = dynamic_cast<Scene::MeshRenderNodeComponent*>(
            stem_node->render_component()
        );
        stem_render_component->set_geometry(stem_geom);
    } else {
        std::unique_ptr<Scene::MeshRenderNodeComponent>
            rc = std::make_unique<Scene::MeshRenderNodeComponent>(stem_geom, dimensioning_material());
        stem_node->set_render_component(std::move(rc));
        stem_render_component = dynamic_cast<Scene::MeshRenderNodeComponent*>(
            stem_node->render_component()
        );
    }
    DEBUG_ASSERT(stem_render_component != nullptr);
    stem_render_component->set_layer_index(Scene::RenderLayerId(PlaterSceneLayer::AlwaysOnTop));

    // arrow children
    auto q_billboard = Eigen::Quaternion<double>::FromTwoVectors(
        Domain::Vec3d::UnitZ(),
        -camera.forward()
    );
    Domain::Transform3d rot_billboard(q_billboard);
    Domain::Vec2d v1_ref_point_ss = camera.project_to_screen_space(
                                        dap.from + rot_billboard * Domain::Vec3d::UnitY()
                                    )
        - v1_ss;
    double angle12_ss = std::atan2(v1_ref_point_ss.y(), v1_ref_point_ss.x())
        - std::atan2(v12_ss.y(), v12_ss.x());
    if (angle12_ss > double(M_PI))
        angle12_ss -= 2.0 * double(M_PI);
    else if (angle12_ss < -double(M_PI))
        angle12_ss += 2.0 * double(M_PI);

    Domain::Transform3d rot_12_ss(Eigen::AngleAxis(angle12_ss, camera.forward()).toRotationMatrix());
    Domain::Transform3d trafo12 = Eigen::Translation<double, 3>(dap.from) * rot_12_ss * rot_billboard;

    double angle21_ss = double(M_PI) + angle12_ss;
    Domain::Transform3d rot_21_ss(Eigen::AngleAxis(angle21_ss, camera.forward()).toRotationMatrix());
    Domain::Transform3d trafo21 = Eigen::Translation<double, 3>(dap.to) * rot_21_ss * rot_billboard;

    Scene::Node* arrow_node = m_current_project->dimensioning_nodes.linear->query_first(
        [](const Scene::Node* n) {
            return n->tag_of_type<MeasureGizmoNodeTag>()->type
                == MeasureGizmoElementType::DimensioningLinearArrow1;
        }
    );
    DEBUG_ASSERT(arrow_node != nullptr);
    arrow_node->set_local_transform(trafo12);

    arrow_node = m_current_project->dimensioning_nodes.linear->query_first([](const Scene::Node* n) {
        return n->tag_of_type<MeasureGizmoNodeTag>()->type
            == MeasureGizmoElementType::DimensioningLinearArrow2;
    });
    DEBUG_ASSERT(arrow_node != nullptr);
    arrow_node->set_local_transform(trafo21);
}

void MeasureGizmo::update_angular_dimensioning()
{
    if (!m_measurement_result.angle.has_value()) {
        // no angular result available
        m_current_project->dimensioning_nodes.angular->set_enabled(false);
        return;
    }

    m_current_project->dimensioning_nodes.angular->set_enabled(true);

    const auto& feature_cache = m_current_project->feature_cache;
    const SurfaceFeature* f1  = &feature_cache.first_selected()->feature;
    const SurfaceFeature* f2  = &feature_cache.second_selected()->feature;

    SurfaceFeatureType ft1 = f1->type();
    SurfaceFeatureType ft2 = f2->type();

    // Order features by type so following conditions are simple.
    if (ft1 > ft2) {
        std::swap(ft1, ft2);
        std::swap(f1, f2);
    }

    // If there is an angle to show, update the arc:
    if (ft1 == SurfaceFeatureType::Edge && ft2 == SurfaceFeatureType::Edge)
        update_arc_edge_edge_dimensioning(*f1, *f2);
    else if (ft1 == SurfaceFeatureType::Edge && ft2 == SurfaceFeatureType::Plane)
        update_arc_edge_plane_dimensioning(*f1, *f2);
    else if (ft1 == SurfaceFeatureType::Plane && ft2 == SurfaceFeatureType::Plane)
        update_arc_plane_plane_dimensioning(*f1, *f2);
}

void MeasureGizmo::update_arc_edge_edge_dimensioning(
    const Measure::SurfaceFeature& f1,
    const Measure::SurfaceFeature& f2
)
{
    double angle  = m_measurement_result.angle->angle;
    double radius = m_measurement_result.angle->radius;
    if (std::abs(angle) < Domain::EPSILON || std::abs(radius) < Domain::EPSILON) {
        // too small to show
        m_current_project->dimensioning_nodes.angular->set_enabled(false);
        return;
    }

    const Domain::Vec3d& center = m_measurement_result.angle->center;
    Domain::Vec3d e1_unit       = edge_direction(m_measurement_result.angle->e1);
    Domain::Vec3d e2_unit       = edge_direction(m_measurement_result.angle->e2);

    unsigned int resolution = std::max<unsigned int>(2, 64 * angle / double(M_PI));
    double step             = angle / double(resolution);
    Domain::Vec3d normal    = e1_unit.cross(e2_unit).normalized();

    std::vector<Domain::Vec3f> lines;
    lines.reserve(2 * resolution);
    for (unsigned int i = 0; i < resolution; ++i) {
        lines.emplace_back(
            (radius
             * (Eigen::Quaternion<double>(Eigen::AngleAxisd(step * double(i + 0), normal)) * e1_unit))
                .cast<float>()
        );
        lines.emplace_back(
            (radius
             * (Eigen::Quaternion<double>(Eigen::AngleAxisd(step * double(i + 1), normal)) * e1_unit))
                .cast<float>()
        );
    }

    std::string id = fmt::format(
        "dimensioning_arc_{}_{}_{}_{}_{}",
        radius,
        angle,
        normal.x(),
        normal.y(),
        normal.z()
    );
    const auto* geom = m_current_project->geometry_manager.get_or_create(id, [&]() {
        return Render::geometry_from_lines(m_device, lines);
    });

    Scene::Node* node = m_current_project->dimensioning_nodes.angular->query_first(
        [](const Scene::Node* n) {
            return n->tag_of_type<MeasureGizmoNodeTag>()->type
                == MeasureGizmoElementType::DimensioningAngularArc;
        }
    );
    DEBUG_ASSERT(node != nullptr);
    Domain::Transform3d trafo = Domain::Transform3d::Identity();
    trafo.translate(center);
    node->set_local_transform(trafo);

    Scene::MeshRenderNodeComponent* render_component = nullptr;
    if (node->has_render_component()) {
        render_component = dynamic_cast<Scene::MeshRenderNodeComponent*>(node->render_component());
        render_component->set_geometry(geom);
    } else {
        std::unique_ptr<Scene::MeshRenderNodeComponent>
            rc = std::make_unique<Scene::MeshRenderNodeComponent>(geom, dimensioning_material());
        node->set_render_component(std::move(rc));
        render_component = dynamic_cast<Scene::MeshRenderNodeComponent*>(node->render_component());
    }
    DEBUG_ASSERT(render_component != nullptr);
    render_component->set_layer_index(Scene::RenderLayerId(PlaterSceneLayer::AlwaysOnTop));
}

void MeasureGizmo::update_arc_edge_plane_dimensioning(
    const Measure::SurfaceFeature& f1,
    const Measure::SurfaceFeature& f2
)
{
    DEBUG_ASSERT(f1.type() == SurfaceFeatureType::Edge && f2.type() == SurfaceFeatureType::Plane);

    const std::pair<Domain::Vec3d, Domain::Vec3d>& e1 = m_measurement_result.angle->e1;
    const std::pair<Domain::Vec3d, Domain::Vec3d>& e2 = m_measurement_result.angle->e2;
    update_arc_edge_edge_dimensioning(
        SurfaceFeature(SurfaceFeatureType::Edge, e1.first, e1.second),
        SurfaceFeature(SurfaceFeatureType::Edge, e2.first, e2.second)
    );
}

void MeasureGizmo::update_arc_plane_plane_dimensioning(
    const Measure::SurfaceFeature& f1,
    const Measure::SurfaceFeature& f2
)
{
    DEBUG_ASSERT(f1.type() == SurfaceFeatureType::Plane && f2.type() == SurfaceFeatureType::Plane);

    const std::pair<Domain::Vec3d, Domain::Vec3d>& e1 = m_measurement_result.angle->e1;
    const std::pair<Domain::Vec3d, Domain::Vec3d>& e2 = m_measurement_result.angle->e2;
    update_arc_edge_edge_dimensioning(
        SurfaceFeature(SurfaceFeatureType::Edge, e1.first, e1.second),
        SurfaceFeature(SurfaceFeatureType::Edge, e2.first, e2.second)
    );
}

void MeasureGizmo::update_ui_dialog()
{
    if (m_current_project->feature_cache.first_selected().has_value())
        m_dialog->spot1().set_from(*m_current_project->feature_cache.first_selected());
    else
        m_dialog->spot1().reset();

    if (m_current_project->feature_cache.second_selected().has_value())
        m_dialog->spot2().set_from(*m_current_project->feature_cache.second_selected());
    else
        m_dialog->spot2().reset();

    m_dialog->update(m_measurement_result, m_current_project->feature_cache);
    Biz::Platform::PlatformServices::instance().render_request_handler().request_render();
}

void MeasureGizmo::update_highlight()
{
    auto& feature_cache = m_current_project->feature_cache;

    if (feature_cache.current.has_value())
        feature_cache.hover_id = HoverID::None;

    Scene::visit(*m_current_project->features_node, [&](Scene::Node& node) {
        node.remove_material_override();
    });

    if (feature_cache.hover_id == HoverID::FirstSelectedFeature) {
        Scene::Node* node = m_current_project->features_node->query_first([](const Scene::Node* n) {
            return n->tag_of_type<MeasureGizmoNodeTag>()->type
                == MeasureGizmoElementType::FirstSelectedFeature;
        });
        DEBUG_ASSERT(node != nullptr);

        // first selected feature is hovered -> highlight it
        set_override_color(*node, FIRST_FEATURE_HOVERED_COLOR);
        // if the first selected feature is a circle or a circle center -> gray out its center child
        if (feature_cache.first_selected()->feature.type() == SurfaceFeatureType::Circle
            || feature_cache.is_selected_circle_center(0))
            set_override_color(*node->children().front().get(), NEUTRAL_COLOR);

        return;
    } else if (feature_cache.hover_id == HoverID::SecondSelectedFeature) {
        Scene::Node* node = m_current_project->features_node->query_first([](const Scene::Node* n) {
            return n->tag_of_type<MeasureGizmoNodeTag>()->type
                == MeasureGizmoElementType::SecondSelectedFeature;
        });
        DEBUG_ASSERT(node != nullptr);

        // second selected feature is hovered -> highlight it
        set_override_color(*node, SECOND_FEATURE_HOVERED_COLOR);
        // if the second selected feature is a circle or a circle center -> gray out its center child
        if (feature_cache.second_selected()->feature.type() == SurfaceFeatureType::Circle
            || feature_cache.is_selected_circle_center(1))
            set_override_color(*node->children().front().get(), NEUTRAL_COLOR);

        return;
    } else if (feature_cache.hover_id == HoverID::FirstCircleCenterFeature) {
        Scene::Node* node = m_current_project->features_node->query_first([](const Scene::Node* n) {
            return n->tag_of_type<MeasureGizmoNodeTag>()->type
                == MeasureGizmoElementType::FirstCircleCenterFeature;
        });
        DEBUG_ASSERT(node != nullptr);

        // first circle center feature is hovered -> highlight it
        set_override_color(*node, FIRST_FEATURE_HOVERED_COLOR);
        // gray out its circle parent
        set_override_color(*node->parent(), NEUTRAL_COLOR);

        return;
    } else if (feature_cache.hover_id == HoverID::SecondCircleCenterFeature) {
        Scene::Node* node = m_current_project->features_node->query_first([](const Scene::Node* n) {
            return n->tag_of_type<MeasureGizmoNodeTag>()->type
                == MeasureGizmoElementType::SecondCircleCenterFeature;
        });
        DEBUG_ASSERT(node != nullptr);

        // second circle center feature is hovered -> highlight it
        set_override_color(*node, SECOND_FEATURE_HOVERED_COLOR);
        // gray out its circle parent
        set_override_color(*node->parent(), NEUTRAL_COLOR);

        return;
    }

    // no feature is hovered, check circle center features
    Scene::Node* node = m_current_project->features_node->query_first([](const Scene::Node* n) {
        return n->tag_of_type<MeasureGizmoNodeTag>()->type
            == MeasureGizmoElementType::FirstCircleCenterFeature;
    });
    if (node != nullptr) {
        // The first selected feature is a circle center
        if (feature_cache.is_selected_circle_center(0)) {
            // set non-hovered color
            set_override_color(*node, FIRST_FEATURE_COLOR);
            // gray out its circle parent
            set_override_color(*node->parent(), NEUTRAL_COLOR);
        }
    }
    node = m_current_project->features_node->query_first([](const Scene::Node* n) {
        return n->tag_of_type<MeasureGizmoNodeTag>()->type
            == MeasureGizmoElementType::SecondCircleCenterFeature;
    });
    if (node != nullptr) {
        // The second selected feature is a circle center
        if (feature_cache.is_selected_circle_center(1)) {
            // set non-hovered color
            set_override_color(*node, SECOND_FEATURE_COLOR);
            // gray out its circle parent
            set_override_color(*node->parent(), NEUTRAL_COLOR);
        }
    }
}

void MeasureGizmo::clear_scene()
{
    m_scene_presenter.scene().remove_child(m_current_project->main_node);
    m_current_project->main_node     = nullptr;
    m_current_project->features_node = nullptr;
    m_current_project->dimensioning_nodes.reset();
    m_current_project->geometry_manager.release_all();
    m_current_project->triangle_mesh_manager.release_all();
}

void MeasureGizmo::clear_features()
{
    m_scene_presenter.scene().remove_children(
        [](const Scene::Node* node) { return node->tag_of_type<MeasureGizmoNodeTag>() != nullptr; },
        m_current_project->features_node
    );
}

void MeasureGizmo::handle_left_click_on_current_feature(Scene::Node& feature_node)
{
    auto& feature_cache = m_current_project->feature_cache;

    if (!feature_cache.first_selected().has_value()) {
        // add first selected feature
        feature_cache.first_selected() = feature_cache.current;
        feature_node.set_debug_name(FIRST_FEATURE_NAME);
        feature_node.set_tag(MeasureGizmoNodeTag{MeasureGizmoElementType::FirstSelectedFeature});
        if (!feature_node.children().empty()) {
            // remove auxiliary feature, if present
            Scene::Node* child = feature_node.children().front().get();
            if (child->tag_of_type<MeasureGizmoNodeTag>()->type
                == MeasureGizmoElementType::AuxiliaryFeature)
                m_scene_presenter.scene().remove_child(child);
        }
        replace_color(feature_node, FIRST_FEATURE_COLOR);
        feature_cache.current.reset();
        feature_cache.hover_id = HoverID::FirstSelectedFeature;
    } else {
        // add or replace second selected feature
        if (feature_cache.second_selected().has_value())
            remove_feature_from_scene(MeasureGizmoElementType::SecondSelectedFeature);

        feature_cache.second_selected() = feature_cache.current;
        feature_node.set_debug_name(SECOND_FEATURE_NAME);
        feature_node.set_tag(MeasureGizmoNodeTag{MeasureGizmoElementType::SecondSelectedFeature});
        if (!feature_node.children().empty()) {
            // remove auxiliary feature, if present
            Scene::Node* child = feature_node.children().front().get();
            if (child->tag_of_type<MeasureGizmoNodeTag>()->type
                == MeasureGizmoElementType::AuxiliaryFeature)
                m_scene_presenter.scene().remove_child(child);
        }
        replace_color(feature_node, SECOND_FEATURE_COLOR);
        feature_cache.current.reset();
        feature_cache.hover_id = HoverID::SecondSelectedFeature;
    }
}

void MeasureGizmo::handle_left_click_on_first_selected_feature(Scene::Node& feature_node)
{
    auto& feature_cache = m_current_project->feature_cache;

    if (feature_cache.first_selected()->is_circle_center()) {
        // left click on grayed circle
        // set back the circle as main feature
        auto& feature    = feature_cache.first_selected();
        feature->feature = *feature->parent;
        feature->parent.reset();
        return;
    }

    if (!feature_cache.second_selected().has_value()) {
        // remove first selected feature
        feature_cache.current = feature_cache.first_selected();
        feature_node.set_debug_name(CURRENT_FEATURE_NAME);
        feature_node.set_tag(MeasureGizmoNodeTag{MeasureGizmoElementType::CurrentFeature});
        feature_cache.first_selected().reset();
    } else {
        // replace first selected feature with second selected feature
        // update cache
        feature_cache.first_selected() = feature_cache.second_selected();
        feature_cache.second_selected().reset();
        remove_feature_from_scene(MeasureGizmoElementType::FirstSelectedFeature);

        // update scene
        Scene::visit(*m_current_project->features_node, [&](Scene::Node& node) {
            const MeasureGizmoNodeTag& tag = *node.tag_of_type<MeasureGizmoNodeTag>();
            if (tag.type == MeasureGizmoElementType::SecondSelectedFeature) {
                node.set_debug_name(FIRST_FEATURE_NAME);
                node.set_tag(MeasureGizmoNodeTag{MeasureGizmoElementType::FirstSelectedFeature});
                replace_color(node, FIRST_FEATURE_COLOR);
            } else if (tag.type == MeasureGizmoElementType::SecondCircleCenterFeature) {
                node.set_debug_name(CIRCLE_CENTER_FEATURE_NAME);
                node.set_tag(MeasureGizmoNodeTag{MeasureGizmoElementType::FirstCircleCenterFeature});
                replace_color(node, NEUTRAL_COLOR);
            }
        });
    }
}

void MeasureGizmo::handle_left_click_on_second_selected_feature(Scene::Node& feature_node)
{
    auto& feature_cache = m_current_project->feature_cache;

    if (feature_cache.second_selected()->is_circle_center()) {
        // left click on grayed circle
        // set back the circle as main feature
        auto& feature    = feature_cache.second_selected();
        feature->feature = *feature->parent;
        feature->parent.reset();
        return;
    }

    // remove second selected feature
    feature_cache.current = feature_cache.second_selected();
    feature_node.set_debug_name(CURRENT_FEATURE_NAME);
    feature_node.set_tag(MeasureGizmoNodeTag{MeasureGizmoElementType::CurrentFeature});
    feature_cache.second_selected().reset();
}

void MeasureGizmo::handle_left_click_on_first_circle_center_feature(Scene::Node& feature_node)
{
    auto& feature = m_current_project->feature_cache.first_selected();
    if (feature->parent.has_value()) {
        // left click on selected center:
        // set back the circle as main feature
        feature->feature = *feature->parent;
        feature->parent.reset();
        // and handle the left click on the first selected feature
        // to remove it
        handle_left_click_on_first_selected_feature(*feature_node.parent());
    } else {
        // left click on unselected (grayed) center:
        // set the center as main feature
        auto [center, radius, normal] = feature->feature.circle();
        feature->parent               = feature->feature;
        feature->feature              = SurfaceFeature(center);
    }
}

void MeasureGizmo::handle_left_click_on_second_circle_center_feature(Scene::Node& feature_node)
{
    auto& feature = m_current_project->feature_cache.second_selected();
    if (feature->parent.has_value()) {
        // left click on selected center:
        // set back the circle as main feature
        feature->feature = *feature->parent;
        feature->parent.reset();
        // and handle the left click on the second selected feature
        // to remove it
        handle_left_click_on_second_selected_feature(*feature_node.parent());
    } else {
        // left click on unselected (grayed) center:
        // set the center as main feature
        auto [center, radius, normal] = feature->feature.circle();
        feature->parent               = feature->feature;
        feature->feature              = SurfaceFeature(center);
    }
}

void MeasureGizmo::handle_hover_first_selected_feature(Scene::Node& feature_node)
{
    auto& feature_cache = m_current_project->feature_cache;
    feature_cache.current.reset();
    feature_cache.hover_id = HoverID::FirstSelectedFeature;
}

void MeasureGizmo::handle_hover_second_selected_feature(Scene::Node& feature_node)
{
    auto& feature_cache = m_current_project->feature_cache;
    feature_cache.current.reset();
    feature_cache.hover_id = HoverID::SecondSelectedFeature;
}

void MeasureGizmo::handle_hover_first_circle_center_feature(Scene::Node& feature_node)
{
    auto& feature_cache = m_current_project->feature_cache;
    feature_cache.current.reset();
    feature_cache.hover_id = HoverID::FirstCircleCenterFeature;
}

void MeasureGizmo::handle_hover_second_circle_center_feature(Scene::Node& feature_node)
{
    auto& feature_cache = m_current_project->feature_cache;
    feature_cache.current.reset();
    feature_cache.hover_id = HoverID::SecondCircleCenterFeature;
}

void MeasureGizmo::add_feature_to_scene(
    const SurfaceFeature& feature,
    MeasureGizmoElementType type,
    const Domain::ElementRef& ref,
    const std::string& debug_name,
    const Domain::ColorRGBA& color,
    const Measuring& measuring,
    Scene::Node& parent_node
)
{
    auto& scene = m_scene_presenter.scene();
    Scene::NodeBuilder builder{scene};
    if (!debug_name.empty())
        builder.set_debug_name(debug_name);
    builder.set_tag(MeasureGizmoNodeTag{type});
    switch (feature.type()) {
    case SurfaceFeatureType::Point: {
        build_point_feature(builder, feature, color);
        break;
    }
    case SurfaceFeatureType::Edge: {
        build_edge_feature(builder, feature, color);
        break;
    }
    case SurfaceFeatureType::Circle: {
        build_circle_feature(builder, feature, color);
        break;
    }
    case SurfaceFeatureType::Plane: {
        build_plane_feature(builder, ref, feature, measuring, color);
        break;
    }
    case SurfaceFeatureType::Undefined:
        break;
    }

    scene.add_child(builder.build().release(), &parent_node);
}

void MeasureGizmo::remove_feature_from_scene(MeasureGizmoElementType type)
{
    m_scene_presenter.scene().remove_children(
        [type](const Scene::Node* node) {
            const MeasureGizmoNodeTag* tag = node->tag_of_type<MeasureGizmoNodeTag>();
            return tag != nullptr && tag->type == type;
        },
        m_current_project->features_node
    );
}

void MeasureGizmo::build_point_feature(
    Scene::NodeBuilder& builder,
    const SurfaceFeature& feature,
    const Domain::ColorRGBA& color
)
{
    Domain::Vec3d point = feature.point();

    std::string id = "feature_point";

    auto trimesh = m_current_project->triangle_mesh_manager.get_or_create(id, [this]() {
        Domain::TriangleMesh mesh = Biz::Algorithms::TriangleMesh::make_sphere(
            SPHERE_RADIUS,
            SPHERE_RESOLUTION_ANGLE
        );
        return std::make_unique<Scene::TriangleMesh>(std::move(mesh.its));
    });
    auto geom    = m_current_project->geometry_manager.get_or_create(id, [&]() {
        return Render::geometry_from_triangle_mesh(m_device, trimesh->triangles());
    });

    Render::Material material = Render::Material{}
                                    .set_shader(m_device.context().shader_manager().shader("gouraud_light"))
                                    .set_uniform("uniform_color", color);

    Domain::Transform3d xform = Domain::Transform3d::Identity();
    xform.translate(point);

    builder.set_mesh(geom, material, Scene::RenderLayerId(PlaterSceneLayer::GizmoHandles))
        .set_aabb(trimesh->aabb_mesh())
        .set_transform(xform);
}

void MeasureGizmo::build_edge_feature(
    Scene::NodeBuilder& builder,
    const SurfaceFeature& feature,
    const Domain::ColorRGBA& color
)
{
    auto [from, to] = feature.edge();
    double length   = (to - from).norm();

    std::string id = fmt::format("feature_edge_{}", length);

    auto trimesh = m_current_project->triangle_mesh_manager.get_or_create(id, [&]() {
        Domain::TriangleMesh mesh = Biz::Algorithms::TriangleMesh::make_cylinder(
            CYLINDER_RADIUS,
            length,
            CYLINDER_RESOLUTION_ANGLE
        );
        return std::make_unique<Scene::TriangleMesh>(std::move(mesh.its));
    });
    auto geom    = m_current_project->geometry_manager.get_or_create(id, [&]() {
        return Render::geometry_from_triangle_mesh(m_device, trimesh->triangles());
    });

    Render::Material material = Render::Material{}
                                    .set_shader(m_device.context().shader_manager().shader("gouraud_light"))
                                    .set_uniform("uniform_color", color);

    Domain::Vec3d unit_dir = (to - from).normalized();
    auto q                 = Eigen::Quaterniond{}.FromTwoVectors(Domain::Vec3d::UnitZ(), unit_dir);
    Domain::Transform3d xform        = Domain::Transform3d::Identity();
    xform.matrix().block<3, 3>(0, 0) = q.toRotationMatrix();
    xform.matrix().block<3, 1>(0, 3) = from;

    builder.set_mesh(geom, material, Scene::RenderLayerId(PlaterSceneLayer::GizmoHandles))
        .set_aabb(trimesh->aabb_mesh())
        .set_transform(xform);
}

void MeasureGizmo::build_circle_feature(
    Scene::NodeBuilder& builder,
    const SurfaceFeature& feature,
    const Domain::ColorRGBA& color
)
{
    auto [center, radius, normal] = feature.circle();

    std::string id = fmt::format("feature_circle_{}", radius);

    auto trimesh = m_current_project->triangle_mesh_manager.get_or_create(id, [&]() {
        Domain::TriangleMesh mesh = Biz::Algorithms::TriangleMesh::make_torus(
            radius,
            TORUS_RADIUS,
            TORUS_MAIN_RESOLUTION_ANGLE,
            TORUS_SECONDARY_RESOLUTION_ANGLE
        );
        return std::make_unique<Scene::TriangleMesh>(std::move(mesh.its));
    });
    auto geom    = m_current_project->geometry_manager.get_or_create(id, [&]() {
        return Render::geometry_from_triangle_mesh(m_device, trimesh->triangles());
    });

    Render::Material material = Render::Material{}
                                    .set_shader(m_device.context().shader_manager().shader("gouraud_light"))
                                    .set_uniform("uniform_color", color);

    auto q                    = Eigen::Quaterniond{}.FromTwoVectors(Domain::Vec3d::UnitZ(), normal);
    Domain::Transform3d xform = Domain::Transform3d::Identity();
    xform.matrix().block<3, 3>(0, 0) = q.toRotationMatrix();
    xform.matrix().block<3, 1>(0, 3) = center;

    builder.set_mesh(geom, material, Scene::RenderLayerId(PlaterSceneLayer::GizmoHandles))
        .set_aabb(trimesh->aabb_mesh())
        .set_transform(xform);
}

void MeasureGizmo::build_plane_feature(
    Scene::NodeBuilder& builder,
    Domain::ElementRef ref,
    const SurfaceFeature& feature,
    const Measuring& measuring,
    const Domain::ColorRGBA& color
)
{
    auto [plane_id, normal, point]          = feature.plane();
    const indexed_triangle_set& its         = measuring.its();
    const std::vector<int>& plane_triangles = measuring.plane_triangle_indices(plane_id);

    // small offset to avoid z-fighting
    Domain::Vec3f offset = 0.01f * normal.cast<float>();

    indexed_triangle_set mesh_its;
    mesh_its.vertices.reserve(3 * plane_triangles.size());
    mesh_its.indices.reserve(3 * plane_triangles.size());
    for (size_t i = 0; i < plane_triangles.size(); ++i) {
        const Domain::Index3& src_triangle = its.indices[plane_triangles[i]];
        size_t base                        = i * 3;
        mesh_its.vertices.emplace_back(offset + its.vertices[src_triangle[0]]);
        mesh_its.vertices.emplace_back(offset + its.vertices[src_triangle[1]]);
        mesh_its.vertices.emplace_back(offset + its.vertices[src_triangle[2]]);
        // to let the triangle be visible from both sides, we add it twice with opposite winding
        Domain::Index3 dst_triangle = {int(base + 0), int(base + 1), int(base + 2)};
        mesh_its.indices.emplace_back(dst_triangle);
        dst_triangle = { int(base + 0), int(base + 2), int(base + 1) };
        mesh_its.indices.emplace_back(dst_triangle);
    }

    std::string id = fmt::format("feature_plane_{}_{}_{}", ref.object_id, ref.instance_id, plane_id);

    auto trimesh = m_current_project->triangle_mesh_manager.get_or_create(id, [&]() {
        return std::make_unique<Scene::TriangleMesh>(std::move(mesh_its));
    });
    auto geom    = m_current_project->geometry_manager.get_or_create(id, [&]() {
        return Render::geometry_from_triangle_mesh(m_device, trimesh->triangles());
    });

    Render::Material material = Render::Material{}
                                    .set_shader(m_device.context().shader_manager().shader("gouraud_light"))
                                    .set_uniform("uniform_color", color);

    builder.set_mesh(geom, material, Scene::RenderLayerId(PlaterSceneLayer::GizmoHandles))
        .set_aabb(trimesh->aabb_mesh());
}

void MeasureGizmo::build_dimensioning_node()
{
    auto& scene = m_scene_presenter.scene();

    Scene::NodeBuilder builder{scene};
    builder.set_debug_name("dimensionings");
    builder.set_tag(MeasureGizmoNodeTag{MeasureGizmoElementType::Dimensionings});

    build_linear_dimensioning_node(builder);
    build_angular_dimensioning_node(builder);

    auto dimensioning_node                     = builder.build();
    m_current_project->dimensioning_nodes.main = dimensioning_node.get();
    scene.add_child(dimensioning_node.release(), m_current_project->main_node);

    m_current_project->dimensioning_nodes
        .linear = m_current_project->dimensioning_nodes.main->query_first([](const Scene::Node* n) {
        return n->tag_of_type<MeasureGizmoNodeTag>()->type
            == MeasureGizmoElementType::DimensioningLinear;
    });
    m_current_project->dimensioning_nodes
        .angular = m_current_project->dimensioning_nodes.main->query_first([](const Scene::Node* n) {
        return n->tag_of_type<MeasureGizmoNodeTag>()->type
            == MeasureGizmoElementType::DimensioningAngular;
    });

    DEBUG_ASSERT(m_current_project->dimensioning_nodes.linear != nullptr);
    DEBUG_ASSERT(m_current_project->dimensioning_nodes.angular != nullptr);

    m_current_project->dimensioning_nodes.main->set_enabled(false);
}

void MeasureGizmo::build_linear_dimensioning_node(Scene::NodeBuilder& builder)
{
    indexed_triangle_set triangle_mesh_its;
    triangle_mesh_its.vertices = {
        {0.0f, 0.0f, 0.0f},
        {0.5f * TRIANGLE_BASE, TRIANGLE_HEIGHT, 0.0f},
        {-0.5f * TRIANGLE_BASE, TRIANGLE_HEIGHT, 0.0f}
    };
    triangle_mesh_its.indices = {{0, 1, 2}};

    std::string id = "dimensioning_linear_arrow";

    auto arrow_trimesh = m_current_project->triangle_mesh_manager.get_or_create(id, [&]() {
        return std::make_unique<Scene::TriangleMesh>(std::move(triangle_mesh_its));
    });
    auto arrow_geom    = m_current_project->geometry_manager.get_or_create(id, [&]() {
        return Render::geometry_from_triangle_mesh(m_device, arrow_trimesh->triangles());
    });

    static constexpr double SCALE_FACTOR = 0.00375;

    builder.child([&](Scene::NodeBuilder& bldr) {
        bldr.set_tag(MeasureGizmoNodeTag{MeasureGizmoElementType::DimensioningLinear})
            .set_debug_name("linear");

        // arrow children
        bldr.child([&](Scene::NodeBuilder& inner_bldr) {
            inner_bldr
                .set_tag(MeasureGizmoNodeTag{MeasureGizmoElementType::DimensioningLinearArrow1})
                .set_debug_name("arrow 1")
                .set_mesh(arrow_geom, dimensioning_material(), Scene::RenderLayerId(PlaterSceneLayer::AlwaysOnTop))
                .set_screen_space_sized_modifier(SCALE_FACTOR);
        });

        bldr.child([&](Scene::NodeBuilder& inner_bldr) {
            inner_bldr
                .set_tag(MeasureGizmoNodeTag{MeasureGizmoElementType::DimensioningLinearArrow2})
                .set_debug_name("arrow 2")
                .set_mesh(arrow_geom, dimensioning_material(), Scene::RenderLayerId(PlaterSceneLayer::AlwaysOnTop))
                .set_screen_space_sized_modifier(SCALE_FACTOR);
        });

        // stem child
        bldr.child([&](Scene::NodeBuilder& inner_bldr) {
            inner_bldr.set_tag(MeasureGizmoNodeTag{MeasureGizmoElementType::DimensioningLinearStem})
                .set_debug_name("stem");
        });
    });
}

void MeasureGizmo::build_angular_dimensioning_node(Scene::NodeBuilder& builder)
{
    builder.child([&](Scene::NodeBuilder& bldr) {
        bldr.set_tag(MeasureGizmoNodeTag{MeasureGizmoElementType::DimensioningAngular})
            .set_debug_name("angular");

        // arc child
        bldr.child([&](Scene::NodeBuilder& inner_bldr) {
            inner_bldr.set_tag(MeasureGizmoNodeTag{MeasureGizmoElementType::DimensioningAngularArc})
                .set_debug_name("arc");
        });
    });
}

Render::Material MeasureGizmo::dimensioning_material()
{
    Render::Material ret = Render::Material{}
                               .set_shader(m_device.context().shader_manager().shader("flat"))
                               .set_uniform("uniform_color", Domain::ColorRGBA::WHITE());
    return ret;
}

} // namespace Slic3r::App::Plater
