#include "Slic3r/Biz/Algorithms/ModelObject.hpp"

#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Biz/Algorithms/Geometry/ConvexHull.hpp"
#include "Slic3r/Biz/Algorithms/Geometry/Geometry.hpp"
#include "Slic3r/Biz/Algorithms/ModelInstance.hpp"
#include "Slic3r/Biz/Algorithms/ModelVolume.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Exception.hpp"

#include <ranges>
#include <iostream>

#include <boost/filesystem.hpp>
#include <boost/nowide/iostream.hpp>

#include <oneapi/tbb/concurrent_vector.h>
#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/parallel_for.h>

using namespace Slic3r::Biz::Algorithms;

namespace Slic3r::Biz::Algorithms::ModelObject {

void translate(Domain::ModelObject& model_object, const Domain::Vec3d& vector)
{
    ModelObject::translate(model_object, vector.x(), vector.y(), vector.z());
}

void translate(Domain::ModelObject& model_object, const double x, const double y, const double z)
{
    for (Domain::ModelVolume* v : model_object.volumes) {
        ModelVolume::translate(*v, x, y, z);
    }

    if (model_object.m_bounding_box_approx_valid) {
        model_object.m_bounding_box_approx = BoundingBox::translated(model_object.m_bounding_box_approx, Domain::Vec3d{x, y, z});
    }

    if (model_object.m_bounding_box_exact_valid) {
        model_object.m_bounding_box_exact = BoundingBox::translated(model_object.m_bounding_box_exact, Domain::Vec3d{x, y, z});
    }
}

void scale(Domain::ModelObject& model_object, const Domain::Vec3d& versor)
{
    for (Domain::ModelVolume* v : model_object.volumes) {
        ModelVolume::scale(*v, versor);
    }

    model_object.invalidate_bounding_box();
}

void scale(Domain::ModelObject& model_object, const double s)
{
    ModelObject::scale(model_object, Domain::Vec3d(s, s, s));
}

void scale(Domain::ModelObject& model_object, const double x, const double y, const double z)
{
    ModelObject::scale(model_object, Domain::Vec3d(x, y, z));
}

void rotate(Domain::ModelObject& model_object, double angle, Domain::Axis axis)
{
    for (Domain::ModelVolume* v : model_object.volumes) {
        ModelVolume::rotate(*v, angle, axis);
    }

    ModelObject::center_around_origin(model_object);
    model_object.invalidate_bounding_box();
}

void rotate(Domain::ModelObject& model_object, double angle, const Domain::Vec3d& axis)
{
    for (Domain::ModelVolume* v : model_object.volumes) {
        ModelVolume::rotate(*v, angle, axis);
    }

    ModelObject::center_around_origin(model_object);
    model_object.invalidate_bounding_box();
}

void mirror(Domain::ModelObject& model_object, Domain::Axis axis)
{
    for (Domain::ModelVolume* v : model_object.volumes) {
        ModelVolume::mirror(*v, axis);
    }

    model_object.invalidate_bounding_box();
}

Domain::ModelVolume* add_volume(Domain::ModelObject* model_object, const Domain::TriangleMesh& mesh)
{
    Domain::ModelVolume* v = ModelVolume::construct_ptr(model_object, mesh);
    model_object->volumes.push_back(v);
    model_object->invalidate_bounding_box();
    return v;
}

Domain::ModelVolume* add_volume(Domain::ModelObject* model_object, Domain::TriangleMesh&& mesh, Domain::ModelVolumeType type)
{
    Domain::ModelVolume* v = ModelVolume::construct_ptr(model_object, std::move(mesh), type);
    model_object->volumes.push_back(v);
    model_object->invalidate_bounding_box();
    return v;
}

Domain::ModelVolume* add_volume(Domain::ModelObject* model_object, const Domain::ModelVolume& other, Domain::TriangleMesh&& mesh)
{
    Domain::ModelVolume* v = ModelVolume::construct_ptr(model_object, other, std::move(mesh));
    model_object->volumes.push_back(v);
    model_object->invalidate_bounding_box();
    return v;
}

Domain::ModelVolume* insert_volume(Domain::ModelObject* model_object, const size_t idx, const Domain::ModelVolume& other, Domain::TriangleMesh&& mesh)
{
    Domain::ModelVolume* v = ModelVolume::construct_ptr(model_object, other, std::move(mesh));
    model_object->volumes.insert(model_object->volumes.begin() + idx, v);
    model_object->invalidate_bounding_box();
    return v;
}

static bool less_than(const Domain::ModelVolume* lhs, const Domain::ModelVolume* rhs)
{
    return lhs->type() < rhs->type();
}

void sort_volumes(Domain::ModelObject* model_object)
{
    std::ranges::stable_sort(
        model_object->volumes,
        less_than
    );
}

bool are_volumes_sorted(const Domain::ModelObject* model_object)
{
    return std::ranges::is_sorted(
        model_object->volumes,
        less_than
    );
}


void center_around_origin(Domain::ModelObject& model_object, const bool include_modifiers)
{
    // calculate the displacements needed to
    // center this object around the origin
    const Domain::BoundingBox3d bb = include_modifiers ? ModelObject::full_raw_mesh_bounding_box(model_object)
                                                       : ModelObject::raw_mesh_bounding_box(model_object);

    // Shift is the vector from the center of the bounding box to the origin
    const Domain::Vec3d shift = - BoundingBox::center(bb);

    ModelObject::translate(model_object, shift);
    model_object.origin_translation += shift;
}

void ensure_on_bed(Domain::ModelObject& model_object, const bool allow_negative_z)
{
    double z_offset = 0.;

    if (allow_negative_z) {
        if (model_object.parts_count() == 1) {
            const double min_z = model_object.min_z();
            const double max_z = model_object.max_z();
            if (min_z >= Domain::SINKING_Z_THRESHOLD || max_z < 0.)
                z_offset = -min_z;
        }
        else {
            const double max_z = model_object.max_z();
            if (max_z < Domain::SINKING_MIN_Z_THRESHOLD)
                z_offset = Domain::SINKING_MIN_Z_THRESHOLD - max_z;
        }
    }
    else
        z_offset = -model_object.min_z();

    if (z_offset != 0.) {
        model_object.translate_instances(z_offset * Domain::Vec3d::UnitZ());
    }
}

// Returns the bounding box of the transformed instances.
// This bounding box is approximate and not snug.
const Domain::BoundingBox3d& bounding_box_approx(const Domain::ModelObject& model_object)
{
    if (!model_object.m_bounding_box_approx_valid) {
        model_object.m_bounding_box_approx_valid = true;
        Domain::BoundingBox3d raw_bbox = ModelObject::raw_mesh_bounding_box(model_object);
        model_object.m_bounding_box_approx = {};
        for (const Domain::ModelInstance* i : model_object.instances) {
            model_object.m_bounding_box_approx = BoundingBox::merge(model_object.m_bounding_box_approx, ModelInstance::transformed_bounding_box(raw_bbox, *i));
        }
    }

    return model_object.m_bounding_box_approx;
}

// Returns the bounding box of the transformed instances.
// This bounding box is approximate and not snug.
const Domain::BoundingBox3d& bounding_box_exact(const Domain::ModelObject& model_object)
{
    if (!model_object.m_bounding_box_exact_valid) {
        model_object.m_bounding_box_exact_valid = true;
        model_object.m_min_max_z_valid = true;
        model_object.m_bounding_box_exact = {};
        for (size_t i = 0; i < model_object.instances.size(); ++i) {
            model_object.m_bounding_box_exact = BoundingBox::merge(model_object.m_bounding_box_exact, ModelObject::instance_bounding_box(model_object, i));
        }
    }

    return model_object.m_bounding_box_exact;
}

// A transformed snug bounding box around the non-modifier object volumes, without the translation applied.
// This bounding box is only used for the actual slicing and for layer editing UI to calculate the layers.
const Domain::BoundingBox3d& raw_bounding_box(const Domain::ModelObject& model_object)
{
    if (!model_object.m_raw_bounding_box_valid) {
        model_object.m_raw_bounding_box_valid = true;
        model_object.m_raw_bounding_box = {};
        if (model_object.instances.empty())
            throw Slic3r::InvalidArgument("Can't call raw_bounding_box() with no instances");

        const Domain::Transform3d inst_matrix = model_object.instances.front()->get_transformation().get_matrix_no_offset();
        for (const Domain::ModelVolume* v : model_object.volumes) {
            if (v->is_model_part()) {
                model_object.m_raw_bounding_box = BoundingBox::merge(model_object.m_raw_bounding_box, ModelVolume::transformed_bounding_box(*v, inst_matrix * v->get_matrix()));
            }
        }
    }

    return model_object.m_raw_bounding_box;
}

Domain::BoundingBox3d instance_bounding_box(const Domain::ModelObject& model_object, const size_t instance_idx, const bool dont_translate)
{
    return ModelObject::instance_bounding_box(model_object, *model_object.instances[instance_idx], dont_translate);
}

// This returns an accurate snug bounding box of the transformed object instance, without the translation applied.
Domain::BoundingBox3d instance_bounding_box(const Domain::ModelObject& model_object, const Domain::ModelInstance& model_instance, bool dont_translate)
{
    Domain::BoundingBox3d bb;
    const Domain::Transform3d inst_matrix = dont_translate ? model_instance.get_transformation().get_matrix_no_offset()
                                                           : model_instance.get_transformation().get_matrix();

    for (Domain::ModelVolume* v : model_object.volumes) {
        if (v->is_model_part()) {
            bb = BoundingBox::merge(bb, ModelVolume::transformed_bounding_box(*v, inst_matrix * v->get_matrix()));
        }
    }

    return bb;
}

Domain::BoundingBox3d instance_bounding_box(const Domain::ModelObject& model_object, const Domain::ModelInstance& instance,
    double world_z, bool dont_translate)
{
    const Domain::Transform3d inst_matrix = dont_translate ? instance.get_transformation().get_matrix_no_offset()
                                                           : instance.get_transformation().get_matrix();
    Domain::BoundingBox3d bb;
    for (Domain::ModelVolume* v : model_object.volumes) {
        if (v->is_model_part())
            bb = BoundingBox::merge(bb, TriangleMesh::transformed_bounding_box(v->mesh(), inst_matrix * v->get_matrix(), world_z));
    }
    return bb;
}

const Domain::BoundingBox3d& raw_mesh_bounding_box(const Domain::ModelObject& model_object)
{
    if (!model_object.m_raw_mesh_bounding_box_valid) {
        model_object.m_raw_mesh_bounding_box_valid = true;
        model_object.m_raw_mesh_bounding_box = {};
        for (const Domain::ModelVolume* v : model_object.volumes) {
            if (v->is_model_part()) {
                model_object.m_raw_mesh_bounding_box = BoundingBox::merge(model_object.m_raw_mesh_bounding_box, ModelVolume::transformed_bounding_box(*v, v->get_matrix()));
            }
        }
    }

    return model_object.m_raw_mesh_bounding_box;
}

Domain::BoundingBox3d full_raw_mesh_bounding_box(const Domain::ModelObject& model_object)
{
    Domain::BoundingBox3d bb;
    for (const Domain::ModelVolume* v : model_object.volumes) {
        bb = BoundingBox::merge(bb, ModelVolume::transformed_bounding_box(*v, v->get_matrix()));
    }

    return bb;
}

// A mesh containing all transformed instances of this object.
Domain::TriangleMesh mesh(const Domain::ModelObject& model_object)
{
    Domain::TriangleMesh mesh;
    Domain::TriangleMesh raw_mesh = model_object.raw_mesh();
    for (const Domain::ModelInstance* i : model_object.instances) {
        Domain::TriangleMesh m = raw_mesh;
        ModelInstance::transform_mesh(m, *i);
        mesh.merge(m);
    }

    return mesh;
}

// Calculate 2D convex hull of a projection of the transformed printable volumes into the XY plane.
// This method is cheap in that it does not make any unnecessary copy of the volume meshes.
// This method is used by the auto arrange function.
Domain::Polygon convex_hull_2d(const Domain::ModelObject& model_object, const Domain::Transform3d& trafo_instance)
{
    tbb::concurrent_vector<Domain::Polygon> chs;
    chs.reserve(model_object.volumes.size());
    tbb::parallel_for(tbb::blocked_range<size_t>(0, model_object.volumes.size()), [&](const tbb::blocked_range<size_t>& range) {
        for (size_t i = range.begin(); i < range.end(); ++i) {
            const Domain::ModelVolume* v = model_object.volumes[i];
            if (v->is_model_part()) {
                chs.emplace_back(TriangleMesh::its_convex_hull_2d_above(v->mesh().its, (trafo_instance * v->get_matrix()).cast<float>(), 0.f));
            }
        }
    });

    Domain::Polygons polygons;
    polygons.assign(chs.begin(), chs.end());
    return Geometry::convex_hull(polygons);
}

std::string get_export_filename(const Domain::ModelObject& model_object)
{
    std::string ret = model_object.input_file;
    if (!model_object.name.empty()) {
        if (ret.empty()) {
            // input_file was empty, just use name
            ret = model_object.name;
        } else {
            // Replace file name in input_file with name, but keep the path and file extension.
            ret = (boost::filesystem::path(model_object.name).parent_path().empty()) ? (boost::filesystem::path(ret).parent_path() / model_object.name).make_preferred().string()
                                                                                     : model_object.name;
        }
    }

    return ret;
}

void print_info(const Domain::ModelObject& model_object)
{
    std::cout << std::fixed;
    boost::nowide::cout << "[" << boost::filesystem::path(model_object.input_file).filename().string() << "]" << std::endl;

    Domain::TriangleMesh mesh = model_object.raw_mesh();
    Domain::BoundingBox3d bb = mesh.bounding_box();
    Domain::Vec3d size = BoundingBox::sizes(bb);
    std::cout << "size_x = " << size.x()   << std::endl;
    std::cout << "size_y = " << size.y()   << std::endl;
    std::cout << "size_z = " << size.z()   << std::endl;
    std::cout << "min_x = "  << bb.min.x() << std::endl;
    std::cout << "min_y = "  << bb.min.y() << std::endl;
    std::cout << "min_z = "  << bb.min.z() << std::endl;
    std::cout << "max_x = "  << bb.max.x() << std::endl;
    std::cout << "max_y = "  << bb.max.y() << std::endl;
    std::cout << "max_z = "  << bb.max.z() << std::endl;
    std::cout << "number_of_facets = " << mesh.facets_count() << std::endl;

    std::cout << "manifold = " << (mesh.stats().manifold() ? "yes" : "no") << std::endl;
    if (!mesh.stats().manifold()) {
        std::cout << "open_edges = " << mesh.stats().open_edges << std::endl;
    }

    if (mesh.stats().repaired()) {
        const Domain::RepairedMeshErrors& stats = mesh.stats().repaired_errors;
        if (stats.degenerate_facets > 0) {
            std::cout << "degenerate_facets = " << stats.degenerate_facets << std::endl;
        }
        if (stats.edges_fixed > 0) {
            std::cout << "edges_fixed = "       << stats.edges_fixed       << std::endl;
        }
        if (stats.facets_removed > 0) {
            std::cout << "facets_removed = "    << stats.facets_removed    << std::endl;
        }
        if (stats.facets_reversed > 0) {
            std::cout << "facets_reversed = "   << stats.facets_reversed   << std::endl;
        }
        if (stats.backwards_edges > 0) {
            std::cout << "backwards_edges = "   << stats.backwards_edges   << std::endl;
        }
    }

    std::cout << "number_of_parts =  " << mesh.stats().number_of_parts << std::endl;
    std::cout << "volume = "           << mesh.volume()                << std::endl;
}

void scale_to_fit(Domain::ModelObject& model_object, const Domain::Vec3d& size)
{
    Domain::Vec3d orig_size = BoundingBox::sizes(ModelObject::bounding_box_exact(model_object));
    const double factor = std::min(size.x() / orig_size.x(), std::min(size.y() / orig_size.y(), size.z() / orig_size.z()));

    ModelObject::scale(model_object, factor);
}

// Support for non-uniform scaling of instances. If an instance is rotated by angles, which are not multiples of ninety degrees,
// then the scaling in world coordinate system is not representable by the Geometry::Transformation structure.
// This situation is solved by baking in the instance transformation into the mesh vertices.
// Rotation and mirroring is being baked in. In case the instance scaling was non-uniform, it is baked in as well.
void bake_xy_rotation_into_meshes(Domain::ModelObject& model_object, const size_t instance_idx)
{
    assert(instance_idx < model_object.instances.size());

    const Domain::Transformation reference_trafo = model_object.instances[instance_idx]->get_transformation();
    const bool   left_handed        = reference_trafo.is_left_handed();
    const bool   has_mirroring      = !reference_trafo.get_mirror().isApprox(Domain::Vec3d(1., 1., 1.));
    const bool   uniform_scaling    = std::abs(reference_trafo.get_scaling_factor().x() - reference_trafo.get_scaling_factor().y()) < Domain::EPSILON &&
                                      std::abs(reference_trafo.get_scaling_factor().x() - reference_trafo.get_scaling_factor().z()) < Domain::EPSILON;
    const double new_scaling_factor = uniform_scaling ? reference_trafo.get_scaling_factor().x() : 1.;

    // Adjust the instances.
    for (size_t i = 0; i < model_object.instances.size(); ++i) {
        Domain::ModelInstance& model_instance = *model_object.instances[i];
        model_instance.set_rotation(Domain::Vec3d(0., 0., Geometry::rotation_diff_z(reference_trafo.get_matrix(), model_instance.get_matrix())));
        model_instance.set_scaling_factor(Domain::Vec3d(new_scaling_factor, new_scaling_factor, new_scaling_factor));
        model_instance.set_mirror(Domain::Vec3d(1., 1., 1.));
    }

    // Adjust the meshes.
    // Transformation to be applied to the meshes.
    Domain::Transformation reference_trafo_mod = reference_trafo;
    reference_trafo_mod.reset_offset();
    if (uniform_scaling) {
        reference_trafo_mod.reset_scaling_factor();
    }

    if (!has_mirroring) {
        reference_trafo_mod.reset_mirror();
    }

    Eigen::Matrix3d     mesh_trafo_3x3           = reference_trafo_mod.get_matrix().matrix().block<3, 3>(0, 0);
    Domain::Transform3d volume_offset_correction = model_object.instances[instance_idx]->get_transformation().get_matrix().inverse() * reference_trafo.get_matrix();
    for (Domain::ModelVolume* model_volume : model_object.volumes) {
        const Domain::Transformation volume_trafo = model_volume->get_transformation();
        const bool   volume_left_handed        = volume_trafo.is_left_handed();
        const bool   volume_has_mirroring      = !volume_trafo.get_mirror().isApprox(Domain::Vec3d(1., 1., 1.));
        const bool   volume_uniform_scaling    = std::abs(volume_trafo.get_scaling_factor().x() - volume_trafo.get_scaling_factor().y()) < Domain::EPSILON &&
                                                 std::abs(volume_trafo.get_scaling_factor().x() - volume_trafo.get_scaling_factor().z()) < Domain::EPSILON;
        const double volume_new_scaling_factor = volume_uniform_scaling ? volume_trafo.get_scaling_factor().x() : 1.;

        // Transform the mesh.
        Domain::Transformation volume_trafo_mod = volume_trafo;
        volume_trafo_mod.reset_offset();
        if (volume_uniform_scaling) {
            volume_trafo_mod.reset_scaling_factor();
        }

        if (!volume_has_mirroring) {
            volume_trafo_mod.reset_mirror();
        }

        Eigen::Matrix3d volume_trafo_3x3 = volume_trafo_mod.get_matrix().matrix().block<3, 3>(0, 0);
        // Following method creates a new shared_ptr<TriangleMesh>
        model_volume->transform_this_mesh(Domain::Transform3d{mesh_trafo_3x3 * volume_trafo_3x3}, left_handed != volume_left_handed);
        // Reset the rotation, scaling and mirroring.
        model_volume->set_rotation(Domain::Vec3d(0., 0., 0.));
        model_volume->set_scaling_factor(Domain::Vec3d(volume_new_scaling_factor, volume_new_scaling_factor, volume_new_scaling_factor));
        model_volume->set_mirror(Domain::Vec3d(1., 1., 1.));
        // Move the reference point of the volume to compensate for the change of the instance trafo.
        model_volume->set_offset(volume_offset_correction * volume_trafo.get_offset());
        // reset the source to disable reload from disk
        model_volume->source = Domain::ModelVolume::Source();
    }

    model_object.invalidate_bounding_box();
}

template <typename ObjectSettingsType>
static ObjectSettingsType create_object_settings_from_volume_settings(
    const Domain::VolumeSettings& volume_settings
)
{
    ObjectSettingsType object_settings;
    for (const Domain::ConfigItem& item : volume_settings.items.all_items()) {
        if (!volume_settings.overrides.find(item.name())
            || !volume_settings.overrides.get(item.name()).has_value()
            || object_settings.items.find(item.name()) == nullptr)
            continue;

        item.visit(
            [&]<typename T>(const T& item_value)
            {
                using ValueType = std::decay_t<T>;
                object_settings.overrides.template set<ValueType>(item.name(), item_value);
            }
        );
    }

    return object_settings;
}

void split(Domain::ModelObject* object, Domain::ModelObjectPtrs* new_objects)
{
    for (Domain::ModelVolume* volume : object->volumes) {
        if (volume->type() != Domain::ModelVolumeType::MODEL_PART)
            continue;

        // splited volume should not be text object
        if (volume->text_configuration.has_value())
            volume->text_configuration.reset();

        std::vector<Domain::TriangleMesh> meshes =
            Slic3r::Biz::Algorithms::TriangleMesh::split(volume->mesh());
        std::sort(
            meshes.begin(),
            meshes.end(),
            Slic3r::Biz::Algorithms::TriangleMesh::is_front_up_left
        );

        size_t counter = 1;
        for (Domain::TriangleMesh& mesh : meshes) {
            // FIXME: crashes if not satisfied
            if (mesh.facets_count() < 3 || mesh.has_zero_volume())
                continue;

            // XXX: this seems to be the only real usage of m_model, maybe refactor this so that it's not needed?
            Domain::ModelObject* new_object = object->get_model()->add_object();
            if (meshes.size() == 1) {
                new_object->name                = volume->name;
                new_object->object_settings     = object->object_settings.overrides.empty() ?
                    create_object_settings_from_volume_settings<Domain::ObjectSettings>(
                        volume->volume_settings
                    ) :
                    object->object_settings;
                new_object->object_settings_sla = object->object_settings_sla.overrides.empty() ?
                    create_object_settings_from_volume_settings<Domain::SLAObjectSettings>(
                        volume->volume_settings
                    ) :
                    object->object_settings_sla;
            } else {
                new_object->name =
                    object->name + (meshes.size() > 1 ? "_" + std::to_string(counter++) : "");
                new_object->object_settings     = object->object_settings;
                new_object->object_settings_sla = object->object_settings_sla;
            }

            new_object->instances.reserve(object->instances.size());
            for (const Domain::ModelInstance* model_instance : object->instances)
                new_object->add_instance(*model_instance);

            Domain::ModelVolume* new_vol =
                Algorithms::ModelObject::add_volume(new_object, *volume, std::move(mesh));

            // Invalidate extruder value in volume's config,
            // otherwise there will no way to change extruder for object after splitting,
            // because volume's extruder value overrides object's extruder value.
            if (new_vol->volume_settings.overrides.get("extruder").has_value()) {
                new_vol->volume_settings.overrides.set("extruder", 0);
            }

            for (Domain::ModelInstance* model_instance : new_object->instances) {
                const Domain::Vec3d shift =
                    model_instance->get_transformation().get_matrix_no_offset()
                    * new_vol->get_offset();
                model_instance->set_offset(model_instance->get_offset() + shift);
            }

            new_vol->set_offset(Domain::Vec3d::Zero());
            // reset the source to disable reload from disk
            new_vol->source = Domain::ModelVolume::Source();
            new_objects->emplace_back(new_object);
        }
    }
}

template <typename ObjectSettingsType>
static void update_volume_settings_from_object_settings(
    Domain::VolumeSettings& volume_settings,
    const ObjectSettingsType& object_settings
)
{
    auto update_override_from_item =
        [](Domain::VolumeSettings& volume_settings, const Domain::ConfigItem& item)
    {
        if (!volume_settings.overrides.find(item.name())
            || volume_settings.overrides.get(item.name()))
            return;
        item.visit(
            [&]<typename T>(const T& item_value)
            {
                using ValueType = std::decay_t<T>;
                volume_settings.overrides.template set<ValueType>(item.name(), item_value);
            }
        );
    };

    for (const Domain::ConfigItem& item : object_settings.items.all_items()) {
        update_override_from_item(volume_settings, item);
    }

    for (const Domain::ConfigItem& item : object_settings.overrides.overridden_items()) {
        update_override_from_item(volume_settings, item);
    }
}

Domain::ModelObject* merge(const std::vector<const Domain::ModelInstance*>& instances)
{
    Domain::Model* model                  = instances.front()->get_object()->get_model();
    Domain::ModelObject* new_object       = model->add_object();
    Domain::ModelInstance* first_instance = new_object->add_instance();
    first_instance->printable             = false;

    for (const Domain::ModelInstance* instance : instances) {
        Domain::ModelObject* object = instance->get_object();
        first_instance->printable |= instance->printable;

        // merge volumes
        for (const Domain::ModelVolume* volume : object->volumes) {
            Domain::ModelVolume* new_volume = new_object->add_volume(*volume);
            // Give the new volumes new id, otherwise there would temporarily
            // be volumes with the same id and all sorts of horrible things may happen.
            new_volume->set_new_unique_id();
            new_volume->set_transformation(instance->get_matrix() * new_volume->get_matrix());

            update_volume_settings_from_object_settings<Domain::ObjectSettings>(
                new_volume->volume_settings,
                object->object_settings
            );
            update_volume_settings_from_object_settings<Domain::SLAObjectSettings>(
                new_volume->volume_settings,
                object->object_settings_sla
            );
        }
        Algorithms::ModelObject::sort_volumes(new_object);

        // merge layers
        for (const auto& range : object->layer_config_ranges)
            new_object->layer_config_ranges.emplace(range);
    }

    return new_object;
}

} // namespace Slic3r::Biz::Algorithms::ModelObject
