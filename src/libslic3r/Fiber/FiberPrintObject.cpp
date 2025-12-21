///|/ Copyright (c) Prusa Research 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "FiberPrintObject.hpp"
#include "libslic3r/Print.hpp"  // For PrintObject definition (needed for layers() method)
#include "libslic3r/Layer.hpp"
#include "libslic3r/Fiber/FiberPlacement.hpp"
#include "libslic3r/Fiber/PathPlanner.hpp"
#include "libslic3r/Fiber/FiberPlasticCoordinator.hpp"
#include "libslic3r/Fiber/FiberGCode.hpp"
#include "libslic3r/GCode/GCodeWriter.hpp"

#include <boost/log/trivial.hpp>
#include <algorithm>

namespace Slic3r {

FiberPrintObject::FiberPrintObject(FiberPrint* print, ModelObject* model_object)
    : _FiberPrintObjectBase(print, model_object)
    , m_fff_print_object(nullptr)
{
}

FiberPrintObject::~FiberPrintObject()
{
    clear_fiber_layers();
}

FiberLayer* FiberPrintObject::get_fiber_layer(size_t layer_id)
{
    auto it = std::find_if(m_fiber_layers.begin(), m_fiber_layers.end(),
        [layer_id](const FiberLayer& layer) { return layer.id == layer_id; });
    return (it == m_fiber_layers.end()) ? nullptr : &(*it);
}

const FiberLayer* FiberPrintObject::get_fiber_layer(size_t layer_id) const
{
    auto it = std::find_if(m_fiber_layers.begin(), m_fiber_layers.end(),
        [layer_id](const FiberLayer& layer) { return layer.id == layer_id; });
    return (it == m_fiber_layers.end()) ? nullptr : &(*it);
}

FiberLayer* FiberPrintObject::get_fiber_layer_at_printz(coordf_t print_z)
{
    auto it = std::find_if(m_fiber_layers.begin(), m_fiber_layers.end(),
        [print_z](const FiberLayer& layer) { return std::abs(layer.print_z - print_z) < EPSILON; });
    return (it == m_fiber_layers.end()) ? nullptr : &(*it);
}

const FiberLayer* FiberPrintObject::get_fiber_layer_at_printz(coordf_t print_z) const
{
    auto it = std::find_if(m_fiber_layers.begin(), m_fiber_layers.end(),
        [print_z](const FiberLayer& layer) { return std::abs(layer.print_z - print_z) < EPSILON; });
    return (it == m_fiber_layers.end()) ? nullptr : &(*it);
}

void FiberPrintObject::create_fiber_layers_from_fff_layers()
{
    clear_fiber_layers();
    
    if (!m_fff_print_object) {
        BOOST_LOG_TRIVIAL(warning) << "FiberPrintObject::create_fiber_layers_from_fff_layers() - No FFF PrintObject set";
        return;
    }
    
    const LayerPtrs& fff_layers = m_fff_print_object->layers();
    m_fiber_layers.reserve(fff_layers.size());
    
    for (const Layer* layer : fff_layers) {
        if (layer) {
            FiberLayer fiber_layer(layer->id(), layer->print_z, layer->slice_z, layer->height);
            
            // Copy layer geometry for fiber placement
            fiber_layer.layer_geometry = layer->lslices;
            
            m_fiber_layers.push_back(std::move(fiber_layer));
        }
    }
    
    BOOST_LOG_TRIVIAL(info) << "FiberPrintObject::create_fiber_layers_from_fff_layers() - Created " 
                            << m_fiber_layers.size() << " fiber layers";
}

void FiberPrintObject::generate_fiber_paths(const FiberPlacementConfig& config)
{
    FiberPlacement placement;
    
    for (FiberLayer& layer : m_fiber_layers) {
        // Clear existing paths
        layer.clear();
        
        // Generate paths for this layer
        placement.generate_paths_for_layer(layer, layer.layer_geometry, config);
    }
    
    BOOST_LOG_TRIVIAL(info) << "FiberPrintObject::generate_fiber_paths - Generated paths for " 
                            << m_fiber_layers.size() << " layers";
}

void FiberPrintObject::plan_continuous_paths(const PathPlanningConfig& path_config)
{
    PathPlanner planner;
    
    for (FiberLayer& layer : m_fiber_layers) {
        if (layer.fiber_paths.empty()) {
            continue; // Skip layers without paths
        }
        
        // Save original segment count
        size_t original_segment_count = layer.fiber_paths.size();
        
        // Plan continuous paths from fiber path segments
        ContinuousPaths continuous_paths = planner.plan_continuous_paths(
            layer.fiber_paths,
            layer.layer_geometry,
            path_config
        );
        
        // Replace fiber paths with continuous paths
        layer.fiber_paths.clear();
        for (const ContinuousPath& cpath : continuous_paths) {
            FiberPath fpath(cpath.polyline, cpath.z, cpath.angle, cpath.spacing);
            fpath.fiber_type = cpath.fiber_type;
            layer.add_fiber_path(fpath);
        }
        
        BOOST_LOG_TRIVIAL(debug) << "FiberPrintObject::plan_continuous_paths - Layer " 
                                 << layer.id << ": " << continuous_paths.size() 
                                 << " continuous paths from " << original_segment_count << " segments";
    }
    
    BOOST_LOG_TRIVIAL(info) << "FiberPrintObject::plan_continuous_paths - Planned continuous paths for " 
                            << m_fiber_layers.size() << " layers";
}

PrintSequence FiberPrintObject::generate_print_sequence(const FiberCoordinationConfig& config)
{
    FiberPlasticCoordinator coordinator;
    
    // Get FFF layers from the regular PrintObject
    std::vector<Layer*> fff_layers;
    if (m_fff_print_object) {
        const LayerPtrs& layers = m_fff_print_object->layers();
        fff_layers.reserve(layers.size());
        for (Layer* layer : layers) {
            fff_layers.push_back(layer);
        }
    }
    
    // Generate print sequence
    m_print_sequence = coordinator.generate_print_sequence(this, fff_layers, config);
    
    BOOST_LOG_TRIVIAL(info) << "FiberPrintObject::generate_print_sequence - Generated sequence with " 
                            << m_print_sequence.size() << " steps";
    
    return m_print_sequence;
}

std::string FiberPrintObject::generate_gcode(const FiberGCodeConfig& config)
{
    FiberGCodeWriter gcode_writer;
    
    // Create a GCodeWriter instance for coordinate conversion
    // Note: In full integration, this would come from the main G-code generator
    GCodeWriter writer;
    
    // Generate G-code from print sequence
    std::string gcode = gcode_writer.generate_sequence_gcode(
        m_print_sequence,
        this,
        writer,
        config
    );
    
    BOOST_LOG_TRIVIAL(info) << "FiberPrintObject::generate_gcode - Generated " 
                            << gcode.length() << " bytes of G-code";
    
    return gcode;
}

void FiberPrintObject::clear_fiber_layers()
{
    m_fiber_layers.clear();
}

} // namespace Slic3r

