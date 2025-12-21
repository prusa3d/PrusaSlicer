///|/ Copyright (c) Prusa Research 2024
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "SLMPrint.hpp"
#include "SLMPrintSteps.hpp" // IWYU pragma: keep
#include "SLMExporter.hpp"
#include "SLMPrintConfig.hpp" // For SLMExportFormatType enum

#include "format.hpp"
#include "StaticMap.hpp"

#include "Geometry.hpp"
#include "Thread.hpp"
#include "Execution/ExecutionTBB.hpp"

#include <unordered_set>
#include <numeric>

#include <boost/log/trivial.hpp>

// libSLM Writer includes - include after boost/log to avoid namespace conflicts
#include <Translators/SLMSOL/Writer.h>
#include <Translators/MTT/Writer.h>
#include <Translators/EOS/Writer.h>
#include <Translators/CLI/Writer.h>
#include <Translators/Realizer/Writer.h>

#include "I18N.hpp"

//! macro used to mark string used at localization,
//! return same string
#define _u8L(s) Slic3r::I18N::translate(s)

namespace Slic3r {

void SLMPrint::clear()
{
    std::scoped_lock<std::mutex> lock(this->state_mutex());
    // The following call should stop background processing if it is running.
    this->invalidate_all_steps();
    for (SLMPrintObject *object : m_objects)
        delete object;
    m_objects.clear();
    m_model.clear_objects();
}

std::vector<ObjectID> SLMPrint::print_object_ids() const
{
    std::vector<ObjectID> ids;
    // Reserve one more for the caller to append the ID of the Print itself.
    ids.reserve(m_objects.size() + 1);
    for (const SLMPrintObject *print_object : m_objects)
        ids.emplace_back(print_object->id());
    return ids;
}

PrintBase::ApplyStatus SLMPrint::apply(const Model &model, DynamicPrintConfig config, std::vector<std::string> *warnings)
{
    // Normalize the config.
    config.option("slm_print_settings_id", true);
    config.option("printer_settings_id", true);
    config.option("physical_printer_settings_id", true);
    
    // Collect changes to print config.
    t_config_option_keys print_diff    = m_print_config.diff(config);
    t_config_option_keys printer_diff   = m_printer_config.diff(config);

    // Do not use the ApplyStatus as we will use the max function when updating apply_status.
    unsigned int apply_status = PrintBase::APPLY_STATUS_UNCHANGED;
    auto update_apply_status = [&apply_status](bool invalidated)
        { apply_status = std::max<unsigned int>(apply_status, invalidated ? PrintBase::APPLY_STATUS_INVALIDATED : PrintBase::APPLY_STATUS_CHANGED); };
    if (! (print_diff.empty() && printer_diff.empty()))
        update_apply_status(false);

    // Grab the lock for the Print / PrintObject milestones.
    std::scoped_lock<std::mutex> lock(this->state_mutex());

    // The following call may stop the background processing.
    bool invalidate_all_model_objects = false;
    if (! print_diff.empty())
        update_apply_status(this->invalidate_state_by_config_options(print_diff, invalidate_all_model_objects));
    if (! printer_diff.empty())
        update_apply_status(this->invalidate_state_by_config_options(printer_diff, invalidate_all_model_objects));

    // Apply config changes
    if (! print_diff.empty())
        m_print_config.apply_only(config, print_diff, true);
    if (! printer_diff.empty())
        m_printer_config.apply_only(config, printer_diff, true);

    // Handle model changes
    if (m_model.id() != model.id()) {
        this->invalidate_all_steps();
        for (SLMPrintObject *object : m_objects)
            delete object;
        m_objects.clear();
        invalidate_all_model_objects = true;
    }

    // Store the model
    m_model = model;

    // Create SLMPrintObject instances for each ModelObject
    for (ModelObject *model_object : m_model.objects) {
        if (!model_object->instances.empty() && model_object->instances.front()->is_printable()) {
            // Check if we already have a print object for this model object
            bool found = false;
            for (SLMPrintObject *existing_obj : m_objects) {
                if (existing_obj->model_object()->id() == model_object->id()) {
                    found = true;
                    break;
                }
            }
            
            if (!found) {
                // Create new SLMPrintObject
                SLMPrintObject *print_object = new SLMPrintObject(this, model_object);
                
                // Set instances
                std::vector<SLMPrintObject::Instance> instances;
                if (!model_object->instances.empty()) {
                    const Transform3d& trafo0 = model_object->instances.front()->get_matrix();
                    for (ModelInstance *model_instance : model_object->instances) {
                        if (model_instance->is_printable()) {
                            instances.emplace_back(
                                model_instance->id(),
                                Point::new_scale(model_instance->get_offset(X), model_instance->get_offset(Y)),
                                float(Geometry::rotation_diff_z(trafo0, model_instance->get_matrix())));
                        }
                    }
                }
                print_object->set_instances(std::move(instances));
                
                m_objects.emplace_back(print_object);
            }
        }
    }

    // Remove print objects for model objects that no longer exist
    auto it = m_objects.begin();
    while (it != m_objects.end()) {
        bool found = false;
        for (ModelObject *model_object : m_model.objects) {
            if ((*it)->model_object()->id() == model_object->id()) {
                found = true;
                break;
            }
        }
        if (!found) {
            delete *it;
            it = m_objects.erase(it);
            invalidate_all_model_objects = true;
        } else {
            ++it;
        }
    }

    return ApplyStatus(apply_status);
}

bool SLMPrint::invalidate_state_by_config_options(const std::vector<t_config_option_key> &opt_keys, bool &invalidate_all_model_objects)
{
    // TODO: Phase 2 - Implement config-based step invalidation
    // This will determine which steps need to be invalidated based on config changes
    // For now, invalidate all steps if any config changes
    bool invalidated = false;
    if (!opt_keys.empty()) {
        invalidated = this->invalidate_all_steps();
        invalidate_all_model_objects = true;
    }
    return invalidated;
}

void SLMPrint::process()
{
    if (m_objects.empty())
        return;

    name_tbb_thread_pool_threads_set_locale();

    SLMPrint::Steps printsteps(this);

    // Process object-level steps
    std::vector<Slic3r::SLMPrintObjectStep> obj_steps = {
        Slic3r::slmposSlice,
        Slic3r::slmposGenerateHatches
    };

    // Process print-level steps
    std::vector<Slic3r::SLMPrintStep> print_steps = {
        Slic3r::slmpsSlice,
        Slic3r::slmpsGenerateHatches
    };

    // Use 0.0 as initial status (min_objstatus is private, but it's just 0.0)
    double st = 0.0;
    BOOST_LOG_TRIVIAL(info) << "Start SLM slicing process.";

    // Process all objects
    printsteps.process_objects(obj_steps, st, st);
    
    // Process print-level steps
    printsteps.process_print(print_steps, st);

    BOOST_LOG_TRIVIAL(info) << "SLM slicing process finished.";
}

bool SLMPrint::is_step_done(SLMPrintObjectStep step) const
{
    if (m_objects.empty())
        return false;
    
    for (const SLMPrintObject* obj : m_objects)
        if (!obj->is_step_done(step))
            return false;
    
    return true;
}

std::string SLMPrint::output_filename(const std::string &filename_base) const
{
    // TODO: Implement filename generation for SLM files
    // Similar to SLAPrint::output_filename()
    if (filename_base.empty())
        return "output.slm";
    return filename_base + ".slm";
}

std::string SLMPrint::validate(std::vector<std::string>* warnings) const
{
    // TODO: Implement validation
    // Check if model is valid, config is valid, etc.
    return std::string();
}

void SLMPrint::export_print(const std::string &fname)
{
    export_print(fname, nullptr);
}

void SLMPrint::export_print(const std::string &fname, std::function<void(int, const std::string&)> progress_callback)
{
    if (m_objects.empty()) {
        BOOST_LOG_TRIVIAL(warning) << "SLM: Cannot export empty print";
        if (progress_callback) progress_callback(0, "No objects to export");
        return;
    }

    BOOST_LOG_TRIVIAL(info) << "Exporting SLM print to: " << fname;
    
    if (progress_callback) progress_callback(0, "Starting export...");

    // Get export format from config
    Slic3r::SLMExportFormatType export_format = m_print_config.slm_export_format.value;

    // Convert PrusaSlicer data to libSLM format
    slm::Header header = SLMExporter::create_header(*this);
    std::vector<slm::Model::Ptr> models;
    std::vector<slm::Layer::Ptr> layers;
    bool contour_first = m_print_config.slm_contour_first.value;

    // Handle multiple objects: each object gets its own model
    uint64_t model_id = 1;
    uint64_t global_layer_id = 0;
    size_t total_layers = 0;
    
    // Count total layers first for progress reporting
    for (const SLMPrintObject *obj : m_objects) {
        if (obj && !obj->scan_paths().empty()) {
            total_layers += obj->scan_paths().size();
        }
    }

    if (progress_callback) {
        progress_callback(5, "Converting objects to models...");
    }

    for (const SLMPrintObject *obj : m_objects) {
        if (!obj || obj->scan_paths().empty())
            continue;

        // Create model for this object
        auto model = SLMExporter::create_model(*this, model_id, obj);
        models.push_back(model);

        if (progress_callback && m_objects.size() > 1) {
            std::string msg = "Processing object " + std::to_string(model_id) + " of " + std::to_string(m_objects.size());
            progress_callback(5 + int((model_id - 1) * 10 / m_objects.size()), msg);
        }

        // Convert layers for this object
        const auto &scan_paths = obj->scan_paths();
        for (size_t layer_idx = 0; layer_idx < scan_paths.size(); ++layer_idx) {
            auto layer = SLMExporter::create_layer(
                scan_paths[layer_idx],
                global_layer_id++,
                uint32_t(model_id),  // Use model_id for geometry
                contour_first
            );
            if (layer && !layer->geometry().empty()) {
                layers.push_back(layer);
            }
            
            // Progress reporting during layer conversion
            if (progress_callback && total_layers > 0) {
                int progress = 15 + int((global_layer_id * 60) / total_layers);
                std::string msg = "Converting layer " + std::to_string(global_layer_id) + " of " + std::to_string(total_layers);
                progress_callback(progress, msg);
            }
        }

        model_id++;
    }

    if (layers.empty()) {
        BOOST_LOG_TRIVIAL(warning) << "SLM: No layers to export";
        if (progress_callback) progress_callback(0, "No layers to export");
        return;
    }

    if (progress_callback) {
        progress_callback(75, "Writing file...");
    }

    // Create appropriate writer based on format
    try {
        switch (export_format) {
        case Slic3r::SLMExportFormatType::slmefSLM: {
            slm::SLMSol::Writer writer(fname);
            writer.setSortLayers(true);
            writer.write(header, models, layers);
            BOOST_LOG_TRIVIAL(info) << "SLM: Exported to SLM Solutions format (.slm)";
            if (progress_callback) progress_callback(90, "Writing SLM format...");
            break;
        }
        case Slic3r::SLMExportFormatType::slmefMTT: {
            slm::MTT::Writer writer(fname);
            writer.setSortLayers(true);
            writer.write(header, models, layers);
            BOOST_LOG_TRIVIAL(info) << "SLM: Exported to Renishaw format (.mtt)";
            if (progress_callback) progress_callback(90, "Writing MTT format...");
            break;
        }
        case Slic3r::SLMExportFormatType::slmefSLI: {
            slm::eos::Writer writer(fname);
            writer.write(header, models, layers);
            BOOST_LOG_TRIVIAL(info) << "SLM: Exported to EOS format (.sli)";
            if (progress_callback) progress_callback(90, "Writing SLI format...");
            break;
        }
        case Slic3r::SLMExportFormatType::slmefCLI: {
            slm::cli::Writer writer(fname);
            writer.write(header, models, layers);
            BOOST_LOG_TRIVIAL(info) << "SLM: Exported to CLI format (.cli)";
            if (progress_callback) progress_callback(90, "Writing CLI format...");
            break;
        }
        case Slic3r::SLMExportFormatType::slmefREA: {
            slm::realizer::Writer writer(fname);
            writer.write(header, models, layers);
            BOOST_LOG_TRIVIAL(info) << "SLM: Exported to DMG Mori Realizer format (.rea)";
            if (progress_callback) progress_callback(90, "Writing REA format...");
            break;
        }
        default: {
            BOOST_LOG_TRIVIAL(error) << "SLM: Unknown export format, using SLM format";
            slm::SLMSol::Writer writer(fname);
            writer.setSortLayers(true);
            writer.write(header, models, layers);
            if (progress_callback) progress_callback(90, "Writing default format...");
            break;
        }
        }
    } catch (const std::exception &e) {
        BOOST_LOG_TRIVIAL(error) << "SLM: Export failed: " << e.what();
        if (progress_callback) progress_callback(0, "Export failed: " + std::string(e.what()));
        throw;
    }

    BOOST_LOG_TRIVIAL(info) << "SLM: Export completed successfully";
    if (progress_callback) progress_callback(100, "Export completed successfully");
}

// SLMPrintObject implementation
SLMPrintObject::SLMPrintObject(SLMPrint* print, ModelObject* model_object)
    : Inherited(print, model_object), m_model_object(model_object)
{
}

SLMPrintObject::~SLMPrintObject()
{
}

bool SLMPrintObject::invalidate_step(SLMPrintObjectStep step)
{
    // TODO: Implement step invalidation logic
    // This will invalidate the step and dependent steps
    return Inherited::invalidate_step(step);
}

bool SLMPrintObject::invalidate_all_steps()
{
    // TODO: Implement all steps invalidation
    return Inherited::invalidate_all_steps();
}

bool SLMPrintObject::invalidate_state_by_config_options(const std::vector<t_config_option_key> &opt_keys)
{
    // TODO: Phase 2 - Implement config-based invalidation
    // This will determine which steps need to be invalidated based on config changes
    return false;
}

} // namespace Slic3r

