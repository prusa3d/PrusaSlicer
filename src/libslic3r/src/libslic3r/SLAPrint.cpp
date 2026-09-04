#include "libslic3r/SLAPrint.hpp"
#include "libslic3r/SLAPrintSteps.hpp" // IWYU pragma: keep
#include "libslic3r/CSGMesh/CSGMeshCopy.hpp"
#include "libslic3r/CSGMesh/PerformCSGMeshBooleans.hpp"

#include "libslic3r/Geometry.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Thread.hpp"

#include <tbb/parallel_for.h>
#include <boost/filesystem/path.hpp>
#include <Slic3r/Assert.hpp>
#include <Slic3r/Log.hpp>

#include "Slic3r/Biz/Parser/IO.hpp"
#include "libslic3r/ModelUtils.hpp"
#include "libslic3r/Utils.hpp"
#include "Slic3r/Time.hpp"

#include <libslic3r/SLAResult.hpp>

#include <libslic3r/Format/SL1.hpp>
#include <boost/algorithm/string.hpp>
#include "Slic3r/Biz/Parser/PlaceholderParser.hpp"
#include "Slic3r/Biz/Algorithms/Scaling.hpp"
#include "Slic3r/Exception.hpp"
#include "libslic3r/InstanceTransformations.hpp"

#include "libslic3r/ModelUtils.hpp"

// #define SLAPRINT_DO_BENCHMARK

#ifdef SLAPRINT_DO_BENCHMARK
#include <libnest2d/tools/benchmark.h>
#endif

#include "libslic3r/I18N_private.hpp"

namespace Slic3r {

using Biz::Parser::PlaceholderParser;
using Domain::ObjectID;
using Domain::SLA::PointsStatus;
using SLASlicingSync::AllOrSome;
using SLASlicingSync::AllSteps;
using SLASlicingSync::InvalidatedSteps;
using SLASlicingSync::PrintAndObjectSteps;
using SLASlicingSync::PrintObjectSteps;
using SLASlicingSync::PrintSteps;
using SLASlicingSync::StepsPerPrintObject;
using ParserConfig = Biz::Parser::IO::Config;
using Biz::Algorithms::Scaling::unscaled;
using Domain::ConfigPack;
using Domain::ConfigPackSLA;
using Domain::FullConfigSLA;
using Domain::FullConfigSLAPtr;
using Domain::PartialObjectConfigSLA;
using Domain::Percentage;
using Domain::Transform3d;
using InstanceTrafos = Biz::Slicing::Sla::Object::InstanceTrafos;
using Slic3r::Biz::Slicing::IThumbnailImageGenerator;
using Slic3r::Biz::Slicing::SliceUntilStep;
using Slic3r::Domain::SlicingId;

bool is_zero_elevation(const SLAPrintObjectConfigView &c)
{
    return c.get<bool>("pad_enable") && c.get<bool>("pad_around_object");
}

// Compile the argument for support creation from the static print config.
sla::SupportTreeConfig make_support_cfg(const SLAPrintObjectConfigView& c)
{
    sla::SupportTreeConfig scfg;

    scfg.enabled = c.get<bool>("supports_enable");
    scfg.tree_type = c.get<Domain::sla::SupportTreeType>("support_tree_type");

    switch(scfg.tree_type) {
    case Domain::sla::SupportTreeType::Default: {
        scfg.head_front_radius_mm = 0.5*c.get<double>("support_head_front_diameter");
        double pillar_r = 0.5 * c.get<double>("support_pillar_diameter");
        scfg.head_back_radius_mm = pillar_r;
        scfg.head_fallback_radius_mm =
            c.get<Percentage>("support_small_pillar_diameter_percent").get_abs_value(1.0) * pillar_r;
        scfg.head_penetration_mm = c.get<double>("support_head_penetration");
        scfg.head_width_mm = c.get<double>("support_head_width");
        scfg.object_elevation_mm = is_zero_elevation(c) ?
                                       0. : c.get<double>("support_object_elevation");
        scfg.bridge_slope = c.get<double>("support_critical_angle") * PI / 180.0 ;
        scfg.max_bridge_length_mm = c.get<double>("support_max_bridge_length");
        scfg.max_pillar_link_distance_mm = c.get<double>("support_max_pillar_link_distance");
        scfg.pillar_connection_mode = c.get<Domain::sla::PillarConnectionMode>("support_pillar_connection_mode");
        scfg.ground_facing_only = c.get<bool>("support_buildplate_only");
        scfg.pillar_widening_factor = c.get<double>("support_pillar_widening_factor");
        scfg.base_radius_mm = 0.5*c.get<double>("support_base_diameter");
        scfg.base_height_mm = c.get<double>("support_base_height");
        scfg.pillar_base_safety_distance_mm =
            c.get<double>("support_base_safety_distance") < EPSILON ?
                scfg.safety_distance_mm : c.get<double>("support_base_safety_distance");

        scfg.max_bridges_on_pillar = unsigned(c.get<int>("support_max_bridges_on_pillar"));
        scfg.max_weight_on_model_support = c.get<double>("support_max_weight_on_model");
        break;
    }
    case Domain::sla::SupportTreeType::Branching:
        [[fallthrough]];
    case Domain::sla::SupportTreeType::Organic:{
        scfg.head_front_radius_mm = 0.5*c.get<double>("branchingsupport_head_front_diameter");
        double pillar_r = 0.5 * c.get<double>("branchingsupport_pillar_diameter");
        scfg.head_back_radius_mm = pillar_r;
        scfg.head_fallback_radius_mm =
            0.01 * c.get<double>("branchingsupport_small_pillar_diameter_percent") * pillar_r;
        scfg.head_penetration_mm = c.get<double>("branchingsupport_head_penetration");
        scfg.head_width_mm = c.get<double>("branchingsupport_head_width");
        scfg.object_elevation_mm = is_zero_elevation(c) ?
                                       0. : c.get<double>("branchingsupport_object_elevation");
        scfg.bridge_slope = c.get<double>("branchingsupport_critical_angle") * PI / 180.0 ;
        scfg.max_bridge_length_mm = c.get<double>("branchingsupport_max_bridge_length");
        scfg.max_pillar_link_distance_mm = c.get<double>("branchingsupport_max_pillar_link_distance");
        scfg.pillar_connection_mode = c.get<Domain::sla::PillarConnectionMode>("branchingsupport_pillar_connection_mode");
        scfg.ground_facing_only = c.get<bool>("branchingsupport_buildplate_only");
        scfg.pillar_widening_factor = c.get<double>("branchingsupport_pillar_widening_factor");
        scfg.base_radius_mm = 0.5*c.get<double>("branchingsupport_base_diameter");
        scfg.base_height_mm = c.get<double>("branchingsupport_base_height");
        scfg.pillar_base_safety_distance_mm =
            c.get<double>("branchingsupport_base_safety_distance") < EPSILON ?
                scfg.safety_distance_mm : c.get<double>("branchingsupport_base_safety_distance");

        scfg.max_bridges_on_pillar = unsigned(c.get<int>("branchingsupport_max_bridges_on_pillar"));
        scfg.max_weight_on_model_support = c.get<double>("branchingsupport_max_weight_on_model");
        break;
    }
    }

    return scfg;
}

sla::PadConfig::EmbedObject builtin_pad_cfg(const SLAPrintObjectConfigView& c)
{
    sla::PadConfig::EmbedObject ret;

    ret.enabled = is_zero_elevation(c);

    if(ret.enabled) {
        ret.everywhere           = c.get<bool>("pad_around_object_everywhere");
        ret.object_gap_mm        = c.get<double>("pad_object_gap");
        ret.stick_width_mm       = c.get<double>("pad_object_connector_width");
        ret.stick_stride_mm      = c.get<double>("pad_object_connector_stride");
        ret.stick_penetration_mm = c.get<double>("pad_object_connector_penetration");
    }

    return ret;
}

sla::PadConfig make_pad_cfg(const SLAPrintObjectConfigView& c)
{
    sla::PadConfig pcfg;

    pcfg.wall_thickness_mm = c.get<double>("pad_wall_thickness");
    pcfg.wall_slope = c.get<double>("pad_wall_slope") * PI / 180.0;

    pcfg.max_merge_dist_mm = c.get<double>("pad_max_merge_distance");
    pcfg.wall_height_mm = c.get<double>("pad_wall_height");
    pcfg.brim_size_mm = c.get<double>("pad_brim_size");

    // set builtin pad implicitly ON
    pcfg.embed_object = builtin_pad_cfg(c);

    return pcfg;
}

bool validate_pad(const indexed_triangle_set &pad, const sla::PadConfig &pcfg)
{
    // An empty pad can only be created if embed_object mode is enabled
    // and the pad is not forced everywhere
    return !pad.empty() || (pcfg.embed_object.enabled && !pcfg.embed_object.everywhere);
}

SLAPrint::SLAPrint(const OnSlaResult& on_sla_result, const OnSlaObject& on_sla_object)
    : m_on_sla_result(on_sla_result)
    , m_on_sla_object(on_sla_object)
{}

void SLAPrint::clear()
{
    std::scoped_lock<std::mutex> lock(this->state_mutex());
    // The following call should stop background processing if it is running.
    this->invalidate_all_steps();
    for (SLAPrintObject *object : m_objects)
        delete object;
    m_objects.clear();
    m_model.clear_objects();
}

// Transformation without rotation around Z and without a shift by X and Y.
Transform3d SLAPrint::sla_trafo(const Domain::ModelObject &model_object) const
{
    Domain::ModelInstance &model_instance = *model_object.instances.front();
    auto trafo = Transform3d::Identity();
    trafo.translate(Vec3d{ 0., 0., model_instance.get_offset().z() * this->relative_correction().z() });
    trafo.linear() = Eigen::DiagonalMatrix<double, 3, 3>(this->relative_correction()) * model_instance.get_matrix().linear();
    if (model_instance.is_left_handed())
        trafo = Eigen::Scaling(Vec3d(-1., 1., 1.)) * trafo;
    return trafo;
}

namespace {
// Transformation without rotation around Z and without a shift by X and Y.
Transform3d sla_trafo(const Domain::ModelObject &model_object, const Vec3d& relative_correction)
{
    Domain::ModelInstance &model_instance = *model_object.instances.front();
    auto trafo = Transform3d::Identity();
    trafo.translate(Vec3d{ 0., 0., model_instance.get_offset().z() * relative_correction.z() });
    trafo.linear() = Eigen::DiagonalMatrix<double, 3, 3>(relative_correction) * model_instance.get_matrix().linear();
    if (model_instance.is_left_handed())
        trafo = Eigen::Scaling(Vec3d(-1., 1., 1.)) * trafo;
    return trafo;
}
}

// List of instances, where the ModelInstance transformation is a composite of sla_trafo and the transformation defined by SLAPrintObject::Instance.
static std::vector<SLAPrintObject::Instance> sla_instances(const Domain::ModelObject &model_object)
{
    std::vector<SLAPrintObject::Instance> instances;
    assert(! model_object.instances.empty());
    if (! model_object.instances.empty()) {
        const Transform3d& trafo0 = model_object.instances.front()->get_matrix();
        for (Domain::ModelInstance *model_instance : model_object.instances) {
            instances.emplace_back(
                model_instance->id(),
                scaled(Vec2d(model_instance->get_offset(X), model_instance->get_offset(Y))),
                float(Geometry::rotation_diff_z(trafo0, model_instance->get_matrix())));
        }
    }
    return instances;
}

std::vector<Domain::ObjectID> SLAPrint::print_object_ids() const
{ 
    std::vector<Domain::ObjectID> out;
    // Reserve one more for the caller to append the ID of the Print itself.
    out.reserve(m_objects.size() + 1);
    for (const SLAPrintObject *print_object : m_objects)
        out.emplace_back(print_object->id());
    return out;
}

namespace {

using StepsPerModelObjectId = std::map<ObjectID, std::set<SLAPrintObjectStep>>;

template <typename Set>
AllOrSome<Set> merge (const AllOrSome<Set>& a, const AllOrSome<Set>& b) {
    if (std::holds_alternative<AllSteps>(a) || std::holds_alternative<AllSteps>(b)) {
        return AllSteps{};
    }

    Set values_a{std::get<Set>(a)};
    Set values_b{std::get<Set>(b)};
    values_a.merge(values_b);
    return values_a;
}
template AllOrSome<PrintSteps> merge(const AllOrSome<PrintSteps>& a, const AllOrSome<PrintSteps>& b);
template AllOrSome<PrintObjectSteps> merge(const AllOrSome<PrintObjectSteps>& a, const AllOrSome<PrintObjectSteps>& b);

template <typename Set>
AllOrSome<Set> merge(const std::vector<AllOrSome<Set>>& steps) {
    AllOrSome<Set> result;
    for (const AllOrSome<Set>& set : steps) {
        result = merge(result, set);
    }
    return result;
}

StepsPerPrintObject merge(const StepsPerPrintObject& a, const StepsPerPrintObject& b) {
    StepsPerPrintObject result{a};
    StepsPerPrintObject same_key_elements{b};

    result.merge(same_key_elements);

    for (const auto& [print_object, invalidated_steps] : same_key_elements) {
        result.at(print_object) = merge(result.at(print_object), invalidated_steps);
    }

    return result;
}

InvalidatedSteps merge(const InvalidatedSteps& a, const InvalidatedSteps& b) {
    return {merge(a.print, b.print), merge(a.object, b.object)};
}


InvalidatedSteps merge(const std::vector<InvalidatedSteps>& invalidated_steps) {
    InvalidatedSteps result;
    for (const InvalidatedSteps& steps : invalidated_steps) {
        result = merge(result, steps);
    }
    return result;
}


struct ModelObjectsSyncResult {
    Domain::ModelObjectPtrs objects;
    std::map<ObjectID, SLAPrintObject*> reuse_candidates;
    InvalidatedSteps invalidated_steps;
};

ModelObjectsSyncResult sync_model_objects(
    const Domain::ModelObjectPtrs& new_objects,
    const std::map<ObjectID, Domain::ModelObject*>& reuse_candidates,
    const PrintObjects& old_objects,
    const Vec3d& relative_correction
)
{
    Domain::ModelObjectPtrs objects;
    StepsPerModelObjectId reused_objects;

    for (Domain::ModelObject* model_object_new : new_objects) {
        const auto current_object_it{reuse_candidates.find(model_object_new->id())};

        if (current_object_it == reuse_candidates.end()) {
            objects.push_back(Domain::ModelObject::new_copy(*model_object_new));
            continue;
        }

        Domain::ModelObject* model_object{current_object_it->second};

        // Check whether a model part volume was added or removed, their transformations or order changed.
        const bool model_parts_differ{model_volume_list_changed(
            *model_object,
            *model_object_new,
            {Domain::ModelVolumeType::MODEL_PART,
             Domain::ModelVolumeType::NEGATIVE_VOLUME,
             Domain::ModelVolumeType::SUPPORT_ENFORCER,
             Domain::ModelVolumeType::SUPPORT_BLOCKER}
        )};

        const bool sla_trafo_differs = model_object->instances.empty() != model_object_new->instances.empty()
            || (!model_object->instances.empty() && (!sla_trafo(*model_object, relative_correction).isApprox(sla_trafo(*model_object_new, relative_correction))
            || model_object->instances.front()->is_left_handed() != model_object_new->instances.front()->is_left_handed()));

        if (model_parts_differ || sla_trafo_differs) {
            objects.push_back(Domain::ModelObject::new_copy(*model_object_new));
            continue;
        }

        std::set<SLAPrintObjectStep> invalidated_steps;

        const bool old_user_modified = model_object->sla_points_status
            == PointsStatus::UserModified;
        const bool new_user_modified = model_object_new->sla_points_status
            == PointsStatus::UserModified;

        const bool supports_equal{
            model_object->sla_support_points == model_object_new->sla_support_points
        };

        const bool switching_to_auto_from_man{old_user_modified && !new_user_modified};
        const bool switching_to_man_from_auto{!old_user_modified && new_user_modified};

        if (switching_to_auto_from_man
            || switching_to_man_from_auto
            || (new_user_modified && !supports_equal)) {
            invalidated_steps.insert(slaposSupportPoints);
        }

        // Invalidate hollowing if drain holes have changed
        if (model_object->sla_drain_holes != model_object_new->sla_drain_holes)
        {
            invalidated_steps.insert(slaposDrillHoles);
        }

        model_object->assign_copy(*model_object_new);

        objects.push_back(model_object);
        reused_objects.insert({model_object->id(), invalidated_steps});
    }

    ModelObjectsSyncResult result;
    result.objects = objects;

    for (SLAPrintObject* print_object : old_objects) {
        const auto reused_model_object_it{
            reused_objects.find(print_object->model_object()->id())
        };

        if (reused_model_object_it != reused_objects.end()) {
            result.invalidated_steps.object[print_object] = reused_model_object_it->second;
            result.reuse_candidates.insert({print_object->model_object()->id(), print_object});
        }
    }

    return result;
}

void delete_old_model_objects(const Domain::ModelObjectPtrs& old_objects, const Domain::ModelObjectPtrs& new_objects) {
    const std::set<Domain::ModelObject*> model_objects_set{
        new_objects.begin(),
        new_objects.end()
    };
    for (Domain::ModelObject* model_object : old_objects) {
        if (model_objects_set.count(model_object) == 0) {
            delete model_object;
        }
    }
}

using Step = std::variant<SLAPrintStep, SLAPrintObjectStep>;

std::vector<Step> propagate(Step step)
{
    return std::visit(
        Domain::overloaded{
            [](const SLAPrintStep& step) -> std::vector<Step>
            {
                if (step == slapsMergeSlicesAndEval) {
                    return {slapsMergeSlicesAndEval, slapsRasterize};
                } else {
                    return {step};
                }
            },
            [](const SLAPrintObjectStep& step) -> std::vector<Step>
            {
                switch (step) {
                case slaposAssembly:
                    return {
                        slaposAssembly,
                        slapsMergeSlicesAndEval,
                        slapsRasterize,
                        slaposHollowing,
                        slaposDrillHoles,
                        slaposObjectSlice,
                        slaposSupportPoints,
                        slaposSupportTree,
                        slaposPad,
                        slaposSliceSupports,
                    };
                case slaposHollowing:
                    return {
                        slaposHollowing,
                        slaposDrillHoles,
                        slaposObjectSlice,
                        slaposSupportPoints,
                        slaposSupportTree,
                        slaposPad,
                        slaposSliceSupports,
                        slapsMergeSlicesAndEval
                    };
                case slaposDrillHoles:
                    return {
                        slaposDrillHoles,
                        slaposObjectSlice,
                        slaposSupportPoints,
                        slaposSupportTree,
                        slaposPad,
                        slaposSliceSupports,
                        slapsMergeSlicesAndEval
                    };
                case slaposObjectSlice:
                    return {
                        slaposObjectSlice,
                        slaposSupportPoints,
                        slaposSupportTree,
                        slaposPad,
                        slaposSliceSupports,
                        slapsMergeSlicesAndEval
                    };
                case slaposSupportPoints:
                    return {
                        slaposSupportPoints,
                        slaposSupportTree,
                        slaposPad,
                        slaposSliceSupports,
                        slapsMergeSlicesAndEval
                    };
                case slaposSupportTree:
                    return {slaposSupportTree, slaposPad, slaposSliceSupports, slapsMergeSlicesAndEval};
                case slaposPad:
                    return {slaposPad, slaposSliceSupports, slapsMergeSlicesAndEval};
                case slaposSliceSupports:
                    return {slaposSliceSupports, slapsMergeSlicesAndEval};
                default:
                    PANIC("Unknown step!");
                }
            }
        },
        step
    );
};

std::vector<Step> steps(const std::vector<std::vector<Step>>& steps)
{
    std::vector<Step> result;
    for (const auto& _steps : steps) {
        result.insert(result.end(), _steps.begin(), _steps.end());
    }
    std::ranges::sort(result);
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

std::vector<Step> all_steps()
{
    std::set<Step> result;
    for (size_t i{}; i < slapsCount; ++i) {
        result.insert(static_cast<SLAPrintStep>(i));
    }
    for (size_t i{}; i < slaposCount; ++i) {
        result.insert(static_cast<SLAPrintObjectStep>(i));
    }
    return std::vector<Step>{result.begin(), result.end()};
}

const std::map<std::string, std::vector<Step>> invalidated_by{
    {"absolute_correction", all_steps()},
    {"area_fill", steps({propagate(slapsMergeSlicesAndEval)})},
    {"bed_custom_model", all_steps()},
    {"bed_custom_texture", steps({})},
    {"bed_shape", steps({})},
    {"bottle_cost", steps({})},
    {"bottle_volume", steps({})},
    {"bottle_weight", steps({})},
    {"branchingsupport_base_diameter", steps({propagate(slaposSupportTree)})},
    {"branchingsupport_base_height", steps({propagate(slaposSupportTree)})},
    {"branchingsupport_base_safety_distance", steps({propagate(slaposSupportTree)})},
    {"branchingsupport_buildplate_only", steps({propagate(slaposSupportTree)})},
    {"branchingsupport_critical_angle", steps({propagate(slaposSupportTree)})},
    {"branchingsupport_head_front_diameter", steps({propagate(slaposSupportTree)})},
    {"branchingsupport_head_penetration", steps({propagate(slaposSupportTree)})},
    {"branchingsupport_head_width", steps({propagate(slaposSupportTree)})},
    {"branchingsupport_max_bridge_length", steps({propagate(slaposSupportTree)})},
    {"branchingsupport_max_bridges_on_pillar", steps({propagate(slaposSupportTree)})},
    {"branchingsupport_max_pillar_link_distance", steps({propagate(slaposSupportTree)})},
    {"branchingsupport_max_weight_on_model", steps({propagate(slaposSupportTree)})},
    {"branchingsupport_object_elevation", steps({propagate(slaposObjectSlice)})},
    {"branchingsupport_pillar_connection_mode", steps({propagate(slaposSupportTree)})},
    {"branchingsupport_pillar_diameter", steps({propagate(slaposSupportTree)})},
    {"branchingsupport_pillar_widening_factor", steps({propagate(slaposSupportTree)})},
    {"branchingsupport_small_pillar_diameter_percent", steps({propagate(slaposSupportTree)})},
    {"delay_after_exposure", steps({propagate(slapsMergeSlicesAndEval)})},
    {"delay_before_exposure", steps({propagate(slapsMergeSlicesAndEval)})},
    {"display_height", steps({propagate(slapsMergeSlicesAndEval)})},
    {"display_mirror_x", steps({propagate(slapsMergeSlicesAndEval)})},
    {"display_mirror_y", steps({propagate(slapsMergeSlicesAndEval)})},
    {"display_orientation", steps({propagate(slapsMergeSlicesAndEval)})},
    {"display_pixels_x", steps({propagate(slapsMergeSlicesAndEval)})},
    {"display_pixels_y", steps({propagate(slapsMergeSlicesAndEval)})},
    {"display_width", steps({propagate(slapsMergeSlicesAndEval)})},
    {"elefant_foot_compensation", all_steps()},
    {"elefant_foot_min_width", all_steps()},
    {"exposure_time", steps({propagate(slapsMergeSlicesAndEval)})},
    {"faded_layers", steps({propagate(slaposObjectSlice)})},
    {"fast_tilt_time", steps({})},
    {"gamma_correction", all_steps()},
    {"high_viscosity_tilt_time", steps({})},
    {"hollowing_closing_distance", steps({propagate(slaposHollowing)})},
    {"hollowing_enable", steps({propagate(slaposHollowing)})},
    {"hollowing_min_thickness", steps({propagate(slaposHollowing)})},
    {"hollowing_quality", steps({propagate(slaposHollowing)})},
    {"initial_exposure_time", steps({propagate(slapsMergeSlicesAndEval)})},
    {"initial_layer_height", all_steps()},
    {"layer_height", steps({propagate(slaposObjectSlice)})},
    {"material_colour", steps({})},
    {"material_correction", all_steps()},
    {"material_correction_x", all_steps()},
    {"material_correction_y", all_steps()},
    {"material_correction_z", all_steps()},
    {"material_density", steps({})},
    {"material_notes", steps({})},
    {"material_print_speed", all_steps()},
    {"material_type", steps({})},
    {"material_vendor", steps({})},
    {"max_exposure_time", steps({propagate(slapsMergeSlicesAndEval)})},
    {"max_initial_exposure_time", steps({propagate(slapsMergeSlicesAndEval)})},
    {"max_print_height", steps({})},
    {"min_exposure_time", steps({propagate(slapsMergeSlicesAndEval)})},
    {"min_initial_exposure_time", steps({propagate(slapsMergeSlicesAndEval)})},
    {"output_filename_format", steps({})},
    {"pad_around_object", steps({propagate(slaposObjectSlice)})},
    {"pad_around_object_everywhere", steps({propagate(slaposObjectSlice)})},
    {"pad_brim_size", steps({propagate(slaposPad)})},
    {"pad_enable", steps({propagate(slaposObjectSlice)})},
    {"pad_max_merge_distance", steps({propagate(slaposPad)})},
    {"pad_object_connector_penetration", steps({propagate(slaposPad)})},
    {"pad_object_connector_stride", steps({propagate(slaposPad)})},
    {"pad_object_connector_width", steps({propagate(slaposPad)})},
    {"pad_object_gap", steps({propagate(slaposSupportTree)})},
    {"pad_wall_height", steps({propagate(slaposPad)})},
    {"pad_wall_slope", steps({propagate(slaposPad)})},
    {"pad_wall_thickness", steps({propagate(slaposObjectSlice)})},
    {"printer_model", steps({})},
    {"printer_notes", steps({})},
    {"printer_technology", steps({})},
    {"printer_variant", steps({})},
    {"relative_correction", all_steps()},
    {"relative_correction_x", all_steps()},
    {"relative_correction_y", all_steps()},
    {"relative_correction_z", all_steps()},
    {"sla_archive_format", steps({propagate(slapsMergeSlicesAndEval)})},
    {"sla_output_precision", steps({propagate(slapsMergeSlicesAndEval)})},
    {"slice_closing_radius", steps({propagate(slaposObjectSlice)})},
    {"slicing_mode", steps({propagate(slaposObjectSlice)})},
    {"slow_tilt_time", steps({})},
    {"support_base_diameter", steps({propagate(slaposSupportTree)})},
    {"support_base_height", steps({propagate(slaposSupportTree)})},
    {"support_base_safety_distance", steps({propagate(slaposSupportTree)})},
    {"support_buildplate_only", steps({propagate(slaposSupportTree)})},
    {"support_critical_angle", steps({propagate(slaposSupportTree)})},
    {"support_enforcers_only", steps({propagate(slaposSupportPoints)})},
    {"support_head_front_diameter", steps({propagate(slaposSupportTree)})},
    {"support_head_penetration", steps({propagate(slaposSupportTree)})},
    {"support_head_width", steps({propagate(slaposSupportTree)})},
    {"support_max_bridge_length", steps({propagate(slaposSupportTree)})},
    {"support_max_bridges_on_pillar", steps({propagate(slaposSupportTree)})},
    {"support_max_pillar_link_distance", steps({propagate(slaposSupportTree)})},
    {"support_max_weight_on_model", steps({propagate(slaposSupportTree)})},
    {"support_object_elevation", steps({propagate(slaposObjectSlice)})},
    {"support_pillar_connection_mode", steps({propagate(slaposSupportTree)})},
    {"support_pillar_diameter", steps({propagate(slaposSupportTree)})},
    {"support_pillar_widening_factor", steps({propagate(slaposSupportTree)})},
    {"support_points_density_relative", steps({propagate(slaposSupportPoints)})},
    {"support_small_pillar_diameter_percent", steps({propagate(slaposSupportTree)})},
    {"support_tree_type", steps({propagate(slaposObjectSlice)})},
    {"supports_enable", steps({propagate(slaposObjectSlice)})},
    {"thumbnails", steps({})},
    {"thumbnails_format", steps({})},
    {"tilt_down_cycles", steps({propagate(slapsMergeSlicesAndEval)})},
    {"tilt_down_delay", steps({propagate(slapsMergeSlicesAndEval)})},
    {"tilt_down_finish_speed", steps({propagate(slapsMergeSlicesAndEval)})},
    {"tilt_down_initial_speed", steps({propagate(slapsMergeSlicesAndEval)})},
    {"tilt_down_offset_delay", steps({propagate(slapsMergeSlicesAndEval)})},
    {"tilt_down_offset_steps", steps({propagate(slapsMergeSlicesAndEval)})},
    {"tilt_up_cycles", steps({propagate(slapsMergeSlicesAndEval)})},
    {"tilt_up_delay", steps({propagate(slapsMergeSlicesAndEval)})},
    {"tilt_up_finish_speed", steps({propagate(slapsMergeSlicesAndEval)})},
    {"tilt_up_initial_speed", steps({propagate(slapsMergeSlicesAndEval)})},
    {"tilt_up_offset_delay", steps({propagate(slapsMergeSlicesAndEval)})},
    {"tilt_up_offset_steps", steps({propagate(slapsMergeSlicesAndEval)})},
    {"tower_hop_height", steps({propagate(slapsMergeSlicesAndEval)})},
    {"tower_speed", steps({propagate(slapsMergeSlicesAndEval)})},
    {"use_tilt", steps({propagate(slapsMergeSlicesAndEval)})},
    {"zcorrection_layers", all_steps()},
};

PrintAndObjectSteps diff_to_invalidated_steps(
    const std::vector<std::string>& diff
)
{
    std::set<Step> steps;
    for (const std::string& opt_key : diff) {
        const std::vector<Step>& invalidated_steps{invalidated_by.at(opt_key)};
        steps.insert(invalidated_steps.begin(), invalidated_steps.end());
    }

    std::set<SLAPrintStep> print_steps;
    std::set<SLAPrintObjectStep> print_object_steps;
    for (const Step& step : steps) {
        std::visit(
            Domain::overloaded{
                [&](const SLAPrintStep& step) { print_steps.insert(step); },
                [&](const SLAPrintObjectStep& step) { print_object_steps.insert(step); }
            },
            step
        );
    }
    return PrintAndObjectSteps{print_steps, print_object_steps};
}

void delete_old_print_objects(const PrintObjects& old_objects, const PrintObjects& new_objects)
{
    std::set<SLAPrintObject*> print_objects_set{new_objects.begin(), new_objects.end()};
    for (SLAPrintObject* print_object : old_objects) {
        if (print_objects_set.count(print_object) == 0) {
            delete print_object;
        }
    }
}

struct PrintObjectsSyncResult {
    std::vector<SLAPrintObject*> objects;
    InvalidatedSteps invalidated_steps;
};

PrintObjectsSyncResult sync_print_objects(
    const Domain::ModelObjectPtrs& model_objects,
    const std::map<ObjectID, SLAPrintObject*>& reuse_candidates,
    const FullConfigSLAPtr& new_full_config,
    const Vec3d relative_correction,
    SLAPrint* print
)
{
    PrintObjectsSyncResult result;

    for (Domain::ModelObject* model_object: model_objects) {
        std::vector<SLAPrintObject::Instance> new_instances = sla_instances(*model_object);
        if (new_instances.empty()) {
            continue;
        }

        const auto it{reuse_candidates.find(model_object->id())};

        const auto object_settings_ptr{std::make_shared<PartialObjectConfigSLA>(
            model_object->object_settings_sla,
            new_full_config->hw_config()
        )};
        const SLAPrintObjectConfigView new_config{
            new_full_config,
            object_settings_ptr
        };

        if (it == reuse_candidates.end()) {
            auto print_object = new SLAPrintObject(print, model_object, new_config);

            // FIXME: this invalidates the transformed mesh in SLAPrintObject
            // which is expensive to calculate (especially the raw_mesh() call)
            print_object->set_trafo(sla_trafo(*model_object, relative_correction), model_object->instances.front()->is_left_handed());
            print_object->set_instances(std::move(new_instances));
            result.objects.emplace_back(print_object);
            continue;
        }

        SLAPrintObject* reused_print_object{it->second};

        // Synchronize Object's config.
        std::vector<std::string> diff = reused_print_object->config().diff_keys(new_config);
        AllOrSome<PrintObjectSteps>& object_steps{
            result.invalidated_steps.object[reused_print_object]
        };
        AllOrSome<PrintSteps>& print_steps{result.invalidated_steps.print};
        const PrintAndObjectSteps invalidated_steps{diff_to_invalidated_steps(diff)};
        print_steps  = merge(print_steps, invalidated_steps.first);
        object_steps = merge(object_steps, invalidated_steps.second);

        reused_print_object->set_config(new_config);

        if (new_instances != reused_print_object->instances()) {
            // Instances changed.
            reused_print_object->set_instances(std::move(new_instances));

            if (std::holds_alternative<PrintSteps>(result.invalidated_steps.print)) {
                auto& print_steps{std::get<PrintSteps>(result.invalidated_steps.print)};
                print_steps.insert(slapsMergeSlicesAndEval);
            }
        }
        result.objects.emplace_back(reused_print_object);
    }

    return result;
}

} // namespace

void SLAPrint::invalidate_object_steps(
    const InvalidatedSteps& steps
) {
    std::set<Domain::ObjectID> result;

    if (std::holds_alternative<PrintSteps>(steps.print)) {
        const auto print_steps{std::get<PrintSteps>(steps.print)};
        for (const SLAPrintStep& step : print_steps) {
            this->invalidate_step(step);
        }
    } else {
        this->invalidate_all_steps();
    }

    const std::set<SLAPrintObject*> existing_objects{m_objects.begin(), m_objects.end()};
    for (const auto& [print_object, invalidated_steps] : steps.object) {
        if (!existing_objects.contains(print_object)) {
            continue;
        }
        if (std::holds_alternative<PrintObjectSteps>(invalidated_steps)) {
            const auto object_steps{std::get<PrintObjectSteps>(invalidated_steps)};
            for (const SLAPrintObjectStep& step : object_steps) {
                if (print_object->invalidate_step(step)) {
                    result.insert(print_object->model_object()->id());
                }
            }
        } else {
            if (print_object->invalidate_all_steps()) {
                result.insert(print_object->model_object()->id());
            }
        }
    }
}

bool InvalidatedSteps::empty() const {
    if (std::holds_alternative<AllSteps>(print)) {
        return false;
    }
    if (!std::get<PrintSteps>(print).empty()) {
        return false;
    }

    for (const auto& [_, steps] : object) {
        if (std::holds_alternative<AllSteps>(steps)) {
            return false;
        }
        if (!std::get<PrintObjectSteps>(steps).empty()) {
            return false;
        }
    }
    return true;
}

struct ModelSyncResult {
    Domain::ModelObjectPtrs model_objects;
    PrintObjects print_objects;
    InvalidatedSteps invalidated_steps;
};

ModelSyncResult sync_model(
    const Domain::Model& old_model,
    const Domain::Model& new_model,
    const FullConfigSLAPtr& new_full_config,
    const PrintObjects& old_objects,
    const std::map<ObjectID, Domain::ModelObject*>& reuse_candidates,
    const Vec3d& relative_correction,
    SLAPrint* print
)
{
    const ModelObjectsSyncResult model_objects_sync_result{
        sync_model_objects(new_model.objects, reuse_candidates, old_objects, relative_correction)
    };

    InvalidatedSteps changed_model_invalidated_steps;
    if (!model_object_list_equal(old_model.objects, model_objects_sync_result.objects)) {
        changed_model_invalidated_steps.print = PrintSteps{slapsMergeSlicesAndEval};
    }

    const PrintObjectsSyncResult print_objects_sync_result{
        sync_print_objects(
            model_objects_sync_result.objects,
            model_objects_sync_result.reuse_candidates,
            new_full_config,
            relative_correction,
            print
        )
    };

    delete_old_model_objects(old_model.objects, model_objects_sync_result.objects);
    delete_old_print_objects(old_objects, print_objects_sync_result.objects);

    InvalidatedSteps changed_objects_invalidated_steps;
    if (old_objects != print_objects_sync_result.objects) {
        changed_objects_invalidated_steps.print = AllSteps{};
    }

    return {
        model_objects_sync_result.objects,
        print_objects_sync_result.objects,
        merge({
            model_objects_sync_result.invalidated_steps,
            print_objects_sync_result.invalidated_steps,
            changed_model_invalidated_steps,
            changed_objects_invalidated_steps
        })
    };
}

InstanceTrafos get_instance_trafos(const SLAPrintObject& object) {
    const double z{object.get_current_elevation()};
    InstanceTrafos instance_trafos;
    for (const SLAPrintObject::Instance& instance : object.instances()) {
        Transform3d trafo{Transform3d::Identity()};

        trafo.translate(to_3d(unscaled<double>(instance.shift), z));
        trafo.rotate(Eigen::AngleAxis<double>(instance.rotation, Vec3d::UnitZ()));
        instance_trafos.emplace_back(instance.instance_id, trafo);
    }
    return instance_trafos;
}

Biz::Slicing::ApplyStatus::Status SLAPrint::update(
    Domain::Model& model,
    const ConfigPack& config,
    const Domain::BedInstance& bed,
    const Domain::Preset::SelectedPresetMetadata& metadata,
    const MetadataSerializeFn& serializer
)
{
    namespace ApplyStatus = Biz::Slicing::ApplyStatus;

    std::set<ObjectID> old_ids{};
    for (const SLAPrintObject* object : m_objects) {
        old_ids.insert(object->model_object()->id());
    }

    InvalidatedSteps invalidated_steps;
    Biz::Slicing::with_limited_instances(model, bed.model_instances, [&](){
        const InstanceTransformations original_transformations{
            transform_instances(model, bed.transformation.get_matrix().inverse())
        };
        ScopeGuard guard{
            [&]() {
                restore_instance_transformations(model, original_transformations);
            }
        };

        invalidated_steps =
            this->apply(model, std::get<ConfigPackSLA>(config), metadata, serializer);
    });
    const bool changed{!invalidated_steps.empty()};
    if (!changed) {
        return ApplyStatus::Unchanged{};
    }

    m_on_sla_result({});

    this->invalidate_object_steps(invalidated_steps);

    std::vector<const SLAPrintObject*> objects_to_keep;
    for (const SLAPrintObject* object : m_objects) {
        if (object->all_steps_done()) {
            objects_to_keep.push_back(object);
        }
    }

    for (const ObjectID id: old_ids) {
        const auto it{std::ranges::find_if(objects_to_keep, [&](const SLAPrintObject* object) {
            return object->model_object()->id() == id;
        })};
        if (it != objects_to_keep.end()) {
            Biz::Slicing::Sla::Object slicing_object{};
            slicing_object.object_id = id;
            slicing_object.instance_trafos = get_instance_trafos(**it);
            // Sending just trafos means the object was not invalidated.
            m_on_sla_object(slicing_object);
        } else {
            Biz::Slicing::Sla::Object slicing_object{};
            slicing_object.object_id = id;
            // Sending an object without trafos means the object has been invalidated.
            m_on_sla_object(slicing_object);
        }
    }

    return ApplyStatus::Changed{};
}

InvalidatedSteps SLAPrint::apply(
    const Domain::Model& model,
    const Domain::ConfigPackSLA& config_pack,
    const Domain::Preset::SelectedPresetMetadata& metadata,
    const MetadataSerializeFn& serializer,
    std::vector<std::string>* warnings
)
{
#ifdef _DEBUG
    check_model_ids_validity(model);
#endif /* _DEBUG */

    const auto new_full_config_ptr{std::make_shared<FullConfigSLA>(config_pack, metadata.hw_config)};
    const SLAPrintConfigView new_print_config{new_full_config_ptr};
    m_metadata = metadata;
    m_metadata_serializer = serializer;


    // Grab the lock for the Print / PrintObject milestones.
    std::scoped_lock<std::mutex> lock(this->state_mutex());

    m_placeholder_parser = PlaceholderParser{Biz::Parser::IO::get_parser_config(new_print_config)};

    // It is also safe to change m_config now after this->invalidate_state_by_config_options() call.
    m_print_config = new_print_config;

    std::map<ObjectID, Domain::ModelObject*> reuse_candidates;
    if (model.id() == m_model.id()) {
        for (Domain::ModelObject* model_object: m_model.objects) {
            reuse_candidates.insert({model_object->id(), model_object});
        }
    }

    m_model.copy_id(model);

    const ModelSyncResult model_sync_result{sync_model(
        m_model,
        model,
        new_full_config_ptr,
        m_objects,
        reuse_candidates,
        this->relative_correction(),
        this
    )};

    m_model.objects = model_sync_result.model_objects;
    m_objects = model_sync_result.print_objects;

    if(m_objects.empty()) {
        m_printer_input = {};
    }

    return model_sync_result.invalidated_steps;
}

namespace {
ParserConfig to_config(const Domain::SLA::PrintStatistics& stats)
{
    ParserConfig config;
    const std::string print_time = Slic3r::short_time(Utils::get_time_dhms(float(stats.estimated_print_time)));
    config.set("print_time", print_time);
    config.set("objects_used_material", stats.objects_used_material);
    config.set("support_used_material", stats.support_used_material);
    config.set("total_cost", stats.total_cost);
    config.set("total_weight", stats.total_weight);
    return config;
}

ParserConfig create_stats_placeholders()
{
    ParserConfig config;
    for (const char* key : {"print_time", "total_cost", "total_weight", 
        "objects_used_material", "support_used_material"})
        config.set(key, std::string{"{"} + key + "}");
    return config;
}

} // namespace

std::string SLAPrint::validate(std::vector<std::string>*) const
{
//    for(SLAPrintObject * po : m_objects) {
//
//        const ModelObject *mo = po->model_object();
//        bool supports_en = po->config().supports_enable.getBool();
//
//        if(supports_en &&
//           mo->sla_points_status == PointsStatus::UserModified &&
//           mo->sla_support_points.empty())
//            return _u8L("Cannot proceed without support points! "
//                     "Add support points or disable support generation.");
//
//        sla::SupportTreeConfig cfg = make_support_cfg(po->config());
//
//        double elv = cfg.object_elevation_mm;
//        
//        sla::PadConfig padcfg = make_pad_cfg(po->config());
//        sla::PadConfig::EmbedObject &builtinpad = padcfg.embed_object;
//        
//        if(supports_en && !builtinpad.enabled && elv < cfg.head_fullwidth())
//            return _u8L(
//                "Elevation is too low for object. Use the \"Pad around "
//                "object\" feature to print the object without elevation.");
//        
//        if(supports_en && builtinpad.enabled &&
//           cfg.pillar_base_safety_distance_mm < builtinpad.object_gap_mm) {
//            return _u8L(
//                "The endings of the support pillars will be deployed on the "
//                "gap between the object and the pad. 'Support base safety "
//                "distance' has to be greater than the 'Pad object gap' "
//                "parameter to avoid this.");
//        }
//        
//        std::string pval = padcfg.validate();
//        if (!pval.empty()) return pval;
//    }
//
//    double expt_max = m_printer_config.max_exposure_time.getFloat();
//    double expt_min = m_printer_config.min_exposure_time.getFloat();
//    double expt_cur = m_material_config.exposure_time.getFloat();
//
//    if (expt_cur < expt_min || expt_cur > expt_max)
//        return _u8L("Exposition time is out of printer profile bounds.");
//
//    double iexpt_max = m_printer_config.max_initial_exposure_time.getFloat();
//    double iexpt_min = m_printer_config.min_initial_exposure_time.getFloat();
//    double iexpt_cur = m_material_config.initial_exposure_time.getFloat();
//
//    if (iexpt_cur < iexpt_min || iexpt_cur > iexpt_max)
//        return _u8L("Initial exposition time is out of printer profile bounds.");
//
//    for (const std::string& prefix : { "", "branching" }) {
//
//        double head_penetration = m_full_print_config.opt_float(prefix + "support_head_penetration");
//        double head_width       = m_full_print_config.opt_float(prefix + "support_head_width");
//
//        if (head_penetration > head_width) {
//            return _u8L("Invalid Head penetration\n"
//                        "Head penetration should not be greater than the Head width.\n"
//                        "Please check value of Head penetration in Print Settings or Material Overrides.");
//        }
//
//        double pinhead_d = m_full_print_config.opt_float(prefix + "support_head_front_diameter");
//        double pillar_d  = m_full_print_config.opt_float(prefix + "support_pillar_diameter");
//
//        if (pinhead_d > pillar_d) {
//            return _u8L("Invalid pinhead diameter\n"
//                        "Pinhead front diameter should be smaller than the Pillar diameter.\n"
//                        "Please check value of Pinhead front diameter in Print Settings or Material Overrides.");
//        }
//    }
//
//    if ((!m_material_config.use_tilt.get_at(0) && is_approx(m_material_config.tower_hop_height.get_at(0), 0.))
//        || (!m_material_config.use_tilt.get_at(1) && is_approx(m_material_config.tower_hop_height.get_at(1), 0.)))
//        return _u8L("Disabling the 'Use tilt' function causes the object to separate away from the film in the "
//                    "vertical direction only. Therefore, it is necessary to set the 'Tower hop height' parameter "
//                    "to reasonable value. The recommended value is 5 mm.");
//
    return "";
}

bool SLAPrint::is_prusa_print(const std::string& printer_model)
{
    static const std::vector<std::string> prusa_printer_models = { "SL1", "SL1S", "M1", "SLX" };
    for (const std::string& model : prusa_printer_models)
        if (model == printer_model)
            return true;

    return false;
}

void SLAPrint::process()
{
    if (m_objects.empty())
        return;

    name_tbb_thread_pool_threads_set_locale();

    // Assumption: at this point the print objects should be populated only with
    // the model objects we have to process and the instances are also filtered
    
    Steps printsteps(this);

    // We want to first process all objects...
    std::vector<SLAPrintObjectStep> level1_obj_steps = {
        slaposAssembly, slaposHollowing, slaposDrillHoles, slaposObjectSlice, slaposSupportPoints, slaposSupportTree, slaposPad
    };

    // and then slice all supports to allow preview to be displayed ASAP
    std::vector<SLAPrintObjectStep> level2_obj_steps = {
        slaposSliceSupports
    };

    SLAPrintStep print_steps[] = { slapsMergeSlicesAndEval, slapsRasterize };
    
    double st = Steps::min_objstatus;

    SPDLOG_INFO("Start slicing process.");

#ifdef SLAPRINT_DO_BENCHMARK
    Benchmark bench;
#else
    struct {
        void start() {} void stop() {} double getElapsedSec() { return .0; }
    } bench;
#endif

    std::array<double, static_cast<int>(slaposCount) + static_cast<int>(slapsCount)> step_times {};

    auto apply_steps_on_objects =
        [this, &st, &printsteps, &step_times, &bench]
        (const std::vector<SLAPrintObjectStep> &steps)
    {
        double incr = 0;
        for (SLAPrintObject *po : m_objects) {
            for (SLAPrintObjectStep step : steps) {

                // Cancellation checking. Each step will check for
                // cancellation on its own and return earlier gracefully.
                // Just after it returns execution gets to this point and
                // throws the canceled signal.
                throw_if_canceled();

                st += incr;

                if (po->set_started(step)) {
                    m_report_status(*this, st, printsteps.label(step));
                    bench.start();
                    printsteps.execute(step, *po);
                    bench.stop();
                    step_times[step] += bench.getElapsedSec();
                    throw_if_canceled();
                    po->set_done(step);
                    po->m_preview->instance_trafos = get_instance_trafos(*po);
                    m_on_sla_object(*po->m_preview);
                }

                incr = printsteps.progressrange(step);
            }
        }
    };

    apply_steps_on_objects(level1_obj_steps);
    apply_steps_on_objects(level2_obj_steps);

    st = Steps::max_objstatus;
    for(SLAPrintStep currentstep : print_steps) {
        throw_if_canceled();

        if (set_started(currentstep)) {
            m_report_status(*this, st, printsteps.label(currentstep));
            bench.start();
            printsteps.execute(currentstep);
            bench.stop();
            step_times[static_cast<int>(slaposCount) + static_cast<int>(currentstep)] += bench.getElapsedSec();
            throw_if_canceled();
            set_done(currentstep);
        }

        st += printsteps.progressrange(currentstep);
    }

#ifdef SLAPRINT_DO_BENCHMARK
    std::string csvbenchstr;
    for (size_t i = 0; i < size_t(slaposCount); ++i)
        csvbenchstr += printsteps.label(SLAPrintObjectStep(i)) + ";";

    for (size_t i = 0; i < size_t(slapsCount); ++i)
        csvbenchstr += printsteps.label(SLAPrintStep(i)) + ";";

    csvbenchstr += "\n";
    for (double t : step_times) csvbenchstr += std::to_string(t) + ";";

    std::cout << "Performance stats: \n" << csvbenchstr << std::endl;
#endif

}

void SLAPrint::slice(
    SlicingId slicing_id,
    IThumbnailImageGenerator&,
    const std::optional<SliceUntilStep> slice_until_step
)
{
    // Clean up after the processing on every exit path, even the exceptional ones.
    const ScopeGuard finalize_and_cleanup_guard{
        [this]() -> void
        {
            this->finalize();
            this->cleanup();
        }
    };

    if (slice_until_step.has_value()) {
        ASSERT(std::holds_alternative<SLAPrintObjectStep>(slice_until_step->step));
        const SLAPrintObjectStep until_object_step =
            std::get<SLAPrintObjectStep>(slice_until_step->step);

        if (!this->set_task_until_object_step_impl(
                static_cast<int>(until_object_step),
                slice_until_step->model_object_id,
                m_objects
            ))
        {
            // The model object has no print object.
            return;
        }
    }

    this->process();
}

// Returns true if an object step is done on all objects and there's at least one object.
bool SLAPrint::is_object_step_done(SLAPrintObjectStep step) const
{
    if (m_objects.empty())
        return false;
    std::scoped_lock<std::mutex> lock(this->state_mutex());
    for (const SLAPrintObject *object : m_objects)
        if (! object->is_step_done_unguarded(step))
            return false;
    return true;
}

SLAPrintObject::SLAPrintObject(
    SLAPrint* print, Domain::ModelObject* model_object, const SLAPrintObjectConfigView& config
)
    : Inherited(print, model_object),
    m_config(config)
{}

bool SLAPrintObject::invalidate_all_steps()
{
    return Inherited::invalidate_all_steps() || m_print->invalidate_all_steps();
}

double SLAPrintObject::get_elevation() const {
    if (is_zero_elevation(m_config)) return 0.;

    bool en = m_config.get<bool>("supports_enable");

    double ret = en ? m_config.get<double>("support_object_elevation") : 0.;

    if(m_config.get<bool>("pad_enable")) {
        // Normally the elevation for the pad itself would be the thickness of
        // its walls but currently it is half of its thickness. Whatever it
        // will be in the future, we provide the config to the get_pad_elevation
        // method and we will have the correct value
        sla::PadConfig pcfg = make_pad_cfg(m_config);
        if(!pcfg.embed_object) ret += pcfg.required_elevation();
    }

    return ret;
}

double SLAPrintObject::get_current_elevation() const
{
    if (is_zero_elevation(m_config)) return 0.;

    bool has_supports = is_step_done(slaposSupportTree);
    bool has_pad      = is_step_done(slaposPad);

    if(!has_supports && !has_pad)
        return 0;
    else if(has_supports && !has_pad) {
        return m_config.get<double>("support_object_elevation");
    }

    return get_elevation();
}

Vec3d SLAPrint::relative_correction() const
{
    Vec3d corr(1., 1., 1.);

    if(print_config().get<std::vector<double>>("relative_correction").size() >= 2) {
        corr.x() = print_config().get<double>("relative_correction_x");
        corr.y() = print_config().get<double>("relative_correction_y");
        corr.z() = print_config().get<double>("relative_correction_z");
    }

    if(print_config().get<std::vector<double>>("material_correction").size() >= 2) {
        corr.x() *= print_config().get<double>("material_correction_x");
        corr.y() *= print_config().get<double>("material_correction_y");
        corr.z() *= print_config().get<double>("material_correction_z");
    }

    return corr;
}

using Domain::TriangleMesh;

namespace { // dummy empty static containers for return values in some methods
const std::vector<ExPolygons> EMPTY_SLICES;
const TriangleMesh EMPTY_MESH;
const ExPolygons EMPTY_SLICE;
}

const SliceRecord SliceRecord::EMPTY(0, std::nanf(""), 0.f);

const std::vector<ExPolygons> &SLAPrintObject::get_support_slices() const
{
    // assert(is_step_done(slaposSliceSupports));
    return (!m_support_slices.empty()) ? m_support_slices : EMPTY_SLICES;
}

const ExPolygons &SliceRecord::get_slice(SliceOrigin o) const
{
    size_t idx = o == soModel ? m_model_slices_idx : m_support_slices_idx;

    if(m_po == nullptr) return EMPTY_SLICE;

    const std::vector<ExPolygons>& v = o == soModel? m_po->get_model_slices() :
                                                     m_po->get_support_slices();

    return idx >= v.size() ? EMPTY_SLICE : v[idx];
}

const TriangleMesh& SLAPrintObject::support_mesh() const
{
    if (m_config.get<bool>("supports_enable") &&
        is_step_done(slaposSupportTree) &&
        m_preview.has_value() &&
        m_preview->support_structure)
        return *m_preview->support_structure;
    return EMPTY_MESH;
}

const TriangleMesh& SLAPrintObject::pad_mesh() const
{
    if (m_config.get<bool>("pad_enable") && is_step_done(slaposPad) && 
        m_preview.has_value() && m_preview->pad)
        return *m_preview->pad;
    return EMPTY_MESH;
}

std::shared_ptr<const Domain::TriangleMesh> SLAPrintObject::get_mesh_to_print() const
{
    if(!m_preview.has_value())
        return nullptr;
    return m_preview->mesh;
}

std::vector<csg::CSGPart> SLAPrintObject::get_parts_to_slice() const
{
    return get_parts_to_slice(slaposCount);
}

std::vector<csg::CSGPart>
SLAPrintObject::get_parts_to_slice(SLAPrintObjectStep untilstep) const
{
    auto laststep = last_completed_step();
    SLAPrintObjectStep s = std::min(untilstep, laststep);

    if (s == slaposCount)
        return {};

    std::vector<csg::CSGPart> ret;

    for (unsigned int step = 0; step <= s; ++step) {
        auto r = m_mesh_to_slice.equal_range(SLAPrintObjectStep(step));
        csg::copy_csgrange_shallow(Range{r.first, r.second}, std::back_inserter(ret));
    }

    return ret;
}

Domain::SLA::SupportPoints SLAPrintObject::transformed_support_points() const
{
    assert(model_object());
    auto spts = model_object()->sla_support_points;
    Transform3f tr = trafo().cast<float>();
    for (Domain::SLA::SupportPoint &suppt : spts) {
        suppt.pos = tr * suppt.pos;
    }
    return spts;
}

Domain::SLA::DrainHoles SLAPrintObject::transformed_drainhole_points() const
{
    assert(model_object());
    Domain::SLA::DrainHoles drainholes = model_object()->sla_drain_holes; // Copy the drainholes
    sla::transform_drainhole_points(drainholes, trafo());
    return drainholes;
}

void SLAPrint::StatusReporter::operator()(
    SLAPrint& p,
    double st,
    Biz::Slicing::ProgressInfo msg
)
{
    m_st = st;
    p.set_status(Percentage{std::round(st)}, msg);
}

namespace csg {

MeshBoolean::cgal::CGALMeshPtr get_cgalmesh(const CSGPartForStep &part)
{
    if (!part.cgalcache && csg::get_mesh(part)) {
        part.cgalcache = csg::get_cgalmesh(static_cast<const csg::CSGPart&>(part));
    }

    return part.cgalcache? clone(*part.cgalcache) : nullptr;
}

} // namespace csg

} // namespace Slic3r
