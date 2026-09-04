#include "Slic3r/Biz/Emboss/SurfaceDrag.hpp"

#include "Slic3r/Biz/Emboss/Emboss.hpp"
#include "Slic3r/App/Scene/Node.hpp"
#include "Slic3r/App/Scene/NodeVisitor.hpp"
#include "Slic3r/App/Scene/Ray.hpp"
#include "Slic3r/App/Scene/SceneNodeTag.hpp"

namespace {
using namespace Slic3r;
struct Drag {
    // Project interactor transformation cache;
    Biz::Scene::TransformMemento memento;

    // volume world transformation before draggig
    Domain::Transform3d to_world;
    Domain::Transform3d instance;
    Domain::Transform3d instance_inv;
    Domain::Transform3d volume_inv;

    // Position of the cross hair
    ImVec2 cross_hair_pos; // []

    // screen coordinate offset volume center from mouse at drag start
    // fixed during dragging
    Domain::Vec2d logical_offset;

    Domain::SquareMatrix4d last_tr = Domain::SquareMatrix4d::Identity(); // for rendring

    // True on hit object surface otherwise false. (cross hair color)
    bool valid = true;
};

void draw_cross_hair(
    const ImVec2& position,
    ImU32 color = ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, .75f)),
    float radius = 12.f,
    int num_segments = 0,
    float thickness = 3.f)
{
    auto draw_list = ImGui::GetForegroundDrawList();
    draw_list->AddCircle(position, radius, color, num_segments, thickness);
    auto dirs = { ImVec2{0, 1}, ImVec2{1, 0}, ImVec2{0, -1}, ImVec2{-1, 0} };
    for (const ImVec2& dir : dirs) {
        ImVec2 start(position.x + dir.x * 0.5 * radius, position.y + dir.y * 0.5 * radius);
        ImVec2 end(position.x + dir.x * 1.5 * radius, position.y + dir.y * 1.5 * radius);
        draw_list->AddLine(start, end, color, thickness);
    }
}

void draw_cross_hair(const Drag& drag) {
    ImU32 color = ImGui::GetColorU32(drag.valid ?
        ImVec4(1.f, 1.f, 1.f, .65f) : // transparent white (valid)
        ImVec4(1.f, .3f, .3f, .65f) // transparent redish (invalid)
    );
    draw_cross_hair(drag.cross_hair_pos, color);
}

Domain::Vec2d logical_to_physical(const App::Render::ScreenInfo& screen_info, const Domain::Vec2d& logical) {
    return Domain::Vec2d{
        screen_info.logical_to_physical(logical.x()),
        screen_info.logical_to_physical(logical.y()),
    };
}

Domain::Vec2d physical_to_logical(const App::Render::ScreenInfo& screen_info, const Domain::Vec2d& physical) {
    return Domain::Vec2d{
        screen_info.physical_to_logical(physical.x()),
        screen_info.physical_to_logical(physical.y()),
    };
}

Domain::Vec2d get_mouse_coor(const App::Scene::GizmoEventContext& ctx) {
    return Domain::Vec2d{
        ctx.mouse_event().x(),
        ctx.mouse_event().y()
    }; // logical coordinate
}

ImVec2 to_imvec2(const Domain::Vec2d& v) {
    return ImVec2(v.x(), v.y());
}
} // namespace

namespace Slic3r::Biz::Emboss{

struct SurfaceDrag::Drag : public ::Drag {}; // pimpl

SurfaceDrag::SurfaceDrag(
    App::Plater::PlaterScenePresenter& scene_presenter,
    Biz::ProjectInteractor& project_interactor):
    m_scene_presenter(scene_presenter),
    m_project_interactor(project_interactor),
    m_drag(nullptr)
{}
SurfaceDrag::~SurfaceDrag() = default;

bool SurfaceDrag::on_drag_start(const App::Scene::GizmoEventContext& ctx, const std::optional<float>& distance)
{
    const Domain::ElementRefs& elements = m_project_interactor.scene_interactor().object_selection().elements;
    if (elements.empty())
        return false;

    const Domain::ElementRef& element = elements.front();

    for (const App::Scene::NodePickResult& pick : ctx.pick_results()) {
        if (!pick.node->has_tag_of_type<App::Scene::SceneNodeTag>())
            continue; // ignore staff(node) infront of text volume

        auto* tag = pick.node->tag_of_type<App::Scene::SceneNodeTag>();
        if (!tag->matches_element(element))
            return false; // Only seleceted Volume could be dragged over surface

        const Domain::Project& project = m_project_interactor.selected_project();
        const Domain::ModelVolume* volume_ptr =
            project.find_volume_by_id(tag->object_id, tag->volume_id);
        ASSERT(volume_ptr != nullptr);
        const Domain::ModelVolume& volume = *volume_ptr;
        if (volume.get_object()->volumes.size() == 1)
            return false; // Object is moved by default drag

        // calc mouse offset to volume center
        const Domain::ModelInstance* instance_ptr =
            project.find_instance_by_id(tag->object_id, tag->instance_id);
        ASSERT(instance_ptr != nullptr);

        m_drag = std::make_unique<Drag>();
        m_drag->to_world = instance_ptr->get_matrix() * volume.get_matrix();
        m_drag->instance = instance_ptr->get_matrix();
        m_drag->instance_inv = instance_ptr->get_matrix().inverse();
        m_drag->volume_inv = volume.get_matrix().inverse();

        Domain::Transform3d dist_tr{ Eigen::Translation<double, 3>(Domain::Vec3d(0, 0, -distance.value_or(0.))) };
        Domain::Vec3d surface_point = distance.has_value() ?
            (Domain::Vec3d)(instance_ptr->get_matrix() * (volume.get_matrix() * dist_tr)).translation() :
            (Domain::Vec3d)m_drag->to_world.translation();

        // Settings for cross hair
        const App::Scene::Camera& camera = m_scene_presenter.scene().camera();        
        Domain::Vec2d scene_pos = camera.project_to_screen_space(surface_point); // physical position
        scene_pos.y() = camera.viewport().height - scene_pos.y();
        m_drag->logical_offset = physical_to_logical(ctx.screen_info(), scene_pos) - get_mouse_coor(ctx);
        m_drag->cross_hair_pos = to_imvec2(get_mouse_coor(ctx) + m_drag->logical_offset);
        return true;
    }
    return false;
}

bool SurfaceDrag::on_dragging(const App::Scene::GizmoEventContext& ctx,
    const std::optional<float>& angle,
    const std::optional<float>& distance,
    const std::optional<double>& up_limit
)
{
    if (m_drag == nullptr)
        return false;

    Biz::Scene::SceneInteractor& scene_interactor = m_project_interactor.scene_interactor();
    if (scene_interactor.object_selection().elements.empty()) { 
        // without selected element
        m_drag = nullptr;
        return false;
    }
    const Domain::ElementRef& element = scene_interactor.object_selection().elements.front();
    const Domain::Project& project = m_project_interactor.selected_project();
    const Domain::ModelVolume* embossed_volume_ptr = project.find_volume_by_id(element.object_id, element.volume_id);
    if (embossed_volume_ptr == nullptr) {
        // cant find last loaded volume -> end dragging
        m_drag = nullptr;
        return false;
    }

    Domain::Vec2d mouse_coor = get_mouse_coor(ctx);
    Domain::Vec2d pick_logical = mouse_coor + m_drag->logical_offset;
    m_drag->cross_hair_pos = to_imvec2(pick_logical);
    Domain::Vec2d pick = logical_to_physical(ctx.screen_info(), pick_logical);
    App::Scene::NodePickResults pick_results;
    App::Scene::Ray pick_ray;
    // ignor return value
    m_scene_presenter.scene().pick_at(pick.x(), pick.y(), pick_results, &pick_ray);
        
    for (const App::Scene::NodePickResult& pick : pick_results) {
        if (!pick.node->has_tag_of_type<App::Scene::SceneNodeTag>())
            continue; // only node tag
        auto* tag = pick.node->tag_of_type<App::Scene::SceneNodeTag>();
        if (tag->volume_id == element.volume_id)
            continue; // skip itself

        if (tag->object_id != element.object_id)
            continue; // another object

        const Domain::ModelVolume* volume_ptr =
            project.find_volume_by_id(tag->object_id, tag->volume_id);
        if (volume_ptr == nullptr)
            continue; // weird
        const Domain::ModelVolume& volume = *volume_ptr;
        if (volume.type() != Domain::ModelVolumeType::MODEL_PART)
            continue; // allowe only the model part

        Domain::Vec3d n = pick.cast.normal;
        Domain::Vec3d p = pick_ray.point_at(pick.cast.distance);

        Domain::Transform3d new_volume_tr = get_volume_transformation(m_drag->to_world, n, p, 
            m_drag->instance_inv, angle, distance, up_limit);
        Domain::Transform3d volume_relative = 
            m_drag->instance * new_volume_tr * m_drag->volume_inv * m_drag->instance_inv;
        Domain::SquareMatrix4d tr = volume_relative.matrix();
        m_drag->last_tr = tr;
        scene_interactor.transform_selection(tr, m_drag->memento);
        m_drag->valid = true;
        return true;
    }

    // pick node not found
    scene_interactor.transform_selection(m_drag->last_tr, m_drag->memento);
    m_drag->valid = false;
    return true;
}

void SurfaceDrag::on_drag_finish(){
    m_project_interactor.scene_interactor()
        .finalize_transform_selection(m_drag->memento, false);
    m_drag = nullptr;
}
void SurfaceDrag::on_drag_cancel(){
    m_project_interactor.scene_interactor()
        .finalize_transform_selection(m_drag->memento, true);
    m_drag = nullptr;
}

bool SurfaceDrag::is_dragging() { return m_drag && m_drag->valid; }
void SurfaceDrag::imgui_draw() { if (m_drag != nullptr) draw_cross_hair(*m_drag); }

Domain::Transform3d get_volume_transformation(
    Domain::Transform3d world, // from volume
    const Domain::Vec3d& world_dir, // wanted new direction
    const Domain::Vec3d& world_position, // wanted new position
    // Invers transformation of text volume instance
    // Help convert world transformation to instance space 
    const Domain::Transform3d& instance_inv,
    // initial rotation in Z axis
    const std::optional<float>& current_angle,
    const std::optional<float>& current_distance,
    const std::optional<double>& up_limit)
{
    auto world_linear = world.linear();
    // Calculate offset: transformation to wanted position
    {
        // Reset skew of the text Z axis:
        // Project the old Z axis into a new Z axis, which is perpendicular to the old XY plane.
        Domain::Vec3d old_z = world_linear.col(2);
        Domain::Vec3d new_z = world_linear.col(0).cross(world_linear.col(1));
        world_linear.col(2) = new_z * (old_z.dot(new_z) / new_z.squaredNorm());
    }

    Domain::Vec3d       text_z_world = world_linear.col(2); // world_linear * Vec3d::UnitZ()
    auto        z_rotation = Eigen::Quaternion<double, Eigen::DontAlign>::FromTwoVectors(text_z_world, world_dir);
    Domain::Transform3d world_new = z_rotation * world;
    auto        world_new_linear = world_new.linear();

    // Fix direction of up vector to zero initial rotation 
    if (up_limit.has_value()) {
        Domain::Vec3d z_world = world_new_linear.col(2);
        z_world.normalize();
        Domain::Vec3d wanted_up = Biz::Emboss::suggest_up(z_world, *up_limit);

        Domain::Vec3d y_world = world_new_linear.col(1);
        auto  y_rotation = Eigen::Quaternion<double, Eigen::DontAlign>::FromTwoVectors(y_world, wanted_up);

        world_new = y_rotation * world_new;
        world_new_linear = world_new.linear();
    }

    // Edit position from right
    Domain::Transform3d volume_new{ Eigen::Translation<double, 3>(instance_inv * world_position) };
    volume_new.linear() = instance_inv.linear() * world_new_linear;

    // Check that transformation matrix is valid transformation
    assert(volume_new.matrix()(0, 0) == volume_new.matrix()(0, 0)); // Check valid transformation not a NAN
    if (volume_new.matrix()(0, 0) != volume_new.matrix()(0, 0))
        return Domain::Transform3d::Identity();

    // Check that scale in world did not changed
    //assert(!calc_scale(world_linear, world_new_linear, Domain::Vec3d::UnitY()).has_value());
    //assert(!calc_scale(world_linear, world_new_linear, Domain::Vec3d::UnitZ()).has_value());

    // apply move in Z direction and rotation by up vector
    if (up_limit.has_value()) {
        Biz::Emboss::apply_transformation(current_angle, current_distance, volume_new);
    }
    else {
        // angle is allowed to change
        Biz::Emboss::apply_transformation({}, current_distance, volume_new);
    }
    return volume_new;
}

Domain::Transform3d world_tr(const Domain::Project& project, const Domain::ElementRef& ref) {
    const Domain::ModelInstance& instance = *project.find_instance_by_id(ref.object_id, ref.instance_id);
    const Domain::ModelVolume& volume = (ref.volume_id != 0) ?
        *project.find_volume_by_id(ref.object_id, ref.volume_id) :
        *project.find_object_by_id(ref.object_id)->volumes.front();
    return instance.get_matrix() * volume.get_matrix();
}

std::optional<float> calc_rotation(const Domain::Project& project, const Domain::ElementRef& ref) 
{
    Domain::Transform3d to_world = world_tr(project, ref);
    return Biz::Emboss::calc_up(to_world, Biz::Emboss::UP_LIMIT);
}

tl::expected<float, DistanceIssue> calc_distance(const Domain::Project& project, const Domain::ElementRef& ref,
    App::Scene::Node& root)
{
    Domain::Transform3d world = world_tr(project, ref);
    Domain::Vec3d pos_world = world.translation();
    // Emboss direction in world coordinate(negative Z axis)
    Domain::Vec3d dir_world = world.linear() * Domain::Vec3d::UnitZ() * -1.;
    dir_world.normalize();

    auto ray_cast = [&ref, &root](const App::Scene::Ray& ray) {
        return App::Scene::visit_conditional_transform<App::Scene::RaycastResult>(root,
            [&ray, &ref](App::Scene::Node& n, App::Scene::RaycastResult& t) {
                auto* tag = n.tag_of_type<App::Scene::SceneNodeTag>();
                if (tag == nullptr || // Not a scene node tag (object)
                    tag->volume_type != Domain::ModelVolumeType::MODEL_PART ||
                    tag->object_id != ref.object_id || // different object
                    tag->instance_id != ref.instance_id || // different instance
                    tag->volume_id == ref.volume_id || // skip itself
                    !n.has_raycast_component()) // only for sure
                    return false;
                return n.raycast_component()->raycast(n.world_transform().matrix(), ray, t);
            });
        };

    double overlap = 1.;
    App::Scene::Ray ray_positive{ .origin = pos_world - dir_world * overlap, .direction = dir_world };
    App::Scene::Ray ray_negative{ .origin = pos_world + dir_world * overlap, .direction = -dir_world };
    auto r_positive = ray_cast(ray_positive);
    auto r_negative = ray_cast(ray_negative);
    for (auto& r : r_positive) r.second.distance = r.second.distance - overlap;
    for (auto& r : r_negative) r.second.distance = overlap - r.second.distance;
    r_positive.insert(r_positive.end(), r_negative.begin(), r_negative.end());
    if (r_positive.empty())
        return tl::unexpected(DistanceIssue::NoSurfacePoint); // no intersection

    std::sort(r_positive.begin(), r_positive.end(), [](const auto& a, const auto& b) {
        return fabs(a.second.distance) < fabs(b.second.distance);  });

    const auto& closest = r_positive.front();
    double distance = closest.second.distance;
    if (Domain::is_approx(distance, 0., 1e-4))
        return tl::unexpected(DistanceIssue::ApproxZero); // numerical discrepancy -> lay on surface

    return distance;
}

void transform_selection_relative(const Domain::Transform3d& tr, Biz::ProjectInteractor& project_interactor)
{
    // get current transformation of the volume
    const Domain::Project& project = project_interactor.selected_project();
    Biz::Scene::SceneInteractor& scene_interactor = project_interactor.scene_interactor();
    ASSERT(!scene_interactor.object_selection().elements.empty());
    const Domain::ElementRef& el = scene_interactor.object_selection().elements.front();
    Domain::Transform3d instance_tr = project.find_instance_by_id(el.object_id, el.instance_id)->get_matrix();
    const Domain::ModelVolume& volume = (el.volume_id != 0) ?
        *project.find_volume_by_id(el.object_id, el.volume_id) :
        *project.find_object_by_id(el.object_id)->volumes.front();
    Domain::Transform3d volume_tr = volume.get_matrix();
    Domain::Transform3d world_relative = instance_tr * volume_tr * tr * volume_tr.inverse() * instance_tr.inverse();
    scene_interactor.transform_selection(world_relative.matrix());
}

} // Slic3r::Biz::Emboss