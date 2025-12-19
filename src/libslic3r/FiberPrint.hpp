///|/ Copyright (c) Prusa Research 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_FiberPrint_hpp_
#define slic3r_FiberPrint_hpp_

#include <vector>
#include <memory>
#include <string>

#include "PrintBase.hpp"
#include "Point.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/ObjectID.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Fiber/FiberLayer.hpp"
#include "libslic3r/Fiber/FiberPrintObject.hpp"

// Forward declaration
namespace Slic3r {
    struct FiberStatistics;
}

namespace Slic3r {

/**
 * @brief Fiber printing step enums
 * 
 * These define the high-level processing steps for fiber printing.
 * Similar to SLAPrintStep, but specific to fiber printing workflow.
 */
enum FiberPrintStep : unsigned int {
    // Placeholder for future steps
    // Example: fiberpsPlacement, fiberpsPathPlanning, fiberpsGCodeGeneration
    fiberpsCount = 0  // Start with 0 steps, will be expanded in later phases
};

/**
 * @brief Fiber print object step enums
 * 
 * These define the per-object processing steps for fiber printing.
 * Similar to SLAPrintObjectStep, but specific to fiber printing.
 */
enum FiberPrintObjectStep : unsigned int {
    // Placeholder for future object-level steps
    // Example: fiberposSlice, fiberposPlacement, fiberposPathGeneration
    fiberposCount = 0  // Start with 0 steps, will be expanded in later phases
};

class FiberPrint;
class GLCanvas;

/**
 * @brief This class is the high level FSM for the fiber printing process.
 *
 * It follows the same pattern as SLAPrint, providing a state machine
 * for fiber printing with background processing support.
 * 
 * This class will:
 * - Manage fiber print objects
 * - Coordinate fiber placement and path planning
 * - Generate G-code for fiber printing
 * - Handle configuration and validation
 */
class FiberPrint : public PrintBaseWithState<FiberPrintStep, fiberpsCount>
{
private:
    typedef PrintBaseWithState<FiberPrintStep, fiberpsCount> Inherited;
    
    class Steps; // See FiberPrintSteps.cpp
    
public:
    FiberPrint() = default;
    
    virtual ~FiberPrint() override { this->clear(); }
    
    PrinterTechnology technology() const noexcept override { return ptFFF; } // Fiber uses FFF base technology
    
    void clear() override;
    bool empty() const override { return m_objects.empty(); }
    
    // List of existing PrintObject IDs, to remove notifications for non-existent IDs.
    std::vector<ObjectID> print_object_ids() const override;
    
    ApplyStatus apply(const Model &model, DynamicPrintConfig config, std::vector<std::string> *warnings = nullptr) override;
    
    void set_task(const TaskParams &params) override { 
        PrintBaseWithState<FiberPrintStep, fiberpsCount>::set_task_impl(params, m_objects); 
    }
    
    void process() override;
    
    void finalize() override { 
        PrintBaseWithState<FiberPrintStep, fiberpsCount>::finalize_impl(m_objects); 
    }
    
    void cleanup() override {}
    
    // Returns true if an object step is done on all objects and there's at least one object.
    bool is_step_done(FiberPrintObjectStep step) const;
    
    // Returns true if the last step was finished with success.
    bool finished() const override { 
        // Will be implemented when steps are defined
        return true; // Placeholder
    }
    
    using PrintObjects = std::vector<FiberPrintObject*>;
    const PrintObjects& objects() const { return m_objects; }
    
    // PrintObject by its ObjectID
    const FiberPrintObject* get_print_object_by_model_object_id(ObjectID object_id) const;
    const FiberPrintObject* get_object(ObjectID object_id) const;
    
    // Configuration accessors (will be implemented when FiberPrintConfig is created)
    // const FiberPrintConfig& print_config() const { return m_print_config; }
    
    std::string output_filename(const std::string &filename_base = std::string()) const override;
    
    std::string validate(std::vector<std::string>* warnings = nullptr) const override;
    
    // Status reporting
    class StatusReporter
    {
        double m_st = 0;
        
    public:
        void operator()(FiberPrint &p,
                        double st,
                        const std::string &msg,
                        unsigned flags = SlicingStatus::DEFAULT,
                        const std::string &logmsg = "");
        
        double status() const { return m_st; }
    } m_report_status;
    
    friend FiberPrintObject;
    
private:
    // Configuration (will be added when FiberPrintConfig is implemented)
    // FiberPrintConfig m_print_config;
    
    PrintObjects m_objects;
    
    // Statistics (Phase 7.3)
public:
    Slic3r::FiberStatistics calculate_statistics(const DynamicPrintConfig* config = nullptr) const;
};

} // namespace Slic3r

#endif /* slic3r_FiberPrint_hpp_ */

