///|/ Copyright (c) Prusa Research 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "FiberPlasticCoordinator.hpp"
#include "libslic3r/Fiber/FiberPrintObject.hpp"
#include "libslic3r/Layer.hpp"
#include <boost/log/trivial.hpp>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace Slic3r {

PrintSequence FiberPlasticCoordinator::generate_print_sequence(
    FiberPrintObject* fiber_object,
    const std::vector<Layer*>& fff_layers,
    const FiberCoordinationConfig& config)
{
    PrintSequence sequence;
    
    if (!fiber_object || fff_layers.empty()) {
        BOOST_LOG_TRIVIAL(warning) << "FiberPlasticCoordinator::generate_print_sequence - Invalid input";
        return sequence;
    }
    
    // Generate sequence based on strategy
    switch (config.sequence) {
        case FiberPrintSequence::PlasticFirst:
            sequence = generate_plastic_first_sequence(fiber_object, fff_layers, config);
            break;
        case FiberPrintSequence::Alternating:
            sequence = generate_alternating_sequence(fiber_object, fff_layers, config);
            break;
        case FiberPrintSequence::FiberOnTop:
            sequence = generate_fiber_on_top_sequence(fiber_object, fff_layers, config);
            break;
        default:
            BOOST_LOG_TRIVIAL(warning) << "FiberPlasticCoordinator::generate_print_sequence - Unknown sequence type";
            break;
    }
    
    // Validate sequence
    std::vector<std::string> warnings;
    if (!validate_sequence(sequence, fiber_object, warnings)) {
        BOOST_LOG_TRIVIAL(warning) << "FiberPlasticCoordinator::generate_print_sequence - Sequence validation failed";
        for (const std::string& warning : warnings) {
            BOOST_LOG_TRIVIAL(warning) << "  " << warning;
        }
    }
    
    BOOST_LOG_TRIVIAL(info) << "FiberPlasticCoordinator::generate_print_sequence - Generated sequence with " 
                            << sequence.size() << " steps";
    
    return sequence;
}

PrintSequence FiberPlasticCoordinator::generate_plastic_first_sequence(
    FiberPrintObject* fiber_object,
    const std::vector<Layer*>& fff_layers,
    const FiberCoordinationConfig& config)
{
    PrintSequence sequence;
    sequence.reserve(fff_layers.size() * 2); // Rough estimate
    
    // Step 1: Print all plastic layers first
    for (const Layer* layer : fff_layers) {
        if (layer && layer->has_extrusions()) {
            sequence.emplace_back(PrintSequenceStep::PlasticLayer, layer->id(), layer->print_z);
        }
    }
    
    // Step 2: Add delay if needed
    if (config.delay_after_plastic > 0.0 || config.cooling_time > 0.0) {
        double total_delay = std::max(config.delay_after_plastic, config.cooling_time);
        if (!fff_layers.empty()) {
            const Layer* last_layer = fff_layers.back();
            if (last_layer) {
                sequence.emplace_back(PrintSequenceStep::Delay, last_layer->id(), last_layer->print_z, total_delay);
            }
        }
    }
    
    // Step 3: Print all fiber layers
    const std::vector<FiberLayer>& fiber_layers = fiber_object->fiber_layers();
    for (const FiberLayer& fiber_layer : fiber_layers) {
        if (fiber_layer.has_fibers()) {
            sequence.emplace_back(PrintSequenceStep::FiberLayer, fiber_layer.id, fiber_layer.print_z);
        }
    }
    
    return sequence;
}

PrintSequence FiberPlasticCoordinator::generate_alternating_sequence(
    FiberPrintObject* fiber_object,
    const std::vector<Layer*>& fff_layers,
    const FiberCoordinationConfig& config)
{
    PrintSequence sequence;
    sequence.reserve(fff_layers.size() * 3); // Rough estimate (plastic + delay + fiber per layer)
    
    const std::vector<FiberLayer>& fiber_layers = fiber_object->fiber_layers();
    
    // Create a map of fiber layers by layer ID for quick lookup
    std::map<size_t, const FiberLayer*> fiber_layer_map;
    for (const FiberLayer& fiber_layer : fiber_layers) {
        if (fiber_layer.has_fibers()) {
            fiber_layer_map[fiber_layer.id] = &fiber_layer;
        }
    }
    
    // Alternate: plastic layer → delay → fiber layer → repeat
    for (const Layer* layer : fff_layers) {
        if (!layer || !layer->has_extrusions()) {
            continue;
        }
        
        // Print plastic layer
        sequence.emplace_back(PrintSequenceStep::PlasticLayer, layer->id(), layer->print_z);
        
        // Add delay after plastic
        double delay = calculate_delay_after_plastic(layer->id(), layer->height, config);
        if (delay > 0.0) {
            add_delay_if_needed(sequence, layer->print_z, delay);
        }
        
        // Print fiber layer if it exists for this layer
        auto it = fiber_layer_map.find(layer->id());
        if (it != fiber_layer_map.end()) {
            const FiberLayer* fiber_layer = it->second;
            sequence.emplace_back(PrintSequenceStep::FiberLayer, fiber_layer->id, fiber_layer->print_z);
        }
    }
    
    return sequence;
}

PrintSequence FiberPlasticCoordinator::generate_fiber_on_top_sequence(
    FiberPrintObject* fiber_object,
    const std::vector<Layer*>& fff_layers,
    const FiberCoordinationConfig& config)
{
    PrintSequence sequence;
    sequence.reserve(fff_layers.size() * 3); // Rough estimate
    
    const std::vector<FiberLayer>& fiber_layers = fiber_object->fiber_layers();
    
    // Create a map of fiber layers by layer ID for quick lookup
    std::map<size_t, const FiberLayer*> fiber_layer_map;
    for (const FiberLayer& fiber_layer : fiber_layers) {
        if (fiber_layer.has_fibers()) {
            fiber_layer_map[fiber_layer.id] = &fiber_layer;
        }
    }
    
    // For each layer: plastic → delay → fiber (immediately)
    for (const Layer* layer : fff_layers) {
        if (!layer || !layer->has_extrusions()) {
            continue;
        }
        
        // Print plastic layer
        sequence.emplace_back(PrintSequenceStep::PlasticLayer, layer->id(), layer->print_z);
        
        // Add delay after plastic (usually minimal for fiber-on-top)
        double delay = calculate_delay_after_plastic(layer->id(), layer->height, config);
        if (delay > 0.0) {
            add_delay_if_needed(sequence, layer->print_z, delay);
        }
        
        // Print fiber layer immediately after plastic
        auto it = fiber_layer_map.find(layer->id());
        if (it != fiber_layer_map.end()) {
            const FiberLayer* fiber_layer = it->second;
            sequence.emplace_back(PrintSequenceStep::FiberLayer, fiber_layer->id, fiber_layer->print_z);
        }
    }
    
    return sequence;
}

double FiberPlasticCoordinator::calculate_delay_after_plastic(
    size_t layer_id,
    coordf_t layer_height,
    const FiberCoordinationConfig& config) const
{
    double delay = config.delay_after_plastic;
    
    // Add cooling time if required
    if (config.wait_for_cooling && config.cooling_time > 0.0) {
        delay = std::max(delay, config.cooling_time);
    }
    
    // Layer-specific factors could be added here:
    // - Thicker layers might need more cooling
    // - First layer might need different timing
    // - Last layer might need different timing
    
    return delay;
}

bool FiberPlasticCoordinator::validate_sequence(
    const PrintSequence& sequence,
    FiberPrintObject* fiber_object,
    std::vector<std::string>& warnings) const
{
    if (!fiber_object) {
        warnings.push_back("Invalid fiber object");
        return false;
    }
    
    if (sequence.empty()) {
        warnings.push_back("Empty print sequence");
        return false;
    }
    
    // Check that all layer IDs are valid
    const std::vector<FiberLayer>& fiber_layers = fiber_object->fiber_layers();
    std::set<size_t> valid_layer_ids;
    for (const FiberLayer& layer : fiber_layers) {
        valid_layer_ids.insert(layer.id);
    }
    
    for (const PrintSequenceStep& step : sequence) {
        if (step.type == PrintSequenceStep::FiberLayer) {
            if (valid_layer_ids.find(step.layer_id) == valid_layer_ids.end()) {
                warnings.push_back("Invalid fiber layer ID: " + std::to_string(step.layer_id));
            }
        }
        
        // Check for negative delays
        if (step.type == PrintSequenceStep::Delay && step.delay_seconds < 0.0) {
            warnings.push_back("Negative delay at layer " + std::to_string(step.layer_id));
        }
    }
    
    // Check sequence ordering (Z should generally increase)
    coordf_t prev_z = -1.0;
    for (const PrintSequenceStep& step : sequence) {
        if (step.print_z < prev_z - 0.001) { // Allow small epsilon for floating point
            warnings.push_back("Sequence Z decreases: " + std::to_string(prev_z) + " -> " + std::to_string(step.print_z));
        }
        prev_z = step.print_z;
    }
    
    return warnings.empty();
}

void FiberPlasticCoordinator::add_delay_if_needed(
    PrintSequence& sequence,
    coordf_t print_z,
    double delay_seconds)
{
    if (delay_seconds > 0.0) {
        // Use the layer ID from the previous step, or 0 if sequence is empty
        size_t layer_id = sequence.empty() ? 0 : sequence.back().layer_id;
        sequence.emplace_back(PrintSequenceStep::Delay, layer_id, print_z, delay_seconds);
    }
}

} // namespace Slic3r

