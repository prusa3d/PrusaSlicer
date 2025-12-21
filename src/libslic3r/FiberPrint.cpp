///|/ Copyright (c) Prusa Research 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "FiberPrint.hpp"
#include "FiberPrintSteps.hpp" // IWYU pragma: keep
#include "format.hpp"
#include "libslic3r/Fiber/FiberStatistics.hpp"

#include <boost/log/trivial.hpp>
#include "I18N.hpp"

//! macro used to mark string used at localization,
//! return same string
#define _u8L(s) Slic3r::I18N::translate(s)

namespace Slic3r {

#include "libslic3r/Fiber/FiberPrintObject.hpp"

void FiberPrint::clear()
{
    // Clear all print objects
    for (FiberPrintObject* object : m_objects) {
        delete object;
    }
    m_objects.clear();
    
    // Invalidate all steps
    this->invalidate_all_steps();
}

std::vector<ObjectID> FiberPrint::print_object_ids() const
{
    std::vector<ObjectID> ids;
    ids.reserve(m_objects.size());
    for (const FiberPrintObject* object : m_objects) {
        ids.emplace_back(object->id());
    }
    return ids;
}

PrintBase::ApplyStatus FiberPrint::apply(const Model &model, DynamicPrintConfig config, std::vector<std::string> *warnings)
{
    // Clear existing objects
    this->clear();
    
    // TODO: Apply configuration
    // This will be implemented when FiberPrintConfig is created
    
    // Create print objects from model objects
    for (ModelObject* model_object : model.objects) {
        if (model_object->id().valid()) {
            FiberPrintObject* print_object = new FiberPrintObject(this, model_object);
            m_objects.push_back(print_object);
        }
    }
    
    // Invalidate all steps since we have new objects
    this->invalidate_all_steps();
    
    return PrintBase::ApplyStatus::APPLY_STATUS_UNCHANGED;
}

void FiberPrint::process()
{
    // This will be implemented when Steps are defined
    // For now, it's a placeholder
    BOOST_LOG_TRIVIAL(info) << "FiberPrint::process() - Placeholder implementation";
}

bool FiberPrint::is_step_done(FiberPrintObjectStep step) const
{
    if (m_objects.empty())
        return false;
    
    for (const FiberPrintObject* object : m_objects) {
        if (!object->is_step_done(step))
            return false;
    }
    
    return true;
}

const FiberPrintObject* FiberPrint::get_print_object_by_model_object_id(ObjectID object_id) const
{
    auto it = std::find_if(m_objects.begin(), m_objects.end(),
        [object_id](const FiberPrintObject* obj) { 
            return obj->model_object()->id() == object_id; 
        });
    return (it == m_objects.end()) ? nullptr : *it;
}

const FiberPrintObject* FiberPrint::get_object(ObjectID object_id) const
{
    auto it = std::find_if(m_objects.begin(), m_objects.end(),
        [object_id](const FiberPrintObject* obj) { 
            return obj->id() == object_id; 
        });
    return (it == m_objects.end()) ? nullptr : *it;
}

std::string FiberPrint::output_filename(const std::string &filename_base) const
{
    // TODO: Generate appropriate output filename for fiber printing
    // This might include fiber-specific extensions or naming conventions
    return filename_base.empty() ? "fiber_print.gcode" : filename_base;
}

std::string FiberPrint::validate(std::vector<std::string>* warnings) const
{
    // TODO: Add validation logic
    // Check for:
    // - Valid configuration
    // - Compatible printer settings
    // - Model validity
    // - Fiber-specific constraints
    
    if (m_objects.empty()) {
        if (warnings)
            warnings->push_back(_u8L("No objects to print"));
        return _u8L("No objects to print");
    }
    
    return std::string(); // Empty string means valid
}

Slic3r::FiberStatistics FiberPrint::calculate_statistics(const DynamicPrintConfig* config) const
{
    // Phase 7.3: Calculate fiber statistics
    if (config == nullptr) {
        // Try to get config from somewhere else if needed
        // For now, create a default config
        DynamicPrintConfig default_config;
        return FiberStatistics::calculate(this, &default_config);
    }
    return FiberStatistics::calculate(this, config);
}

void FiberPrint::StatusReporter::operator()(FiberPrint &p,
                                            double st,
                                            const std::string &msg,
                                            unsigned flags,
                                            const std::string &logmsg)
{
    m_st = st;
    // TODO: Implement status reporting
    // This will integrate with PrusaSlicer's status reporting system
    if (!logmsg.empty()) {
        BOOST_LOG_TRIVIAL(info) << logmsg;
    }
}

} // namespace Slic3r

