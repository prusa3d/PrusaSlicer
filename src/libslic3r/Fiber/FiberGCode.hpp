///|/ Copyright (c) Prusa Research 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_FiberGCode_hpp_
#define slic3r_FiberGCode_hpp_

#include <string>
#include <vector>
#include "libslic3r/Fiber/FiberLayer.hpp"
#include "libslic3r/Fiber/FiberPlasticCoordinator.hpp"
#include "libslic3r/Fiber/FiberAdvanced.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/GCode/GCodeWriter.hpp"

namespace Slic3r {

// Forward declarations
class FiberPrintObject;
class GCodeWriter;

/**
 * @brief G-code flavor for fiber printing
 */
enum class FiberGCodeFlavor {
    Marlin,         // Standard Marlin commands
    RepRap,         // RepRap firmware
    Custom,         // Custom commands (user-defined)
};

/**
 * @brief Fiber printing method
 */
enum class FiberPrintMethod {
    DualPrinthead,          // Method 1: Separate printheads (T0=plastic, T1=fiber)
    CoExtrusion,            // Method 2a: Co-extrusion during printing (placeholder)
    PreEmbeddedFilament,    // Method 2b: Pre-embedded fiber filament
};

/**
 * @brief Configuration for fiber G-code generation
 */
struct FiberGCodeConfig
{
    // Fiber printing method
    FiberPrintMethod method = FiberPrintMethod::DualPrinthead;  // Default: Method 1
    
    // G-code flavor
    FiberGCodeFlavor flavor = FiberGCodeFlavor::Marlin;
    
    // Fiber extruder ID (0-based, e.g., 1 means T1)
    // Used only for Method 1 (DualPrinthead)
    // This is the extruder that will be used for fiber printing
    // Uses PrusaSlicer's existing multi-extruder system
    unsigned int fiber_extruder_id = 1;  // Default: Extruder 1 (T1)
    
    // Plastic extruder ID (0-based, typically 0)
    // This is the extruder used for plastic printing
    unsigned int plastic_extruder_id = 0;  // Default: Extruder 0 (T0)
    
    // For Method 2 (embedded fiber): Extruder that has fiber embedded
    // For Method 2b (PreEmbeddedFilament): This is the extruder with pre-embedded fiber filament
    // For Method 2a (CoExtrusion): Placeholder for future implementation
    unsigned int embedded_fiber_extruder_id = 0;  // Default: Extruder 0
    
    // Fiber start command (e.g., "M106" or custom)
    // Note: Tool change to fiber extruder is handled by GCodeWriter::toolchange()
    // This command is additional fiber-specific activation (if needed)
    std::string fiber_start_command = "M106";  // Default: fan on (can be customized)
    
    // Fiber stop command (e.g., "M107" or custom)
    // Note: Tool change away from fiber is handled by GCodeWriter::toolchange()
    // This command is additional fiber-specific deactivation (if needed)
    std::string fiber_stop_command = "M107";   // Default: fan off (can be customized)
    
    // Fiber speed control command (e.g., "M108" or custom)
    std::string fiber_speed_command = "M108";  // Placeholder - customize for your printer
    
    // Fiber pressure/tension control command
    std::string fiber_pressure_command = "M109"; // Placeholder - customize for your printer
    
    // Fiber speed (mm/s)
    double fiber_speed = 50.0;
    
    // Fiber pressure/tension (if applicable)
    double fiber_pressure = 0.0;
    
    // Enable comments in G-code
    bool enable_comments = true;
    
    // Layer change comment format
    std::string layer_comment_format = ";LAYER:{layer_id}";
    
    // Fiber path comment format
    std::string fiber_comment_format = ";FIBER_PATH:{path_id}";
    
    // Future: Support for multiple fiber types via multiple extruders
    // For now, we support one fiber extruder, but architecture is extensible
    // std::map<FiberType, unsigned int> fiber_type_to_extruder; // Future feature
};

/**
 * @brief G-code writer for fiber printing
 * 
 * This class generates G-code commands for fiber printing, including:
 * - Fiber start/stop commands
 * - Fiber path movements
 * - Tool changes (plastic ↔ fiber)
 * - Integration with existing G-code pipeline
 */
class FiberGCodeWriter
{
public:
    FiberGCodeWriter() = default;
    
    /**
     * @brief Generate G-code for a fiber path
     * 
     * Converts a FiberPath to G-code movement commands.
     * 
     * @param path The fiber path to convert
     * @param writer The G-code writer (for coordinate conversion, etc.)
     * @param config Fiber G-code configuration
     * @return G-code string for the path
     */
    std::string generate_path_gcode(
        const FiberPath& path,
        GCodeWriter& writer,
        const FiberGCodeConfig& config
    );
    
    /**
     * @brief Generate G-code for an advanced fiber path with variable speed
     * 
     * Converts an AdvancedFiberPath to G-code movement commands.
     * Supports variable speed profiles and custom pre/post G-code.
     * 
     * @param path The advanced fiber path to convert
     * @param writer The G-code writer (for coordinate conversion, etc.)
     * @param config Fiber G-code configuration
     * @return G-code string for the path
     */
    std::string generate_advanced_path_gcode(
        const AdvancedFiberPath& path,
        GCodeWriter& writer,
        const FiberGCodeConfig& config
    );
    
    /**
     * @brief Generate G-code for fiber start
     * 
     * Generates commands to start fiber deposition.
     */
    std::string generate_fiber_start(
        const FiberGCodeConfig& config,
        double speed = 0.0,
        double pressure = 0.0
    ) const;
    
    /**
     * @brief Generate G-code for fiber stop
     * 
     * Generates commands to stop fiber deposition.
     */
    std::string generate_fiber_stop(const FiberGCodeConfig& config) const;
    
    /**
     * @brief Generate G-code for tool change (plastic → fiber)
     * 
     * Uses PrusaSlicer's existing multi-extruder tool change system.
     * Switches to the fiber extruder (T{fiber_extruder_id}).
     * 
     * @param writer The G-code writer (manages extruder state)
     * @param config Fiber G-code configuration (contains fiber_extruder_id)
     * @return G-code string for tool change to fiber extruder
     */
    std::string generate_toolchange_to_fiber(
        GCodeWriter& writer,
        const FiberGCodeConfig& config
    ) const;
    
    /**
     * @brief Generate G-code for tool change (fiber → plastic)
     * 
     * Uses PrusaSlicer's existing multi-extruder tool change system.
     * Switches to the plastic extruder (typically T0).
     * 
     * @param writer The G-code writer (manages extruder state)
     * @param config Fiber G-code configuration (contains plastic_extruder_id)
     * @return G-code string for tool change to plastic extruder
     */
    std::string generate_toolchange_to_plastic(
        GCodeWriter& writer,
        const FiberGCodeConfig& config
    ) const;
    
    /**
     * @brief Generate G-code for delay
     * 
     * Generates G4 (dwell) command for delays.
     */
    std::string generate_delay(double seconds, const FiberGCodeConfig& config) const;
    
    /**
     * @brief Generate layer marker comment
     */
    std::string generate_layer_comment(size_t layer_id, const FiberGCodeConfig& config) const;
    
    /**
     * @brief Generate fiber path comment
     */
    std::string generate_path_comment(size_t path_id, const FiberGCodeConfig& config) const;
    
    /**
     * @brief Generate G-code for a complete fiber layer
     * 
     * Generates all G-code for a fiber layer including:
     * - Layer marker
     * - Fiber start
     * - All fiber paths
     * - Fiber stop
     */
    std::string generate_layer_gcode(
        const FiberLayer& layer,
        GCodeWriter& writer,
        const FiberGCodeConfig& config
    );
    
    /**
     * @brief Generate G-code for print sequence
     * 
     * Generates G-code following the print sequence (plastic/fiber coordination).
     * This integrates with the existing FFF G-code generation.
     * 
     * For Method 1 (DualPrinthead): Uses tool changes between plastic and fiber
     * For Method 2 (Embedded): Embeds fiber commands in plastic extrusion
     */
    std::string generate_sequence_gcode(
        const PrintSequence& sequence,
        FiberPrintObject* fiber_object,
        GCodeWriter& writer,
        const FiberGCodeConfig& config
    );
    
    /**
     * @brief Generate G-code for embedded fiber extrusion (Method 2)
     * 
     * For Method 2b (PreEmbeddedFilament): Generates G-code where fiber is embedded
     * in the plastic extrusion. Fiber orientation follows print path direction.
     * 
     * @param extrusion_path The plastic extrusion path
     * @param fiber_active Whether fiber should be active during this extrusion
     * @param writer The G-code writer
     * @param config Fiber G-code configuration
     * @return G-code string with embedded fiber commands
     */
    std::string generate_embedded_fiber_extrusion(
        const Polyline& extrusion_path,
        bool fiber_active,
        GCodeWriter& writer,
        const FiberGCodeConfig& config
    ) const;
    
    /**
     * @brief Generate G-code for co-extrusion (Method 2a - placeholder)
     * 
     * Placeholder for future co-extrusion implementation.
     * 
     * @param extrusion_path The plastic extrusion path
     * @param fiber_path The fiber path (if different from plastic)
     * @param writer The G-code writer
     * @param config Fiber G-code configuration
     * @return G-code string for co-extrusion
     */
    std::string generate_coextrusion_gcode(
        const Polyline& extrusion_path,
        const Polyline& fiber_path,
        GCodeWriter& writer,
        const FiberGCodeConfig& config
    ) const;
    
private:
    /**
     * @brief Generate G-code for Method 1 (dual printhead)
     */
    std::string generate_sequence_gcode_dual_printhead(
        const PrintSequence& sequence,
        FiberPrintObject* fiber_object,
        GCodeWriter& writer,
        const FiberGCodeConfig& config
    );
    
    /**
     * @brief Generate G-code for Method 2b (pre-embedded filament)
     */
    std::string generate_sequence_gcode_embedded(
        const PrintSequence& sequence,
        FiberPrintObject* fiber_object,
        GCodeWriter& writer,
        const FiberGCodeConfig& config
    );
    
    /**
     * @brief Generate G-code for Method 2a (co-extrusion - placeholder)
     */
    std::string generate_sequence_gcode_coextrusion(
        const PrintSequence& sequence,
        FiberPrintObject* fiber_object,
        GCodeWriter& writer,
        const FiberGCodeConfig& config
    );
    
    /**
     * @brief Format G-code command with parameters
     */
    std::string format_command(
        const std::string& command,
        const std::vector<std::pair<char, double>>& params,
        const FiberGCodeConfig& config
    ) const;
    
    /**
     * @brief Convert point to G-code coordinates
     */
    Vec2d point_to_gcode(const Point& point, GCodeWriter& writer) const;
};

} // namespace Slic3r

#endif /* slic3r_FiberGCode_hpp_ */

