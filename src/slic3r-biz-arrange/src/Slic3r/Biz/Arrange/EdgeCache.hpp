#pragma once

#include <vector>
#include <algorithm>
#include <cmath>
#include <cassert>
#include <cstddef>

#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Domain/Polygon.hpp"
#include "Slic3r/Domain/Types.hpp"

namespace Slic3r::Biz::Arrange {

// Position on the circumference of an ExPolygon.
// countour_id:  0th is contour, 1..N are holes
// dist: position given as a floating point number within <0., 1.>
struct ContourLocation
{
    size_t contour_id;
    double dist;
};

void fill_distances(const Domain::Polygon& poly, std::vector<double>& distances);

Domain::Vec2crd coords(const Domain::Polygon& poly, const std::vector<double>& distances, double distance);

// A class for getting a point on the circumference of the polygon (in log time)
//
// This is a transformation of the provided polygon to be able to pinpoint
// locations on the circumference. The optimizer will pass a floating point
// value e.g. within <0,1> and we have to transform this value quickly into a
// coordinate on the circumference. By definition 0 should yield the first
// vertex and 1.0 would be the last (which should coincide with first).
//
// We also have to make this work for the holes of the captured polygon.
class EdgeCache
{
    struct ContourCache
    {
        const Domain::Polygon* poly;
        std::vector<double> distances;
    } m_contour;

    std::vector<ContourCache> m_holes;

    void create_cache(const Domain::ExPolygon& sh);

    Domain::Vec2crd coords(const ContourCache& cache, double distance) const;

public:
    explicit EdgeCache(const Domain::ExPolygon* sh)
    {
        create_cache(*sh);
    }

    // Given coeff for accuracy <0., 1.>, return the number of vertices to skip
    // when fetching corners.
    static inline size_t stride(const size_t N, double accuracy)
    {
        size_t n = std::max(size_t{1}, N);
        return static_cast<Domain::coord_t>(std::round(N / std::pow(n, std::pow(accuracy, 1. / 3.))));
    }

    void sample_contour(double accuracy, std::vector<ContourLocation>& samples);

    Domain::Vec2crd coords(const ContourLocation& loc) const
    {
        assert(loc.contour_id <= m_holes.size());

        return loc.contour_id > 0 ? coords(m_holes[loc.contour_id - 1], loc.dist) :
                                    coords(m_contour, loc.dist);
    }
};

} // namespace Slic3r::Biz::Arrange
