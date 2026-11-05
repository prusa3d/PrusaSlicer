#pragma once

#include "Slic3r/Domain/Axis.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/ModelVolume.hpp"
#include "Slic3r/Domain/Types.hpp"

#include <vector>

namespace Slic3r::Biz::Algorithms::TriangleMesh {
struct SourceFacet;
} // namespace Slic3r::Biz::Algorithms::TriangleMesh

namespace Slic3r::Biz::Algorithms::ModelVolume {

Domain::ModelVolume* construct_ptr(Domain::ModelObject* object, const Domain::TriangleMesh& mesh, Domain::ModelVolumeType type = Domain::ModelVolumeType::MODEL_PART);

Domain::ModelVolume* construct_ptr(Domain::ModelObject* object, Domain::TriangleMesh&& mesh, Domain::ModelVolumeType type = Domain::ModelVolumeType::MODEL_PART);

Domain::ModelVolume* construct_ptr(Domain::ModelObject* object, const Domain::ModelVolume& other, Domain::TriangleMesh&& mesh);

void translate(Domain::ModelVolume& model_volume, double x, double y, double z);

void translate(Domain::ModelVolume& model_volume, const Domain::Vec3d& displacement);

void scale(Domain::ModelVolume& model_volume, const Domain::Vec3d& scaling_factors);

void scale(Domain::ModelVolume& model_volume, double x, double y, double z);

void scale(Domain::ModelVolume& model_volume, double s);

void rotate(Domain::ModelVolume& model_volume, double angle, Domain::Axis axis);

void rotate(Domain::ModelVolume& model_volume, double angle, const Domain::Vec3d& axis);

void mirror(Domain::ModelVolume& model_volume, Domain::Axis axis);

// Split this volume, append the result to the object owning this volume.
// Return the number of volumes created from this one.
// This is useful to assign different materials to different volumes of an object.
size_t split(Domain::ModelVolume* volume, unsigned int max_extruders);

bool is_splittable(const Domain::ModelVolume& model_volume);

/**
 * @brief Remaps the painting of source_volume onto derived_volume, whose mesh was derived from source_mesh. The derived_volume may be the source_volume itself.
 *
 * @param source_mesh Mesh painted by source_volume, in the coordinate system of the mesh of derived_volume. It may differ from the current mesh of source_volume.
 */
void remap_facets_annotations(
    const Domain::ModelVolume& source_volume,
    const Domain::TriangleMesh& source_mesh,
    const std::vector<TriangleMesh::SourceFacet>& source_facets,
    Domain::ModelVolume& derived_volume
);

void calculate_convex_hull(Domain::ModelVolume& model_volume);

// Returns the bbox of the given ModelVolume transformed by the given transformation
Domain::BoundingBox3d transformed_bounding_box(const Domain::ModelVolume& model_volume, const Domain::Transform3d& trafo);

/**
 * @brief Whether the volume has a support enforcer.
 *
 * True when the volume is a support-enforcer volume, or a model part painted with support-enforcer facets.
 *
 * @param model_volume The volume to test.
 * @return True if the volume has a support enforcer, false otherwise.
 */
bool has_support_enforcers(const Domain::ModelVolume& model_volume);

/**
 * @brief Whether the volume has a usable source file and can be reloaded from disk.
 *
 * @param model_volume The volume to test.
 * @return True if the volume can be reloaded from disk, false otherwise.
 */
bool is_reloadable_from_disk(const Domain::ModelVolume& model_volume);

} // namespace Slic3r::Biz::Algorithms::ModelVolume
