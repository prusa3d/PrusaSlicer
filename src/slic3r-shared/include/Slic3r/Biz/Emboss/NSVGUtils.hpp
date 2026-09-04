#ifndef slic3r_NSVGUtils_hpp_
#define slic3r_NSVGUtils_hpp_

#include <memory>
#include <string>
#include <cmath>

#include "Slic3r/Domain/EmbossShape.hpp"
#include "Slic3r/Domain/Polygon.hpp"
#include "Slic3r/Domain/Types.hpp"

// Helper function to work with nano svg
namespace Slic3r::Biz::Emboss {

/**
@brief Paramreters for conversion curve from SVG to lines in Polygon
*/
struct NSVGLineParams
{
    // Smaller will divide curve to more lines
    // NOTE: Value is in image scale
    double tesselation_tolerance = 10.f;

    // Maximal depth of recursion for conversion curve to lines
    int max_level = 10;

    // Multiplicator of point coors
    // NOTE: Every point coor from image(float) is multiplied by scale and rounded to integer --> Slic3r::Point
    double scale = 1. / Domain::SCALING_FACTOR;

    // Flag wether y is negative, when true than y coor is multiplied by -1
    bool is_y_negative = true;

    // Is used only with rounded Stroke
    double arc_tolerance = 1.;

    // Maximal count of heal iteration
    unsigned max_heal_iteration = 10;

    explicit NSVGLineParams(double tesselation_tolerance): 
        tesselation_tolerance(tesselation_tolerance), 
        arc_tolerance(std::pow(tesselation_tolerance, 1/3.))
    {}
};

/**
@brief Convert .svg opened by nanoSvg to shapes stored in expolygons with ids
@param image Parsed svg file by NanoSvg
@param tesselation_tolerance Smaller will divide curve to more lines
NOTE: Value is in image scale
@param max_level Maximal depth for conversion curve to lines
@param scale Multiplicator of point coors
NOTE: Every point coor from image(float) is multiplied by scale and rounded to integer
@return Shapes from svg image - fill + stroke
*/
Domain::ExPolygonsWithIds create_shape_with_ids(const NSVGimage &image, const NSVGLineParams &param);

// help functions - prepare to be tested
/// <param name="is_y_negative">Flag is y negative, when true than y coor is multiplied by -1</param>
Domain::Polygons to_polygons(const NSVGimage &image, const NSVGLineParams &param);

void bounds(const NSVGimage &image, Domain::Vec2f &min, Domain::Vec2f &max);

// read text data from file
std::unique_ptr<std::string> read_from_disk(const std::string &path);

using NSVGimage_ptr = std::unique_ptr<NSVGimage, void (*)(NSVGimage*)>;
NSVGimage_ptr nsvgParseFromFile(const std::string &svg_file_path, const char *units = "mm", float dpi = 96.0f);
NSVGimage_ptr nsvgParse(const std::string& file_data, const char *units = "mm", float dpi = 96.0f);

/**
@brief Iterate over shapes and calculate count
@param image Contain pointer to first shape
@return Count of shapes
*/
size_t get_shapes_count(const NSVGimage &image);

//void save(const NSVGimage &image, std::ostream &data);
//bool save(const NSVGimage &image, const std::string &svg_file_path);
} // namespace Slic3r
#endif // slic3r_NSVGUtils_hpp_
