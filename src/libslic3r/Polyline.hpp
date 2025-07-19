///|/ Copyright (c) Prusa Research 2016 - 2023 Tomáš Mészáros @tamasmeszaros, Pavel Mikuš @Godrak, Vojtěch Bubník @bubnikv, Lukáš Hejl @hejllukas, Lukáš Matěna @lukasmatena, Oleksandra Iushchenko @YuSanka, Enrico Turri @enricoturri1966
///|/ Copyright (c) Slic3r 2013 - 2016 Alessandro Ranellucci @alranel
///|/
///|/ ported from lib/Slic3r/Polyline.pm:
///|/ Copyright (c) Prusa Research 2018 Vojtěch Bubník @bubnikv
///|/ Copyright (c) Slic3r 2011 - 2014 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2012 Mark Hindess
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
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

#include "libslic3r.h"
#include "Line.hpp"
#include "MultiPoint.hpp"
#include "libslic3r/Point.hpp"

namespace Slic3r {

class Polyline;
struct ThickPolyline;
class BoundingBox;

typedef std::vector<Polyline> Polylines;
typedef std::vector<ThickPolyline> ThickPolylines;

class Polyline : public MultiPoint {
public:
    Polyline() = default;
    Polyline(const Polyline &other) : MultiPoint(other.points) {}
    Polyline(Polyline &&other) : MultiPoint(std::move(other.points)) {}
    Polyline(std::initializer_list<Point> list) : MultiPoint(list) {}
    explicit Polyline(const Point &p1, const Point &p2) { points.reserve(2); points.emplace_back(p1); points.emplace_back(p2); }
    explicit Polyline(const Points &points) : MultiPoint(points) {}
    explicit Polyline(Points &&points) : MultiPoint(std::move(points)) {}
    Polyline& operator=(const Polyline &other) { points = other.points; return *this; }
    Polyline& operator=(Polyline &&other) { points = std::move(other.points); return *this; }
	static Polyline new_scale(const std::vector<Vec2d> &points) {
		Polyline pl;
		pl.points.reserve(points.size());
		for (const Vec2d &pt : points)
			pl.points.emplace_back(Point::new_scale(pt(0), pt(1)));
		return pl;
    }
    
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
    void append(const Polyline &src) 
    { 
        points.insert(points.end(), src.points.begin(), src.points.end());
    }

    void append(Polyline &&src) 
    {
        if (this->points.empty()) {
            this->points = std::move(src.points);
        } else {
            this->points.insert(this->points.end(), src.points.begin(), src.points.end());
            src.points.clear();
        }
    }
  
    Point& operator[](Points::size_type idx) { return this->points[idx]; }
    const Point& operator[](Points::size_type idx) const { return this->points[idx]; }

    double length() const;
    const Point& last_point() const { return this->points.back(); }
    const Point& leftmost_point() const;
    Lines lines() const;

    void clip_end(double distance);
    void clip_start(double distance);
    void extend_end(double distance);
    void extend_start(double distance);
    Points equally_spaced_points(double distance) const;
    void simplify(double tolerance);
//    template <class T> void simplify_by_visibility(const T &area);
    void split_at(const Point &point, Polyline* p1, Polyline* p2) const;
    bool is_straight() const;
    bool is_closed() const { return this->points.front() == this->points.back(); }

    using iterator = Points::iterator;
    using const_iterator = Points::const_iterator;
};

inline bool operator==(const Polyline &lhs, const Polyline &rhs) { return lhs.points == rhs.points; }
inline bool operator!=(const Polyline &lhs, const Polyline &rhs) { return lhs.points != rhs.points; }

extern BoundingBox get_extents(const Polyline &polyline);
extern BoundingBox get_extents(const Polylines &polylines);

// Return True when erase some otherwise False.
bool remove_same_neighbor(Polyline &polyline);
bool remove_same_neighbor(Polylines &polylines);

inline double total_length(const Polylines &polylines) {
    double total = 0;
    for (const Polyline &pl : polylines)
        total += pl.length();
    return total;
}

inline size_t total_lines_count(const Polylines &polylines) {
    size_t lines_cnt = 0;
    for (const Polyline &polyline : polylines) {
        if (polyline.points.size() > 1) {
            lines_cnt += polyline.points.size() - 1;
        }
    }

    return lines_cnt;
}

inline Lines to_lines(const Polyline &poly) {
    Lines lines;
    if (poly.points.size() >= 2) {
        lines.reserve(poly.points.size() - 1);
        for (Points::const_iterator it = poly.points.begin(); it != poly.points.end() - 1; ++it) {
            lines.emplace_back(*it, *(it + 1));
        }
    }

    return lines;
}

inline Lines to_lines(const Polylines &polylines) {
    const size_t lines_cnt = total_lines_count(polylines);

    Lines lines;
    lines.reserve(lines_cnt);
    for (const Polyline &polyline : polylines) {
        for (Points::const_iterator it = polyline.points.begin(); it != polyline.points.end() - 1; ++it) {
            lines.emplace_back(*it, *(it + 1));
        }
    }

    return lines;
}

inline Polylines to_polylines(const std::vector<Points> &paths)
{
    Polylines out;
    out.reserve(paths.size());
    for (const Points &path : paths)
        out.emplace_back(path);
    return out;
}

inline Polylines to_polylines(std::vector<Points> &&paths)
{
    Polylines out;
    out.reserve(paths.size());
    for (Points &path : paths)
        out.emplace_back(std::move(path));
    return out;
}

inline void polylines_append(Polylines &dst, const Polylines &src) 
{ 
    dst.insert(dst.end(), src.begin(), src.end());
}

inline void polylines_append(Polylines &dst, Polylines &&src) 
{
    if (dst.empty()) {
        dst = std::move(src);
    } else {
        std::move(std::begin(src), std::end(src), std::back_inserter(dst));
        src.clear();
    }
}

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

const Point& leftmost_point(const Polylines &polylines);

bool remove_degenerate(Polylines &polylines);

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
    double       length()       const { return Slic3r::length(this->points); }

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
    std::vector<coordf_t>   width;
    std::pair<bool,bool>    endpoints { false, false };
};

inline ThickPolylines to_thick_polylines(Polylines &&polylines, const coordf_t width)
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

class Polyline3 : public MultiPoint3
{
public:
    double length() const;
    Lines3 lines() const;
};

typedef std::vector<Polyline3> Polylines3;

}

#endif
