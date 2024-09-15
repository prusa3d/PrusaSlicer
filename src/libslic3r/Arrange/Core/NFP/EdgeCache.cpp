///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "EdgeCache.hpp"

#include <iterator>

#include "CircularEdgeIterator.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Line.hpp"

namespace Slic3r { namespace arr2 {

void EdgeCache::create_cache(const ExPolygon &sh)
{
    m_contour.distances.reserve(sh.contour.size());
    m_holes.reserve(sh.holes.size());

    m_contour.poly = &sh.contour;

    fill_distances(sh.contour, m_contour.distances);

    for (const Polygon &hole : sh.holes) {
        auto &hc = m_holes.emplace_back();
        hc.poly = &hole;
        fill_distances(hole, hc.distances);
    }
}

Vec2crd EdgeCache::coords(const ContourCache &cache, double distance) const
{
    assert(cache.poly);
    return arr2::coords(*cache.poly, cache.distances, distance);
}

void EdgeCache::sample_contour(double accuracy, std::vector<ContourLocation> &samples)
{
    const auto N = m_contour.distances.size();
    const auto S = stride(N, accuracy);

    if (N == 0 || S == 0)
        return;

    samples.reserve(N / S + 1);
    for(size_t i = 0; i < N; i += S) {
        samples.emplace_back(
            ContourLocation{0, m_contour.distances[i] / m_contour.distances.back()});
    }

    for (size_t hidx = 1; hidx <= m_holes.size(); ++hidx) {
        auto& hc = m_holes[hidx - 1];

        const auto NH = hc.distances.size();
        const auto SH = stride(NH, accuracy);

        if (NH == 0 || SH == 0)
            continue;

        samples.reserve(samples.size() + NH / SH + 1);
        for (size_t i = 0; i < NH; i += SH) {
            samples.emplace_back(
                ContourLocation{hidx, hc.distances[i] / hc.distances.back()});
        }
    }
}

Vec2crd coords(const Polygon &poly, const std::vector<double> &distances, double distance)
{
    assert(poly.size() > 1 && distance >= .0 && distance <= 1.0);

    // distance is from 0.0 to 1.0, we scale it up to the full length of
    // the circumference
    double d = distance * distances.back();

    // Magic: we find the right edge in log time
    auto it = std::lower_bound(distances.begin(), distances.end(), d);

    assert(it != distances.end());

    auto idx  = it - distances.begin(); // get the index of the edge
    auto &pts = poly.points;
    auto edge = idx == long(pts.size() - 1) ? Line(pts.back(), pts.front()) :
                                              Line(pts[idx], pts[idx + 1]);

    // Get the remaining distance on the target edge
    auto ed = d - (idx > 0 ? *std::prev(it) : 0 );

    double t = ed / edge.length();
    Vec2d n {double(edge.b.x()) - edge.a.x(), double(edge.b.y()) - edge.a.y()};
    Vec2crd ret = (edge.a.cast<double>() + t * n).cast<coord_t>();

    return ret;
}

void fill_distances(const Polygon &poly, std::vector<double> &distances)
{
    distances.reserve(poly.size());

    double dist = 0.;
    auto lrange = line_range(poly);
    for (const Line l : lrange) {
        dist += l.length();
        distances.emplace_back(dist);
    }
}

}} // namespace Slic3r::arr2
