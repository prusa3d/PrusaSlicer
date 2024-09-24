///|/ Copyright (c) Prusa Research 2016 - 2023 Vojtěch Bubník @bubnikv, Lukáš Hejl @hejllukas, Lukáš Matěna @lukasmatena, Enrico Turri @enricoturri1966, Oleksandra Iushchenko @YuSanka
///|/ Copyright (c) SuperSlicer 2023 Remi Durand @supermerill
///|/ Copyright (c) 2017 Eyal Soha @eyal0
///|/ Copyright (c) Slic3r 2013 - 2016 Alessandro Ranellucci @alranel
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_ExtrusionEntity_hpp_
#define slic3r_ExtrusionEntity_hpp_

#include <assert.h>
#include <stddef.h>
#include <optional>
#include <string_view>
#include <numeric>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>
#include <cassert>
#include <cstddef>

#include "libslic3r.h"
#include "ExtrusionRole.hpp"
#include "Flow.hpp"
#include "Polygon.hpp"
#include "Polyline.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Point.hpp"

namespace Slic3r {

class ExPolygon;

using ExPolygons = std::vector<ExPolygon>;
class ExtrusionEntityCollection;
class Extruder;

class ExtrusionEntity
{
public:
    virtual ExtrusionRole role() const = 0;
    virtual bool is_collection() const { return false; }
    virtual bool is_loop() const { return false; }
    virtual bool can_reverse() const { return true; }
    virtual ExtrusionEntity* clone() const = 0;
    // Create a new object, initialize it with this object using the move semantics.
    virtual ExtrusionEntity* clone_move() = 0;
    virtual ~ExtrusionEntity() = default;
    virtual void reverse() = 0;
    virtual const Point& first_point() const = 0;
    virtual const Point& last_point() const = 0;
    // Returns an approximately middle point of a path, loop or an extrusion collection.
    // Used to get a sample point of an extrusion or extrusion collection, which is possibly deep inside its island.
    virtual const Point& middle_point() const = 0;
    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion width.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    virtual void polygons_covered_by_width(Polygons &out, const float scaled_epsilon) const = 0;
    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion spacing.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    // Useful to calculate area of an infill, which has been really filled in by a 100% rectilinear infill.
    virtual void polygons_covered_by_spacing(Polygons &out, const float scaled_epsilon) const = 0;
    Polygons polygons_covered_by_width(const float scaled_epsilon = 0.f) const
        { Polygons out; this->polygons_covered_by_width(out, scaled_epsilon); return out; }
    Polygons polygons_covered_by_spacing(const float scaled_epsilon = 0.f) const
        { Polygons out; this->polygons_covered_by_spacing(out, scaled_epsilon); return out; }
    // Minimum volumetric velocity of this extrusion entity. Used by the constant nozzle pressure algorithm.
    virtual double min_mm3_per_mm() const = 0;
    virtual Polyline as_polyline() const = 0;
    virtual void   collect_polylines(Polylines &dst) const = 0;
    virtual void   collect_points(Points &dst) const = 0;
    virtual Polylines as_polylines() const { Polylines dst; this->collect_polylines(dst); return dst; }
    virtual double length() const = 0;
    virtual double total_volume() const = 0;
};

using ExtrusionEntitiesPtr = std::vector<ExtrusionEntity*>;

// Const reference for ordering extrusion entities without having to modify them.
class ExtrusionEntityReference final
{
public:
    ExtrusionEntityReference() = delete;
    ExtrusionEntityReference(const ExtrusionEntity &extrusion_entity, bool flipped) : 
        m_extrusion_entity(&extrusion_entity), m_flipped(flipped) {}
    ExtrusionEntityReference operator=(const ExtrusionEntityReference &rhs) 
        { m_extrusion_entity = rhs.m_extrusion_entity; m_flipped = rhs.m_flipped; return *this; }

    const ExtrusionEntity& extrusion_entity() const { return *m_extrusion_entity; }
    template<typename Type>
    const Type*            cast()             const { return dynamic_cast<const Type*>(m_extrusion_entity); }
    bool                   flipped()          const { return m_flipped; }

private:
    const ExtrusionEntity *m_extrusion_entity;
    bool                   m_flipped;
};

using ExtrusionEntityReferences = std::vector<ExtrusionEntityReference>;

struct ExtrusionFlow
{
    ExtrusionFlow() = default;
    ExtrusionFlow(double mm3_per_mm, float width, float height) : 
        mm3_per_mm{ mm3_per_mm }, width{ width }, height{ height } {}
    ExtrusionFlow(const Flow &flow) :
        mm3_per_mm(flow.mm3_per_mm()), width(flow.width()), height(flow.height()) {}

    // Volumetric velocity. mm^3 of plastic per mm of linear head motion. Used by the G-code generator.
    double          mm3_per_mm{ -1. };
    // Width of the extrusion, used for visualization purposes.
    float           width{ -1.f };
    // Height of the extrusion, used for visualization purposes.
    float           height{ -1.f };
};

inline bool operator==(const ExtrusionFlow &lhs, const ExtrusionFlow &rhs)
{
    return lhs.mm3_per_mm == rhs.mm3_per_mm && lhs.width == rhs.width && lhs.height == rhs.height;
}

struct OverhangAttributes {
    float start_distance_from_prev_layer;
    float end_distance_from_prev_layer;
    float proximity_to_curled_lines; //value between 0 and 1
};

inline bool operator==(const OverhangAttributes &lhs, const OverhangAttributes &rhs) {
    if (std::abs(lhs.start_distance_from_prev_layer - rhs.start_distance_from_prev_layer) > std::numeric_limits<float>::epsilon()) {
        return false;
    }
    if (std::abs(lhs.end_distance_from_prev_layer - rhs.end_distance_from_prev_layer) > std::numeric_limits<float>::epsilon()) {
        return false;
    }
    if (std::abs(lhs.proximity_to_curled_lines - rhs.proximity_to_curled_lines) > std::numeric_limits<float>::epsilon()) {
        return false;
    }
    return true;
}

struct ExtrusionAttributes : ExtrusionFlow
{
    ExtrusionAttributes() = default;
    ExtrusionAttributes(ExtrusionRole role) : role{ role } {}
    ExtrusionAttributes(ExtrusionRole role, const Flow &flow) : role{ role }, ExtrusionFlow{ flow } {}
    ExtrusionAttributes(ExtrusionRole role, const ExtrusionFlow &flow) : role{ role }, ExtrusionFlow{ flow } {}
    ExtrusionAttributes(ExtrusionRole role, const ExtrusionFlow &flow, const bool maybe_self_crossing)
        : role{role}, ExtrusionFlow{flow}, maybe_self_crossing(maybe_self_crossing) {}

    // What is the role / purpose of this extrusion?
    ExtrusionRole   role{ ExtrusionRole::None };
    bool maybe_self_crossing{false};
    // OVerhangAttributes are currently computed for perimeters if dynamic overhangs are enabled. 
    // They are used to control fan and print speed in export.
    std::optional<OverhangAttributes> overhang_attributes;
};

inline bool operator==(const ExtrusionAttributes &lhs, const ExtrusionAttributes &rhs)
{
    return static_cast<const ExtrusionFlow&>(lhs) == static_cast<const ExtrusionFlow&>(rhs) &&
           lhs.role == rhs.role && lhs.overhang_attributes == rhs.overhang_attributes;
}

class ExtrusionPath : public ExtrusionEntity
{
public:
    Polyline polyline;

    ExtrusionPath(ExtrusionRole role) : m_attributes{ role } {}
    ExtrusionPath(const ExtrusionAttributes &attributes) : m_attributes(attributes) {}
    ExtrusionPath(const ExtrusionPath &rhs) : polyline(rhs.polyline), m_attributes(rhs.m_attributes) {}
    ExtrusionPath(ExtrusionPath &&rhs) : polyline(std::move(rhs.polyline)), m_attributes(rhs.m_attributes) {}
    ExtrusionPath(const Polyline &polyline, const ExtrusionAttributes &attribs) : polyline(polyline), m_attributes(attribs) {}
    ExtrusionPath(Polyline &&polyline, const ExtrusionAttributes &attribs) : polyline(std::move(polyline)), m_attributes(attribs) {}

    ExtrusionPath& operator=(const ExtrusionPath &rhs) { this->polyline = rhs.polyline; m_attributes = rhs.m_attributes; return *this; }
    ExtrusionPath& operator=(ExtrusionPath &&rhs) { this->polyline = std::move(rhs.polyline); m_attributes = rhs.m_attributes; return *this; }

	ExtrusionEntity* clone() const override { return new ExtrusionPath(*this); }
    // Create a new object, initialize it with this object using the move semantics.
	ExtrusionEntity* clone_move() override { return new ExtrusionPath(std::move(*this)); }
    void reverse() override { this->polyline.reverse(); }
    const Point& first_point() const override { return this->polyline.points.front(); }
    const Point& last_point() const override { return this->polyline.points.back(); }
    const Point& middle_point() const override { return this->polyline.points[this->polyline.size() / 2]; }
    size_t size() const { return this->polyline.size(); }
    bool empty() const { return this->polyline.empty(); }
    bool is_closed() const { return ! this->empty() && this->polyline.points.front() == this->polyline.points.back(); }
    // Produce a list of extrusion paths into retval by clipping this path by ExPolygons.
    // Currently not used.
    void intersect_expolygons(const ExPolygons &collection, ExtrusionEntityCollection* retval) const;
    // Produce a list of extrusion paths into retval by removing parts of this path by ExPolygons.
    // Currently not used.
    void subtract_expolygons(const ExPolygons &collection, ExtrusionEntityCollection* retval) const;
    void clip_end(double distance);
    void simplify(double tolerance);
    double length() const override;
   
    const ExtrusionAttributes&  attributes() const { return m_attributes; }
    ExtrusionRole               role() const override { return m_attributes.role; }
    float                       width() const { return m_attributes.width; }
    float                       height() const { return m_attributes.height; }
    double                      mm3_per_mm() const { return m_attributes.mm3_per_mm; }
    // Minimum volumetric velocity of this extrusion entity. Used by the constant nozzle pressure algorithm.
    double                      min_mm3_per_mm() const override { return m_attributes.mm3_per_mm; }
    std::optional<OverhangAttributes>& overhang_attributes_mutable() { return m_attributes.overhang_attributes; }

    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion width.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    void        polygons_covered_by_width(Polygons &out, const float scaled_epsilon) const override;
    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion spacing.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    // Useful to calculate area of an infill, which has been really filled in by a 100% rectilinear infill.
    void        polygons_covered_by_spacing(Polygons &out, const float scaled_epsilon) const override;
    Polygons    polygons_covered_by_width(const float scaled_epsilon = 0.f) const
        { Polygons out; this->polygons_covered_by_width(out, scaled_epsilon); return out; }
    Polygons    polygons_covered_by_spacing(const float scaled_epsilon = 0.f) const
        { Polygons out; this->polygons_covered_by_spacing(out, scaled_epsilon); return out; }

    Polyline    as_polyline() const override { return this->polyline; }
    void        collect_polylines(Polylines &dst) const override { if (! this->polyline.empty()) dst.emplace_back(this->polyline); }
    void        collect_points(Points &dst) const override { append(dst, this->polyline.points); }
    double      total_volume() const override { return m_attributes.mm3_per_mm * unscale<double>(length()); }

private:
    void        _inflate_collection(const Polylines &polylines, ExtrusionEntityCollection* collection) const;

    ExtrusionAttributes     m_attributes;
};

class ExtrusionPathOriented : public ExtrusionPath
{
public:
    ExtrusionPathOriented(const ExtrusionAttributes &attribs) : ExtrusionPath(attribs) {}
    ExtrusionPathOriented(const Polyline &polyline, const ExtrusionAttributes &attribs) : ExtrusionPath(polyline, attribs) {}
    ExtrusionPathOriented(Polyline &&polyline, const ExtrusionAttributes &attribs) : ExtrusionPath(std::move(polyline), attribs) {}

    ExtrusionEntity* clone() const override { return new ExtrusionPathOriented(*this); }
    // Create a new object, initialize it with this object using the move semantics.
    ExtrusionEntity* clone_move() override { return new ExtrusionPathOriented(std::move(*this)); }
    virtual bool can_reverse() const override { return false; }
};

typedef std::vector<ExtrusionPath> ExtrusionPaths;

// Single continuous extrusion path, possibly with varying extrusion thickness, extrusion height or bridging / non bridging.
class ExtrusionMultiPath : public ExtrusionEntity
{
public:
    ExtrusionPaths paths;
    
    ExtrusionMultiPath() {}
    ExtrusionMultiPath(const ExtrusionMultiPath &rhs) : paths(rhs.paths) {}
    ExtrusionMultiPath(ExtrusionMultiPath &&rhs) : paths(std::move(rhs.paths)) {}
    ExtrusionMultiPath(const ExtrusionPaths &paths) : paths(paths) {}
    ExtrusionMultiPath(const ExtrusionPath &path) { this->paths.push_back(path); }

    ExtrusionMultiPath& operator=(const ExtrusionMultiPath &rhs) { this->paths = rhs.paths; return *this; }
    ExtrusionMultiPath& operator=(ExtrusionMultiPath &&rhs) { this->paths = std::move(rhs.paths); return *this; }

    bool is_loop() const override { return false; }
    bool can_reverse() const override { return true; }
	ExtrusionEntity* clone() const override { return new ExtrusionMultiPath(*this); }
    // Create a new object, initialize it with this object using the move semantics.
	ExtrusionEntity* clone_move() override { return new ExtrusionMultiPath(std::move(*this)); }
    void reverse() override;
    const Point& first_point() const override { return this->paths.front().polyline.points.front(); }
    const Point& last_point() const override { return this->paths.back().polyline.points.back(); }
    const Point& middle_point() const override { auto &path = this->paths[this->paths.size() / 2]; return path.polyline.points[path.polyline.size() / 2]; }
    size_t size() const { return this->paths.size(); }
    bool empty() const { return this->paths.empty(); }
    double length() const override;
    ExtrusionRole role() const override { return this->paths.empty() ? ExtrusionRole::None : this->paths.front().role(); }
    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion width.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    void polygons_covered_by_width(Polygons &out, const float scaled_epsilon) const override;
    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion spacing.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    // Useful to calculate area of an infill, which has been really filled in by a 100% rectilinear infill.
    void polygons_covered_by_spacing(Polygons &out, const float scaled_epsilon) const override;
    Polygons polygons_covered_by_width(const float scaled_epsilon = 0.f) const
        { Polygons out; this->polygons_covered_by_width(out, scaled_epsilon); return out; }
    Polygons polygons_covered_by_spacing(const float scaled_epsilon = 0.f) const
        { Polygons out; this->polygons_covered_by_spacing(out, scaled_epsilon); return out; }
    // Minimum volumetric velocity of this extrusion entity. Used by the constant nozzle pressure algorithm.
    double min_mm3_per_mm() const override;
    Polyline as_polyline() const override;
    void   collect_polylines(Polylines &dst) const override { Polyline pl = this->as_polyline(); if (! pl.empty()) dst.emplace_back(std::move(pl)); }
    void   collect_points(Points &dst) const override { 
        size_t n = std::accumulate(paths.begin(), paths.end(), 0, [](const size_t n, const ExtrusionPath &p){ return n + p.polyline.size(); });
        dst.reserve(dst.size() + n);
        for (const ExtrusionPath &p : this->paths)
            append(dst, p.polyline.points);
    }
    double total_volume() const override { double volume =0.; for (const auto& path : paths) volume += path.total_volume(); return volume; }
};

// Single continuous extrusion loop, possibly with varying extrusion thickness, extrusion height or bridging / non bridging.
class ExtrusionLoop : public ExtrusionEntity
{
public:
    ExtrusionPaths paths;

    ExtrusionLoop() = default;
    ExtrusionLoop(ExtrusionLoopRole role) : m_loop_role(role) {}
    ExtrusionLoop(const ExtrusionPaths &paths, ExtrusionLoopRole role = elrDefault) : paths(paths), m_loop_role(role) {}
    ExtrusionLoop(ExtrusionPaths &&paths, ExtrusionLoopRole role = elrDefault) : paths(std::move(paths)), m_loop_role(role) {}
    ExtrusionLoop(const ExtrusionPath &path, ExtrusionLoopRole role = elrDefault) : m_loop_role(role) 
        { this->paths.push_back(path); }
    ExtrusionLoop(ExtrusionPath &&path, ExtrusionLoopRole role = elrDefault) : m_loop_role(role)
        { this->paths.emplace_back(std::move(path)); }
    bool is_loop() const override{ return true; }
    bool can_reverse() const override { return false; }
	ExtrusionEntity* clone() const override{ return new ExtrusionLoop (*this); }
    // Create a new object, initialize it with this object using the move semantics.
	ExtrusionEntity* clone_move() override { return new ExtrusionLoop(std::move(*this)); }
    double          area() const;
    bool            is_counter_clockwise() const { return this->area() > 0; }
    bool            is_clockwise() const { return this->area() < 0; }
    // Reverse shall never be called on ExtrusionLoop using a virtual function call, it is most likely never what one wants,
    // as this->can_reverse() returns false for an ExtrusionLoop.
    void            reverse() override;
    // Used by PerimeterGenerator to reorient extrusion loops.
    void            reverse_loop();
    const Point&    first_point() const override { return this->paths.front().polyline.points.front(); }
    const Point&    last_point() const override { assert(this->first_point() == this->paths.back().polyline.points.back()); return this->first_point(); }
    const Point&    middle_point() const override { auto& path = this->paths[this->paths.size() / 2]; return path.polyline.points[path.polyline.size() / 2]; }
    Polygon         polygon() const;
    double          length() const override;
    bool            split_at_vertex(const Point &point, const double scaled_epsilon = scaled<double>(0.001));
    void            split_at(const Point &point, bool prefer_non_overhang, const double scaled_epsilon = scaled<double>(0.001));
    struct ClosestPathPoint {
        size_t path_idx;
        size_t segment_idx;
        Point  foot_pt;
    };
    ClosestPathPoint get_closest_path_and_point(const Point& point, bool prefer_non_overhang) const;
    void clip_end(double distance, ExtrusionPaths* paths) const;
    // Test, whether the point is extruded by a bridging flow.
    // This used to be used to avoid placing seams on overhangs, but now the EdgeGrid is used instead.
    bool has_overhang_point(const Point &point) const;
    ExtrusionRole role() const override { return this->paths.empty() ? ExtrusionRole::None : this->paths.front().role(); }
    ExtrusionLoopRole loop_role() const { return m_loop_role; }
    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion width.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    void polygons_covered_by_width(Polygons &out, const float scaled_epsilon) const override;
    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion spacing.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    // Useful to calculate area of an infill, which has been really filled in by a 100% rectilinear infill.
    void polygons_covered_by_spacing(Polygons &out, const float scaled_epsilon) const  override;
    Polygons polygons_covered_by_width(const float scaled_epsilon = 0.f) const
        { Polygons out; this->polygons_covered_by_width(out, scaled_epsilon); return out; }
    Polygons polygons_covered_by_spacing(const float scaled_epsilon = 0.f) const
        { Polygons out; this->polygons_covered_by_spacing(out, scaled_epsilon); return out; }
    // Minimum volumetric velocity of this extrusion entity. Used by the constant nozzle pressure algorithm.
    double min_mm3_per_mm() const override;
    Polyline as_polyline() const override { return this->polygon().split_at_first_point(); }
    void   collect_polylines(Polylines &dst) const override { Polyline pl = this->as_polyline(); if (! pl.empty()) dst.emplace_back(std::move(pl)); }
    void   collect_points(Points &dst) const override { 
        size_t n = std::accumulate(paths.begin(), paths.end(), 0, [](const size_t n, const ExtrusionPath &p){ return n + p.polyline.size(); });
        dst.reserve(dst.size() + n);
        for (const ExtrusionPath &p : this->paths)
            append(dst, p.polyline.points);
    }
    double total_volume() const override { double volume =0.; for (const auto& path : paths) volume += path.total_volume(); return volume; }

#ifndef NDEBUG
	bool validate() const {
		assert(this->first_point() == this->paths.back().polyline.points.back());
		for (size_t i = 1; i < paths.size(); ++ i)
			assert(this->paths[i - 1].polyline.points.back() == this->paths[i].polyline.points.front());
		return true;
	}
#endif /* NDEBUG */

private:
    ExtrusionLoopRole m_loop_role{ elrDefault };
};

inline void extrusion_paths_append(ExtrusionPaths &dst, Polylines &polylines, const ExtrusionAttributes &attributes)
{
    dst.reserve(dst.size() + polylines.size());
    for (Polyline &polyline : polylines)
        if (polyline.is_valid())
            dst.emplace_back(polyline, attributes);
}

inline void extrusion_paths_append(ExtrusionPaths &dst, Polylines &&polylines, const ExtrusionAttributes &attributes)
{
    dst.reserve(dst.size() + polylines.size());
    for (Polyline &polyline : polylines)
        if (polyline.is_valid())
            dst.emplace_back(std::move(polyline), attributes);
    polylines.clear();
}

inline void extrusion_entities_append_paths(ExtrusionEntitiesPtr &dst, const Polylines &polylines, const ExtrusionAttributes &attributes, bool can_reverse = true)
{
    dst.reserve(dst.size() + polylines.size());
    for (const Polyline &polyline : polylines)
        if (polyline.is_valid())
            dst.emplace_back(can_reverse ? new ExtrusionPath(polyline, attributes) : new ExtrusionPathOriented(polyline, attributes));
}

inline void extrusion_entities_append_paths(ExtrusionEntitiesPtr &dst, Polylines &&polylines, const ExtrusionAttributes &attributes, bool can_reverse = true)
{
    dst.reserve(dst.size() + polylines.size());
    for (Polyline &polyline : polylines)
        if (polyline.is_valid())
            dst.emplace_back(can_reverse ?
                new ExtrusionPath(std::move(polyline), attributes) :
                new ExtrusionPathOriented(std::move(polyline), attributes));
    polylines.clear();
}

inline void extrusion_entities_append_loops(ExtrusionEntitiesPtr &dst, Polygons &&loops, const ExtrusionAttributes &attributes)
{
    dst.reserve(dst.size() + loops.size());
    for (Polygon &poly : loops) {
        if (poly.is_valid()) {
            ExtrusionPath path(attributes);
            path.polyline.points = std::move(poly.points);
            path.polyline.points.push_back(path.polyline.points.front());
            dst.emplace_back(new ExtrusionLoop(std::move(path)));
        }
    }
    loops.clear();
}

inline void extrusion_entities_append_loops_and_paths(ExtrusionEntitiesPtr &dst, Polylines &&polylines, const ExtrusionAttributes &attributes)
{
    dst.reserve(dst.size() + polylines.size());
    for (Polyline &polyline : polylines)
        if (polyline.is_valid())
            dst.emplace_back(polyline.is_closed() ?
                static_cast<ExtrusionEntity*>(new ExtrusionLoop(ExtrusionPath{ std::move(polyline), attributes })) :
                static_cast<ExtrusionEntity*>(new ExtrusionPath(std::move(polyline), attributes)));
    polylines.clear();
}

}

#endif
