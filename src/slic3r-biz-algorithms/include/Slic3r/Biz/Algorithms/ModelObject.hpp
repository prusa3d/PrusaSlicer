#pragma once

#include "Slic3r/Domain/Axis.hpp"
#include "Slic3r/Domain/BoundingBox.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/Types.hpp"

namespace Slic3r::Biz::Algorithms::ModelObject {

void translate(Domain::ModelObject& model_object, const Domain::Vec3d& vector);

void translate(Domain::ModelObject& model_object, double x, double y, double z);

void scale(Domain::ModelObject& model_object, const Domain::Vec3d& versor);

void scale(Domain::ModelObject& model_object, double s);

void scale(Domain::ModelObject& model_object, double x, double y, double z);

void rotate(Domain::ModelObject& model_object, double angle, Domain::Axis axis);

void rotate(Domain::ModelObject& model_object, double angle, const Domain::Vec3d& axis);

void mirror(Domain::ModelObject& model_object, Domain::Axis axis);

Domain::ModelVolume* add_volume(Domain::ModelObject* model_object, const Domain::TriangleMesh& mesh);

Domain::ModelVolume* add_volume(Domain::ModelObject* model_object, Domain::TriangleMesh&& mesh, Domain::ModelVolumeType type);

Domain::ModelVolume* add_volume(Domain::ModelObject* model_object, const Domain::ModelVolume& other, Domain::TriangleMesh&& mesh);

void sort_volumes(Domain::ModelObject* model_object);

bool are_volumes_sorted(const Domain::ModelObject* model_object);

Domain::ModelVolume* insert_volume(Domain::ModelObject* model_object, size_t idx, const Domain::ModelVolume& other, Domain::TriangleMesh&& mesh);

void center_around_origin(Domain::ModelObject& model_object, bool include_modifiers = true);

void ensure_on_bed(Domain::ModelObject& model_object, bool allow_negative_z = false);

/**
 * Returns the bounding box of the transformed instances. This bounding box is approximate and not snug, it is being cached.
 */
const Domain::BoundingBox3d& bounding_box_approx(const Domain::ModelObject& model_object);

/**
 * Returns an exact bounding box of the transformed instances. The result it is being cached.
 */
const Domain::BoundingBox3d& bounding_box_exact(const Domain::ModelObject& model_object);

/**
 * A transformed snug bounding box around the non-modifier object volumes, without the translation applied.
 * This bounding box is only used for the actual slicing.
 */
const Domain::BoundingBox3d& raw_bounding_box(const Domain::ModelObject& model_object);

/**
 * A snug bounding box around the transformed non-modifier object volumes.
 */
Domain::BoundingBox3d instance_bounding_box(const Domain::ModelObject& model_object, size_t instance_idx, bool dont_translate = false);

/**
 * A snug bounding box around the transformed non-modifier object volumes.
 */
Domain::BoundingBox3d instance_bounding_box(const Domain::ModelObject& model_object, const Domain::ModelInstance& instance, bool dont_translate = false);

/**
 * A snug bounding box around the part of the transformed non-modifier object volumes above the given z in world coordinates.
 */
Domain::BoundingBox3d instance_bounding_box(const Domain::ModelObject& model_object, const Domain::ModelInstance& instance, double world_z,
    bool dont_translate = false);

/**
 * A snug bounding box of a non-transformed (non-rotated, non-scaled, non-translated) sum of non-modifier object volumes.
 */
const Domain::BoundingBox3d& raw_mesh_bounding_box(const Domain::ModelObject& model_object);

/**
 * A snug bounding box of a non-transformed (non-rotated, non-scaled, non-translated) sum of all object volumes.
 */
Domain::BoundingBox3d full_raw_mesh_bounding_box(const Domain::ModelObject& model_object);

/**
 * A mesh containing all transformed instances of this object.
 */
Domain::TriangleMesh mesh(const Domain::ModelObject& model_object);

/**
 * Calculate 2D convex hull of a projection of the transformed printable volumes into the XY plane.
 * This method is cheap in that it does not make any unnecessary copy of the volume meshes.
 * This method is used by the auto arrange function.
 */
Domain::Polygon convex_hull_2d(const Domain::ModelObject& model_object, const Domain::Transform3d& trafo_instance);


std::string get_export_filename(const Domain::ModelObject& model_object);

/**
 * Print object statistics to console.
 * @param model_object
 */
void print_info(const Domain::ModelObject& model_object);

/**
 * Scale the current ModelObject to fit by altering the scaling factor of ModelInstances.
 * It operates on the total size by duplicating the object according to all the instances.
 * @param size Sizef3 the size vector.
 */
void scale_to_fit(Domain::ModelObject& model_object, const Domain::Vec3d& size);

/**
 * Support for non-uniform scaling of instances. If an instance is rotated by angles, which are not multiples of ninety degrees,
 * then the scaling in world coordinate system is not representable by the Geometry::Transformation structure.
 * This situation is solved by baking in the instance transformation into the mesh vertices.
 * Rotation and mirroring is being baked in. In case the instance scaling was non-uniform, it is baked in as well.
 */
void bake_xy_rotation_into_meshes(Domain::ModelObject& model_object, size_t instance_idx);

/**
 * Splits the given object.
 * Appends pointers to the newly created objects to new_objects.
 */
void split(Domain::ModelObject* object, std::vector<Domain::ModelObject*>* new_objects);

/**
 * Merges the given instances into one new object.
 * Return a pointer to the newly created object
 */
Domain::ModelObject* merge(const std::vector<const Domain::ModelInstance*>& new_objects);

} // namespace Slic3r::Biz::Algorithms::ModelObject
