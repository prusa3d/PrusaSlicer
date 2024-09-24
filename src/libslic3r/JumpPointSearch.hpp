///|/ Copyright (c) Prusa Research 2022 - 2023 Pavel Mikuš @Godrak
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef SRC_LIBSLIC3R_JUMPPOINTSEARCH_HPP_
#define SRC_LIBSLIC3R_JUMPPOINTSEARCH_HPP_

#include <unordered_map>
#include <unordered_set>

#include "BoundingBox.hpp"
#include "Polygon.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/Line.hpp"

namespace Slic3r {
class Layer;

class JPSPathFinder
{
    using Pixel = Point;
    std::unordered_set<Pixel, PointHash> inpassable;
    coordf_t print_z;
    BoundingBox max_search_box;
    Lines bed_shape;

    const coord_t resolution = scaled(1.5);
    Pixel         pixelize(const Point &p) { return p / resolution; }
    Point         unpixelize(const Pixel &p) { return p * resolution; }

public:
    JPSPathFinder() = default;
    void     init_bed_shape(const Points &bed_shape) { this->bed_shape = (to_lines(Polygon{bed_shape})); };
    void     clear();
    void     add_obstacles(const Lines &obstacles);
    void     add_obstacles(const Layer *layer, const Point &global_origin);
    Polyline find_path(const Point &start, const Point &end);
};

} // namespace Slic3r

#endif /* SRC_LIBSLIC3R_JUMPPOINTSEARCH_HPP_ */
