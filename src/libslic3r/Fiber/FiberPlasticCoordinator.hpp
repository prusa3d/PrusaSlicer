///|/ Copyright (c) Prusa Research 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_FiberPlasticCoordinator_hpp_
#define slic3r_FiberPlasticCoordinator_hpp_

#include <vector>
#include <string>
#include <memory>
#include "libslic3r/Point.hpp"

namespace Slic3r {

// Forward declarations
class FiberPrintObject;
class Layer;

/**
 * @brief Print sequence strategy types
 */
enum class FiberPrintSequence {
    PlasticFirst,      // Print all plastic layers first, then all fiber layers
    Alternating,       // Plastic layer → Fiber layer → Plastic layer → ...
    FiberOnTop,        // Fiber immediately after each plastic layer
};

/**
 * @brief Represents a single step in the print sequence
 */
struct PrintSequenceStep
{
    enum Type {
        PlasticLayer,  // Print a plastic layer
        FiberLayer,    // Print fiber on a layer
        Delay,         // Wait/delay
    };
    
    Type type;
    size_t layer_id;           // Which layer this step applies to
    coordf_t print_z;          // Z height for this step
    double delay_seconds;      // For Delay type: how long to wait
    
    PrintSequenceStep(Type t, size_t id, coordf_t z, double delay = 0.0)
        : type(t), layer_id(id), print_z(z), delay_seconds(delay) {}
};

using PrintSequence = std::vector<PrintSequenceStep>;

/**
 * @brief Configuration for plastic-fiber coordination
 */
struct FiberCoordinationConfig
{
    // Print sequence strategy
    FiberPrintSequence sequence = FiberPrintSequence::FiberOnTop;
    
    // Delay after plastic before starting fiber (seconds)
    double delay_after_plastic = 0.0;
    
    // Cooling time requirement (seconds)
    double cooling_time = 0.0;
    
    // Whether to wait for plastic to cool before fiber
    bool wait_for_cooling = false;
};

/**
 * @brief Coordinates when to print plastic vs fiber
 * 
 * This class determines the sequence of printing operations:
 * - When to print plastic layers
 * - When to print fiber layers
 * - Timing between operations
 * - Integration with FFF pipeline
 */
class FiberPlasticCoordinator
{
public:
    FiberPlasticCoordinator() = default;
    
    /**
     * @brief Generate print sequence for a fiber print object
     * 
     * Creates a sequence of print steps (plastic/fiber/delay) based on
     * the coordination configuration.
     * 
     * @param fiber_object The fiber print object
     * @param fff_layers The regular FFF layers (for reference)
     * @param config Coordination configuration
     * @return Print sequence (ordered list of steps)
     */
    PrintSequence generate_print_sequence(
        FiberPrintObject* fiber_object,
        const std::vector<Layer*>& fff_layers,
        const FiberCoordinationConfig& config
    );
    
    /**
     * @brief Generate sequence for plastic-first strategy
     */
    PrintSequence generate_plastic_first_sequence(
        FiberPrintObject* fiber_object,
        const std::vector<Layer*>& fff_layers,
        const FiberCoordinationConfig& config
    );
    
    /**
     * @brief Generate sequence for alternating strategy
     */
    PrintSequence generate_alternating_sequence(
        FiberPrintObject* fiber_object,
        const std::vector<Layer*>& fff_layers,
        const FiberCoordinationConfig& config
    );
    
    /**
     * @brief Generate sequence for fiber-on-top strategy
     */
    PrintSequence generate_fiber_on_top_sequence(
        FiberPrintObject* fiber_object,
        const std::vector<Layer*>& fff_layers,
        const FiberCoordinationConfig& config
    );
    
    /**
     * @brief Calculate delay time after plastic layer
     * 
     * Takes into account:
     * - Base delay from config
     * - Cooling time requirements
     * - Layer-specific factors
     */
    double calculate_delay_after_plastic(
        size_t layer_id,
        coordf_t layer_height,
        const FiberCoordinationConfig& config
    ) const;
    
    /**
     * @brief Validate print sequence
     * 
     * Checks for:
     * - Valid layer references
     * - Proper ordering
     * - Timing consistency
     */
    bool validate_sequence(
        const PrintSequence& sequence,
        FiberPrintObject* fiber_object,
        std::vector<std::string>& warnings
    ) const;
    
private:
    /**
     * @brief Add delay step to sequence if needed
     */
    void add_delay_if_needed(
        PrintSequence& sequence,
        coordf_t print_z,
        double delay_seconds
    );
};

} // namespace Slic3r

#endif /* slic3r_FiberPlasticCoordinator_hpp_ */

