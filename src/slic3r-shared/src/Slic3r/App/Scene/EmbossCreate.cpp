
#include "Slic3r/App/Scene/EmbossCreate.hpp"
#include "Slic3r/App/Scene/SceneNodeTag.hpp"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include <Slic3r/Biz/Algorithms/Geometry/ConvexHull.hpp> // calc 2d convex hull for guess volume position

namespace Slic3r::App::Scene {
namespace {
std::optional<Domain::Vec2d> get_z_zero_coor(const Ray& pick_ray) {
    double d_z = pick_ray.direction.z();
    if (fabs(d_z) - 1e-4 <= 0.)
        return std::nullopt; // almost parallel to Z axis solve as no bed under mouse

    // prerequisity: bed is alligned -> parallel with Z plane AND Z = 0
    Domain::Vec3d z0 = pick_ray.point_at(-pick_ray.origin.z() / d_z);
    return Domain::Vec2d(z0.x(), z0.y());
}

Domain::Transform3d get_volume_tr(const Domain::ModelInstance& selected_instance, const Domain::Project& project, const Ray& pick_ray, const ConstNodePickResults& picks) {
    for (const ConstNodePickResult& pick : picks) {
        if (pick.node->has_tag_of_type<SceneNodeTag>()) {
            const auto* tag = pick.node->tag_of_type<SceneNodeTag>();
            Domain::SelectionId instance_id = selected_instance.id().id;
            Domain::SelectionId object_id = selected_instance.get_object()->id().id;
            if (tag->instance_id != instance_id ||
                tag->object_id != object_id)
                continue; // not selected instance

            const Domain::ModelVolume* volume = project.find_volume_by_id(tag->object_id, tag->volume_id);
            if (volume == nullptr)
                continue; // no volume under mouse

            // TODO: What to do with Negative volume
            if (volume->type() != Domain::ModelVolumeType::MODEL_PART)
                continue; // skip modifiers + SupportBlock/Enforce

            Domain::Vec3d pick_point = pick_ray.point_at(pick.cast.distance);
            Domain::Vec3d pick_normal = pick.cast.normal;
            // normal could be from scaled object it needs normalize
            pick_normal.normalize();

            Domain::Transform3d surface_trmat = Biz::Emboss::create_transformation_onto_surface(pick_point, pick_normal, Biz::Emboss::UP_LIMIT);
            return selected_instance.get_matrix().inverse() * surface_trmat;
        }
    }
    // create volume near selected instance
    const Domain::ModelObject* object = selected_instance.get_object();
    Domain::BoundingBox3d bb;
    for (const auto v : object->volumes) {
        bb = Biz::Algorithms::BoundingBox::merge(bb,
            Biz::Algorithms::BoundingBox::transformed(
                v->get_convex_hull().bounding_box(), v->get_matrix())
        );
    }
    bb = Biz::Algorithms::BoundingBox::transformed(bb, selected_instance.get_matrix());
    Domain::Vec3d point((bb.min.x() + bb.max.x()) / 2., bb.min.y(), 0); // x -> object middle, y -> object min
    Domain::Transform3d surface_trmat = Biz::Emboss::create_transformation_onto_surface(point, Domain::Vec3d::UnitY(), Biz::Emboss::UP_LIMIT);
    return selected_instance.get_matrix().inverse() * surface_trmat;
}

Domain::Point get_screen_center(const Domain::ModelVolume& volume, const Domain::ModelInstance& instance, const Camera& camera) {
    const Domain::Transform3d to_world = instance.get_matrix() * volume.get_matrix();
    const Domain::TriangleMesh& hull = volume.get_convex_hull();
    Domain::Points points;
    points.reserve(hull.its.vertices.size());
    for (const Domain::Vec3f& v : hull.its.vertices) {
        Domain::Vec3d v_world = to_world * v.cast<double>();
        Domain::Vec2d coor = camera.project_to_screen_space(v_world);
        // projection to screen coor space has reverse Y against mouse coordinate
        coor.y() = camera.viewport().height - coor.y();
        points.push_back(coor.cast<Domain::coord_t>());
    }

    Domain::Polygon hull_2d = Biz::Algorithms::Geometry::convex_hull(points);
    return hull_2d.centroid();
}
} // namespace

TrafoGuess guess_volume_transformation(const Domain::ElementRefs& selection, const Domain::Project& project, const Scene& scene) {
    const Camera& camera = scene.camera();
    const Render::Rect& v = camera.viewport();
    Domain::Vec2f logic_center{
        v.x + v.width / 2.f,
        v.y + v.height / 2.f
    };

    if (selection.empty()) {
        Ray ray = camera.ray_at(logic_center.x(), logic_center.y());
        auto bed_coor = get_z_zero_coor(ray);
        if (bed_coor.has_value()) {
            return TrafoGuess{
                .instance = nullptr,
                .bed_coor = *bed_coor
            }; // no selection, return identity
        } else {
            // look out of the bed and try add volume without selection
            return {}; // position 0, 0
        }
    }

    const Domain::ElementRef& first_el = selection.front();
    const Domain::ModelInstance* instance_ptr = 
        project.find_instance_by_id(first_el.object_id, first_el.instance_id);
    float squared_norm = std::numeric_limits<float>::max();
    std::optional<Domain::Point> screen_coor;
    auto eval_center_closest = [&camera, &squared_norm, &screen_coor, &logic_center, &instance_ptr]
    (const Domain::ModelVolume& volume, const Domain::ModelInstance& instance) {
        Domain::Point center = get_screen_center(volume, instance, camera);
        if (float norm = (center.cast<float>() - logic_center).squaredNorm();
            squared_norm > norm) { // is cloeser to screen center
            squared_norm = norm;
            screen_coor = center;
            instance_ptr = &instance;
        }
        };

    // find closest selected volume to screen center and use it for transformation guess
    for (const Domain::ElementRef& el : selection) {
        const Domain::ModelInstance* instance = 
            project.find_instance_by_id(el.object_id, el.instance_id);
        if (instance == nullptr) {
            continue; // e.g. wipe tower
        }
        if (el.volume_id == 0) { // Whole object selected
            const Domain::ModelObject* obj = project.find_object_by_id(el.object_id);
            for (const Domain::ModelVolume* vol : obj->volumes) {
                eval_center_closest(*vol, *instance);
            }
            continue;
        }
        const Domain::ModelVolume* vol = project.find_volume_by_id(el.object_id, el.volume_id);
        eval_center_closest(*vol, *instance);
    }

    const Domain::Vec2f p = screen_coor.has_value() ? screen_coor->cast<float>() : logic_center;
    ConstNodePickResults pick_results;
    Ray pick_ray;
    scene.pick_at(p.x(), p.y(), pick_results, &pick_ray);
    return TrafoGuess{
        .transformation = get_volume_tr(*instance_ptr, project, pick_ray, pick_results),
        .instance = instance_ptr
    };
}
} // namespace Slic3r::App::Scene
