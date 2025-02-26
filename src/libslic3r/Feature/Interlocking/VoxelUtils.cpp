// Copyright (c) 2022 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

#include <boost/log/trivial.hpp>

#include "VoxelUtils.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Fill/FillRectilinear.hpp"
#include "libslic3r/Surface.hpp"

namespace Slic3r
{

DilationKernel::DilationKernel(GridPoint3 kernel_size, DilationKernel::Type type) : kernel_size_(kernel_size)
{
    coord_t mult = kernel_size.x() * kernel_size.y() * kernel_size.z(); // multiplier for division to avoid rounding and to avoid use of floating point numbers
    relative_cells_.reserve(mult);
    GridPoint3 half_kernel = kernel_size / 2;

    GridPoint3 start = -half_kernel;
    GridPoint3 end = kernel_size - half_kernel;
    for (coord_t x = start.x(); x < end.x(); x++)
    {
        for (coord_t y = start.y(); y < end.y(); y++)
        {
            for (coord_t z = start.z(); z < end.z(); z++)
            {
                GridPoint3 current(x, y, z);
                if (type != Type::CUBE)
                {
                    GridPoint3 limit((x < 0) ? start.x() : end.x() - 1, (y < 0) ? start.y() : end.y() - 1, (z < 0) ? start.z() : end.z() - 1);
                    if (limit.x() == 0)
                        limit.x() = 1;
                    if (limit.y() == 0)
                        limit.y() = 1;
                    if (limit.z() == 0)
                        limit.z() = 1;
                    const GridPoint3 rel_dists = (mult * current).array() / limit.array();
                    if ((type == Type::DIAMOND && rel_dists.x() + rel_dists.y() + rel_dists.z() > mult) || (type == Type::PRISM && rel_dists.x() + rel_dists.y() > mult))
                    {
                        continue; // don't consider this cell
                    }
                }
                relative_cells_.emplace_back(x, y, z);
            }
        }
    }
}

bool VoxelUtils::walkLine(Vec3crd start, Vec3crd end, const std::function<bool(GridPoint3)>& process_cell_func) const
{
    Vec3crd diff = end - start;

    const GridPoint3 start_cell = toGridPoint(start);
    const GridPoint3 end_cell = toGridPoint(end);
    if (start_cell == end_cell)
    {
        return process_cell_func(start_cell);
    }

    Vec3crd current_cell = start_cell;
    while (true)
    {
        bool continue_ = process_cell_func(current_cell);

        if (! continue_)
        {
            return false;
        }

        int stepping_dim = -1; // dimension in which the line next exits the current cell
        double percentage_along_line = std::numeric_limits<double>::max();
        for (int dim = 0; dim < 3; dim++)
        {
            if (diff[dim] == 0)
            {
                continue;
            }
            coord_t crossing_boundary = toLowerCoord(current_cell[dim], dim) + (diff[dim] > 0) * cell_size_[dim];
            double percentage_along_line_here = (crossing_boundary - start[dim]) / static_cast<double>(diff[dim]);
            if (percentage_along_line_here < percentage_along_line)
            {
                percentage_along_line = percentage_along_line_here;
                stepping_dim = dim;
            }
        }
        assert(stepping_dim != -1);
        if (percentage_along_line > 1.0)
        {
            // next cell is beyond the end
            return true;
        }
        current_cell[stepping_dim] += (diff[stepping_dim] > 0) ? 1 : -1;
    }
    return true;
}


bool VoxelUtils::walkPolygons(const ExPolygon& polys, coord_t z, const std::function<bool(GridPoint3)>& process_cell_func) const
{
    for (const Polygon& poly : to_polygons(polys))
    {
        Point last = poly.back();
        for (Point p : poly)
        {
            bool continue_ = walkLine(Vec3crd(last.x(), last.y(), z), Vec3crd(p.x(), p.y(), z), process_cell_func);
            if (! continue_)
            {
                return false;
            }
            last = p;
        }
    }
    return true;
}

bool VoxelUtils::walkDilatedPolygons(const ExPolygon& polys, coord_t z, const DilationKernel& kernel, const std::function<bool(GridPoint3)>& process_cell_func) const
{
    ExPolygon translated = polys;
    GridPoint3 k = kernel.kernel_size_;
    k.x() %= 2;
    k.y() %= 2;
    k.z() %= 2;
    const Vec3crd translation = (Vec3crd(1, 1, 1) - k).array() * cell_size_.array() / 2;
    if (translation.x() && translation.y())
    {
        translated.translate(Point(translation.x(), translation.y()));
    }
    return walkPolygons(translated, z + translation.z(), dilate(kernel, process_cell_func));
}

bool VoxelUtils::_walkAreas(const ExPolygon &ex_polygon, coord_t z, const std::function<bool(GridPoint3)> &process_cell_func) const
{
    Points grid_points;
    try {
        const BoundingBox ex_polygon_bbox = get_extents(ex_polygon);
        grid_points                       = sample_grid_pattern(ex_polygon, cell_size_.x(), ex_polygon_bbox);
    } catch (InfillFailedException &) {
        BOOST_LOG_TRIVIAL(warning) << "Sampling ExPolygon failed.";
    }

    const Vec3crd grid_point_offset(cell_size_.x() / 2, cell_size_.y() / 2, z);
    for (const Point &grid_point : grid_points) {
        if (const bool continue_ = process_cell_func(toGridPoint(grid_point, grid_point_offset)); !continue_) {
            return false;
        }
    }

    return true;
}

bool VoxelUtils::walkDilatedAreas(const ExPolygon& polys, coord_t z, const DilationKernel& kernel, const std::function<bool(GridPoint3)>& process_cell_func) const
{
    ExPolygon translated = polys;
    GridPoint3 k = kernel.kernel_size_;
    k.x() %= 2;
    k.y() %= 2;
    k.z() %= 2;
    const Vec3crd translation = (Vec3crd(1, 1, 1) - k).array() * cell_size_.array() / 2 // offset half a cell when using an even kernel
                               - cell_size_.array() / 2; // offset half a cell so that the dots of spreadDotsArea are centered on the middle of the cell isntead of the lower corners.
    if (translation.x() && translation.y())
    {
        translated.translate(Point(translation.x(), translation.y()));
    }
    return _walkAreas(translated, z + translation.z(), dilate(kernel, process_cell_func));
}

std::function<bool(GridPoint3)> VoxelUtils::dilate(const DilationKernel& kernel, const std::function<bool(GridPoint3)>& process_cell_func) const
{
    return [&process_cell_func, &kernel](GridPoint3 loc)
    {
        for (const GridPoint3& rel : kernel.relative_cells_)
        {
            bool continue_ = process_cell_func(loc + rel);
            if (! continue_)
                return false;
        }
        return true;
    };
}
} // namespace cura
