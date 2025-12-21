///|/ Copyright (c) Prusa Research 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "FiberGCode.hpp"
#include "libslic3r/Fiber/FiberPrintObject.hpp"
#include "libslic3r/GCode/GCodeWriter.hpp"
#include "libslic3r/format.hpp"
#include <boost/log/trivial.hpp>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <map>

namespace Slic3r {

std::string FiberGCodeWriter::generate_path_gcode(
    const FiberPath& path,
    GCodeWriter& writer,
    const FiberGCodeConfig& config)
{
    if (path.polyline.empty()) {
        return "";
    }
    
    std::ostringstream gcode;
    
    // Add path comment if enabled
    if (config.enable_comments) {
        gcode << generate_path_comment(0, config) << "\n"; // TODO: Use actual path ID
    }
    
    // Generate G-code for each segment in the path
    for (size_t i = 0; i < path.polyline.points.size(); ++i) {
        const Point& point = path.polyline.points[i];
        Vec2d gcode_point = point_to_gcode(point, writer);
        
        // For fiber, we typically don't use E (extrusion) axis
        // Instead, we use the fiber start/stop commands
        // Movement is just X, Y, Z, F (feedrate)
        
        if (i == 0) {
            // First point: move to position (travel)
            gcode << writer.travel_to_xy(gcode_point, "Move to fiber path start");
        } else {
            // Subsequent points: move while fiber is active
            // Note: Fiber is already started before this path
            gcode << writer.travel_to_xy(gcode_point, "Fiber path");
        }
        gcode << "\n";
    }
    
    return gcode.str();
}

std::string FiberGCodeWriter::generate_advanced_path_gcode(
    const AdvancedFiberPath& path,
    GCodeWriter& writer,
    const FiberGCodeConfig& config)
{
    if (path.polyline.empty()) {
        return "";
    }
    
    std::ostringstream gcode;
    
    // Add path comment if enabled
    if (config.enable_comments) {
        gcode << generate_path_comment(0, config) << "\n"; // TODO: Use actual path ID
    }
    
    // Add pre-G-code if specified
    if (!path.pre_gcode.empty()) {
        gcode << path.pre_gcode << "\n";
    }
    
    // Calculate segment lengths for speed interpolation
    double total_length = 0.0;
    std::vector<double> segment_lengths;
    segment_lengths.reserve(path.polyline.points.size() - 1);
    
    bool has_variable_speed = (path.speed_profile.interpolation != SpeedProfile::Constant);
    
    if (has_variable_speed) {
        for (size_t i = 0; i < path.polyline.points.size() - 1; ++i) {
            Point diff = path.polyline.points[i + 1] - path.polyline.points[i];
            double len_scaled = std::sqrt(double(diff.x() * diff.x() + diff.y() * diff.y()));
            double len = unscale<double>(len_scaled);
            segment_lengths.push_back(len);
            total_length += len;
        }
    }
    
    double accumulated_length = 0.0;
    for (size_t i = 0; i < path.polyline.points.size(); ++i) {
        const Point& point = path.polyline.points[i];
        Vec2d gcode_point = point_to_gcode(point, writer);
        
        // Calculate speed for this segment if variable speed is enabled
        double feedrate = config.fiber_speed;
        if (has_variable_speed && i < path.polyline.points.size() - 1) {
            double position = (total_length > 0) ? (accumulated_length / total_length) : 0.0;
            feedrate = path.speed_profile.get_speed_at(position);
            accumulated_length += segment_lengths[i];
        }
        
        if (i == 0) {
            // First point: move to position (travel)
            gcode << writer.travel_to_xy(gcode_point, "Move to fiber path start");
        } else {
            // Subsequent points: move while fiber is active
            // Set feedrate if variable speed
            if (has_variable_speed) {
                gcode << writer.set_speed(feedrate * 60.0, "Fiber path", ""); // Convert mm/s to mm/min
            }
            gcode << writer.travel_to_xy(gcode_point, "Fiber path");
        }
        gcode << "\n";
    }
    
    // Add post-G-code if specified
    if (!path.post_gcode.empty()) {
        gcode << path.post_gcode << "\n";
    }
    
    return gcode.str();
}

std::string FiberGCodeWriter::generate_fiber_start(
    const FiberGCodeConfig& config,
    double speed,
    double pressure) const
{
    std::ostringstream gcode;
    
    // Use configured speed if not provided
    if (speed <= 0.0) {
        speed = config.fiber_speed;
    }
    
    // Generate fiber start command
    // Format: {command} [S{speed}] [P{pressure}]
    std::vector<std::pair<char, double>> params;
    if (speed > 0.0) {
        params.push_back({'S', speed});
    }
    if (pressure > 0.0) {
        params.push_back({'P', pressure});
    }
    
    std::string command = format_command(config.fiber_start_command, params, config);
    gcode << command;
    
    if (config.enable_comments) {
        gcode << " ; Start fiber deposition";
    }
    gcode << "\n";
    
    // Set fiber speed if separate command is configured
    if (!config.fiber_speed_command.empty() && speed > 0.0) {
        params.clear();
        params.push_back({'S', speed});
        command = format_command(config.fiber_speed_command, params, config);
        gcode << command;
        if (config.enable_comments) {
            gcode << " ; Set fiber speed";
        }
        gcode << "\n";
    }
    
    return gcode.str();
}

std::string FiberGCodeWriter::generate_fiber_stop(const FiberGCodeConfig& config) const
{
    std::ostringstream gcode;
    
    gcode << config.fiber_stop_command;
    if (config.enable_comments) {
        gcode << " ; Stop fiber deposition";
    }
    gcode << "\n";
    
    return gcode.str();
}

std::string FiberGCodeWriter::generate_toolchange_to_fiber(
    GCodeWriter& writer,
    const FiberGCodeConfig& config) const
{
    std::ostringstream gcode;
    
    if (config.enable_comments) {
        gcode << "; Tool change: Plastic (T" << config.plastic_extruder_id 
              << ") -> Fiber (T" << config.fiber_extruder_id << ")\n";
    }
    
    // Use PrusaSlicer's existing multi-extruder tool change system
    // This handles retraction, wipe, temperature, etc. automatically
    gcode << writer.set_extruder(config.fiber_extruder_id);
    
    // Additional fiber-specific activation (if needed)
    // This is in addition to the tool change
    // Some printers may need additional commands to activate fiber head
    if (!config.fiber_start_command.empty()) {
        gcode << generate_fiber_start(config);
    }
    
    return gcode.str();
}

std::string FiberGCodeWriter::generate_toolchange_to_plastic(
    GCodeWriter& writer,
    const FiberGCodeConfig& config) const
{
    std::ostringstream gcode;
    
    // Additional fiber-specific deactivation (if needed)
    // This is before the tool change
    // Some printers may need additional commands to deactivate fiber head
    if (!config.fiber_stop_command.empty()) {
        gcode << generate_fiber_stop(config);
    }
    
    if (config.enable_comments) {
        gcode << "; Tool change: Fiber (T" << config.fiber_extruder_id 
              << ") -> Plastic (T" << config.plastic_extruder_id << ")\n";
    }
    
    // Use PrusaSlicer's existing multi-extruder tool change system
    // This handles retraction, wipe, temperature, etc. automatically
    gcode << writer.set_extruder(config.plastic_extruder_id);
    
    return gcode.str();
}

std::string FiberGCodeWriter::generate_delay(double seconds, const FiberGCodeConfig& config) const
{
    if (seconds <= 0.0) {
        return "";
    }
    
    std::ostringstream gcode;
    
    // G4 P{milliseconds} - Dwell command
    int milliseconds = static_cast<int>(seconds * 1000.0);
    gcode << "G4 P" << milliseconds;
    
    if (config.enable_comments) {
        gcode << " ; Wait " << std::fixed << std::setprecision(2) << seconds << " seconds";
    }
    gcode << "\n";
    
    return gcode.str();
}

std::string FiberGCodeWriter::generate_layer_comment(size_t layer_id, const FiberGCodeConfig& config) const
{
    if (!config.enable_comments) {
        return "";
    }
    
    std::string comment = config.layer_comment_format;
    // Replace {layer_id} placeholder
    size_t pos = comment.find("{layer_id}");
    if (pos != std::string::npos) {
        comment.replace(pos, 10, std::to_string(layer_id));
    }
    
    return comment;
}

std::string FiberGCodeWriter::generate_path_comment(size_t path_id, const FiberGCodeConfig& config) const
{
    if (!config.enable_comments) {
        return "";
    }
    
    std::string comment = config.fiber_comment_format;
    // Replace {path_id} placeholder
    size_t pos = comment.find("{path_id}");
    if (pos != std::string::npos) {
        comment.replace(pos, 9, std::to_string(path_id));
    }
    
    return comment;
}

std::string FiberGCodeWriter::generate_layer_gcode(
    const FiberLayer& layer,
    GCodeWriter& writer,
    const FiberGCodeConfig& config)
{
    std::ostringstream gcode;
    
    // Layer marker
    gcode << generate_layer_comment(layer.id, config) << "\n";
    
    if (layer.fiber_paths.empty()) {
        return gcode.str();
    }
    
    // Start fiber
    gcode << generate_fiber_start(config);
    
    // Generate G-code for each fiber path
    for (size_t i = 0; i < layer.fiber_paths.size(); ++i) {
        const FiberPath& path = layer.fiber_paths[i];
        gcode << generate_path_gcode(path, writer, config);
    }
    
    // Stop fiber
    gcode << generate_fiber_stop(config);
    
    return gcode.str();
}

std::string FiberGCodeWriter::generate_sequence_gcode(
    const PrintSequence& sequence,
    FiberPrintObject* fiber_object,
    GCodeWriter& writer,
    const FiberGCodeConfig& config)
{
    std::ostringstream gcode;
    
    if (!fiber_object || sequence.empty()) {
        return gcode.str();
    }
    
    // Different logic based on printing method
    if (config.method == FiberPrintMethod::DualPrinthead) {
        // Method 1: Dual printhead with tool changes
        return generate_sequence_gcode_dual_printhead(sequence, fiber_object, writer, config);
    } else if (config.method == FiberPrintMethod::PreEmbeddedFilament) {
        // Method 2b: Pre-embedded fiber filament
        return generate_sequence_gcode_embedded(sequence, fiber_object, writer, config);
    } else if (config.method == FiberPrintMethod::CoExtrusion) {
        // Method 2a: Co-extrusion (placeholder)
        return generate_sequence_gcode_coextrusion(sequence, fiber_object, writer, config);
    }
    
    return gcode.str();
}

std::string FiberGCodeWriter::generate_sequence_gcode_dual_printhead(
    const PrintSequence& sequence,
    FiberPrintObject* fiber_object,
    GCodeWriter& writer,
    const FiberGCodeConfig& config)
{
    std::ostringstream gcode;
    
    // Process each step in the sequence
    for (const PrintSequenceStep& step : sequence) {
        switch (step.type) {
            case PrintSequenceStep::PlasticLayer:
                // TODO: Generate plastic layer G-code
                // This will integrate with existing FFF G-code generation
                // For now, ensure we're on the plastic extruder
                if (config.enable_comments) {
                    gcode << "; Plastic layer " << step.layer_id << " (Z=" 
                          << std::fixed << std::setprecision(3) << step.print_z << ")\n";
                }
                // Ensure we're using the plastic extruder
                gcode << writer.set_extruder(config.plastic_extruder_id);
                gcode << "; TODO: Generate plastic layer G-code (integrate with FFF pipeline)\n";
                break;
                
            case PrintSequenceStep::FiberLayer: {
                // Switch to fiber extruder before printing fiber
                gcode << generate_toolchange_to_fiber(writer, config);
                
                // Find the fiber layer
                const FiberLayer* fiber_layer = fiber_object->get_fiber_layer(step.layer_id);
                if (fiber_layer && !fiber_layer->fiber_paths.empty()) {
                    gcode << generate_layer_gcode(*fiber_layer, writer, config);
                }
                
                // Switch back to plastic extruder after fiber layer
                gcode << generate_toolchange_to_plastic(writer, config);
                break;
            }
                
            case PrintSequenceStep::Delay:
                gcode << generate_delay(step.delay_seconds, config);
                break;
        }
    }
    
    return gcode.str();
}

std::string FiberGCodeWriter::generate_sequence_gcode_embedded(
    const PrintSequence& sequence,
    FiberPrintObject* fiber_object,
    GCodeWriter& writer,
    const FiberGCodeConfig& config)
{
    std::ostringstream gcode;
    
    // For Method 2b (PreEmbeddedFilament):
    // - No tool changes needed (fiber is in the filament)
    // - Fiber orientation follows print path direction
    // - Fiber activation is embedded in extrusion commands
    
    // Ensure we're using the embedded fiber extruder
    gcode << writer.set_extruder(config.embedded_fiber_extruder_id);
    
    // Process each step in the sequence
    for (const PrintSequenceStep& step : sequence) {
        switch (step.type) {
            case PrintSequenceStep::PlasticLayer: {
                if (config.enable_comments) {
                    gcode << "; Layer " << step.layer_id << " (Z=" 
                          << std::fixed << std::setprecision(3) << step.print_z 
                          << ") - Plastic with embedded fiber\n";
                }
                
                // TODO: Integrate with FFF G-code generation
                // For embedded fiber, we need to:
                // 1. Generate plastic extrusion paths
                // 2. Check if fiber should be active for each path
                // 3. Embed fiber activation commands in extrusion G-code
                
                // Find corresponding fiber layer to determine fiber paths
                const FiberLayer* fiber_layer = fiber_object->get_fiber_layer(step.layer_id);
                bool has_fiber = (fiber_layer && !fiber_layer->fiber_paths.empty());
                
                if (has_fiber) {
                    // Activate fiber for this layer
                    gcode << generate_fiber_start(config);
                }
                
                gcode << "; TODO: Generate plastic extrusion G-code with embedded fiber\n";
                gcode << "; Fiber orientation will follow print path direction\n";
                
                if (has_fiber) {
                    // Deactivate fiber after layer
                    gcode << generate_fiber_stop(config);
                }
                break;
            }
                
            case PrintSequenceStep::FiberLayer:
                // For Method 2b, fiber is embedded in plastic, so FiberLayer steps
                // are handled during PlasticLayer steps
                // This step can be ignored or used for fiber path planning
                if (config.enable_comments) {
                    gcode << "; Fiber layer " << step.layer_id 
                          << " - handled during plastic extrusion\n";
                }
                break;
                
            case PrintSequenceStep::Delay:
                gcode << generate_delay(step.delay_seconds, config);
                break;
        }
    }
    
    return gcode.str();
}

std::string FiberGCodeWriter::generate_sequence_gcode_coextrusion(
    const PrintSequence& sequence,
    FiberPrintObject* fiber_object,
    GCodeWriter& writer,
    const FiberGCodeConfig& config)
{
    std::ostringstream gcode;
    
    // Method 2a: Co-extrusion (placeholder for future implementation)
    if (config.enable_comments) {
        gcode << "; Co-extrusion method (Method 2a) - Placeholder\n";
        gcode << "; This will be implemented in a future update\n";
        gcode << "; Co-extrusion embeds fiber during plastic extrusion\n";
    }
    
    // Placeholder implementation
    gcode << "; TODO: Implement co-extrusion G-code generation\n";
    gcode << "; This requires special printhead that feeds fiber during extrusion\n";
    
    return gcode.str();
}

std::string FiberGCodeWriter::generate_embedded_fiber_extrusion(
    const Polyline& extrusion_path,
    bool fiber_active,
    GCodeWriter& writer,
    const FiberGCodeConfig& config) const
{
    std::ostringstream gcode;
    
    if (extrusion_path.empty()) {
        return gcode.str();
    }
    
    // For Method 2b: Generate extrusion G-code with embedded fiber commands
    // Fiber orientation follows the extrusion path direction
    
    if (fiber_active) {
        // Activate fiber before extrusion
        gcode << generate_fiber_start(config);
    }
    
    // Generate extrusion moves (this would integrate with existing FFF extrusion)
    // For now, placeholder
    if (config.enable_comments) {
        gcode << "; Extrusion path with " << (fiber_active ? "embedded fiber" : "no fiber") << "\n";
    }
    gcode << "; TODO: Generate actual extrusion G-code (G1 X Y E commands)\n";
    gcode << "; Fiber orientation follows path direction\n";
    
    if (fiber_active) {
        // Deactivate fiber after extrusion
        gcode << generate_fiber_stop(config);
    }
    
    return gcode.str();
}

std::string FiberGCodeWriter::generate_coextrusion_gcode(
    const Polyline& extrusion_path,
    const Polyline& fiber_path,
    GCodeWriter& writer,
    const FiberGCodeConfig& config) const
{
    std::ostringstream gcode;
    
    // Method 2a: Co-extrusion (placeholder)
    if (config.enable_comments) {
        gcode << "; Co-extrusion: Plastic and fiber extruded together\n";
        gcode << "; Placeholder for future implementation\n";
    }
    
    gcode << "; TODO: Implement co-extrusion G-code\n";
    gcode << "; Requires special printhead with fiber feed mechanism\n";
    
    return gcode.str();
}

std::string FiberGCodeWriter::format_command(
    const std::string& command,
    const std::vector<std::pair<char, double>>& params,
    const FiberGCodeConfig& config) const
{
    std::ostringstream gcode;
    gcode << command;
    
    for (const auto& param : params) {
        gcode << " " << param.first << std::fixed << std::setprecision(3) << param.second;
    }
    
    return gcode.str();
}

Vec2d FiberGCodeWriter::point_to_gcode(const Point& point, GCodeWriter& writer) const
{
    // Convert scaled point to G-code coordinates
    Vec2d gcode_point(unscaled<double>(point.x()), unscaled<double>(point.y()));
    // Apply origin offset (GCodeWriter handles this internally, but we need to account for it)
    return gcode_point;
}

} // namespace Slic3r

