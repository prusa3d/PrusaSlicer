
#include "Slic3r/Biz/Utils/CutUtils.hpp"

#include <Slic3r/Log.hpp>
#include <cmath>
#include <string>
#include <utility>
#include <cassert>
#include <cstddef>

#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Biz/Algorithms/Geometry/Geometry.hpp"
#include "Slic3r/Biz/Algorithms/ModelVolume.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/ModelVolume.hpp"

#include "libslic3r/TriangleMeshSlicer.hpp" // cut_mesh

namespace Slic3r::Biz {

using namespace Algorithms::Geometry;
using namespace Domain;

namespace tm = Algorithms::TriangleMesh;
namespace mv = Algorithms::ModelVolume;
using Domain::TriangleMesh;

static void apply_tolerance(Domain::ModelVolume* vol)
{
    Domain::ModelVolume::CutInfo& cut_info = vol->cut_info;

    assert(cut_info.is_connector);
    if (!cut_info.is_processed)
        return;

    Vec3d sf = vol->get_scaling_factor();

    // make a "hole" wider
    sf[X] += double(cut_info.radius_tolerance);
    sf[Y] += double(cut_info.radius_tolerance);

    // make a "hole" dipper
    sf[Z] += double(cut_info.height_tolerance);

    vol->set_scaling_factor(sf);

    // correct offset in respect to the new depth
    Vec3d rot_norm = rotation_transform(vol->get_rotation()) * Vec3d::UnitZ();
    if (rot_norm.norm() != 0.0)
        rot_norm.normalize();

    double z_offset = 0.5 * static_cast<double>(cut_info.height_tolerance);
    if (cut_info.connector_type == Domain::CutConnectorType::Plug ||
        cut_info.connector_type == Domain::CutConnectorType::Snap)
        z_offset -= 0.05; // add small Z offset to better preview

    vol->set_offset(vol->get_offset() + rot_norm * z_offset);
}

static void add_cut_volume(TriangleMesh& mesh, Domain::ModelObject* object, const Domain::ModelVolume* src_volume, const Transform3d& cut_matrix, const std::string& suffix = {}, Domain::ModelVolumeType type = Domain::ModelVolumeType::MODEL_PART)
{
    if (mesh.empty())
        return;

    mesh.transform(cut_matrix);
    Domain::ModelVolume* vol = Algorithms::ModelObject::add_volume(object, mesh);
    vol->set_type(type);

    vol->name = src_volume->name + suffix;
    vol->cut_info = src_volume->cut_info;
    vol->volume_settings = src_volume->volume_settings;
}

static void process_volume_cut(Domain::ModelVolume* volume, const Transform3d& instance_matrix, const Transform3d& cut_matrix,
                                ModelObjectCutAttributes attributes, TriangleMesh& upper_mesh, TriangleMesh& lower_mesh)
{
    const auto volume_matrix = volume->get_matrix();

    const Transformation cut_transformation = Transformation(cut_matrix);
    const Transform3d invert_cut_matrix = cut_transformation.get_rotation_matrix().inverse() * translation_transform(-1 * cut_transformation.get_offset());

    // Transform the mesh by the combined transformation matrix.
    // Flip the triangles in case the composite transformation is left handed.
    TriangleMesh mesh(volume->mesh());
    mesh.transform(invert_cut_matrix * instance_matrix * volume_matrix, true);

    using Biz::Algorithms::TriangleMesh::construct;

    indexed_triangle_set upper_its, lower_its;
    cut_mesh(mesh.its, 0.0f, &upper_its, &lower_its);
    if (attributes.keep_upper)
        upper_mesh = TriangleMesh(construct(upper_its));
    if (attributes.keep_lower)
        lower_mesh = TriangleMesh(construct(lower_its));
}

static void process_connector_cut(Domain::ModelVolume* volume, const Transform3d& instance_matrix, const Transform3d& cut_matrix,
                                    ModelObjectCutAttributes attributes, Domain::ModelObject* upper, Domain::ModelObject* lower,
                                    std::vector<Domain::ModelObject*>& dowels)
{
    assert(volume->cut_info.is_connector);
    volume->cut_info.set_processed();

    const auto volume_matrix = volume->get_matrix();

    // ! Don't apply instance transformation for the conntectors.
    // This transformation is already there
    if (volume->cut_info.connector_type != Domain::CutConnectorType::Dowel) {
        if (attributes.keep_upper) {
            Domain::ModelVolume* vol = nullptr;
            if (volume->cut_info.connector_type == Domain::CutConnectorType::Snap) {
                Domain::TriangleMesh mesh = Domain::TriangleMesh(
                    Biz::Algorithms::TriangleMesh::its_make_cylinder(1.0, 1.0, (PI / 180.))
                );
                vol = Biz::Algorithms::ModelObject::add_volume(
                    upper,
                    std::move(mesh),
                    ModelVolumeType::NEGATIVE_VOLUME
                );
                vol->set_transformation(volume->get_transformation());
                vol->cut_info = volume->cut_info;
                vol->name     = volume->name;
            }
            else
                vol = upper->add_volume(*volume);

            vol->set_transformation(volume_matrix);
            apply_tolerance(vol);
        }
        if (attributes.keep_lower) {
            Domain::ModelVolume* vol = lower->add_volume(*volume);
            vol->set_transformation(volume_matrix);
            // for lower part change type of connector from NEGATIVE_VOLUME to MODEL_PART if this connector is a plug
            vol->set_type(Domain::ModelVolumeType::MODEL_PART);
        }
    }
    else {
        if (attributes.create_dowels) {
            Domain::ModelObject* dowel{ nullptr };
            // Clone the object to duplicate instances, materials etc.
            volume->get_object()->clone_for_cut(&dowel);

            // add one more solid part same as connector if this connector is a dowel
            Domain::ModelVolume* vol = dowel->add_volume(*volume);
            vol->set_type(Domain::ModelVolumeType::MODEL_PART);

            // But discard rotation and Z-offset for this volume
            vol->set_rotation(Vec3d::Zero());
            vol->set_offset(Z, 0.0);

            dowels.push_back(dowel);
        }

        // Cut the dowel
        apply_tolerance(volume);

        // Perform cut
        TriangleMesh upper_mesh, lower_mesh;
        process_volume_cut(volume, Transform3d::Identity(), cut_matrix, attributes, upper_mesh, lower_mesh);

        // add small Z offset to better preview
        upper_mesh.translate((-0.05 * Vec3d::UnitZ()).cast<float>());
        lower_mesh.translate((0.05 * Vec3d::UnitZ()).cast<float>());

        // Add cut parts to the related objects
        add_cut_volume(upper_mesh, upper, volume, cut_matrix, "_A", volume->type());
        add_cut_volume(lower_mesh, lower, volume, cut_matrix, "_B", volume->type());
    }
}

static void process_modifier_cut(Domain::ModelVolume* volume, const Transform3d& instance_matrix, const Transform3d& inverse_cut_matrix,
                                 ModelObjectCutAttributes attributes, Domain::ModelObject* upper, Domain::ModelObject* lower)
{
    const auto volume_matrix = instance_matrix * volume->get_matrix();

    // Modifiers are not cut, but we still need to add the instance transformation
    // to the modifier volume transformation to preserve their shape properly.
    volume->set_transformation(Transformation(volume_matrix));

    if (attributes.keep_as_parts) {
        upper->add_volume(*volume);
        return;
    }

    // Some logic for the negative volumes/connectors. Add only needed modifiers
    auto bb = mv::transformed_bounding_box(*volume, inverse_cut_matrix * volume_matrix);
    bool is_crossed_by_cut = bb.min[Z] <= 0 && bb.max[Z] >= 0;
    if (attributes.keep_upper && (bb.min[Z] >= 0 || is_crossed_by_cut))
        upper->add_volume(*volume);
    if (attributes.keep_lower && (bb.max[Z] <= 0 || is_crossed_by_cut))
        lower->add_volume(*volume);
}

static void process_solid_part_cut(Domain::ModelVolume* volume, const Transform3d& instance_matrix, const Transform3d& cut_matrix,
                            ModelObjectCutAttributes attributes, Domain::ModelObject* upper, Domain::ModelObject* lower)
{
    // Perform cut
    TriangleMesh upper_mesh, lower_mesh;
    process_volume_cut(volume, instance_matrix, cut_matrix, attributes, upper_mesh, lower_mesh);

    // Add required cut parts to the objects

    if (attributes.keep_as_parts) {
        add_cut_volume(upper_mesh, upper, volume, cut_matrix, "_A");
        if (!lower_mesh.empty()) {
            add_cut_volume(lower_mesh, upper, volume, cut_matrix, "_B");
            upper->volumes.back()->cut_info.is_from_upper = false;
        }
        return;
    }

    if (attributes.keep_upper)
        add_cut_volume(upper_mesh, upper, volume, cut_matrix);

    if (attributes.keep_lower && !lower_mesh.empty())
        add_cut_volume(lower_mesh, lower, volume, cut_matrix);
}

static void reset_instance_transformation(Domain::ModelObject* object, size_t src_instance_idx,
                                          const Transform3d& cut_matrix = Transform3d::Identity(),
                                          bool place_on_cut = false, bool flip = false)
{
    // Reset instance transformation except offset and Z-rotation

    for (size_t i = 0; i < object->instances.size(); ++i) {
        auto& obj_instance = object->instances[i];
        const double rot_z = obj_instance->get_rotation().z();
        
        Transformation inst_trafo = Transformation(obj_instance->get_transformation().get_matrix_no_scaling_factor());
        // add respect to mirroring
        if (obj_instance->is_left_handed())
            inst_trafo = inst_trafo * Transformation(scale_transform(Vec3d(-1, 1, 1)));

        obj_instance->set_transformation(inst_trafo);

        Vec3d rotation = Vec3d::Zero();
        if (!flip && !place_on_cut) {
            if ( i != src_instance_idx)
            rotation[Z] = rot_z;
        }
        else {
            Transform3d rotation_matrix = Transform3d::Identity();
            if (flip)
                rotation_matrix = rotation_transform(PI * Vec3d::UnitX());

            if (place_on_cut)
                rotation_matrix = rotation_matrix * Transformation(cut_matrix).get_rotation_matrix().inverse();

            if (i != src_instance_idx)
                rotation_matrix = rotation_transform(rot_z * Vec3d::UnitZ()) * rotation_matrix;

            rotation = Transformation(rotation_matrix).get_rotation();
        }

        obj_instance->set_rotation(rotation);
    }
}


Cut::Cut(const Domain::ModelObject* object, int instance, const Transform3d& cut_matrix,
         ModelObjectCutAttributes attributes)
    : m_instance(instance), m_cut_matrix(cut_matrix), m_attributes(attributes)
{
    m_model = Domain::Model();
    if (object)
        m_model.add_object(*object);
}

void Cut::post_process(Domain::ModelObject* object, Domain::ModelObjectPtrs& cut_object_ptrs, bool keep, bool place_on_cut, bool flip)
{
    if (!object) return;

    if (keep && !object->volumes.empty()) {
        reset_instance_transformation(object, m_instance, m_cut_matrix, place_on_cut, flip);
        cut_object_ptrs.push_back(object);
    }
    else
        m_model.objects.push_back(object); // will be deleted in m_model.clear_objects();
}

void Cut::post_process(Domain::ModelObject* upper, Domain::ModelObject* lower, Domain::ModelObjectPtrs& cut_object_ptrs)
{
    post_process(upper, cut_object_ptrs,
        m_attributes.keep_upper,
        m_attributes.place_on_cut_upper,
        m_attributes.flip_upper);

    post_process(lower, cut_object_ptrs,
        m_attributes.keep_lower,
        m_attributes.place_on_cut_lower,
        m_attributes.place_on_cut_lower || m_attributes.flip_lower);
}


void Cut::finalize(const Domain::ModelObjectPtrs& objects)
{
    //clear model from temporarry objects
    m_model.clear_objects();

    // add to model result objects
    m_model.objects = objects;
}

const Domain::ModelObjectPtrs& Cut::perform_with_plane()
{
    if (!m_attributes.keep_upper && !m_attributes.keep_lower) {
        m_model.clear_objects();
        return m_model.objects;
    }

    Domain::ModelObject* mo = m_model.objects.front();

    SPDLOG_TRACE("ModelObject::cut - start");

    // Clone the object to duplicate instances, materials etc.
    Domain::ModelObject* upper{ nullptr };
    if (m_attributes.keep_upper)
        mo->clone_for_cut(&upper);

    Domain::ModelObject* lower{ nullptr };
    if (m_attributes.keep_lower && !m_attributes.keep_as_parts)
        mo->clone_for_cut(&lower);

    std::vector<Domain::ModelObject*> dowels;

    // Because transformations are going to be applied to meshes directly,
    // we reset transformation of all instances and volumes,
    // except for translation and Z-rotation on instances, which are preserved
    // in the transformation matrix and not applied to the mesh transform.

    const auto              instance_matrix = mo->instances[m_instance]->get_transformation().get_matrix_no_offset();
    const Transformation    cut_transformation = Transformation(m_cut_matrix);
    const Transform3d       inverse_cut_matrix = cut_transformation.get_rotation_matrix().inverse() * translation_transform(-1. * cut_transformation.get_offset());

    for (Domain::ModelVolume* volume : mo->volumes) {
        volume->reset_extra_facets();

        if (!volume->is_model_part()) {
            if (volume->cut_info.is_processed)
                process_modifier_cut(volume, instance_matrix, inverse_cut_matrix, m_attributes, upper, lower);
            else
                process_connector_cut(volume, instance_matrix, m_cut_matrix, m_attributes, upper, lower, dowels);
        }
        else if (!volume->mesh().empty())
            process_solid_part_cut(volume, instance_matrix, m_cut_matrix, m_attributes, upper, lower);
    }

    // Post-process cut parts

    if (m_attributes.keep_as_parts && upper->volumes.empty()) {
        m_model = Domain::Model();
        m_model.objects.push_back(upper);
        return m_model.objects;
    }

    Domain::ModelObjectPtrs cut_object_ptrs;

    if (m_attributes.keep_as_parts && !upper->volumes.empty()) {
        reset_instance_transformation(upper, m_instance, m_cut_matrix);
        cut_object_ptrs.push_back(upper);
    }
    else {
        // Delete all modifiers which are not intersecting with solid parts bounding box
        auto delete_extra_modifiers = [this](Domain::ModelObject* mo) {
            if (!mo) return;
            const Domain::BoundingBox3d obj_bb = Algorithms::ModelObject::instance_bounding_box(*mo, m_instance);
            const Transform3d inst_matrix = mo->instances[m_instance]->get_transformation().get_matrix();

            for (int i = int(mo->volumes.size()) - 1; i >= 0; --i)
                if (const Domain::ModelVolume* vol = mo->volumes[i];
                    !vol->is_model_part() && !vol->is_cut_connector()) {
                    auto bb = mv::transformed_bounding_box(*vol, inst_matrix * vol->get_matrix());
                    if (!obj_bb.overlap(bb))
                        mo->delete_volume(i);
                }
        };

        post_process(upper, lower, cut_object_ptrs);
        delete_extra_modifiers(upper);
        delete_extra_modifiers(lower);

        if (m_attributes.create_dowels && !dowels.empty()) {
            for (auto dowel : dowels) {
                reset_instance_transformation(dowel, m_instance);
                dowel->name += "-Dowel-" + dowel->volumes[0]->name;
                cut_object_ptrs.push_back(dowel);
            }
        }
    }

    SPDLOG_TRACE("ModelObject::cut - end");

    finalize(cut_object_ptrs);

    return m_model.objects;
}

static void distribute_modifiers_from_object(Domain::ModelObject* from_obj, const int instance_idx, Domain::ModelObject* to_obj1, Domain::ModelObject* to_obj2)
{
    auto              obj1_bb = to_obj1 ? Algorithms::ModelObject::instance_bounding_box(*to_obj1, instance_idx) : BoundingBoxf3();
    auto              obj2_bb = to_obj2 ? Algorithms::ModelObject::instance_bounding_box(*to_obj2, instance_idx) : BoundingBoxf3();
    const Transform3d inst_matrix = from_obj->instances[instance_idx]->get_transformation().get_matrix();

    for (Domain::ModelVolume* vol : from_obj->volumes)
        if (!vol->is_model_part()) {
            // Don't add modifiers which are processed connectors
            if (vol->cut_info.is_connector && !vol->cut_info.is_processed)
                continue;

            // Modifiers are not cut, but we still need to add the instance transformation
            // to the modifier volume transformation to preserve their shape properly.
            const auto modifier_trafo = Transformation(from_obj->instances[instance_idx]->get_transformation().get_matrix_no_offset() * vol->get_matrix());

            auto bb = mv::transformed_bounding_box(*vol, inst_matrix * vol->get_matrix());
            // Don't add modifiers which are not intersecting with solid parts
            if (obj1_bb.overlap(bb))
                to_obj1->add_volume(*vol)->set_transformation(modifier_trafo);
            if (obj2_bb.overlap(bb))
                to_obj2->add_volume(*vol)->set_transformation(modifier_trafo);
        }
}

static void merge_solid_parts_inside_object(Domain::ModelObjectPtrs& objects)
{
    for (Domain::ModelObject* mo : objects) {
        TriangleMesh mesh;
        // Merge all SolidPart but not Connectors
        for (const Domain::ModelVolume* mv : mo->volumes) {
            if (mv->is_model_part() && !mv->is_cut_connector()) {
                TriangleMesh m = mv->mesh();
                m.transform(mv->get_matrix());
                mesh.merge(m);
            }
        }
        if (!mesh.empty()) {
            Domain::ModelVolume* new_volume = Algorithms::ModelObject::add_volume(mo, mesh);
            new_volume->name = mo->name;
            // Delete all merged SolidPart but not Connectors
            for (int i = int(mo->volumes.size()) - 2; i >= 0; --i) {
                const Domain::ModelVolume* mv = mo->volumes[i];
                if (mv->is_model_part() && !mv->is_cut_connector())
                    mo->delete_volume(i);
            }
            // Ensuring that volumes start with solid parts for proper slicing
            mo->sort_volumes(true);
        }
    }
}


const Domain::ModelObjectPtrs& Cut::perform_by_contour(std::vector<Part> parts, int dowels_count)
{
    Domain::ModelObject* cut_mo = m_model.objects.front();

    // Clone the object to duplicate instances, materials etc.
    Domain::ModelObject* upper{ nullptr };
    if (m_attributes.keep_upper) cut_mo->clone_for_cut(&upper);
    Domain::ModelObject* lower{ nullptr };
    if (m_attributes.keep_lower) cut_mo->clone_for_cut(&lower);

    const size_t cut_parts_cnt = parts.size();
    bool has_modifiers = false;

    // Distribute SolidParts to the Upper/Lower object
    for (size_t id = 0; id < cut_parts_cnt; ++id) {
        if (parts[id].is_modifier)
            has_modifiers = true; // modifiers will be added later to the related parts
        else if (Domain::ModelObject* obj = (parts[id].selected ? upper : lower))
            obj->add_volume(*(cut_mo->volumes[id]));
    }

    if (has_modifiers) {
        // Distribute Modifiers to the Upper/Lower object
        distribute_modifiers_from_object(cut_mo, m_instance, upper, lower);
    }

    Domain::ModelObjectPtrs cut_object_ptrs;

    Domain::ModelVolumePtrs& volumes = cut_mo->volumes;
    if (volumes.size() == cut_parts_cnt) {
        // Means that object is cut without connectors

        // Just add Upper and Lower objects to cut_object_ptrs
        post_process(upper, lower, cut_object_ptrs);

        // Now merge all model parts together:
        merge_solid_parts_inside_object(cut_object_ptrs);

        // replace initial objects in model with cut object 
        finalize(cut_object_ptrs);
    }
    else if (volumes.size() > cut_parts_cnt) {
        // Means that object is cut with connectors

        // All volumes are distributed to Upper / Lower object,
        // So we don’t need them anymore
        for (size_t id = 0; id < cut_parts_cnt; id++)
            delete* (volumes.begin() + id);
        volumes.erase(volumes.begin(), volumes.begin() + cut_parts_cnt);

        // Perform cut just to get connectors
        Cut cut(cut_mo, m_instance, m_cut_matrix, m_attributes);
        const Domain::ModelObjectPtrs& cut_connectors_obj = cut.perform_with_plane();
        assert(dowels_count > 0 ? cut_connectors_obj.size() >= 3 : cut_connectors_obj.size() == 2);

        // Connectors from upper object
        for (const Domain::ModelVolume* volume : cut_connectors_obj[0]->volumes)
            upper->add_volume(*volume, volume->type());

        // Connectors from lower object
        for (const Domain::ModelVolume* volume : cut_connectors_obj[1]->volumes)
            lower->add_volume(*volume, volume->type());

        // Add Upper and Lower objects to cut_object_ptrs
        post_process(upper, lower, cut_object_ptrs);

        // Now merge all model parts together:
        merge_solid_parts_inside_object(cut_object_ptrs);

        // replace initial objects in model with cut object
        finalize(cut_object_ptrs);

        // Add Dowel-connectors as separate objects to model
        if (cut_connectors_obj.size() >= 3)
            for (size_t id = 2; id < cut_connectors_obj.size(); id++)
                m_model.add_object(*cut_connectors_obj[id]);
    }

    return m_model.objects;
}


const Domain::ModelObjectPtrs& Cut::perform_with_groove(const Groove& groove, const Transform3d& rotation_m, bool keep_as_parts/* = false*/)
{
    Domain::ModelObject* cut_mo = m_model.objects.front();

    // Clone the object to duplicate instances, materials etc.
    Domain::ModelObject* upper{ nullptr };
    cut_mo->clone_for_cut(&upper);
    Domain::ModelObject* lower{ nullptr };
    cut_mo->clone_for_cut(&lower);

    const double groove_half_depth = 0.5 * double(groove.depth);

    Domain::Model tmp_model_for_cut = Domain::Model();

    Domain::Model tmp_model = Domain::Model();
    tmp_model.add_object(*cut_mo);
    Domain::ModelObject* tmp_object = tmp_model.objects.front();

    auto add_volumes_from_cut = [](Domain::ModelObject* object, bool keep_upper, const Domain::Model& tmp_model_for_cut) {
        const auto& volumes = tmp_model_for_cut.objects.front()->volumes;
        for (const Domain::ModelVolume* volume : volumes)
            if (volume->is_model_part()) {
                if ((keep_upper && volume->is_from_upper()) ||
                    (!keep_upper && !volume->is_from_upper())) {
                    Domain::ModelVolume* new_vol = object->add_volume(*volume);
                    new_vol->reset_from_upper();
                }
            }
    };

    auto cut = [this, add_volumes_from_cut]
                (Domain::ModelObject* object, const Transform3d& cut_matrix, bool keep_upper, Domain::Model& tmp_model_for_cut) {
        Cut cut(object, m_instance, cut_matrix);

        tmp_model_for_cut = Domain::Model();
        tmp_model_for_cut.add_object(*cut.perform_with_plane().front());
        assert(!tmp_model_for_cut.objects.empty());

        object->clear_volumes();
        add_volumes_from_cut(object, keep_upper, tmp_model_for_cut);
        reset_instance_transformation(object, m_instance);
    };

    // cut by upper plane

    const Transform3d cut_matrix_upper = translation_transform(rotation_m * (groove_half_depth * Vec3d::UnitZ())) * m_cut_matrix;
    {
        cut(tmp_object, cut_matrix_upper, false, tmp_model_for_cut);
        add_volumes_from_cut(upper, true, tmp_model_for_cut);
    }

    // cut by lower plane

    const Transform3d cut_matrix_lower = translation_transform(rotation_m * (-groove_half_depth * Vec3d::UnitZ())) * m_cut_matrix;
    {
        cut(tmp_object, cut_matrix_lower, true, tmp_model_for_cut);
        add_volumes_from_cut(lower, false, tmp_model_for_cut);
    }

    // cut middle part with 2 angles and add parts to related upper/lower objects

    const double h_side_shift = 0.5 * double(groove.width + groove.depth / tan(groove.flaps_angle));

    // cut by angle1 plane
    {
        const Transform3d cut_matrix_angle1 = translation_transform(rotation_m * (-h_side_shift * Vec3d::UnitX())) * m_cut_matrix * rotation_transform(Vec3d(0, -groove.flaps_angle, -groove.angle));

        cut(tmp_object, cut_matrix_angle1, false, tmp_model_for_cut);
        add_volumes_from_cut(lower, true, tmp_model_for_cut);
    }

    // cut by angle2 plane
    {
        const Transform3d cut_matrix_angle2 = translation_transform(rotation_m * (h_side_shift * Vec3d::UnitX())) * m_cut_matrix * rotation_transform(Vec3d(0, groove.flaps_angle, groove.angle));

        cut(tmp_object, cut_matrix_angle2, false, tmp_model_for_cut);
        add_volumes_from_cut(lower, true, tmp_model_for_cut);
    }

    // apply tolerance to the middle part
    {
        const double h_groove_shift_tolerance = groove_half_depth - (double)groove.depth_tolerance;

        const Transform3d cut_matrix_lower_tolerance = translation_transform(rotation_m * (-h_groove_shift_tolerance * Vec3d::UnitZ())) * m_cut_matrix;
        cut(tmp_object, cut_matrix_lower_tolerance, true, tmp_model_for_cut);

        const double h_side_shift_tolerance = h_side_shift - 0.5 * double(groove.width_tolerance);

        const Transform3d cut_matrix_angle1_tolerance = translation_transform(rotation_m * (-h_side_shift_tolerance * Vec3d::UnitX())) * m_cut_matrix * rotation_transform(Vec3d(0, -groove.flaps_angle, -groove.angle));
        cut(tmp_object, cut_matrix_angle1_tolerance, false, tmp_model_for_cut);

        const Transform3d cut_matrix_angle2_tolerance = translation_transform(rotation_m * (h_side_shift_tolerance * Vec3d::UnitX())) * m_cut_matrix * rotation_transform(Vec3d(0, groove.flaps_angle, groove.angle));
        cut(tmp_object, cut_matrix_angle2_tolerance, true, tmp_model_for_cut);
    }

    // this part can be added to the upper object now
    add_volumes_from_cut(upper, false, tmp_model_for_cut);

    Domain::ModelObjectPtrs cut_object_ptrs;

    if (keep_as_parts) {
        // add volumes from lower object to the upper, but mark them as a lower
        const auto& volumes = lower->volumes;
        for (const Domain::ModelVolume* volume : volumes) {
            Domain::ModelVolume* new_vol = upper->add_volume(*volume);
            new_vol->cut_info.is_from_upper = false;
        }

        // add modifiers
        for (const Domain::ModelVolume* volume : cut_mo->volumes)
            if (!volume->is_model_part()) {
                // Modifiers are not cut, but we still need to add the instance transformation
                // to the modifier volume transformation to preserve their shape properly.
                const auto modifier_trafo = Transformation(cut_mo->instances[m_instance]->get_transformation().get_matrix_no_offset() * volume->get_matrix());
                upper->add_volume(*volume)->set_transformation(modifier_trafo);
        }

        reset_instance_transformation(upper, m_instance, m_cut_matrix);
        reset_instance_transformation(lower, m_instance, m_cut_matrix);

        cut_object_ptrs.push_back(upper);

        // add lower object to the cut_object_ptrs just to correct delete it from the Model destructor and avoid memory leaks
        cut_object_ptrs.push_back(lower);
    }
    else {
        reset_instance_transformation(upper, m_instance, m_cut_matrix);
        reset_instance_transformation(lower, m_instance, m_cut_matrix);

        // Add modifiers if object has any
        // Note: make it after all transformations are reset for upper/lower object
        for (const Domain::ModelVolume* volume : cut_mo->volumes)
            if (!volume->is_model_part()) {
                distribute_modifiers_from_object(cut_mo, m_instance, upper, lower);
                break;
            }

        assert(!upper->volumes.empty() && !lower->volumes.empty());

        // Add Upper and Lower parts to cut_object_ptrs

        post_process(upper, lower, cut_object_ptrs);

        // Now merge all model parts together:
        merge_solid_parts_inside_object(cut_object_ptrs);
    }

    finalize(cut_object_ptrs);

    return m_model.objects;
}

} // namespace Slic3r

