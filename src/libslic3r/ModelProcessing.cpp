///|/ Copyright (c) Prusa Research 2016 - 2023 Tomáš Mészáros @tamasmeszaros, Oleksandra Iushchenko @YuSanka, David Kocík @kocikdav, Enrico Turri @enricoturri1966, Lukáš Matěna @lukasmatena, Vojtěch Bubník @bubnikv, Lukáš Hejl @hejllukas, Filip Sykala @Jony01, Vojtěch Král @vojtechkral
///|/ Copyright (c) 2021 Boleslaw Ciesielski
///|/ Copyright (c) 2019 John Drake @foxox
///|/ Copyright (c) 2019 Sijmen Schoon
///|/ Copyright (c) Slic3r 2014 - 2016 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2015 Maksim Derbasov @ntfshard
///|/
///|/ ported from lib/Slic3r/Model.pm:
///|/ Copyright (c) Prusa Research 2016 - 2022 Vojtěch Bubník @bubnikv, Enrico Turri @enricoturri1966
///|/ Copyright (c) Slic3r 2012 - 2016 Alessandro Ranellucci @alranel
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "Model.hpp"
#include "ModelProcessing.hpp"

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>

namespace Slic3r::ModelProcessing {

// Generate next extruder ID string, in the range of (1, max_extruders).
static inline int auto_extruder_id(unsigned int max_extruders, unsigned int& cntr)
{
    int out = ++cntr;
    if (cntr == max_extruders)
        cntr = 0;
    return out;
}

void convert_to_multipart_object(Model& model, unsigned int max_extruders)
{
    assert(model.objects.size() >= 2);
    if (model.objects.size() < 2)
        return;

    Model tmp_model = Model();
    tmp_model.add_object();

    ModelObject* object = tmp_model.objects[0];
    object->input_file = model.objects.front()->input_file;
    object->name = boost::filesystem::path(model.objects.front()->input_file).stem().string();
    //FIXME copy the config etc?

    unsigned int extruder_counter = 0;
    for (const ModelObject* o : model.objects)
        for (const ModelVolume* v : o->volumes) {
            // If there are more than one object, put all volumes together 
            // Each object may contain any number of volumes and instances
            // The volumes transformations are relative to the object containing them...
            Geometry::Transformation trafo_volume = v->get_transformation();
            // Revert the centering operation.
            trafo_volume.set_offset(trafo_volume.get_offset() - o->origin_translation);
            int counter = 1;
            auto copy_volume = [o, max_extruders, &counter, &extruder_counter](ModelVolume* new_v) {
                assert(new_v != nullptr);
                new_v->name = (counter > 1) ? o->name + "_" + std::to_string(counter++) : o->name;
                new_v->config.set("extruder", auto_extruder_id(max_extruders, extruder_counter));
                return new_v;
                };
            if (o->instances.empty()) {
                copy_volume(object->add_volume(*v))->set_transformation(trafo_volume);
            }
            else {
                for (const ModelInstance* i : o->instances)
                    // ...so, transform everything to a common reference system (world)
                    copy_volume(object->add_volume(*v))->set_transformation(i->get_transformation() * trafo_volume);
            }
        }

    // commented-out to fix #2868
//    object->add_instance();
//    object->instances[0]->set_offset(object->raw_mesh_bounding_box().center());

    model.clear_objects();
    model.add_object(*object);
}

void convert_from_imperial_units(Model& model, bool only_small_volumes)
{
    static constexpr const float in_to_mm = 25.4f;
    for (ModelObject* obj : model.objects)
        if (!only_small_volumes || get_object_mesh_stats(obj).volume < volume_threshold_inches) {
            obj->scale_mesh_after_creation(in_to_mm);
            for (ModelVolume* v : obj->volumes) {
                assert(!v->source.is_converted_from_meters);
                v->source.is_converted_from_inches = true;
            }
        }
}

void convert_from_imperial_units(ModelVolume* volume)
{
    assert(!volume->source.is_converted_from_meters);
    volume->scale_geometry_after_creation(25.4f);
    volume->set_offset(Vec3d(0, 0, 0));
    volume->source.is_converted_from_inches = true;
}

void convert_from_meters(Model& model, bool only_small_volumes)
{
    static constexpr const double m_to_mm = 1000;
    for (ModelObject* obj : model.objects)
        if (!only_small_volumes || get_object_mesh_stats(obj).volume < volume_threshold_meters) {
            obj->scale_mesh_after_creation(m_to_mm);
            for (ModelVolume* v : obj->volumes) {
                assert(!v->source.is_converted_from_inches);
                v->source.is_converted_from_meters = true;
            }
        }
}

void convert_from_meters(ModelVolume* volume)
{
    assert(!volume->source.is_converted_from_inches);
    volume->scale_geometry_after_creation(1000.f);
    volume->set_offset(Vec3d(0, 0, 0));
    volume->source.is_converted_from_meters = true;
}

void convert_units(Model& model_to, ModelObject* object_from, ConversionType conv_type, std::vector<int> volume_idxs)
{
    BOOST_LOG_TRIVIAL(trace) << "ModelObject::convert_units - start";

    float koef = conv_type == ConversionType::CONV_FROM_INCH ? 25.4f   : conv_type == ConversionType::CONV_TO_INCH ? 0.0393700787f :
                 conv_type == ConversionType::CONV_FROM_METER ? 1000.f : conv_type == ConversionType::CONV_TO_METER ? 0.001f : 1.f;

    ModelObject* new_object = model_to.add_object(*object_from);
    new_object->sla_support_points.clear();
    new_object->sla_drain_holes.clear();
    new_object->sla_points_status = sla::PointsStatus::NoPoints;
    new_object->clear_volumes();
    new_object->input_file.clear();

    int vol_idx = 0;
    for (ModelVolume* volume : object_from->volumes) {
        if (!volume->mesh().empty()) {
            TriangleMesh mesh(volume->mesh());

            ModelVolume* vol = new_object->add_volume(mesh);
            vol->name = volume->name;
            vol->set_type(volume->type());
            // Don't copy the config's ID.
            vol->config.assign_config(volume->config);
            assert(vol->config.id().valid());
            assert(vol->config.id() != volume->config.id());
            vol->set_material(volume->material_id(), *volume->material());
            vol->source.input_file = volume->source.input_file;
            vol->source.object_idx = (int)model_to.objects.size()-1;
            vol->source.volume_idx = vol_idx;
            vol->source.is_converted_from_inches = volume->source.is_converted_from_inches;
            vol->source.is_converted_from_meters = volume->source.is_converted_from_meters;
            vol->source.is_from_builtin_objects = volume->source.is_from_builtin_objects;

            vol->supported_facets.assign(volume->supported_facets);
            vol->seam_facets.assign(volume->seam_facets);
            vol->mm_segmentation_facets.assign(volume->mm_segmentation_facets);

            // Perform conversion only if the target "imperial" state is different from the current one.
            // This check supports conversion of "mixed" set of volumes, each with different "imperial" state.
            if (//vol->source.is_converted_from_inches != from_imperial && 
                (volume_idxs.empty() ||
                    std::find(volume_idxs.begin(), volume_idxs.end(), vol_idx) != volume_idxs.end())) {
                vol->scale_geometry_after_creation(koef);
                vol->set_offset(Vec3d(koef, koef, koef).cwiseProduct(volume->get_offset()));
                if (conv_type == ConversionType::CONV_FROM_INCH || conv_type == ConversionType::CONV_TO_INCH)
                    vol->source.is_converted_from_inches = conv_type == ConversionType::CONV_FROM_INCH;
                if (conv_type == ConversionType::CONV_FROM_METER || conv_type == ConversionType::CONV_TO_METER)
                    vol->source.is_converted_from_meters = conv_type == ConversionType::CONV_FROM_METER;
                assert(!vol->source.is_converted_from_inches || !vol->source.is_converted_from_meters);
            }
            else
                vol->set_offset(volume->get_offset());
        }
        vol_idx++;
    }
    new_object->invalidate_bounding_box();

    BOOST_LOG_TRIVIAL(trace) << "ModelObject::convert_units - end";
}

TriangleMeshStats get_object_mesh_stats(const ModelObject* object)
{
    TriangleMeshStats full_stats;
    full_stats.volume = 0.f;

    // fill full_stats from all objet's meshes
    for (ModelVolume* volume : object->volumes)
    {
        const TriangleMeshStats& stats = volume->mesh().stats();

        // initialize full_stats (for repaired errors)
        full_stats.open_edges += stats.open_edges;
        full_stats.repaired_errors.merge(stats.repaired_errors);

        // another used satistics value
        if (volume->is_model_part()) {
            Transform3d trans = object->instances.empty() ? volume->get_matrix() : (volume->get_matrix() * object->instances[0]->get_matrix());
            full_stats.volume += stats.volume * std::fabs(trans.matrix().block(0, 0, 3, 3).determinant());
            full_stats.number_of_parts += stats.number_of_parts;
        }
    }

    return full_stats;
}

int get_repaired_errors_count(const ModelVolume* volume)
{
    const RepairedMeshErrors& errors = volume->mesh().stats().repaired_errors;
    return  errors.degenerate_facets + 
            errors.edges_fixed + 
            errors.facets_removed +
            errors.facets_reversed + 
            errors.backwards_edges;
}

int get_repaired_errors_count(const ModelObject* object, const int vol_idx /*= -1*/)
{
    if (vol_idx >= 0)
        return get_repaired_errors_count(object->volumes[vol_idx]);

    const RepairedMeshErrors& errors = get_object_mesh_stats(object).repaired_errors;
    return  errors.degenerate_facets + 
            errors.edges_fixed + 
            errors.facets_removed +
            errors.facets_reversed + 
            errors.backwards_edges;
}



/// <summary>
/// Compare TriangleMeshes by Bounding boxes (mainly for sort)
/// From Front(Z) Upper(Y) TopLeft(X) corner.
/// 1. Seraparate group not overlaped i Z axis
/// 2. Seraparate group not overlaped i Y axis
/// 3. Start earlier in X (More on left side)
/// </summary>
/// <param name="triangle_mesh1">Compare from</param>
/// <param name="triangle_mesh2">Compare to</param>
/// <returns>True when triangle mesh 1 is closer, upper or lefter than triangle mesh 2 other wise false</returns>
static bool is_front_up_left(const TriangleMesh &trinagle_mesh1, const TriangleMesh &triangle_mesh2)
{
    // stats form t1
    const Vec3f &min1 = trinagle_mesh1.stats().min;
    const Vec3f &max1 = trinagle_mesh1.stats().max;
    // stats from t2
    const Vec3f &min2 = triangle_mesh2.stats().min;
    const Vec3f &max2 = triangle_mesh2.stats().max;
    // priority Z, Y, X
    for (int axe = 2; axe > 0; --axe) {
        if (max1[axe] < min2[axe])
            return true;
        if (min1[axe] > max2[axe])
            return false;
    }
    return min1.x() < min2.x();
}

// Split this volume, append the result to the object owning this volume.
// Return the number of volumes created from this one.
// This is useful to assign different materials to different volumes of an object.
size_t split(ModelVolume* volume, unsigned int max_extruders)
{
    std::vector<TriangleMesh> meshes = volume->mesh().split();
    if (meshes.size() <= 1)
        return 1;

    std::sort(meshes.begin(), meshes.end(), is_front_up_left);

    // splited volume should not be text object
    if (volume->text_configuration.has_value())
        volume->text_configuration.reset();

    ModelObject* object = volume->get_object();

    size_t idx = 0;
    size_t ivolume = std::find(object->volumes.begin(), object->volumes.end(), volume) - object->volumes.begin();
    const std::string& name = volume->name;

    unsigned int extruder_counter = 0;
    const Vec3d offset = volume->get_offset();

    for (TriangleMesh &mesh : meshes) {
        if (mesh.empty() || mesh.has_zero_volume())
            // Repair may have removed unconnected triangles, thus emptying the mesh.
            continue;

        if (idx == 0) {
            volume->set_mesh(std::move(mesh));
            volume->calculate_convex_hull();
            // Assign a new unique ID, so that a new GLVolume will be generated.
            volume->set_new_unique_id();
            // reset the source to disable reload from disk
            volume->source = ModelVolume::Source();
        }
        else
            object->insert_volume((++ivolume), *volume, std::move(mesh));//object->volumes.insert(object->volumes.begin() + (++ivolume), new ModelVolume(object, *volume, std::move(mesh)));

        object->volumes[ivolume]->set_offset(Vec3d::Zero());
        object->volumes[ivolume]->center_geometry_after_creation();
        object->volumes[ivolume]->translate(offset);
        object->volumes[ivolume]->name = name + "_" + std::to_string(idx + 1);
        object->volumes[ivolume]->config.set("extruder", auto_extruder_id(max_extruders, extruder_counter));
        object->volumes[ivolume]->discard_splittable();
        ++ idx;
    }

    // discard volumes for which the convex hull was not generated or is degenerate
    size_t i = 0;
    while (i < object->volumes.size()) {
        const std::shared_ptr<const TriangleMesh> &hull = object->volumes[i]->get_convex_hull_shared_ptr();
        if (hull == nullptr || hull->its.vertices.empty() || hull->its.indices.empty()) {
            object->delete_volume(i);
            --idx;
            --i;
        }
        ++i;
    }

    return idx;
}

void split(ModelObject* object, ModelObjectPtrs* new_objects)
{
    for (ModelVolume* volume : object->volumes) {
        if (volume->type() != ModelVolumeType::MODEL_PART)
            continue;

        // splited volume should not be text object 
        if (volume->text_configuration.has_value())
            volume->text_configuration.reset();

        std::vector<TriangleMesh> meshes = volume->mesh().split();
        std::sort(meshes.begin(), meshes.end(), is_front_up_left);

        size_t counter = 1;
        for (TriangleMesh &mesh : meshes) {
            // FIXME: crashes if not satisfied
            if (mesh.facets_count() < 3 || mesh.has_zero_volume())
                continue;

            // XXX: this seems to be the only real usage of m_model, maybe refactor this so that it's not needed?
            ModelObject* new_object = object->get_model()->add_object();
            if (meshes.size() == 1) {
                new_object->name = volume->name;
                // Don't copy the config's ID.
                new_object->config.assign_config(object->config.size() > 0 ? object->config : volume->config);
            }
            else {
                new_object->name = object->name + (meshes.size() > 1 ? "_" + std::to_string(counter++) : "");
                // Don't copy the config's ID.
                new_object->config.assign_config(object->config);
            }
            assert(new_object->config.id().valid());
            assert(new_object->config.id() != object->config.id());
            new_object->instances.reserve(object->instances.size());
            for (const ModelInstance* model_instance : object->instances)
                new_object->add_instance(*model_instance);
            ModelVolume* new_vol = new_object->add_volume(*volume, std::move(mesh));

            // Invalidate extruder value in volume's config,
            // otherwise there will no way to change extruder for object after splitting,
            // because volume's extruder value overrides object's extruder value.
            if (new_vol->config.has("extruder"))
                new_vol->config.set_key_value("extruder", new ConfigOptionInt(0));

            for (ModelInstance* model_instance : new_object->instances) {
                const Vec3d shift = model_instance->get_transformation().get_matrix_no_offset() * new_vol->get_offset();
                model_instance->set_offset(model_instance->get_offset() + shift);
            }

            new_vol->set_offset(Vec3d::Zero());
            // reset the source to disable reload from disk
            new_vol->source = ModelVolume::Source();
            new_objects->emplace_back(new_object);
        }
    }
}

void merge(ModelObject* object)
{
    if (object->volumes.size() == 1) {
        // We can't merge meshes if there's just one volume
        return;
    }

    TriangleMesh mesh;

    for (ModelVolume* volume : object->volumes)
        if (!volume->mesh().empty())
            mesh.merge(volume->mesh());

    object->clear_volumes();
    ModelVolume* vol = object->add_volume(mesh);

    if (!vol)
        return;
}

}
