///|/ Copyright (c) Prusa Research 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_FiberPrintObject_hpp_
#define slic3r_FiberPrintObject_hpp_

#include <vector>
#include <memory>
#include "libslic3r/PrintBase.hpp"
#include "libslic3r/Fiber/FiberLayer.hpp"
#include "libslic3r/Fiber/FiberPlacement.hpp"
#include "libslic3r/Fiber/PathPlanner.hpp"
#include "libslic3r/Fiber/FiberPlasticCoordinator.hpp"
#include "libslic3r/Fiber/FiberGCode.hpp"
#include "libslic3r/Layer.hpp"

namespace Slic3r {

// Forward declarations
class PrintObject;  // Regular FFF PrintObject
class FiberPrint;
enum FiberPrintObjectStep : unsigned int;

// Base class typedef (moved here to avoid circular dependency)
using _FiberPrintObjectBase =
    PrintObjectBaseWithState<FiberPrint, FiberPrintObjectStep, 0>;

/**
 * @brief Fiber print object - extends PrintObjectBaseWithState
 * 
 * This class represents a single object in a fiber print job.
 * It stores both the regular FFF layers (from slicing) and the fiber layers
 * (for fiber placement).
 */
class FiberPrintObject : public _FiberPrintObjectBase
{
public:
    FiberPrintObject(FiberPrint* print, ModelObject* model_object);
    ~FiberPrintObject();
    
    ModelObject* model_object() const { return const_cast<ModelObject*>(PrintObjectBase::model_object()); }
    
    // Expose protected methods from base class for PrintBaseWithState
    using _FiberPrintObjectBase::is_step_enabled_unguarded;
    using _FiberPrintObjectBase::is_step_started_unguarded;
    using _FiberPrintObjectBase::enable_step_unguarded;
    using _FiberPrintObjectBase::finalize_impl;
    
    // Expose step enum types
    static constexpr int PrintObjectStepEnumSize = 0;
    using PrintObjectStepEnum = FiberPrintObjectStep;
    
    // Get the corresponding regular PrintObject (for FFF slicing)
    // This will be set when we integrate with existing slicing
    PrintObject* fff_print_object() const { return m_fff_print_object; }
    void set_fff_print_object(PrintObject* po) { m_fff_print_object = po; }
    
    // Fiber layers - one per regular layer
    const std::vector<FiberLayer>& fiber_layers() const { return m_fiber_layers; }
    std::vector<FiberLayer>& fiber_layers() { return m_fiber_layers; }
    
    // Print sequence (plastic/fiber coordination)
    PrintSequence m_print_sequence;
    
    // Get fiber layer by ID (corresponds to Layer::id())
    FiberLayer* get_fiber_layer(size_t layer_id);
    const FiberLayer* get_fiber_layer(size_t layer_id) const;
    
    // Get fiber layer by print_z
    FiberLayer* get_fiber_layer_at_printz(coordf_t print_z);
    const FiberLayer* get_fiber_layer_at_printz(coordf_t print_z) const;
    
    // Create fiber layers from regular layers (after slicing)
    // This integrates with existing FFF slicing
    void create_fiber_layers_from_fff_layers();
    
    // Generate fiber paths for all layers using placement strategy
    void generate_fiber_paths(const FiberPlacementConfig& config);
    
    // Plan continuous paths from fiber path segments (Phase 3)
    void plan_continuous_paths(const PathPlanningConfig& path_config);
    
    // Generate print sequence coordinating plastic and fiber (Phase 4)
    PrintSequence generate_print_sequence(const FiberCoordinationConfig& config);
    
    // Get print sequence (if already generated)
    const PrintSequence& print_sequence() const { return m_print_sequence; }
    
    // Generate G-code for fiber printing (Phase 5)
    std::string generate_gcode(const FiberGCodeConfig& config);
    
    // Clear all fiber layers
    void clear_fiber_layers();
    
    // Check if object has been sliced
    bool is_sliced() const { return m_fff_print_object != nullptr && !m_fiber_layers.empty(); }
    
private:
    // Reference to the regular FFF PrintObject (for reusing slicing)
    PrintObject* m_fff_print_object;
    
    // Fiber layers - one per regular layer
    std::vector<FiberLayer> m_fiber_layers;
};

} // namespace Slic3r

#endif /* slic3r_FiberPrintObject_hpp_ */

