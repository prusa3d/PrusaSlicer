#ifndef slic3r_Polyline_hpp_
#define slic3r_Polyline_hpp_

#include <stddef.h>
#include <string>
#include <vector>
#include <algorithm>
#include <initializer_list>
#include <iterator>
#include <utility>
#include <cstddef>

#include "Slic3r/Domain/MultiPoint.hpp"
#include "Slic3r/Domain/Polyline.hpp"
#include "Slic3r/Domain/Point.hpp"
#include "Slic3r/Biz/Algorithms/Polyline.hpp"
#include "libslic3r.h"
#include "Line.hpp"
#include "MultiPoint.hpp"
#include "libslic3r/Point.hpp"

namespace Slic3r {

using Polyline = Slic3r::Domain::Polyline;
using Polylines = Slic3r::Domain::Polylines;

struct ThickPolyline;

typedef std::vector<ThickPolyline> ThickPolylines;

extern BoundingBox get_extents(const Slic3r::Polyline &polyline);
extern BoundingBox get_extents(const Slic3r::Polylines &polylines);

// Merge polylines at their respective end points.
// dst_first: the merge point is at dst.begin() or dst.end()?
// src_first: the merge point is at src.begin() or src.end()?
// The orientation of the resulting polyline is unknown, the output polyline may start
// either with src piece or dst piece.
template<typename PointsType>
inline void polylines_merge(PointsType &dst, bool dst_first, PointsType &&src, bool src_first)
{
    if (dst_first) {
        if (src_first)
            std::reverse(dst.begin(), dst.end());
        else
            std::swap(dst, src);
    } else if (! src_first)
        std::reverse(src.begin(), src.end());
    // Merge src into dst.
    append(dst, std::move(src));
}

// Returns index of a segment of a polyline and foot point of pt on polyline.
std::pair<int, Point> foot_pt(const Points &polyline, const Point &pt);

struct ThickPolyline {
    ThickPolyline() = default;
    ThickLines thicklines() const;

    const Point& first_point()  const { return this->points.front(); }
    const Point& last_point()   const { return this->points.back(); }
    size_t       size()         const { return this->points.size(); }
    bool         is_valid()     const { return this->points.size() >= 2; }
    bool         empty()        const { return this->points.empty(); }
    double       length()       const { return Slic3r::Biz::Algorithms::Polyline::length(this->points); }

    void         clear() { this->points.clear(); this->width.clear(); }

    void reverse() {
        std::reverse(this->points.begin(), this->points.end());
        std::reverse(this->width.begin(), this->width.end());
        std::swap(this->endpoints.first, this->endpoints.second);
    }

    void clip_end(double distance);

    // Make this closed ThickPolyline starting in the specified index.
    // Be aware that this method can be applicable just for closed ThickPolyline.
    // On open ThickPolyline make no effect.
    void start_at_index(int index);

    BoundingBox bounding_box() const;

    Points                  points;
    // vector of startpoint width and endpoint width of each line segment. The size should be always (points.size()-1) * 2
    // e.g. let four be points a,b,c,d. that are three lines ab, bc, cd. for each line, there should be start width, so the width vector is:
    // w(a), w(b), w(b), w(c), w(c), w(d)
    std::vector<double>   width;
    std::pair<bool,bool>    endpoints { false, false };
};

inline ThickPolylines to_thick_polylines(Polylines &&polylines, const double width)
{
    ThickPolylines out;
    out.reserve(polylines.size());
    for (Polyline &polyline : polylines) {
        out.emplace_back();
        out.back().width.assign((polyline.points.size() - 1) * 2, width);
        out.back().points = std::move(polyline.points);
    }
    return out;
}

size_t total_lines_count(const ThickPolylines &thick_polylines);

Lines to_lines(const ThickPolyline &thick_polyline);
Lines to_lines(const ThickPolylines &thick_polylines);

BoundingBox get_extents(const ThickPolyline &thick_polyline);
BoundingBox get_extents(const ThickPolylines &thick_polylines);

}

#endif
