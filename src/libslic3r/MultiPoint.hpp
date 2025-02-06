///|/ Copyright (c) Prusa Research 2016 - 2023 Tomáš Mészáros @tamasmeszaros, Vojtěch Bubník @bubnikv, Lukáš Hejl @hejllukas, Enrico Turri @enricoturri1966
///|/ Copyright (c) Slic3r 2013 - 2016 Alessandro Ranellucci @alranel
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_MultiPoint_hpp_
#define slic3r_MultiPoint_hpp_

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <algorithm>
#include <vector>
#include <Eigen/Geometry>
#include <initializer_list>
#include <iterator>
#include <utility>
#include <cassert>
#include <cinttypes>
#include <cmath>
#include <cstddef>

#include "libslic3r.h"
#include "Line.hpp"
#include "Point.hpp"
#include "libslic3r/Point.hpp"

namespace Slic3r {

class BoundingBox;
class BoundingBox3;

// Reduces polyline in the <begin, end) range, outputs into the output iterator.
// Output iterator may be equal to input iterator as long as the iterator value type move operator supports move at the same input / output address.
template<typename SquareLengthType, typename InputIterator, typename OutputIterator, typename TakeFloaterPredicate, typename PointGetter>
inline OutputIterator douglas_peucker(InputIterator begin, InputIterator end, OutputIterator out, TakeFloaterPredicate take_floater_predicate, PointGetter point_getter)
{
    using InputIteratorCategory = typename std::iterator_traits<InputIterator>::iterator_category;
    static_assert(std::is_base_of_v<std::input_iterator_tag, InputIteratorCategory>);
    using Vector = Eigen::Matrix<SquareLengthType, 2, 1, Eigen::DontAlign>;
    if (begin != end) {
        // Supporting in-place reduction and the data type may be generic, thus we are always making a copy of the point value before there is a chance
        // to override input by moving the data to the output.
        auto a = point_getter(*begin);
        *out ++ = std::move(*begin);
        if (auto next = std::next(begin); next == end) {
            // Single point input only.
        } else if (std::next(next) == end) {
            // Two points input.
            *out ++ = std::move(*next);
        } else {
            InputIterator anchor  = begin;
            InputIterator floater = std::prev(end);
            std::vector<InputIterator> dpStack;
            if constexpr (std::is_base_of_v<std::random_access_iterator_tag, InputIteratorCategory>)
                dpStack.reserve(end - begin);
            dpStack.emplace_back(floater);
            auto f = point_getter(*floater);
            for (;;) {
                assert(anchor != floater);
                bool            take_floater = false;
                InputIterator   furthest     = anchor;
                if (std::next(anchor) == floater) {
                    // Two point segment. Accept the floater.
                    take_floater = true;
                } else {
                    std::optional<SquareLengthType> max_dist_sq;
                    // Find point furthest from line seg created by (anchor, floater) and note it.
                    const Vector v = (f - a).template cast<SquareLengthType>();
                    if (const SquareLengthType l2 = v.squaredNorm(); l2 == 0) {
                        // Zero length segment, find the furthest point between anchor and floater.
                        for (auto it = std::next(anchor); it != floater; ++ it) {
                            if (SquareLengthType dist_sq = (point_getter(*it) - a).template cast<SquareLengthType>().squaredNorm(); !max_dist_sq.has_value() || dist_sq > max_dist_sq) {
                                max_dist_sq = dist_sq;
                                furthest    = it;
                            }
                        }
                    } else {
                        // Find Find the furthest point from the line <anchor, floater>.
                        const double dl2 = double(l2);
                        const Vec2d  dv  = v.template cast<double>();
                        for (auto it = std::next(anchor); it != floater; ++ it) {
                            const auto   p  = point_getter(*it);
                            const Vector va = (p - a).template cast<SquareLengthType>();
                            const SquareLengthType t = va.dot(v);
                            SquareLengthType dist_sq;
                            if (t <= 0) {
                                dist_sq = va.squaredNorm();
                            } else if (t >= l2) {
                                dist_sq = (p - f).template cast<SquareLengthType>().squaredNorm();
                            } else if (double dt = double(t) / dl2; dt <= 0) {
                                dist_sq = va.squaredNorm();
                            } else if (dt >= 1.) {
                                dist_sq = (p - f).template cast<SquareLengthType>().squaredNorm();
                            } else {
                                const Vector w = (dt * dv).cast<SquareLengthType>();
                                dist_sq = (w - va).squaredNorm();
                            }

                            if (!max_dist_sq.has_value() || dist_sq > max_dist_sq) {
                                max_dist_sq  = dist_sq;
                                furthest     = it;
                            }
                        }                        
                    }

                    assert(max_dist_sq.has_value());

                    // Remove points between the anchor and the floater when the predicate is satisfied.
                    take_floater = take_floater_predicate(anchor, floater, *max_dist_sq);
                }

                if (take_floater) {
                    // The points between anchor and floater are close to the <anchor, floater> line.
                    // Drop the points between them.
                    a = f;
                    *out ++ = std::move(*floater);
                    anchor = floater;
                    assert(dpStack.back() == floater);
                    dpStack.pop_back();
                    if (dpStack.empty())
                        break;

                    floater = dpStack.back();
                    f = point_getter(*floater);
                } else {
                    // The furthest point is too far from the segment <anchor, floater>. 
                    // Divide recursively.
                    floater = furthest;
                    f = point_getter(*floater);
                    dpStack.emplace_back(floater);
                }
            }
        }
    }
    return out;
}

template<typename SquareLengthType, typename InputIterator, typename OutputIterator, typename PointGetter>
inline OutputIterator douglas_peucker(InputIterator begin, InputIterator end, OutputIterator out, const double tolerance, PointGetter point_getter) {
    const auto tolerance_sq = static_cast<SquareLengthType>(sqr(tolerance));

    const auto take_floater_predicate = [&tolerance_sq](InputIterator, InputIterator, const SquareLengthType max_dist_sq) -> bool {
        return max_dist_sq <= tolerance_sq;
    };

    return douglas_peucker<SquareLengthType>(begin, end, out, take_floater_predicate, point_getter);
}

template<typename OutputIterator>
inline OutputIterator douglas_peucker(Points::const_iterator begin, Points::const_iterator end, OutputIterator out, const double tolerance)
{
    return douglas_peucker<int64_t>(begin, end, out, tolerance, [](const Point &p) { return p; });
}

template<typename OutputIterator>
inline OutputIterator douglas_peucker(Pointfs::const_iterator begin, Pointfs::const_iterator end, OutputIterator out, const double tolerance)
{
    return douglas_peucker<double>(begin, end, out, tolerance, [](const Vec2d &p) { return p; });
}

inline Points douglas_peucker(const Points &src, const double tolerance) 
{
    Points out;
    out.reserve(src.size());
    douglas_peucker(src.begin(), src.end(), std::back_inserter(out), tolerance);
    return out;
}

class MultiPoint
{
public:
    Points points;

    MultiPoint() = default;
    MultiPoint(const MultiPoint &other) : points(other.points) {}
    MultiPoint(MultiPoint &&other) : points(std::move(other.points)) {}
    MultiPoint(std::initializer_list<Point> list) : points(list) {}
    explicit MultiPoint(const Points &_points) : points(_points) {}
    virtual ~MultiPoint() = default;
    MultiPoint& operator=(const MultiPoint &other) { points = other.points; return *this; }
    MultiPoint& operator=(MultiPoint &&other) { points = std::move(other.points); return *this; }
    void scale(double factor);
    void scale(double factor_x, double factor_y);
    void translate(double x, double y) { this->translate(Point(coord_t(x), coord_t(y))); }
    void translate(const Point &vector);
    void rotate(double angle) { this->rotate(cos(angle), sin(angle)); }
    void rotate(double cos_angle, double sin_angle);
    void rotate(double angle, const Point &center);
    virtual void reverse() { std::reverse(this->points.begin(), this->points.end()); }

    const Point& front() const { return this->points.front(); }
    const Point& back() const { return this->points.back(); }
    const Point& first_point() const { return this->front(); }
    size_t size() const { return points.size(); }
    bool   empty() const { return points.empty(); }
    bool   is_valid() const { return this->points.size() >= 2; }

    // Return index of a polygon point exactly equal to point.
    // Return -1 if no such point exists.
    int  find_point(const Point &point) const;
    // Return index of the closest point to point closer than scaled_epsilon.
    // Return -1 if no such point exists.
    int  find_point(const Point &point, const double scaled_epsilon) const;
    int  closest_point_index(const Point &point) const {
        int idx = -1;
        if (! this->points.empty()) {
            idx = 0;
            double dist_min = (point - this->points.front()).cast<double>().norm();
            for (int i = 1; i < int(this->points.size()); ++ i) {
                double d = (this->points[i] - point).cast<double>().norm();
                if (d < dist_min) {
                    dist_min = d;
                    idx = i;
                }
            }
        }
        return idx;
    }
    const Point* closest_point(const Point &point) const { return this->points.empty() ? nullptr : &this->points[this->closest_point_index(point)]; }
    BoundingBox bounding_box() const;
    // Return true if there are exact duplicates.
    bool has_duplicate_points() const;
    // Remove exact duplicates, return true if any duplicate has been removed.
    bool remove_duplicate_points();
    void clear() { this->points.clear(); }
    void append(const Point &point) { this->points.push_back(point); }
    void append(const Points &src) { this->append(src.begin(), src.end()); }
    void append(const Points::const_iterator &begin, const Points::const_iterator &end) { this->points.insert(this->points.end(), begin, end); }
    void append(Points &&src)
    {
        if (this->points.empty()) {
            this->points = std::move(src);
        } else {
            this->points.insert(this->points.end(), src.begin(), src.end());
            src.clear();
        }
    }

    static Points douglas_peucker(const Points &src, const double tolerance) { return Slic3r::douglas_peucker(src, tolerance); }
    static Points visivalingam(const Points &src, const double tolerance);

    inline auto begin()        { return points.begin(); }
    inline auto begin()  const { return points.begin(); }
    inline auto end()          { return points.end();   }
    inline auto end()    const { return points.end();   }
    inline auto cbegin() const { return points.begin(); }
    inline auto cend()   const { return points.end();   }
    inline auto rbegin()       { return points.rbegin(); }
    inline auto rbegin() const { return points.rbegin(); }
    inline auto rend()         { return points.rend();   }
    inline auto rend()   const { return points.rend();   }
    inline auto crbegin()const { return points.crbegin(); }
    inline auto crend()  const { return points.crend(); }
};

class MultiPoint3
{
public:
    Points3 points;

    void append(const Vec3crd& point) { this->points.push_back(point); }

    void translate(double x, double y);
    void translate(const Point& vector);
    bool is_valid() const { return this->points.size() >= 2; }

    BoundingBox3 bounding_box() const;

    // Remove exact duplicates, return true if any duplicate has been removed.
    bool remove_duplicate_points();
};

extern BoundingBox get_extents(const MultiPoint &mp);
extern BoundingBox get_extents_rotated(const Points &points, double angle);
extern BoundingBox get_extents_rotated(const MultiPoint &mp, double angle);

inline double length(const Points::const_iterator begin, const Points::const_iterator end) {
    double total = 0;
    if (begin != end) {
        auto it = begin;
        for (auto it_prev = it ++; it != end; ++ it, ++ it_prev)
            total += (*it - *it_prev).cast<double>().norm();
    }
    return total;
}

inline double length(const Points &pts) {
    return length(pts.begin(), pts.end());
}

inline double area(const Points &polygon) {
    double area = 0.;
    for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i ++)
		area += double(polygon[i](0) + polygon[j](0)) * double(polygon[i](1) - polygon[j](1));
    return area;
}

} // namespace Slic3r

#endif
