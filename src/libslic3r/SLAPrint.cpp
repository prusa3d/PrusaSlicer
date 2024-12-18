///|/ Copyright (c) Prusa Research 2018 - 2023 Tomáš Mészáros @tamasmeszaros, Lukáš Matěna @lukasmatena, Pavel Mikuš @Godrak, Oleksandra Iushchenko @YuSanka, Vojtěch Bubník @bubnikv, Roman Beránek @zavorka, Enrico Turri @enricoturri1966
///|/ Copyright (c) 2022 ole00 @ole00
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "SLAPrint.hpp"
#include "SLAPrintSteps.hpp" // IWYU pragma: keep
#include "CSGMesh/CSGMeshCopy.hpp"
#include "CSGMesh/PerformCSGMeshBooleans.hpp"
#include "format.hpp"
#include "StaticMap.hpp"

#include "Format/SLAArchiveFormatRegistry.hpp"

#include "Geometry.hpp"
#include "Thread.hpp"

#include <unordered_set>
#include <numeric>

#include <tbb/parallel_for.h>
#include <boost/filesystem/path.hpp>
#include <boost/log/trivial.hpp>

#include "libslic3r/MultipleBeds.hpp"

// #define SLAPRINT_DO_BENCHMARK

#ifdef SLAPRINT_DO_BENCHMARK
#include <libnest2d/tools/benchmark.h>
#endif

#include "I18N.hpp"

//! macro used to mark string used at localization,
//! return same string
#define _u8L(s) Slic3r::I18N::translate(s)

namespace Slic3r {


bool is_zero_elevation(const SLAPrintObjectConfig &c)
{
    return c.pad_enable.getBool() && c.pad_around_object.getBool();
}

// Compile the argument for support creation from the static print config.
sla::SupportTreeConfig make_support_cfg(const SLAPrintObjectConfig& c)
{
    sla::SupportTreeConfig scfg;

    scfg.enabled = c.supports_enable.getBool();
    scfg.tree_type = c.support_tree_type.value;

    switch(scfg.tree_type) {
    case sla::SupportTreeType::Default: {
        scfg.head_front_radius_mm = 0.5*c.support_head_front_diameter.getFloat();
        double pillar_r = 0.5 * c.support_pillar_diameter.getFloat();
        scfg.head_back_radius_mm = pillar_r;
        scfg.head_fallback_radius_mm =
            0.01 * c.support_small_pillar_diameter_percent.getFloat() * pillar_r;
        scfg.head_penetration_mm = c.support_head_penetration.getFloat();
        scfg.head_width_mm = c.support_head_width.getFloat();
        scfg.object_elevation_mm = is_zero_elevation(c) ?
                                       0. : c.support_object_elevation.getFloat();
        scfg.bridge_slope = c.support_critical_angle.getFloat() * PI / 180.0 ;
        scfg.max_bridge_length_mm = c.support_max_bridge_length.getFloat();
        scfg.max_pillar_link_distance_mm = c.support_max_pillar_link_distance.getFloat();
        scfg.pillar_connection_mode = c.support_pillar_connection_mode.value;
        scfg.ground_facing_only = c.support_buildplate_only.getBool();
        scfg.pillar_widening_factor = c.support_pillar_widening_factor.getFloat();
        scfg.base_radius_mm = 0.5*c.support_base_diameter.getFloat();
        scfg.base_height_mm = c.support_base_height.getFloat();
        scfg.pillar_base_safety_distance_mm =
            c.support_base_safety_distance.getFloat() < EPSILON ?
                scfg.safety_distance_mm : c.support_base_safety_distance.getFloat();

        scfg.max_bridges_on_pillar = unsigned(c.support_max_bridges_on_pillar.getInt());
        scfg.max_weight_on_model_support = c.support_max_weight_on_model.getFloat();
        break;
    }
    case sla::SupportTreeType::Branching:
        [[fallthrough]];
    case sla::SupportTreeType::Organic:{
        scfg.head_front_radius_mm = 0.5*c.branchingsupport_head_front_diameter.getFloat();
        double pillar_r = 0.5 * c.branchingsupport_pillar_diameter.getFloat();
        scfg.head_back_radius_mm = pillar_r;
        scfg.head_fallback_radius_mm =
            0.01 * c.branchingsupport_small_pillar_diameter_percent.getFloat() * pillar_r;
        scfg.head_penetration_mm = c.branchingsupport_head_penetration.getFloat();
        scfg.head_width_mm = c.branchingsupport_head_width.getFloat();
        scfg.object_elevation_mm = is_zero_elevation(c) ?
                                       0. : c.branchingsupport_object_elevation.getFloat();
        scfg.bridge_slope = c.branchingsupport_critical_angle.getFloat() * PI / 180.0 ;
        scfg.max_bridge_length_mm = c.branchingsupport_max_bridge_length.getFloat();
        scfg.max_pillar_link_distance_mm = c.branchingsupport_max_pillar_link_distance.getFloat();
        scfg.pillar_connection_mode = c.branchingsupport_pillar_connection_mode.value;
        scfg.ground_facing_only = c.branchingsupport_buildplate_only.getBool();
        scfg.pillar_widening_factor = c.branchingsupport_pillar_widening_factor.getFloat();
        scfg.base_radius_mm = 0.5*c.branchingsupport_base_diameter.getFloat();
        scfg.base_height_mm = c.branchingsupport_base_height.getFloat();
        scfg.pillar_base_safety_distance_mm =
            c.branchingsupport_base_safety_distance.getFloat() < EPSILON ?
                scfg.safety_distance_mm : c.branchingsupport_base_safety_distance.getFloat();

        scfg.max_bridges_on_pillar = unsigned(c.branchingsupport_max_bridges_on_pillar.getInt());
        scfg.max_weight_on_model_support = c.branchingsupport_max_weight_on_model.getFloat();
        break;
    }
    }
    
    return scfg;
}

sla::PadConfig::EmbedObject builtin_pad_cfg(const SLAPrintObjectConfig& c)
{
    sla::PadConfig::EmbedObject ret;
    
    ret.enabled = is_zero_elevation(c);
    
    if(ret.enabled) {
        ret.everywhere           = c.pad_around_object_everywhere.getBool();
        ret.object_gap_mm        = c.pad_object_gap.getFloat();
        ret.stick_width_mm       = c.pad_object_connector_width.getFloat();
        ret.stick_stride_mm      = c.pad_object_connector_stride.getFloat();
        ret.stick_penetration_mm = c.pad_object_connector_penetration
                                       .getFloat();
    }
    
    return ret;
}

sla::PadConfig make_pad_cfg(const SLAPrintObjectConfig& c)
{
    sla::PadConfig pcfg;
    
    pcfg.wall_thickness_mm = c.pad_wall_thickness.getFloat();
    pcfg.wall_slope = c.pad_wall_slope.getFloat() * PI / 180.0;
    
    pcfg.max_merge_dist_mm = c.pad_max_merge_distance.getFloat();
    pcfg.wall_height_mm = c.pad_wall_height.getFloat();
    pcfg.brim_size_mm = c.pad_brim_size.getFloat();
    
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
Transform3d SLAPrint::sla_trafo(const ModelObject &model_object) const
{
    ModelInstance &model_instance = *model_object.instances.front();
    auto trafo = Transform3d::Identity();
    trafo.translate(Vec3d{ 0., 0., model_instance.get_offset().z() * this->relative_correction().z() });
    trafo.linear() = Eigen::DiagonalMatrix<double, 3, 3>(this->relative_correction()) * model_instance.get_matrix().linear();
    if (model_instance.is_left_handed())
        trafo = Eigen::Scaling(Vec3d(-1., 1., 1.)) * trafo;
    return trafo;
}

// List of instances, where the ModelInstance transformation is a composite of sla_trafo and the transformation defined by SLAPrintObject::Instance.
static std::vector<SLAPrintObject::Instance> sla_instances(const ModelObject &model_object)
{
    std::vector<SLAPrintObject::Instance> instances;
    assert(! model_object.instances.empty());
    if (! model_object.instances.empty()) {
        const Transform3d& trafo0 = model_object.instances.front()->get_matrix();
        for (ModelInstance *model_instance : model_object.instances)
            if (model_instance->is_printable()) {
                instances.emplace_back(
                    model_instance->id(),
                    Point::new_scale(model_instance->get_offset(X), model_instance->get_offset(Y)),
                    float(Geometry::rotation_diff_z(trafo0, model_instance->get_matrix())));
            }
    }
    return instances;
}

std::vector<ObjectID> SLAPrint::print_object_ids() const 
{ 
    std::vector<ObjectID> out;
    // Reserve one more for the caller to append the ID of the Print itself.
    out.reserve(m_objects.size() + 1);
    for (const SLAPrintObject *print_object : m_objects)
        out.emplace_back(print_object->id());
    return out;
}

static t_config_option_keys print_config_diffs(const StaticPrintConfig     &current_config,
                                               const DynamicPrintConfig &new_full_config,
                                               DynamicPrintConfig       &material_overrides)
{
    using namespace std::string_view_literals;

    static const constexpr StaticSet overriden_keys = {
        "support_head_front_diameter"sv,
        "support_head_penetration"sv,
        "support_head_width"sv,
        "support_pillar_diameter"sv,
        "branchingsupport_head_front_diameter"sv,
        "branchingsupport_head_penetration"sv,
        "branchingsupport_head_width"sv,
        "branchingsupport_pillar_diameter"sv,
        "support_points_density_relative"sv,
        "elefant_foot_compensation"sv,
        "absolute_correction"sv,
    };

    static constexpr auto material_ow_prefix = "material_ow_";

    t_config_option_keys           print_diff;
    for (const t_config_option_key &opt_key : current_config.keys()) {
        const ConfigOption *opt_old = current_config.option(opt_key);
        assert(opt_old != nullptr);
        const ConfigOption *opt_new = new_full_config.option(opt_key);
        // assert(opt_new != nullptr);
        if (opt_new == nullptr)
            //FIXME This may happen when executing some test cases.
            continue;
        const ConfigOption *opt_new_override = std::binary_search(overriden_keys.begin(), overriden_keys.end(), opt_key) ? new_full_config.option(material_ow_prefix + opt_key) : nullptr;
        if (opt_new_override != nullptr && ! opt_new_override->is_nil()) {
            // An override is available at some of the material presets.
            bool overriden = opt_new->overriden_by(opt_new_override);
            if (overriden || *opt_old != *opt_new) {
                auto opt_copy = opt_new->clone();
                opt_copy->apply_override(opt_new_override);
                bool changed = *opt_old != *opt_copy;
                if (changed)
                    print_diff.emplace_back(opt_key);
                if (changed || overriden) {
                    // overrides will be applied to the placeholder parser, which layers these parameters over full_print_config.
                    material_overrides.set_key_value(opt_key, opt_copy);
                } else
                    delete opt_copy;
            }
        } else if (*opt_new != *opt_old)
            print_diff.emplace_back(opt_key);
    }

    return print_diff;
}


SLAPrint::ApplyStatus SLAPrint::apply(const Model &model, DynamicPrintConfig config)
{
#ifdef _DEBUG
    check_model_ids_validity(model);
#endif /* _DEBUG */

    // Normalize the config.
    config.option("sla_print_settings_id",        true);
    config.option("sla_material_settings_id",     true);
    config.option("printer_settings_id",          true);
    config.option("physical_printer_settings_id", true);
    // Collect changes to print config.
    DynamicPrintConfig mat_overrides;
    t_config_option_keys print_diff    = m_print_config.diff(config);
    t_config_option_keys printer_diff  = print_config_diffs(m_printer_config, config, mat_overrides);
    t_config_option_keys material_diff = m_material_config.diff(config);
    t_config_option_keys object_diff   = print_config_diffs(m_default_object_config, config, mat_overrides);
    t_config_option_keys placeholder_parser_diff = m_placeholder_parser.config_diff(config);

    config.apply(mat_overrides, true);

    // Do not use the ApplyStatus as we will use the max function when updating apply_status.
    unsigned int apply_status = APPLY_STATUS_UNCHANGED;
    auto update_apply_status = [&apply_status](bool invalidated)
        { apply_status = std::max<unsigned int>(apply_status, invalidated ? APPLY_STATUS_INVALIDATED : APPLY_STATUS_CHANGED); };
    if (! (print_diff.empty() && printer_diff.empty() && material_diff.empty() && object_diff.empty()))
        update_apply_status(false);

    // Grab the lock for the Print / PrintObject milestones.
    std::scoped_lock<std::mutex> lock(this->state_mutex());

    // The following call may stop the background processing.
    bool invalidate_all_model_objects = false;
    if (! print_diff.empty())
        update_apply_status(this->invalidate_state_by_config_options(print_diff, invalidate_all_model_objects));
    if (! printer_diff.empty())
        update_apply_status(this->invalidate_state_by_config_options(printer_diff, invalidate_all_model_objects));
    if (! material_diff.empty())
        update_apply_status(this->invalidate_state_by_config_options(material_diff, invalidate_all_model_objects));

    // Multiple beds hack: We currently use one SLAPrint for all beds. It must be invalidated
    // when beds are switched. If not done explicitly, supports from previously sliced object
    // might end up with wrong offset.
    static int last_bed_idx = s_multiple_beds.get_active_bed();
    int current_bed = s_multiple_beds.get_active_bed();
    if (current_bed != last_bed_idx) {
        invalidate_all_model_objects = true;
        last_bed_idx = current_bed;
    }

    // Apply variables to placeholder parser. The placeholder parser is currently used
    // only to generate the output file name.
    if (! placeholder_parser_diff.empty()) {
        // update_apply_status(this->invalidate_step(slapsRasterize));
        m_placeholder_parser.apply_config(config);
        // Set the profile aliases for the PrintBase::output_filename()
        m_placeholder_parser.set("print_preset",            config.option("sla_print_settings_id")->clone());
        m_placeholder_parser.set("material_preset",         config.option("sla_material_settings_id")->clone());
        m_placeholder_parser.set("printer_preset",          config.option("printer_settings_id")->clone());
        m_placeholder_parser.set("physical_printer_preset", config.option("physical_printer_settings_id")->clone());
    }

    // It is also safe to change m_config now after this->invalidate_state_by_config_options() call.
    m_print_config.apply_only(config, print_diff, true);
    m_printer_config.apply_only(config, printer_diff, true);
    // Handle changes to material config.
    m_material_config.apply_only(config, material_diff, true);
    // Handle changes to object config defaults
    m_default_object_config.apply_only(config, object_diff, true);

    if (!m_archiver || !printer_diff.empty())
        m_archiver = SLAArchiveWriter::create(m_printer_config.sla_archive_format.value.c_str(), m_printer_config);

    struct ModelObjectStatus {
        enum Status {
            Unknown,
            Old,
            New,
            Moved,
            Deleted,
        };
        ModelObjectStatus(ObjectID id, Status status = Unknown) : id(id), status(status) {}
        ObjectID                id;
        Status                  status;
        // Search by id.
        bool operator<(const ModelObjectStatus &rhs) const { return id < rhs.id; }
    };
    std::set<ModelObjectStatus> model_object_status;

    // 1) Synchronize model objects.
    if (model.id() != m_model.id() || invalidate_all_model_objects) {
        // Kill everything, initialize from scratch.
        // Stop background processing.
        this->call_cancel_callback();
        update_apply_status(this->invalidate_all_steps());
        for (SLAPrintObject *object : m_objects) {
            model_object_status.emplace(object->model_object()->id(), ModelObjectStatus::Deleted);
            update_apply_status(object->invalidate_all_steps());
            delete object;
        }
        m_objects.clear();
        m_model.assign_copy(model);
        for (const ModelObject *model_object : m_model.objects)
            model_object_status.emplace(model_object->id(), ModelObjectStatus::New);
    } else {
        if (model_object_list_equal(m_model, model)) {
            // The object list did not change.
            for (const ModelObject *model_object : m_model.objects)
                model_object_status.emplace(model_object->id(), ModelObjectStatus::Old);
        } else if (model_object_list_extended(m_model, model)) {
            // Add new objects. Their volumes and configs will be synchronized later.
            update_apply_status(this->invalidate_step(slapsMergeSlicesAndEval));
            for (const ModelObject *model_object : m_model.objects)
                model_object_status.emplace(model_object->id(), ModelObjectStatus::Old);
            for (size_t i = m_model.objects.size(); i < model.objects.size(); ++ i) {
                model_object_status.emplace(model.objects[i]->id(), ModelObjectStatus::New);
                m_model.objects.emplace_back(ModelObject::new_copy(*model.objects[i]));
                m_model.objects.back()->set_model(&m_model);
            }
        } else {
            // Reorder the objects, add new objects.
            // First stop background processing before shuffling or deleting the PrintObjects in the object list.
            this->call_cancel_callback();
            update_apply_status(this->invalidate_step(slapsMergeSlicesAndEval));
            // Second create a new list of objects.
            std::vector<ModelObject*> model_objects_old(std::move(m_model.objects));
            m_model.objects.clear();
            m_model.objects.reserve(model.objects.size());
            auto by_id_lower = [](const ModelObject *lhs, const ModelObject *rhs){ return lhs->id() < rhs->id(); };
            std::sort(model_objects_old.begin(), model_objects_old.end(), by_id_lower);
            for (const ModelObject *mobj : model.objects) {
                auto it = std::lower_bound(model_objects_old.begin(), model_objects_old.end(), mobj, by_id_lower);
                if (it == model_objects_old.end() || (*it)->id() != mobj->id()) {
                    // New ModelObject added.
                    m_model.objects.emplace_back(ModelObject::new_copy(*mobj));
                    m_model.objects.back()->set_model(&m_model);
                    model_object_status.emplace(mobj->id(), ModelObjectStatus::New);
                } else {
                    // Existing ModelObject re-added (possibly moved in the list).
                    m_model.objects.emplace_back(*it);
                    model_object_status.emplace(mobj->id(), ModelObjectStatus::Moved);
                }
            }
            bool deleted_any = false;
            for (ModelObject *&model_object : model_objects_old) {
                if (model_object_status.find(ModelObjectStatus(model_object->id())) == model_object_status.end()) {
                    model_object_status.emplace(model_object->id(), ModelObjectStatus::Deleted);
                    deleted_any = true;
                } else
                    // Do not delete this ModelObject instance.
                    model_object = nullptr;
            }
            if (deleted_any) {
                // Delete PrintObjects of the deleted ModelObjects.
                std::vector<SLAPrintObject*> print_objects_old = std::move(m_objects);
                m_objects.clear();
                m_objects.reserve(print_objects_old.size());
                for (SLAPrintObject *print_object : print_objects_old) {
                    auto it_status = model_object_status.find(ModelObjectStatus(print_object->model_object()->id()));
                    assert(it_status != model_object_status.end());
                    if (it_status->status == ModelObjectStatus::Deleted) {
                        update_apply_status(print_object->invalidate_all_steps());
                        delete print_object;
                    } else
                        m_objects.emplace_back(print_object);
                }
                for (ModelObject *model_object : model_objects_old)
                    delete model_object;
            }
        }
    }

    // 2) Map print objects including their transformation matrices.
    struct PrintObjectStatus {
        enum Status {
            Unknown,
            Deleted,
            Reused,
            New
        };
        PrintObjectStatus(SLAPrintObject *print_object, Status status = Unknown) :
            id(print_object->model_object()->id()),
            print_object(print_object),
            trafo(print_object->trafo()),
            status(status) {}
        PrintObjectStatus(ObjectID id) : id(id), print_object(nullptr), trafo(Transform3d::Identity()), status(Unknown) {}
        // ID of the ModelObject & PrintObject
        ObjectID         id;
        // Pointer to the old PrintObject
        SLAPrintObject  *print_object;
        // Trafo generated with model_object->world_matrix(true)
        Transform3d      trafo;
        Status           status;
        // Search by id.
        bool operator<(const PrintObjectStatus &rhs) const { return id < rhs.id; }
    };
    std::multiset<PrintObjectStatus> print_object_status;
    for (SLAPrintObject *print_object : m_objects)
        print_object_status.emplace(PrintObjectStatus(print_object));

    // 3) Synchronize ModelObjects & PrintObjects.
    std::vector<SLAPrintObject*> print_objects_new;
    print_objects_new.reserve(std::max(m_objects.size(), m_model.objects.size()));
    bool new_objects = false;
    for (size_t idx_model_object = 0; idx_model_object < model.objects.size(); ++ idx_model_object) {
        ModelObject &model_object = *m_model.objects[idx_model_object];
        auto it_status = model_object_status.find(ModelObjectStatus(model_object.id()));
        assert(it_status != model_object_status.end());
        assert(it_status->status != ModelObjectStatus::Deleted);
        // PrintObject for this ModelObject, if it exists.
        auto it_print_object_status = print_object_status.end();
        if (it_status->status != ModelObjectStatus::New) {
            // Update the ModelObject instance, possibly invalidate the linked PrintObjects.
            assert(it_status->status == ModelObjectStatus::Old || it_status->status == ModelObjectStatus::Moved);
            const ModelObject &model_object_new       = *model.objects[idx_model_object];
            it_print_object_status = print_object_status.lower_bound(PrintObjectStatus(model_object.id()));
            if (it_print_object_status != print_object_status.end() && it_print_object_status->id != model_object.id())
                it_print_object_status = print_object_status.end();
            // Check whether a model part volume was added or removed, their transformations or order changed.
            bool model_parts_differ =
                model_volume_list_changed(model_object, model_object_new,
                                          {ModelVolumeType::MODEL_PART,
                                           ModelVolumeType::NEGATIVE_VOLUME,
                                           ModelVolumeType::SUPPORT_ENFORCER,
                                           ModelVolumeType::SUPPORT_BLOCKER});
            bool sla_trafo_differs  =
                model_object.instances.empty() != model_object_new.instances.empty() ||
                (! model_object.instances.empty() &&
                  (! sla_trafo(model_object).isApprox(sla_trafo(model_object_new)) ||
                    model_object.instances.front()->is_left_handed() != model_object_new.instances.front()->is_left_handed()));
            if (model_parts_differ || sla_trafo_differs) {
                // The very first step (the slicing step) is invalidated. One may freely remove all associated PrintObjects.
                if (it_print_object_status != print_object_status.end()) {
                    update_apply_status(it_print_object_status->print_object->invalidate_all_steps());
                    const_cast<PrintObjectStatus&>(*it_print_object_status).status = PrintObjectStatus::Deleted;
                }
                // Copy content of the ModelObject including its ID, do not change the parent.
                model_object.assign_copy(model_object_new);
            } else {
                // Synchronize Object's config.
                bool object_config_changed = ! model_object.config.timestamp_matches(model_object_new.config);
                if (object_config_changed)
                    model_object.config.assign_config(model_object_new.config);
                if (! object_diff.empty() || object_config_changed) {
                    SLAPrintObjectConfig new_config = m_default_object_config;
                    new_config.apply(model_object.config.get(), true);
                    if (it_print_object_status != print_object_status.end()) {
                        t_config_option_keys diff = it_print_object_status->print_object->config().diff(new_config);
                        if (! diff.empty()) {
                            update_apply_status(it_print_object_status->print_object->invalidate_state_by_config_options(diff));
                            it_print_object_status->print_object->config_apply_only(new_config, diff, true);
                        }
                    }
                }

                bool old_user_modified = model_object.sla_points_status == sla::PointsStatus::UserModified;
                bool new_user_modified = model_object_new.sla_points_status == sla::PointsStatus::UserModified;
                if ((old_user_modified && ! new_user_modified) || // switching to automatic supports from manual supports
                    (! old_user_modified && new_user_modified) || // switching to manual supports from automatic supports
                    (new_user_modified && model_object.sla_support_points != model_object_new.sla_support_points)) {
                    if (it_print_object_status != print_object_status.end())
                        update_apply_status(it_print_object_status->print_object->invalidate_step(slaposSupportPoints));

                    model_object.sla_support_points = model_object_new.sla_support_points;
                }
                model_object.sla_points_status = model_object_new.sla_points_status;
                
                // Invalidate hollowing if drain holes have changed
                if (model_object.sla_drain_holes != model_object_new.sla_drain_holes)
                {
                    model_object.sla_drain_holes = model_object_new.sla_drain_holes;
                    update_apply_status(it_print_object_status->print_object->invalidate_step(slaposDrillHoles));
                }

                // Copy the ModelObject name, input_file and instances. The instances will compared against PrintObject instances in the next step.
                model_object.name       = model_object_new.name;
                model_object.input_file = model_object_new.input_file;
                model_object.clear_instances();
                model_object.instances.reserve(model_object_new.instances.size());
                for (const ModelInstance *model_instance : model_object_new.instances) {
                    model_object.instances.emplace_back(new ModelInstance(*model_instance));
                    model_object.instances.back()->set_model_object(&model_object);
                }
            }
        }

        std::vector<SLAPrintObject::Instance> new_instances = sla_instances(model_object);
        if (it_print_object_status != print_object_status.end() && it_print_object_status->status != PrintObjectStatus::Deleted) {
            // The SLAPrintObject is already there.
            if (new_instances.empty()) {
                const_cast<PrintObjectStatus&>(*it_print_object_status).status = PrintObjectStatus::Deleted;
            } else {
                if (new_instances != it_print_object_status->print_object->instances()) {
                    // Instances changed.
                    it_print_object_status->print_object->set_instances(new_instances);
                    update_apply_status(this->invalidate_step(slapsMergeSlicesAndEval));
                }
                print_objects_new.emplace_back(it_print_object_status->print_object);
                const_cast<PrintObjectStatus&>(*it_print_object_status).status = PrintObjectStatus::Reused;
            }
        } else if (! new_instances.empty()) {
            auto print_object = new SLAPrintObject(this, &model_object);

            // FIXME: this invalidates the transformed mesh in SLAPrintObject
            // which is expensive to calculate (especially the raw_mesh() call)
            print_object->set_trafo(sla_trafo(model_object), model_object.instances.front()->is_left_handed());

            print_object->set_instances(std::move(new_instances));

            print_object->config_apply(m_default_object_config, true);
            print_object->config_apply(model_object.config.get(), true);
            print_objects_new.emplace_back(print_object);
            new_objects = true;
        }
    }

    if (m_objects != print_objects_new) {
        this->call_cancel_callback();
        update_apply_status(this->invalidate_all_steps());
        m_objects = print_objects_new;
        // Delete the PrintObjects marked as Unknown or Deleted.
        for (auto &pos : print_object_status)
            if (pos.status == PrintObjectStatus::Unknown || pos.status == PrintObjectStatus::Deleted) {
                update_apply_status(pos.print_object->invalidate_all_steps());
                delete pos.print_object;
            }
        if (new_objects)
            update_apply_status(false);
    }

    if(m_objects.empty()) {
        m_printer_input = {};
        m_print_statistics = {};
    }

#ifdef _DEBUG
    check_model_ids_equal(m_model, model);
#endif /* _DEBUG */

    m_full_print_config = std::move(config);

    return static_cast<ApplyStatus>(apply_status);
}

// Generate a recommended output file name based on the format template, default extension, and template parameters
// (timestamps, object placeholders derived from the model, current placeholder prameters and print statistics.
// Use the final print statistics if available, or just keep the print statistics placeholders if not available yet (before the output is finalized).
std::string SLAPrint::output_filename(const std::string &filename_base) const
{
    DynamicConfig config = this->finished() ? this->print_statistics().config() : this->print_statistics().placeholders();
    std::string default_ext = get_default_extension(m_printer_config.sla_archive_format.value.c_str());
    if (default_ext.empty())
        default_ext = "sl1";

    default_ext.insert(default_ext.begin(), '.');

    config.set_key_value("default_output_extension",
                         new ConfigOptionString(default_ext));

    return this->PrintBase::output_filename(m_print_config.output_filename_format.value, default_ext, filename_base, &config);
}

std::string SLAPrint::validate(std::vector<std::string>*) const
{
    for(SLAPrintObject * po : m_objects) {

        const ModelObject *mo = po->model_object();
        bool supports_en = po->config().supports_enable.getBool();

        if(supports_en &&
           mo->sla_points_status == sla::PointsStatus::UserModified &&
           mo->sla_support_points.empty())
            return _u8L("Cannot proceed without support points! "
                     "Add support points or disable support generation.");

        sla::SupportTreeConfig cfg = make_support_cfg(po->config());

        double elv = cfg.object_elevation_mm;
        
        sla::PadConfig padcfg = make_pad_cfg(po->config());
        sla::PadConfig::EmbedObject &builtinpad = padcfg.embed_object;
        
        if(supports_en && !builtinpad.enabled && elv < cfg.head_fullwidth())
            return _u8L(
                "Elevation is too low for object. Use the \"Pad around "
                "object\" feature to print the object without elevation.");
        
        if(supports_en && builtinpad.enabled &&
           cfg.pillar_base_safety_distance_mm < builtinpad.object_gap_mm) {
            return _u8L(
                "The endings of the support pillars will be deployed on the "
                "gap between the object and the pad. 'Support base safety "
                "distance' has to be greater than the 'Pad object gap' "
                "parameter to avoid this.");
        }
        
        std::string pval = padcfg.validate();
        if (!pval.empty()) return pval;
    }

    double expt_max = m_printer_config.max_exposure_time.getFloat();
    double expt_min = m_printer_config.min_exposure_time.getFloat();
    double expt_cur = m_material_config.exposure_time.getFloat();

    if (expt_cur < expt_min || expt_cur > expt_max)
        return _u8L("Exposition time is out of printer profile bounds.");

    double iexpt_max = m_printer_config.max_initial_exposure_time.getFloat();
    double iexpt_min = m_printer_config.min_initial_exposure_time.getFloat();
    double iexpt_cur = m_material_config.initial_exposure_time.getFloat();

    if (iexpt_cur < iexpt_min || iexpt_cur > iexpt_max)
        return _u8L("Initial exposition time is out of printer profile bounds.");

    for (const std::string& prefix : { "", "branching" }) {

        double head_penetration = m_full_print_config.opt_float(prefix + "support_head_penetration");
        double head_width       = m_full_print_config.opt_float(prefix + "support_head_width");

        if (head_penetration > head_width) {
            return _u8L("Invalid Head penetration\n"
                        "Head penetration should not be greater than the Head width.\n"
                        "Please check value of Head penetration in Print Settings or Material Overrides.");
        }

        double pinhead_d = m_full_print_config.opt_float(prefix + "support_head_front_diameter");
        double pillar_d  = m_full_print_config.opt_float(prefix + "support_pillar_diameter");

        if (pinhead_d > pillar_d) {
            return _u8L("Invalid pinhead diameter\n"
                        "Pinhead front diameter should be smaller than the Pillar diameter.\n"
                        "Please check value of Pinhead front diameter in Print Settings or Material Overrides.");
        }
    }

    if ((!m_material_config.use_tilt.get_at(0) && m_material_config.tower_hop_height.get_at(0) == 0)
        || (!m_material_config.use_tilt.get_at(1) && m_material_config.tower_hop_height.get_at(1) == 0))
        return _u8L("Disabling the 'Use tilt' function causes the object to separate away from the film in the "
                    "vertical direction only. Therefore, it is necessary to set the 'Tower hop height' parameter "
                    "to reasonable value. The recommended value is 5 mm.");

    return "";
}

void SLAPrint::export_print(const std::string &fname, const ThumbnailsList &thumbnails, const std::string &projectname)
{
    if (m_archiver)
        m_archiver->export_print(fname, *this, thumbnails, projectname);
    else {
        throw ExportError(format(_u8L("Unknown archive format: %s"), m_printer_config.sla_archive_format.value));
    }
}

bool SLAPrint::is_prusa_print(const std::string& printer_model)
{
    static const std::vector<std::string> prusa_printer_models = { "SL1", "SL1S", "M1", "SLX" };
    for (const std::string& model : prusa_printer_models)
        if (model == printer_model)
            return true;

    return false;
}

bool SLAPrint::invalidate_step(SLAPrintStep step)
{
    bool invalidated = Inherited::invalidate_step(step);

    // propagate to dependent steps
    if (step == slapsMergeSlicesAndEval) {
        invalidated |= this->invalidate_all_steps();
    }

    return invalidated;
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

    BOOST_LOG_TRIVIAL(info) << "Start slicing process.";

#ifdef SLAPRINT_DO_BENCHMARK
    Benchmark bench;
#else
    struct {
        void start() {} void stop() {} double getElapsedSec() { return .0; }
    } bench;
#endif

    std::array<double, slaposCount + slapsCount> step_times {};

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
            step_times[slaposCount + currentstep] += bench.getElapsedSec();
            throw_if_canceled();
            set_done(currentstep);
        }
        
        st += printsteps.progressrange(currentstep);
    }

    // If everything vent well
    m_report_status(*this, 100, _u8L("Slicing done"));

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

bool SLAPrint::invalidate_state_by_config_options(const std::vector<t_config_option_key> &opt_keys, bool &invalidate_all_model_objects)
{
    using namespace std::string_view_literals;

    if (opt_keys.empty())
        return false;

    static constexpr StaticSet steps_full = {
        "initial_layer_height"sv,
        "material_correction"sv,
        "material_correction_x"sv,
        "material_correction_y"sv,
        "material_correction_z"sv,
        "material_print_speed"sv,
        "relative_correction"sv,
        "relative_correction_x"sv,
        "relative_correction_y"sv,
        "relative_correction_z"sv,
        "absolute_correction"sv,
        "elefant_foot_compensation"sv,
        "elefant_foot_min_width"sv,
        "zcorrection_layers"sv,
        "gamma_correction"sv,
    };

    // Cache the plenty of parameters, which influence the final rasterization only,
    // or they are only notes not influencing the rasterization step.
    static constexpr StaticSet steps_rasterize = {
        "min_exposure_time"sv,
        "max_exposure_time"sv,
        "exposure_time"sv,
        "min_initial_exposure_time"sv,
        "max_initial_exposure_time"sv,
        "initial_exposure_time"sv,
        "display_width"sv,
        "display_height"sv,
        "display_pixels_x"sv,
        "display_pixels_y"sv,
        "display_mirror_x"sv,
        "display_mirror_y"sv,
        "display_orientation"sv,
        "sla_archive_format"sv,
        "sla_output_precision"sv,
        // tilt params
        "delay_before_exposure"sv,
        "delay_after_exposure"sv,
        "tower_hop_height"sv,
        "tower_speed"sv,
        "use_tilt"sv,
        "tilt_down_initial_speed"sv,
        "tilt_down_offset_steps"sv,
        "tilt_down_offset_delay"sv,
        "tilt_down_finish_speed"sv,
        "tilt_down_cycles"sv,
        "tilt_down_delay"sv,
        "tilt_up_initial_speed"sv,
        "tilt_up_offset_steps"sv,
        "tilt_up_offset_delay"sv,
        "tilt_up_finish_speed"sv,
        "tilt_up_cycles"sv,
        "tilt_up_delay"sv,
        "area_fill"sv,
    };

    static StaticSet steps_ignore = {
        "bed_shape"sv,
        "max_print_height"sv,
        "printer_technology"sv,
        "output_filename_format"sv,
        "fast_tilt_time"sv,
        "slow_tilt_time"sv,
        "high_viscosity_tilt_time"sv,
        "bottle_cost"sv,
        "bottle_volume"sv,
        "bottle_weight"sv,
        "material_density"sv,
        "material_ow_support_pillar_diameter"sv,
        "material_ow_support_head_front_diameter"sv,
        "material_ow_support_head_penetration"sv,
        "material_ow_support_head_width"sv,
        "material_ow_branchingsupport_pillar_diameter"sv,
        "material_ow_branchingsupport_head_front_diameter"sv,
        "material_ow_branchingsupport_head_penetration"sv,
        "material_ow_branchingsupport_head_width"sv,
        "material_ow_elefant_foot_compensation"sv,
        "material_ow_support_points_density_relative"sv,
        "material_ow_absolute_correction"sv,
        "printer_model"sv,
    };

    std::vector<SLAPrintStep> steps;
    std::vector<SLAPrintObjectStep> osteps;
    bool invalidated = false;

    for (std::string_view opt_key : opt_keys) {
        if (steps_rasterize.find(opt_key) != steps_rasterize.end()) {
            // These options only affect the final rasterization, or they are just notes without influence on the output,
            // so there is nothing to invalidate.
            steps.emplace_back(slapsMergeSlicesAndEval);
        } else if (steps_ignore.find(opt_key) != steps_ignore.end()) {
            // These steps have no influence on the output. Just ignore them.
        } else if (steps_full.find(opt_key) != steps_full.end()) {
            steps.emplace_back(slapsMergeSlicesAndEval);
            osteps.emplace_back(slaposObjectSlice);
            invalidate_all_model_objects = true;
        } else {
            // All values should be covered.
            assert(false);
        }
    }

    sort_remove_duplicates(steps);
    for (SLAPrintStep step : steps)
        invalidated |= this->invalidate_step(step);
    sort_remove_duplicates(osteps);
    for (SLAPrintObjectStep ostep : osteps)
        for (SLAPrintObject *object : m_objects)
            invalidated |= object->invalidate_step(ostep);
    return invalidated;
}

// Returns true if an object step is done on all objects and there's at least one object.
bool SLAPrint::is_step_done(SLAPrintObjectStep step) const
{
    if (m_objects.empty())
        return false;
    std::scoped_lock<std::mutex> lock(this->state_mutex());
    for (const SLAPrintObject *object : m_objects)
        if (! object->is_step_done_unguarded(step))
            return false;
    return true;
}

SLAPrintObject::SLAPrintObject(SLAPrint *print, ModelObject *model_object)
    : Inherited(print, model_object)
{}

SLAPrintObject::~SLAPrintObject() {}

// Called by SLAPrint::apply().
// This method only accepts SLAPrintObjectConfig option keys.
bool SLAPrintObject::invalidate_state_by_config_options(const std::vector<t_config_option_key> &opt_keys)
{
    if (opt_keys.empty())
        return false;

    std::vector<SLAPrintObjectStep> steps;
    bool invalidated = false;
    for (const t_config_option_key &opt_key : opt_keys) {
        if (   opt_key == "hollowing_enable"
            || opt_key == "hollowing_min_thickness"
            || opt_key == "hollowing_quality"
            || opt_key == "hollowing_closing_distance"
            ) {
            steps.emplace_back(slaposHollowing);
        } else if (
               opt_key == "layer_height"
            || opt_key == "faded_layers"
            || opt_key == "pad_enable"
            || opt_key == "pad_wall_thickness"
            || opt_key == "supports_enable"
            || opt_key == "support_tree_type"
            || opt_key == "support_object_elevation"
            || opt_key == "branchingsupport_object_elevation"
            || opt_key == "pad_around_object"
            || opt_key == "pad_around_object_everywhere"
            || opt_key == "slice_closing_radius"
            || opt_key == "slicing_mode") {
            steps.emplace_back(slaposObjectSlice);
        } else if (
               opt_key == "support_points_density_relative"
            || opt_key == "support_enforcers_only"
            || opt_key == "support_points_minimal_distance"
            ) {
            steps.emplace_back(slaposSupportPoints);
        } else if (
               opt_key == "support_head_front_diameter"
            || opt_key == "support_head_penetration"
            || opt_key == "support_head_width"
            || opt_key == "support_pillar_diameter"
            || opt_key == "support_pillar_widening_factor"
            || opt_key == "support_small_pillar_diameter_percent"
            || opt_key == "support_max_weight_on_model"
            || opt_key == "support_max_bridges_on_pillar"
            || opt_key == "support_pillar_connection_mode"
            || opt_key == "support_buildplate_only"
            || opt_key == "support_base_diameter"
            || opt_key == "support_base_height"
            || opt_key == "support_critical_angle"
            || opt_key == "support_max_bridge_length"
            || opt_key == "support_max_pillar_link_distance"
            || opt_key == "support_base_safety_distance"
            || opt_key == "branchingsupport_head_front_diameter"
            || opt_key == "branchingsupport_head_penetration"
            || opt_key == "branchingsupport_head_width"
            || opt_key == "branchingsupport_pillar_diameter"
            || opt_key == "branchingsupport_pillar_widening_factor"
            || opt_key == "branchingsupport_small_pillar_diameter_percent"
            || opt_key == "branchingsupport_max_weight_on_model"
            || opt_key == "branchingsupport_max_bridges_on_pillar"
            || opt_key == "branchingsupport_pillar_connection_mode"
            || opt_key == "branchingsupport_buildplate_only"
            || opt_key == "branchingsupport_base_diameter"
            || opt_key == "branchingsupport_base_height"
            || opt_key == "branchingsupport_critical_angle"
            || opt_key == "branchingsupport_max_bridge_length"
            || opt_key == "branchingsupport_max_pillar_link_distance"
            || opt_key == "branchingsupport_base_safety_distance"
            || opt_key == "pad_object_gap"
            ) {
            steps.emplace_back(slaposSupportTree);
        } else if (
               opt_key == "pad_wall_height"
            || opt_key == "pad_brim_size"
            || opt_key == "pad_max_merge_distance"
            || opt_key == "pad_wall_slope"
            || opt_key == "pad_edge_radius"
            || opt_key == "pad_object_connector_stride"
            || opt_key == "pad_object_connector_width"
            || opt_key == "pad_object_connector_penetration"
            ) {
            steps.emplace_back(slaposPad);
        } else {
            // All keys should be covered.
            assert(false);
        }
    }

    sort_remove_duplicates(steps);
    for (SLAPrintObjectStep step : steps)
        invalidated |= this->invalidate_step(step);
    return invalidated;
}

bool SLAPrintObject::invalidate_step(SLAPrintObjectStep step)
{
    bool invalidated = Inherited::invalidate_step(step);
    // propagate to dependent steps
    if (step == slaposAssembly) {
        invalidated |= this->invalidate_all_steps();
    } else if (step == slaposHollowing) {
        invalidated |= invalidated |= this->invalidate_steps({ slaposDrillHoles, slaposObjectSlice, slaposSupportPoints, slaposSupportTree, slaposPad, slaposSliceSupports });
    } else if (step == slaposDrillHoles) {
        invalidated |= this->invalidate_steps({ slaposObjectSlice, slaposSupportPoints, slaposSupportTree, slaposPad, slaposSliceSupports });
        invalidated |= m_print->invalidate_step(slapsMergeSlicesAndEval);
    } else if (step == slaposObjectSlice) {
        invalidated |= this->invalidate_steps({ slaposSupportPoints, slaposSupportTree, slaposPad, slaposSliceSupports });
        invalidated |= m_print->invalidate_step(slapsMergeSlicesAndEval);
    } else if (step == slaposSupportPoints) {
        invalidated |= this->invalidate_steps({ slaposSupportTree, slaposPad, slaposSliceSupports });
        invalidated |= m_print->invalidate_step(slapsMergeSlicesAndEval);
    } else if (step == slaposSupportTree) {
        invalidated |= this->invalidate_steps({ slaposPad, slaposSliceSupports });
        invalidated |= m_print->invalidate_step(slapsMergeSlicesAndEval);
    } else if (step == slaposPad) {
        invalidated |= this->invalidate_steps({slaposSliceSupports});
        invalidated |= m_print->invalidate_step(slapsMergeSlicesAndEval);
    } else if (step == slaposSliceSupports) {
        invalidated |= m_print->invalidate_step(slapsMergeSlicesAndEval);
    }
    return invalidated;
}

bool SLAPrintObject::invalidate_all_steps()
{
    return Inherited::invalidate_all_steps() || m_print->invalidate_all_steps();
}

double SLAPrintObject::get_elevation() const {
    if (is_zero_elevation(m_config)) return 0.;

    bool en = m_config.supports_enable.getBool();

    double ret = en ? m_config.support_object_elevation.getFloat() : 0.;

    if(m_config.pad_enable.getBool()) {
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
        return m_config.support_object_elevation.getFloat();
    }

    return get_elevation();
}

Vec3d SLAPrint::relative_correction() const
{
    Vec3d corr(1., 1., 1.);

    if(printer_config().relative_correction.values.size() >= 2) {
        corr.x() = printer_config().relative_correction_x.value;
        corr.y() = printer_config().relative_correction_y.value;
        corr.z() = printer_config().relative_correction_z.value;
    }

    if(material_config().material_correction.values.size() >= 2) {
        corr.x() *= material_config().material_correction_x.value;
        corr.y() *= material_config().material_correction_y.value;
        corr.z() *= material_config().material_correction_z.value;
    }

    return corr;
}

namespace { // dummy empty static containers for return values in some methods
const std::vector<ExPolygons> EMPTY_SLICES;
const TriangleMesh EMPTY_MESH;
const indexed_triangle_set EMPTY_TRIANGLE_SET;
const ExPolygons EMPTY_SLICE;
const std::vector<sla::SupportPoint> EMPTY_SUPPORT_POINTS;
}

const SliceRecord SliceRecord::EMPTY(0, std::nanf(""), 0.f);

const std::vector<sla::SupportPoint>& SLAPrintObject::get_support_points() const
{
    return m_supportdata? m_supportdata->input.pts : EMPTY_SUPPORT_POINTS;
}

const std::vector<ExPolygons> &SLAPrintObject::get_support_slices() const
{
    // assert(is_step_done(slaposSliceSupports));
    if (!m_supportdata) return EMPTY_SLICES;
    return m_supportdata->support_slices;
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
    if (m_config.supports_enable.getBool() &&
        is_step_done(slaposSupportTree) &&
        m_supportdata)
        return m_supportdata->tree_mesh;

    return EMPTY_MESH;
}

const TriangleMesh& SLAPrintObject::pad_mesh() const
{
    if(m_config.pad_enable.getBool() && is_step_done(slaposPad) && m_supportdata)
        return m_supportdata->pad_mesh;

    return EMPTY_MESH;
}

const std::shared_ptr<const indexed_triangle_set> &
SLAPrintObject::get_mesh_to_print() const
{
    int s = last_completed_step();

    while (s > 0 && ! m_preview_meshes[s])
        --s;

    return m_preview_meshes[s];
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

sla::SupportPoints SLAPrintObject::transformed_support_points() const
{
    assert(model_object());

    return sla::transformed_support_points(*model_object(), trafo());
}

sla::DrainHoles SLAPrintObject::transformed_drainhole_points() const
{
    assert(model_object());

    return sla::transformed_drainhole_points(*model_object(), trafo());
}

DynamicConfig SLAPrintStatistics::config() const
{
    DynamicConfig config;
    const std::string print_time = Slic3r::short_time(get_time_dhms(float(this->estimated_print_time)));
    config.set_key_value("print_time", new ConfigOptionString(print_time));
    config.set_key_value("objects_used_material", new ConfigOptionFloat(this->objects_used_material));
    config.set_key_value("support_used_material", new ConfigOptionFloat(this->support_used_material));
    config.set_key_value("total_cost", new ConfigOptionFloat(this->total_cost));
    config.set_key_value("total_weight", new ConfigOptionFloat(this->total_weight));
    return config;
}

DynamicConfig SLAPrintStatistics::placeholders()
{
    DynamicConfig config;
    for (const char *key : {
        "print_time", "total_cost", "total_weight",
        "objects_used_material", "support_used_material" })
        config.set_key_value(key, new ConfigOptionString(std::string("{") + key + "}"));

    return config;
}

std::string SLAPrintStatistics::finalize_output_path(const std::string &path_in) const
{
    std::string final_path;
    try {
        boost::filesystem::path path(path_in);
        DynamicConfig cfg = this->config();
        PlaceholderParser pp;
        std::string new_stem = pp.process(path.stem().string(), 0, &cfg);
        final_path = (path.parent_path() / (new_stem + path.extension().string())).string();
    }
    catch (const std::exception &ex) {
        BOOST_LOG_TRIVIAL(error) << "Failed to apply the print statistics to the export file name: " << ex.what();
        final_path = path_in;
    }
    return final_path;
}

void SLAPrint::StatusReporter::operator()(SLAPrint &         p,
                                          double             st,
                                          const std::string &msg,
                                          unsigned           flags,
                                          const std::string &logmsg)
{
    m_st = st;
    BOOST_LOG_TRIVIAL(info)
        << st << "% " << msg << (logmsg.empty() ? "" : ": ") << logmsg
        << log_memory_info();

    p.set_status(int(std::round(st)), msg, flags);
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
