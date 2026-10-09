#include "Slic3r/Biz/Algorithms/SlaPoints.hpp"

#include "Slic3r/Biz/Algorithms/AABBMesh.hpp"

#include <limits>
#include <vector>

namespace Slic3r::Biz::Algorithms::SlaPoints {

using Domain::ModelObject;
using Domain::ModelVolume;
using Domain::Transform3d;
using Domain::SLA::DrainHoles;
using Domain::SLA::SupportPoints;

namespace {

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
        size_t nearest     = 0;
        double nearest_sq_distance = std::numeric_limits<double>::max();
        for (size_t i = 0; i < meshes.size(); ++i) {
            const double sq_distance = meshes[i].squared_distance(item.pos.template cast<double>());
            if (sq_distance < nearest_sq_distance) {
                nearest             = i;
                nearest_sq_distance = sq_distance;
            }
        }
        per_volume[nearest].push_back(item);
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

} // namespace Slic3r::Biz::Algorithms::SlaPoints
