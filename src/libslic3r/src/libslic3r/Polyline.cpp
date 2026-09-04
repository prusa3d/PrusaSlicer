#include <utility>
#include <cmath>
#include <limits>
#include <cassert>

#include "Slic3r/Biz/Algorithms/Polyline.hpp"
#include "Polyline.hpp"
#include "Slic3r/Exception.hpp"
#include "Line.hpp"
#include "libslic3r/MultiPoint.hpp"
#include "libslic3r/libslic3r.h"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"

using namespace Slic3r::Biz;

namespace Slic3r {

namespace BB = Biz::Algorithms::BoundingBox;

BoundingBox ThickPolyline::bounding_box() const {
    return BB::construct(this->points);
}

// Temporary proxy function.
BoundingBox get_extents(const Polyline& polyline)
{
    const Domain::BoundingBox2crd bbox = Algorithms::Polyline::get_bounding_box(polyline);
    return BoundingBox{bbox.min, bbox.max};
}

// Temporary proxy function.
BoundingBox get_extents(const Polylines &polylines)
{
    Domain::BoundingBox2crd bb;
    if (! polylines.empty()) {
        bb = Algorithms::Polyline::get_bounding_box(polylines.front());
        for (size_t i = 1; i < polylines.size(); ++ i)
            bb = Algorithms::BoundingBox::merge(bb, Algorithms::Polyline::get_bounding_box(polylines[i]));
    }
    return BoundingBox{bb.min, bb.max};
}

std::pair<int, Point> foot_pt(const Points &polyline, const Point &pt)
{
    if (polyline.size() < 2)
        return std::make_pair(-1, Point(0, 0));

    auto  d2_min  = std::numeric_limits<double>::max();
    Point foot_pt_min;
    Point prev = polyline.front();
    auto  it = polyline.begin();
    auto  it_proj = polyline.begin();
    for (++ it; it != polyline.end(); ++ it) {
        Point foot_pt;
        if (double d2 = Algorithms::Line::distance_to_squared(Line(prev, *it), pt, foot_pt); d2 < d2_min) {
            d2_min      = d2;
            foot_pt_min = foot_pt;
            it_proj     = it;
        }
        prev = *it;
    }
    return std::make_pair(int(it_proj - polyline.begin()) - 1, foot_pt_min);
}

size_t total_lines_count(const ThickPolylines &thick_polylines) {
    size_t lines_cnt = 0;
    for (const ThickPolyline &thick_polyline : thick_polylines) {
        if (thick_polyline.points.size() > 1) {
            lines_cnt += thick_polyline.points.size() - 1;
        }
    }

    return lines_cnt;
}

Lines to_lines(const ThickPolyline &thick_polyline) {
    Lines lines;
    if (thick_polyline.points.size() >= 2) {
        lines.reserve(thick_polyline.points.size() - 1);

        for (Points::const_iterator it = thick_polyline.points.begin(); it != thick_polyline.points.end() - 1; ++it) {
            lines.emplace_back(*it, *(it + 1));
        }
    }

    return lines;
}

Lines to_lines(const ThickPolylines &thick_polylines) {
    const size_t lines_cnt = total_lines_count(thick_polylines);

    Lines lines;
    lines.reserve(lines_cnt);
    for (const ThickPolyline &thick_polyline : thick_polylines) {
        for (Points::const_iterator it = thick_polyline.points.begin(); it != thick_polyline.points.end() - 1; ++it) {
            lines.emplace_back(*it, *(it + 1));
        }
    }

    return lines;
}

BoundingBox get_extents(const ThickPolyline &thick_polyline) {
    return thick_polyline.bounding_box();
}

BoundingBox get_extents(const ThickPolylines &thick_polylines) {
    BoundingBox bbox;
    if (!thick_polylines.empty()) {
        bbox = thick_polylines.front().bounding_box();
        for (size_t i = 1; i < thick_polylines.size(); ++i) {
            bbox = BB::merge(bbox, BB::construct(thick_polylines[i].points));
        }
    }

    return bbox;
}

ThickLines ThickPolyline::thicklines() const
{
    ThickLines lines;
    if (this->points.size() >= 2) {
        lines.reserve(this->points.size() - 1);
        for (size_t i = 0; i + 1 < this->points.size(); ++ i)
            lines.emplace_back(this->points[i], this->points[i + 1], this->width[2 * i], this->width[2 * i + 1]);
    }
    return lines;
}

// Removes the given distance from the end of the ThickPolyline
void ThickPolyline::clip_end(double distance)
{
    if (! this->empty()) {
        assert(this->width.size() == (this->points.size() - 1) * 2);
        while (distance > 0) {
            Vec2d last_point = this->last_point().cast<double>();
            this->points.pop_back();
            if (this->points.empty()) {
                assert(this->width.empty());
                break;
            }
            double last_width = this->width.back();
            this->width.pop_back();

            Vec2d    vec            = this->last_point().cast<double>() - last_point;
            double width_diff     = this->width.back() - last_width;
            double   vec_length_sqr = vec.squaredNorm();
            if (vec_length_sqr > distance * distance) {
                double t = (distance / std::sqrt(vec_length_sqr));
                this->points.emplace_back((last_point + vec * t).cast<coord_t>());
                this->width.emplace_back(last_width + width_diff * t);
                assert(this->width.size() == (this->points.size() - 1) * 2);
                return;
            } else
                this->width.pop_back();

            distance -= std::sqrt(vec_length_sqr);
        }
    }
    assert(this->points.empty() ? this->width.empty() : this->width.size() == (this->points.size() - 1) * 2);
}

void ThickPolyline::start_at_index(int index)
{
    assert(index >= 0 && index < this->points.size());
    assert(this->points.front() == this->points.back() && this->width.front() == this->width.back());
    if (index != 0 && index + 1 != int(this->points.size()) && this->points.front() == this->points.back() && this->width.front() == this->width.back()) {
        this->points.pop_back();
        assert(this->points.size() * 2 == this->width.size());
        std::rotate(this->points.begin(), this->points.begin() + index, this->points.end());
        std::rotate(this->width.begin(), this->width.begin() + 2 * index, this->width.end());
        this->points.emplace_back(this->points.front());
    }
}

}
