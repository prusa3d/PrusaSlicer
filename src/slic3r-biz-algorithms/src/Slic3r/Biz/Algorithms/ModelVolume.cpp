#include "Slic3r/Biz/Algorithms/ModelVolume.hpp"

#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Biz/Algorithms/FacetsAnnotation.hpp"
#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Domain/TriangleMesh.hpp"

#include <boost/filesystem/path.hpp>

using namespace Slic3r::Biz::Algorithms;

namespace Slic3r::Biz::Algorithms::ModelVolume {

Domain::ModelVolume *construct_ptr(Domain::ModelObject* object, const Domain::TriangleMesh& mesh, Domain::ModelVolumeType type)
{
    Domain::ModelVolume* volume = new Domain::ModelVolume(object, mesh, type);
    if (volume->m_mesh->facets_count() > 1) {
        ModelVolume::calculate_convex_hull(*volume);
    }

    return volume;
}

Domain::ModelVolume *construct_ptr(Domain::ModelObject* object, Domain::TriangleMesh&& mesh, Domain::ModelVolumeType type)
{
    Domain::ModelVolume* volume = new Domain::ModelVolume(object, std::move(mesh), type);
    if (volume->m_mesh->facets_count() > 1) {
        ModelVolume::calculate_convex_hull(*volume);
    }

    return volume;
}

Domain::ModelVolume *construct_ptr(Domain::ModelObject* object, const Domain::ModelVolume& other, Domain::TriangleMesh&& mesh)
{
    Domain::ModelVolume* volume = new Domain::ModelVolume(object, other, std::move(mesh));
    if (volume->m_mesh->facets_count() > 1) {
        ModelVolume::calculate_convex_hull(*volume);
    }

    return volume;
}

void translate(Domain::ModelVolume& model_volume, const double x, const double y, const double z)
{
    ModelVolume::translate(model_volume, Domain::Vec3d(x, y, z));
}

void translate(Domain::ModelVolume& model_volume, const Domain::Vec3d& displacement)
{
    model_volume.set_offset(model_volume.get_offset() + displacement);
}

void scale(Domain::ModelVolume& model_volume, const Domain::Vec3d& scaling_factors)
{
    model_volume.set_scaling_factor(model_volume.get_scaling_factor().cwiseProduct(scaling_factors));
}

void scale(Domain::ModelVolume& model_volume, const double x, const double y, const double z)
{
    ModelVolume::scale(model_volume, Domain::Vec3d(x, y, z));
}

void scale(Domain::ModelVolume& model_volume, const double s)
{
    ModelVolume::scale(model_volume, Domain::Vec3d(s, s, s));
}

void rotate(Domain::ModelVolume& model_volume, const double angle, const Domain::Axis axis)
{
    switch (axis) {
    case Domain::X: {
        rotate(model_volume, angle, Domain::Vec3d::UnitX());
        break;
    }
    case Domain::Y: {
        rotate(model_volume, angle, Domain::Vec3d::UnitY());
        break;
    }
    case Domain::Z: {
        rotate(model_volume, angle, Domain::Vec3d::UnitZ());
        break;
    }
    default:
        break;
    }
}

void rotate(Domain::ModelVolume& model_volume, const double angle, const Domain::Vec3d& axis)
{
    model_volume.set_rotation(model_volume.get_rotation() + Domain::extract_rotation(Eigen::Quaterniond(Eigen::AngleAxisd(angle, axis)).toRotationMatrix()));
}

void mirror(Domain::ModelVolume& model_volume, const Domain::Axis axis)
{
    Domain::Vec3d mirror = model_volume.get_mirror();
    switch (axis) {
    case Domain::X: {
        mirror.x() *= -1.;
        break;
    }
    case Domain::Y: {
        mirror.y() *= -1.;
        break;
    }
    case Domain::Z: {
        mirror.z() *= -1.;
        break;
    }
    default:
        break;
    }
    model_volume.set_mirror(mirror);
}

// Split this volume, append the result to the object owning this volume.
// Return the number of volumes created from this one.
// This is useful to assign different materials to different volumes of an object.
size_t split(Domain::ModelVolume* volume, unsigned int max_extruders)
{
    std::vector<Domain::TriangleMesh> meshes = TriangleMesh::split(volume->mesh());
    if (meshes.size() <= 1)
        return 1;

    std::sort(
        meshes.begin(),
        meshes.end(),
        Slic3r::Biz::Algorithms::TriangleMesh::is_front_up_left
    );

    // splited volume should not be text object
    if (volume->text_configuration.has_value())
        volume->text_configuration.reset();

    Domain::ModelObject* object = volume->get_object();

    size_t idx = 0;
    size_t ivolume = std::find(object->volumes.begin(), object->volumes.end(), volume) - object->volumes.begin();
    const std::string& name = volume->name;

    int extruder_counter = 0;
    const Domain::Vec3d offset = volume->get_offset();

    for (Domain::TriangleMesh& mesh : meshes) {
        if (mesh.empty() || mesh.has_zero_volume())
            // Repair may have removed unconnected triangles, thus emptying the mesh.
            continue;

        if (idx == 0) {
            volume->set_mesh(std::move(mesh));
            Algorithms::ModelVolume::calculate_convex_hull(*volume);
            // Assign a new unique ID, so that a new GLVolume will be generated.
            volume->set_new_unique_id();
            // reset the source to disable reload from disk
            volume->source = Domain::ModelVolume::Source();
        }
        else
            Algorithms::ModelObject::insert_volume(object, (++ivolume), *volume, std::move(mesh));

        object->volumes[ivolume]->set_offset(Domain::Vec3d::Zero());
        Algorithms::ModelVolume::translate(*object->volumes[ivolume], offset);
        object->volumes[ivolume]->name = name + "_" + std::to_string(idx + 1);
        object->volumes[ivolume]->volume_settings.overrides.set("extruder", extruder_counter++);
        object->volumes[ivolume]->discard_splittable();
        ++idx;
        if (static_cast<unsigned int>(extruder_counter) == max_extruders)
            extruder_counter = 0;
    }

    // discard volumes for which the convex hull was not generated or is degenerate
    size_t i = 0;
    while (i < object->volumes.size()) {
        const std::shared_ptr<const Domain::TriangleMesh>& hull = object->volumes[i]->get_convex_hull_shared_ptr();
        if (hull == nullptr || hull->its.vertices.empty() || hull->its.indices.empty()) {
            object->delete_volume(i);
            --idx;
            --i;
        }
        ++i;
    }

    return idx;
}

bool is_splittable(const Domain::ModelVolume& model_volume)
{
    // The call mesh.is_splittable() is expensive, so cache the value to calculate it only once.
    if (model_volume.m_is_splittable == -1) {
        model_volume.m_is_splittable = TriangleMesh::its_is_splittable(model_volume.mesh().its);
    }

    return model_volume.m_is_splittable == 1;
}

void calculate_convex_hull(Domain::ModelVolume& model_volume)
{
    model_volume.m_convex_hull = std::make_shared<Domain::TriangleMesh>(TriangleMesh::convex_hull_3d(model_volume.mesh()));
    assert(model_volume.m_convex_hull.get());
}

Domain::BoundingBox3d transformed_bounding_box(const Domain::ModelVolume& model_volume, const Domain::Transform3d& trafo)
{
    const auto& ch = model_volume.get_convex_hull_shared_ptr();
    const Domain::TriangleMesh* m = (ch != nullptr) ? ch.get() : &model_volume.mesh();
    return TriangleMesh::transformed_bounding_box(*m, trafo);
}

bool has_support_enforcers(const Domain::ModelVolume& model_volume)
{
    if (model_volume.is_support_enforcer()) {
        return true;
    }

    return model_volume.is_model_part()
        && FacetsAnnotation::has_facets(
               model_volume.supported_facets,
               Domain::TriangleSelector::TriangleStateType::ENFORCER
        );
}

bool is_reloadable_from_disk(const Domain::ModelVolume& model_volume)
{
    const Domain::ModelVolume::Source& source = model_volume.source;
    if (source.is_from_builtin_objects || source.input_file.empty()) {
        return false;
    }

    return !boost::filesystem::path(source.input_file).extension().string().empty();
}

} // namespace Slic3r::Biz::Algorithms::ModelVolume
