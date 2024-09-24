///|/ Copyright (c) Prusa Research 2017 - 2021 Enrico Turri @enricoturri1966, Vojtěch Bubník @bubnikv, Tomáš Mészáros @tamasmeszaros
///|/
///|/ ported from lib/Slic3r/Format/OBJ.pm:
///|/ Copyright (c) Prusa Research 2017 Vojtěch Bubník @bubnikv
///|/ Copyright (c) Slic3r 2012 - 2014 Alessandro Ranellucci @alranel
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include <boost/log/trivial.hpp>
#include <nanosvg/nanosvg.h>
#include <memory>
#include <string>
#include <utility>
#include <cassert>
#include <cstddef>

#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/NSVGUtils.hpp"
#include "libslic3r/Emboss.hpp"
#include "admesh/stl.h"
#include "libslic3r/EmbossShape.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Point.hpp"

namespace {
std::string get_file_name(const std::string &file_path)
{
    if (file_path.empty())
        return file_path;

    size_t pos_last_delimiter = file_path.find_last_of("/\\");
    if (pos_last_delimiter == std::string::npos) {
        // should not happend that in path is not delimiter
        assert(false);
        pos_last_delimiter = 0;
    }

    size_t pos_point = file_path.find_last_of('.');
    if (pos_point == std::string::npos || pos_point < pos_last_delimiter // last point is inside of directory path
    ) {
        // there is no extension
        assert(false);
        pos_point = file_path.size();
    }

    size_t offset = pos_last_delimiter + 1;             // result should not contain last delimiter ( +1 )
    size_t count  = pos_point - pos_last_delimiter - 1; // result should not contain extension point ( -1 )
    return file_path.substr(offset, count);
}
}

namespace Slic3r {

bool load_svg(const std::string &input_file, Model &output_model)
{
    EmbossShape::SvgFile svg_file{input_file};
    const NSVGimage* image = init_image(svg_file);
    if (image == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "SVG file(\"" << input_file << "\") couldn't be parsed by nano svg.";
        return false; 
    }

    double tesselation_tolerance = 1e10;
    NSVGLineParams params(tesselation_tolerance);
    ExPolygonsWithIds shapes = create_shape_with_ids(*image, params);
    if (shapes.empty()) {
        BOOST_LOG_TRIVIAL(error) << "SVG file(\"" << input_file << "\") do not contain embossedabled shape.";
        return false; // No shapes in svg
    }

    double depth_in_mm = 10.; // in mm
    bool use_surface = false;
    EmbossProjection emboss_projection{depth_in_mm, use_surface};    

    EmbossShape emboss_shape;
    emboss_shape.shapes_with_ids = std::move(shapes);
    emboss_shape.projection = std::move(emboss_projection);
    emboss_shape.svg_file = std::move(svg_file);

    // unify to one expolygons
    // EmbossJob.cpp --> ExPolygons create_shape(DataBase &input, Fnc was_canceled) {
    ExPolygons union_shape = union_with_delta(emboss_shape, Emboss::UNION_DELTA, Emboss::UNION_MAX_ITERATIN);

    // create projection
    double scale = emboss_shape.scale;
    double depth = emboss_shape.projection.depth / scale;    
    auto projectZ = std::make_unique<Emboss::ProjectZ>(depth);
    Transform3d tr{Eigen::Scaling(scale)};
    Emboss::ProjectTransform project(std::move(projectZ), tr);

    // convert 2d shape to 3d triangles
    indexed_triangle_set its = Emboss::polygons2model(union_shape, project);
    TriangleMesh triangl_mesh(std::move(its));

    // add mesh to model
    ModelObject *object = output_model.add_object();
    assert(object != nullptr);
    if (object == nullptr)
        return false;
    object->name = get_file_name(input_file);
    ModelVolume* volume = object->add_volume(std::move(triangl_mesh));
    assert(volume != nullptr);
    if (volume == nullptr) {
        output_model.delete_object(object);
        return false;
    }
    volume->name = object->name; // copy
    volume->emboss_shape = std::move(emboss_shape);
    object->invalidate_bounding_box();
    return true;
}

}; // namespace Slic3r
