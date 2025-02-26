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
#include "FileReader.hpp"
#include "Model.hpp"
#include "ModelProcessing.hpp"
#include "TriangleMesh.hpp"

#include "Format/AMF.hpp"
#include "Format/OBJ.hpp"
#include "Format/STL.hpp"
#include "Format/3mf.hpp"
#include "Format/STEP.hpp"
#include "Format/SVG.hpp"
#include "Format/PrintRequest.hpp"

#include <boost/filesystem.hpp>

#include "I18N.hpp"

namespace Slic3r::FileReader
{

bool is_project_file(const std::string& input_file)
{
    return boost::algorithm::iends_with(input_file, ".3mf") || boost::algorithm::iends_with(input_file, ".zip");
} 

// Loading model from a file, it may be a simple geometry file as STL or OBJ, however it may be a project file as well.
static Model read_model_from_file(const std::string& input_file, LoadAttributes options)
{
    Model model;

    DynamicPrintConfig temp_config;
    ConfigSubstitutionContext temp_config_substitutions_context(ForwardCompatibilitySubstitutionRule::EnableSilent);

    bool result = false;
    if (boost::algorithm::iends_with(input_file, ".stl"))
        result = load_stl(input_file.c_str(), &model);
    else if (boost::algorithm::iends_with(input_file, ".obj"))
        result = load_obj(input_file.c_str(), &model);
    else if (boost::algorithm::iends_with(input_file, ".step") || boost::algorithm::iends_with(input_file, ".stp"))
        result = load_step(input_file.c_str(), &model);
    else if (boost::algorithm::iends_with(input_file, ".amf") || boost::algorithm::iends_with(input_file, ".amf.xml"))
//?        result = load_amf(input_file.c_str(), &temp_config, &temp_config_substitutions_context, &model, options & LoadAttribute::CheckVersion);
//? LoadAttribute::CheckVersion is needed here, when we loading just a geometry
        result = load_amf(input_file.c_str(), &temp_config, &temp_config_substitutions_context, &model, false); 
    else if (boost::algorithm::iends_with(input_file, ".3mf") || boost::algorithm::iends_with(input_file, ".zip")) {
        //FIXME options & LoadAttribute::CheckVersion ? 
        boost::optional<Semver> prusaslicer_generator_version;
        result = load_3mf(input_file.c_str(), temp_config, temp_config_substitutions_context, &model, false, prusaslicer_generator_version);
    } else if (boost::algorithm::iends_with(input_file, ".svg"))
        result = load_svg(input_file, model);
    else if (boost::ends_with(input_file, ".printRequest"))
        result = load_printRequest(input_file.c_str(), &model);
    else
        throw Slic3r::RuntimeError(L("Unknown file format. Input file must have .stl, .obj, .step/.stp, .svg, .amf(.xml) or extension .3mf(.zip)."));

    if (!result)
        throw Slic3r::RuntimeError(L("Loading of a model file failed."));

    if (model.objects.empty())
        throw Slic3r::RuntimeError(L("The supplied file couldn't be read because it's empty"));

    if (!boost::ends_with(input_file, ".printRequest"))
        for (ModelObject* o : model.objects)
            o->input_file = input_file;

    if (options & LoadAttribute::AddDefaultInstances)
        model.add_default_instances();

    return model;
}

// Loading model from a file, it may be a simple geometry file as STL or OBJ, however it may be a project file as well.
static Model read_all_from_file(const std::string& input_file,
                                DynamicPrintConfig* config,
                                ConfigSubstitutionContext* config_substitutions,
                                boost::optional<Semver> &prusaslicer_generator_version,
                                LoadAttributes options)
{
    assert(is_project_file(input_file));
    assert(config != nullptr);
    assert(config_substitutions != nullptr);

    Model model;

    bool result = false;
    if (is_project_file(input_file))
        result = load_3mf(input_file.c_str(), *config, *config_substitutions, &model, options & LoadAttribute::CheckVersion, prusaslicer_generator_version);
    else
        throw Slic3r::RuntimeError(L("Unknown file format. Input file must have .3mf extension."));

    if (!result)
        throw Slic3r::RuntimeError(L("Loading of a model file failed."));

    if (model.objects.empty())
        throw Slic3r::RuntimeError(L("The supplied file couldn't be read because it's empty"));

    for (ModelObject* o : model.objects)
        o->input_file = input_file;

    if (options & LoadAttribute::AddDefaultInstances)
        model.add_default_instances();

    for (CustomGCode::Info& info : model.get_custom_gcode_per_print_z_vector()) {
        CustomGCode::update_custom_gcode_per_print_z_from_config(info, config);
        CustomGCode::check_mode_for_custom_gcode_per_print_z(info);
    }
    sort_remove_duplicates(config_substitutions->substitutions);
    return model;
}

TriangleMesh load_mesh(const std::string& input_file)
{
    Model model;
    try {
        model = read_model_from_file(input_file, LoadAttribute::AddDefaultInstances);
    }
    catch (std::exception&) {
        throw Slic3r::RuntimeError(L("Error! Invalid model"));
    }

    return model.mesh();
}

static bool looks_like_multipart_object(const Model& model)
{
    if (model.objects.size() <= 1)
        return false;

    BoundingBoxf3 tbb;

    for (const ModelObject* obj : model.objects) {
        if (obj->volumes.size() > 1 || obj->config.keys().size() > 1)
            return false;

        BoundingBoxf3 bb_this = obj->volumes[0]->mesh().bounding_box();

        // FIXME: There is sadly the case when instances are empty (AMF files). The normalization of instances in that
        // case is performed only after this function is called. For now (shortly before the 2.7.2 release, let's
        // just do this non-invasive check. Reordering all the functions could break it much more.
        BoundingBoxf3 tbb_this = (!obj->instances.empty() ? obj->instances[0]->transform_bounding_box(bb_this) : bb_this);

        if (!tbb.defined)
            tbb = tbb_this;
        else if (tbb.intersects(tbb_this) || tbb.shares_boundary(tbb_this))
            // The volumes has intersects bounding boxes or share some boundary
            return true;
    }
    return false;
}

static bool looks_like_imperial_units(const Model& model)
{
    if (model.objects.empty())
        return false;

    for (ModelObject* obj : model.objects)
        if (ModelProcessing::get_object_mesh_stats(obj).volume < ModelProcessing::volume_threshold_inches) {
            if (!obj->is_cut())
                return true;
            bool all_cut_parts_look_like_imperial_units = true;
            for (ModelObject* obj_other : model.objects) {
                if (obj_other == obj)
                    continue;
                if (obj_other->cut_id.is_equal(obj->cut_id) && ModelProcessing::get_object_mesh_stats(obj_other).volume >= ModelProcessing::volume_threshold_inches) {
                    all_cut_parts_look_like_imperial_units = false;
                    break;
                }
            }
            if (all_cut_parts_look_like_imperial_units)
                return true;
        }

    return false;
}

static bool looks_like_saved_in_meters(const Model& model)
{
    if (model.objects.size() == 0)
        return false;

    for (ModelObject* obj : model.objects)
        if (ModelProcessing::get_object_mesh_stats(obj).volume < ModelProcessing::volume_threshold_meters)
            return true;

    return false;
}

static constexpr const double zero_volume = 0.0000000001;

static int removed_objects_with_zero_volume(Model& model)
{
    if (model.objects.size() == 0)
        return 0;

    int removed = 0;
    for (int i = int(model.objects.size()) - 1; i >= 0; i--)
        if (ModelProcessing::get_object_mesh_stats(model.objects[i]).volume < zero_volume) {
            model.delete_object(size_t(i));
            removed++;
        }
    return removed;
}

Model load_model(const std::string& input_file,
                 LoadAttributes options/* = LoadAttribute::AddDefaultInstances*/, 
                 LoadStats* stats/*= nullptr*/)
{
    Model model = read_model_from_file(input_file, options);

    for (auto obj : model.objects)
        if (obj->name.empty())
            obj->name = boost::filesystem::path(obj->input_file).filename().string();

    if (stats) {
        // 3mf contains information about units, so there is no need to detect possible convertions for these files
        bool from_3mf = boost::algorithm::iends_with(input_file, ".3mf") || boost::algorithm::iends_with(input_file, ".zip");

        stats->deleted_objects_cnt          = removed_objects_with_zero_volume(model);
        stats->looks_like_multipart_object  = looks_like_multipart_object(model);
        stats->looks_like_saved_in_meters   = !from_3mf && looks_like_saved_in_meters(model);
        stats->looks_like_imperial_units    = !from_3mf && looks_like_imperial_units(model);
    }

    return model;
}

Model load_model_with_config(const std::string& input_file, 
                             DynamicPrintConfig* config, 
                             ConfigSubstitutionContext* config_substitutions,
                             boost::optional<Semver>& prusaslicer_generator_version,
                             LoadAttributes options, 
                             LoadStats* stats)
{
    Model model = read_all_from_file(input_file, config, config_substitutions, prusaslicer_generator_version, options);

    if (stats && !model.mesh().empty()) {
        stats->deleted_objects_cnt          = removed_objects_with_zero_volume(model);
        stats->looks_like_multipart_object  = looks_like_multipart_object(model);
    }

    return model;
}

}
