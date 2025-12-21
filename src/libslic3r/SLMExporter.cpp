///|/ Copyright (c) Prusa Research 2024
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "SLMExporter.hpp"

#include "SLMPrint.hpp"
#include "SLMScanPath.hpp"
#include "PrintConfig.hpp"

// libSLM includes
#include <App/Layer.h>
#include <App/Model.h>
#include <App/Header.h>
#include <Eigen/Dense>

#include <boost/log/trivial.hpp>
#include <algorithm>

namespace Slic3r {

float SLMExporter::unscaled(float coord_scaled)
{
    return float(unscale<double>(coord_t(coord_scaled)));
}

std::pair<float, float> SLMExporter::unscaled_point(const Point &pt)
{
    Vec2d unscaled_pt = unscale(pt);
    return std::make_pair(float(unscaled_pt.x()), float(unscaled_pt.y()));
}

std::shared_ptr<slm::HatchGeometry> SLMExporter::convert_hatches_to_geometry(
    const ScanVectors &scan_vectors,
    uint32_t model_id,
    uint32_t build_style_id)
{
    if (scan_vectors.empty())
        return nullptr;
    
    auto hatch_geom = std::make_shared<slm::HatchGeometry>(model_id, build_style_id);
    
    // Convert scan vectors to Eigen matrix format
    // Each scan vector becomes 2 rows: [start_x, start_y] and [end_x, end_y]
    Eigen::MatrixXf coords(scan_vectors.size() * 2, 2);
    
    for (size_t i = 0; i < scan_vectors.size(); ++i) {
        const ScanVector &vec = scan_vectors[i];
        
        // Convert start point (scaled to mm)
        std::pair<float, float> start_unscaled = unscaled_point(vec.start);
        coords(i * 2, 0) = start_unscaled.first;
        coords(i * 2, 1) = start_unscaled.second;
        
        // Convert end point (scaled to mm)
        std::pair<float, float> end_unscaled = unscaled_point(vec.end);
        coords(i * 2 + 1, 0) = end_unscaled.first;
        coords(i * 2 + 1, 1) = end_unscaled.second;
    }
    
    hatch_geom->coords = coords;
    return hatch_geom;
}

std::shared_ptr<slm::ContourGeometry> SLMExporter::convert_contours_to_geometry(
    const ScanVectors &scan_vectors,
    uint32_t model_id,
    uint32_t build_style_id)
{
    if (scan_vectors.empty())
        return nullptr;
    
    auto contour_geom = std::make_shared<slm::ContourGeometry>(model_id, build_style_id);
    
    // Convert scan vectors to Eigen matrix format
    // Each scan vector becomes 2 rows: [start_x, start_y] and [end_x, end_y]
    Eigen::MatrixXf coords(scan_vectors.size() * 2, 2);
    
    for (size_t i = 0; i < scan_vectors.size(); ++i) {
        const ScanVector &vec = scan_vectors[i];
        
        // Convert start point (scaled to mm)
        std::pair<float, float> start_unscaled = unscaled_point(vec.start);
        coords(i * 2, 0) = start_unscaled.first;
        coords(i * 2, 1) = start_unscaled.second;
        
        // Convert end point (scaled to mm)
        std::pair<float, float> end_unscaled = unscaled_point(vec.end);
        coords(i * 2 + 1, 0) = end_unscaled.first;
        coords(i * 2 + 1, 1) = end_unscaled.second;
    }
    
    contour_geom->coords = coords;
    return contour_geom;
}

std::shared_ptr<slm::Layer> SLMExporter::create_layer(
    const LayerScanPaths &layer_paths,
    uint64_t layer_id,
    uint32_t model_id,
    bool contour_first)
{
    if (layer_paths.empty())
        return nullptr;
    
    // Convert Z height from mm to microns (libSLM uses microns)
    uint64_t z_microns = uint64_t(layer_paths.z * 1000.0f);
    
    auto layer = std::make_shared<slm::Layer>(layer_id, z_microns);
    
    // Convert contours and hatches to libSLM geometry
    if (!layer_paths.contours.empty()) {
        auto contour_geom = convert_contours_to_geometry(
            layer_paths.contours, 
            model_id, 
            1); // CoreContour_Volume
        if (contour_geom) {
            layer->addContourGeometry(contour_geom);
        }
    }
    
    if (!layer_paths.hatches.empty()) {
        auto hatch_geom = convert_hatches_to_geometry(
            layer_paths.hatches, 
            model_id, 
            8); // CoreNormalHatch
        if (hatch_geom) {
            layer->addHatchGeometry(hatch_geom);
        }
    }
    
    return layer;
}

std::shared_ptr<slm::BuildStyle> SLMExporter::create_build_style(
    const SLMPrint &print,
    uint64_t build_style_id)
{
    const SLMPrintConfig &config = print.print_config();
    
    auto build_style = std::make_shared<slm::BuildStyle>();
    
    // Set laser parameters from config
    // Note: focus parameter is not directly available in SLMPrintConfig, using power as placeholder
    build_style->setStyle(
        build_style_id,
        config.slm_laser_power.value,              // focus (using power as placeholder)
        config.slm_laser_power.value,              // power (W)
        uint64_t(config.slm_exposure_time.value), // point exposure time (ms)
        0,                                         // point distance time (not used)
        config.slm_laser_speed.value,             // speed (mm/s)
        1,                                        // laser ID
        slm::LaserMode::PULSE                     // laser mode
    );

    // Set point distance (microns)
    build_style->pointDistance = uint64_t(config.slm_point_distance.value);

    return build_style;
}

std::shared_ptr<slm::Model> SLMExporter::create_model(
    const SLMPrint &print,
    uint64_t model_id,
    const SLMPrintObject *print_object)
{
    auto model = std::make_shared<slm::Model>(model_id, 0); // topSliceNum will be set later
    
    // Set model name
    if (print_object && print_object->model_object()) {
        std::string model_name = print_object->model_object()->name;
        if (model_name.empty()) {
            model_name = "SLM Model " + std::to_string(model_id);
        }
        std::u16string u16_name(model_name.begin(), model_name.end());
        model->setName(u16_name);
    } else if (!print.objects().empty() && print.objects()[0]->model_object()) {
        std::string model_name = print.objects()[0]->model_object()->name;
        if (model_name.empty()) {
            model_name = "SLM Model " + std::to_string(model_id);
        }
        std::u16string u16_name(model_name.begin(), model_name.end());
        model->setName(u16_name);
    } else {
        model->setName(u"SLM Model");
    }

    // Add build style
    auto build_style = create_build_style(print, 8); // CoreNormalHatch
    model->addBuildStyle(build_style);

    // Set top slice number (number of layers)
    const SLMPrintObject *obj = print_object ? print_object : 
                                 (!print.objects().empty() ? print.objects()[0] : nullptr);
    if (obj) {
        uint64_t top_slice = obj->scan_paths().size();
        model->setTopSlice(top_slice);
    }

    return model;
}

slm::Header SLMExporter::create_header(const SLMPrint &print)
{
    slm::Header header;
    
    header.fileName = "prusaslicer_export";
    header.creator = "PrusaSlicer";
    header.setVersion(std::make_tuple(1, 0)); // Version 1.0
    header.zUnit = 1000; // Z unit in microns (1000 = 1mm in microns)

    return header;
}

} // namespace Slic3r

