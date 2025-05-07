///|/ Copyright (c) Prusa Research 2016 - 2023 Vojtěch Bubník @bubnikv, Pavel Mikuš @Godrak, Filip Sykala @Jony01, Lukáš Hejl @hejllukas, Enrico Turri @enricoturri1966, Lukáš Matěna @lukasmatena, Tomáš Mészáros @tamasmeszaros
///|/ Copyright (c) Slic3r 2013 - 2016 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2014 Petr Ledvina @ledvinap
///|/ Copyright (c) 2014 Kamil Kwolek
///|/ Copyright (c) 2013 Jose Luis Perez Diez
///|/
///|/ ported from lib/Slic3r/Point.pm:
///|/ Copyright (c) Prusa Research 2018 Vojtěch Bubník @bubnikv
///|/ Copyright (c) Slic3r 2011 - 2015 Alessandro Ranellucci @alranel
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "Point.hpp"

#include <algorithm>
#include <cstring>

#include "Int128.hpp"
#include "BoundingBox.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r {

std::vector<Vec3f> transform(const std::vector<Vec3f>& points, const Transform3f& t)
{
    unsigned int vertices_count = (unsigned int)points.size();
    if (vertices_count == 0)
        return std::vector<Vec3f>();

    unsigned int data_size = 3 * vertices_count * sizeof(float);

    Eigen::MatrixXf src(3, vertices_count);
    ::memcpy((void*)src.data(), (const void*)points.data(), data_size);

    Eigen::MatrixXf dst(3, vertices_count);
    dst = t * src.colwise().homogeneous();

    std::vector<Vec3f> ret_points(vertices_count, Vec3f::Zero());
    ::memcpy((void*)ret_points.data(), (const void*)dst.data(), data_size);
    return ret_points;
}

Pointf3s transform(const Pointf3s& points, const Transform3d& t)
{
    unsigned int vertices_count = (unsigned int)points.size();
    if (vertices_count == 0)
        return Pointf3s();

    unsigned int data_size = 3 * vertices_count * sizeof(double);

    Eigen::MatrixXd src(3, vertices_count);
    ::memcpy((void*)src.data(), (const void*)points.data(), data_size);

    Eigen::MatrixXd dst(3, vertices_count);
    dst = t * src.colwise().homogeneous();

    Pointf3s ret_points(vertices_count, Vec3d::Zero());
    ::memcpy((void*)ret_points.data(), (const void*)dst.data(), data_size);
    return ret_points;
}

void Point::rotate(double angle, const Point &center)
{
    Vec2d  cur = this->cast<double>();
    double s   = ::sin(angle);
    double c   = ::cos(angle);
    auto   d   = cur - center.cast<double>();
    this->x() = fast_round_up<coord_t>(center.x() + c * d.x() - s * d.y());
    this->y() = fast_round_up<coord_t>(center.y() + s * d.x() + c * d.y());
}

bool has_duplicate_points(Points &&pts)
{
    std::sort(pts.begin(), pts.end());
    for (size_t i = 1; i < pts.size(); ++ i)
        if (pts[i - 1] == pts[i])
            return true;
    return false;
}

Points collect_duplicates(Points pts /* Copy */)
{
    std::sort(pts.begin(), pts.end());
    Points duplicits;
    const Point *prev = &pts.front();
    for (size_t i = 1; i < pts.size(); ++i) {
        const Point *act = &pts[i];
        if (*prev == *act) {
            // duplicit point
            if (!duplicits.empty() && duplicits.back() == *act)
                continue; // only unique duplicits
            duplicits.push_back(*act);
        }
        prev = act;
    }
    return duplicits;
}

template<bool IncludeBoundary>
BoundingBox get_extents(const Points &pts)
{ 
    BoundingBox out;
    BoundingBox::construct<IncludeBoundary>(out, pts.begin(), pts.end());
    return out;
}
template BoundingBox get_extents<false>(const Points &pts);
template BoundingBox get_extents<true>(const Points &pts);

// if IncludeBoundary, then a bounding box is defined even for a single point.
// otherwise a bounding box is only defined if it has a positive area.
template<bool IncludeBoundary>
BoundingBox get_extents(const VecOfPoints &pts)
{
    BoundingBox bbox;
    for (const Points &p : pts)
        bbox.merge(get_extents<IncludeBoundary>(p));
    return bbox;
}
template BoundingBox get_extents<false>(const VecOfPoints &pts);
template BoundingBox get_extents<true>(const VecOfPoints &pts);

BoundingBoxf get_extents(const std::vector<Vec2d> &pts)
{
    BoundingBoxf bbox;
    for (const Vec2d &p : pts)
        bbox.merge(p);
    return bbox;
}

int nearest_point_index(const Points &points, const Point &pt)
{
    int64_t distance = std::numeric_limits<int64_t>::max();
    int     idx      = -1;

    for (const Point &pt2 : points) {
        // If the X distance of the candidate is > than the total distance of the
        // best previous candidate, we know we don't want it.
        int64_t d = sqr<int64_t>(pt2.x() - pt.x());
        if (d < distance) {
            // If the Y distance of the candidate is > than the total distance of the
            // best previous candidate, we know we don't want it.
            d += sqr<int64_t>(pt2.y() - pt.y());
            if (d < distance) {
                idx      = &pt2 - points.data();
                distance = d;
            }
        }
    }

    return idx;
}

std::ostream& operator<<(std::ostream &stm, const Vec2d &pointf)
{
    return stm << pointf(0) << "," << pointf(1);
}

namespace int128 {

int orient(const Vec2crd &p1, const Vec2crd &p2, const Vec2crd &p3)
{
    Slic3r::Vector v1(p2 - p1);
    Slic3r::Vector v2(p3 - p1);
    return Int128::sign_determinant_2x2_filtered(v1.x(), v1.y(), v2.x(), v2.y());
}

int cross(const Vec2crd &v1, const Vec2crd &v2)
{
    return Int128::sign_determinant_2x2_filtered(v1.x(), v1.y(), v2.x(), v2.y());
}

}

}
