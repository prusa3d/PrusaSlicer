#include "Slic3r/Biz/Algorithms/SlaPoints.hpp"

#include "Slic3r/Biz/Algorithms/AABBMesh.hpp"

#include <algorithm>
#include <limits>
#include <vector>

namespace Slic3r::Biz::Algorithms::SlaPoints {

using Domain::ModelObject;
using Domain::ModelVolume;
using Domain::Transform3d;
using Domain::SLA::DrainHoles;
using Domain::SLA::SupportPoints;

namespace {

size_t nearest(const std::vector<AABBMesh>& meshes, const Domain::Vec3f& position)
{
    size_t result              = 0;
    double nearest_sq_distance = std::numeric_limits<double>::max();
    for (size_t i = 0; i < meshes.size(); ++i) {
        const double sq_distance = meshes[i].squared_distance(position.cast<double>());
        if (sq_distance < nearest_sq_distance) {
            result              = i;
            nearest_sq_distance = sq_distance;
        }
    }
    return result;
}

template <class Items>
std::vector<Items> distribute(const Items& items, const std::vector<AABBMesh>& meshes)
{
    std::vector<Items> result(meshes.size());
    for (const auto& item : items) {
        result[nearest(meshes, item.pos)].push_back(item);
    }
    return result;
}

template <class Items>
void reproject(Items& items, const AABBMesh& mesh)
{
    for (auto& item : items) {
        int          face;
        Domain::Vec3d closest;
        mesh.squared_distance(item.pos.template cast<double>(), face, closest);
        item.pos = closest.cast<float>();
    }
}

template <class Items, class Member>
Items gather_in_object(const ModelObject& object, Member member)
{
    Items result;
    for (const ModelVolume* volume : object.volumes) {
        if (!is_sla_geometry(*volume)) {
            continue;
        }
        Items items = volume->*member;
        Domain::SLA::transform(items, volume->get_matrix());
        result.insert(result.end(), items.begin(), items.end());
    }
    return result;
}

template <class Items, class Member>
void set_in_object(ModelObject& object, const Items& items, Member member)
{
    std::vector<ModelVolume*> volumes;
    for (ModelVolume* volume : object.volumes) {
        if (is_sla_geometry(*volume)) {
            (volume->*member).clear();
            volumes.push_back(volume);
        }
    }
    if (volumes.empty() || items.empty()) {
        return;
    }

    // The distance has to be measured in object coordinates, a volume may be scaled unevenly.
    // AABBMesh keeps a pointer to its triangle set, so all of them are kept alive here.
    std::vector<indexed_triangle_set> transformed(volumes.size());
    std::vector<AABBMesh>             meshes;
    meshes.reserve(volumes.size());
    for (size_t i = 0; i < volumes.size(); ++i) {
        transformed[i]           = volumes[i]->mesh().its;
        const Transform3d& trafo = volumes[i]->get_matrix();
        for (Domain::Vec3f& vertex : transformed[i].vertices) {
            vertex = (trafo * vertex.cast<double>()).cast<float>();
        }
        meshes.emplace_back(transformed[i]);
    }

    std::vector<Items> per_volume(volumes.size());
    for (const auto& item : items) {
        per_volume[nearest(meshes, item.pos)].push_back(item);
    }

    for (size_t i = 0; i < volumes.size(); ++i) {
        Domain::SLA::transform(per_volume[i], volumes[i]->get_matrix().inverse());
        volumes[i]->*member = std::move(per_volume[i]);
    }
}

} // namespace

bool is_sla_geometry(const ModelVolume& volume)
{
    return volume.is_model_part() || volume.is_negative_volume();
}

SupportPoints support_points_in_object(const ModelObject& object)
{
    return gather_in_object<SupportPoints>(object, &ModelVolume::sla_support_points);
}

DrainHoles drain_holes_in_object(const ModelObject& object)
{
    return gather_in_object<DrainHoles>(object, &ModelVolume::sla_drain_holes);
}

void set_support_points_in_object(ModelObject& object, const SupportPoints& points)
{
    set_in_object(object, points, &ModelVolume::sla_support_points);
}

void set_drain_holes_in_object(ModelObject& object, const DrainHoles& holes)
{
    set_in_object(object, holes, &ModelVolume::sla_drain_holes);
}

bool has_support_points(const ModelObject& object)
{
    for (const ModelVolume* volume : object.volumes) {
        if (is_sla_geometry(*volume) && !volume->sla_support_points.empty()) {
            return true;
        }
    }
    return false;
}

bool has_drain_holes(const ModelObject& object)
{
    for (const ModelVolume* volume : object.volumes) {
        if (is_sla_geometry(*volume) && !volume->sla_drain_holes.empty()) {
            return true;
        }
    }
    return false;
}

void clear_support_points(ModelObject& object)
{
    for (ModelVolume* volume : object.volumes) {
        volume->sla_support_points.clear();
    }
}

void clear_drain_holes(ModelObject& object)
{
    for (ModelVolume* volume : object.volumes) {
        volume->sla_drain_holes.clear();
    }
}

void distribute_to_parts(
    const SupportPoints& points,
    const DrainHoles& holes,
    const std::vector<ModelVolume*>& parts
)
{
    if (parts.empty()) {
        return;
    }
    std::vector<AABBMesh> meshes;
    meshes.reserve(parts.size());
    for (const ModelVolume* part : parts) {
        meshes.emplace_back(part->mesh());
    }
    std::vector<SupportPoints> part_points = distribute(points, meshes);
    std::vector<DrainHoles>    part_holes  = distribute(holes, meshes);
    for (size_t i = 0; i < parts.size(); ++i) {
        parts[i]->sla_support_points = std::move(part_points[i]);
        parts[i]->sla_drain_holes    = std::move(part_holes[i]);
    }
}

void reproject_on_new_mesh(ModelVolume& volume, const Transform3d& old_matrix)
{
    if (volume.sla_support_points.empty() && volume.sla_drain_holes.empty()) {
        return;
    }
    const Transform3d to_new = volume.get_matrix().inverse() * old_matrix;
    Domain::SLA::transform(volume.sla_support_points, to_new);
    Domain::SLA::transform(volume.sla_drain_holes, to_new);
    const AABBMesh mesh{volume.mesh()};
    reproject(volume.sla_support_points, mesh);
    reproject(volume.sla_drain_holes, mesh);
}

void drop_user_status_without_points(ModelObject& object)
{
    if (object.sla_points_status == Domain::SLA::PointsStatus::UserModified && !has_support_points(object)) {
        object.sla_points_status = Domain::SLA::PointsStatus::AutoGenerated;
    }
}

Domain::SLA::PointsStatus merged_points_status(const std::vector<Domain::SLA::PointsStatus>& statuses)
{
    using Domain::SLA::PointsStatus;
    if (statuses.empty()) {
        return PointsStatus::NoPoints;
    }
    if (std::ranges::all_of(statuses, [](PointsStatus status) { return status == PointsStatus::UserModified; })) {
        return PointsStatus::UserModified;
    }
    if (std::ranges::all_of(statuses, [](PointsStatus status) { return status == PointsStatus::NoPoints; })) {
        return PointsStatus::NoPoints;
    }
    return PointsStatus::AutoGenerated;
}

} // namespace Slic3r::Biz::Algorithms::SlaPoints
