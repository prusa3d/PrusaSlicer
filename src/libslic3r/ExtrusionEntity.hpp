#ifndef slic3r_ExtrusionEntity_hpp_
#define slic3r_ExtrusionEntity_hpp_

#include "libslic3r.h"
#include "Polygon.hpp"
#include "Polyline.hpp"

namespace Slic3r {

class ExPolygonCollection;
class ExtrusionEntityCollection;
class Extruder;

/* Each ExtrusionRole value identifies a distinct set of { extruder, speed } */
enum ExtrusionRole {
    erNone,
    erPerimeter,
    erExternalPerimeter,
    erOverhangPerimeter,
    erInternalInfill,
    erSolidInfill,
    erTopSolidInfill,
    erBridgeInfill,
    erGapFill,
    erSkirt,
    erSupportMaterial,
    erSupportMaterialInterface,
    erWipeTower,
    erCustom,
    // Extrusion role for a collection with multiple extrusion roles.
    erMixed,
    erCount,
};

inline bool is_perimeter(ExtrusionRole role)
{
    return role == erPerimeter
        || role == erExternalPerimeter
        || role == erOverhangPerimeter;
}

inline bool is_infill(ExtrusionRole role)
{
    return role == erBridgeInfill
        || role == erInternalInfill
        || role == erSolidInfill
        || role == erTopSolidInfill;
}

inline bool is_solid_infill(ExtrusionRole role)
{
    return role == erBridgeInfill
        || role == erSolidInfill
        || role == erTopSolidInfill;
}

inline bool is_bridge(ExtrusionRole role) {
    return role == erBridgeInfill
        || role == erOverhangPerimeter;
}

/* Special flags describing loop */
enum ExtrusionLoopRole {
    elrDefault,
    elrContourInternalPerimeter,
    elrSkirt,
};

class ExtrusionEntity
{
public:
    virtual ExtrusionRole role() const = 0;
    virtual bool is_collection() const { return false; }
    virtual bool is_loop() const { return false; }
    virtual bool can_reverse() const { return true; }
    virtual ExtrusionEntity* clone() const = 0;
    virtual ~ExtrusionEntity() {};
    virtual void reverse() = 0;
    virtual Point first_point() const = 0;
    virtual Point last_point() const = 0;
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
    virtual Polylines as_polylines() const { Polylines dst; this->collect_polylines(dst); return dst; }
    virtual double length() const = 0;
    virtual double total_volume() const = 0;
};

typedef std::vector<ExtrusionEntity*> ExtrusionEntitiesPtr;

class ExtrusionPath : public ExtrusionEntity
{
public:
    Polyline polyline;
    // Volumetric velocity. mm^3 of plastic per mm of linear head motion. Used by the G-code generator.
    double mm3_per_mm;
    // Width of the extrusion, used for visualization purposes.
    float width;
    // Height of the extrusion, used for visualization purposed.
    float height;
    // Feedrate of the extrusion, used for visualization purposed.
    float feedrate;
    // Id of the extruder, used for visualization purposed.
    unsigned int extruder_id;
    // Id of the color, used for visualization purposed in the color printing case.
    unsigned int cp_color_id;
    // Fan speed for the extrusion, used for visualization purposes.
    float fan_speed;

    ExtrusionPath(ExtrusionRole role) : mm3_per_mm(-1), width(-1), height(-1), feedrate(0.0f), extruder_id(0), cp_color_id(0), fan_speed(0.0f), m_role(role) {};
    ExtrusionPath(ExtrusionRole role, double mm3_per_mm, float width, float height) : mm3_per_mm(mm3_per_mm), width(width), height(height), feedrate(0.0f), extruder_id(0), cp_color_id(0), fan_speed(0.0f), m_role(role) {};
    ExtrusionPath(const ExtrusionPath &rhs) : polyline(rhs.polyline), mm3_per_mm(rhs.mm3_per_mm), width(rhs.width), height(rhs.height), feedrate(rhs.feedrate), extruder_id(rhs.extruder_id), cp_color_id(rhs.cp_color_id), fan_speed(rhs.fan_speed), m_role(rhs.m_role) {}
    ExtrusionPath(ExtrusionPath &&rhs) : polyline(std::move(rhs.polyline)), mm3_per_mm(rhs.mm3_per_mm), width(rhs.width), height(rhs.height), feedrate(rhs.feedrate), extruder_id(rhs.extruder_id), cp_color_id(rhs.cp_color_id), fan_speed(rhs.fan_speed), m_role(rhs.m_role) {}
//    ExtrusionPath(ExtrusionRole role, const Flow &flow) : m_role(role), mm3_per_mm(flow.mm3_per_mm()), width(flow.width), height(flow.height), feedrate(0.0f), extruder_id(0) {};

    ExtrusionPath& operator=(const ExtrusionPath &rhs) { m_role = rhs.m_role; this->mm3_per_mm = rhs.mm3_per_mm; this->width = rhs.width; this->height = rhs.height; this->feedrate = rhs.feedrate, this->extruder_id = rhs.extruder_id, this->cp_color_id = rhs.cp_color_id, this->fan_speed = rhs.fan_speed, this->polyline = rhs.polyline; return *this; }
    ExtrusionPath& operator=(ExtrusionPath &&rhs) { m_role = rhs.m_role; this->mm3_per_mm = rhs.mm3_per_mm; this->width = rhs.width; this->height = rhs.height; this->feedrate = rhs.feedrate, this->extruder_id = rhs.extruder_id, this->cp_color_id = rhs.cp_color_id, this->fan_speed = rhs.fan_speed, this->polyline = std::move(rhs.polyline); return *this; }

    ExtrusionPath* clone() const { return new ExtrusionPath (*this); }
    void reverse() { this->polyline.reverse(); }
    Point first_point() const override { return this->polyline.points.front(); }
    Point last_point() const override { return this->polyline.points.back(); }
    size_t size() const { return this->polyline.size(); }
    bool empty() const { return this->polyline.empty(); }
    bool is_closed() const { return ! this->empty() && this->polyline.points.front() == this->polyline.points.back(); }
    // Produce a list of extrusion paths into retval by clipping this path by ExPolygonCollection.
    // Currently not used.
    void intersect_expolygons(const ExPolygonCollection &collection, ExtrusionEntityCollection* retval) const;
    // Produce a list of extrusion paths into retval by removing parts of this path by ExPolygonCollection.
    // Currently not used.
    void subtract_expolygons(const ExPolygonCollection &collection, ExtrusionEntityCollection* retval) const;
    void clip_end(double distance);
    void simplify(double tolerance);
    double length() const override;
    ExtrusionRole role() const override { return m_role; }
    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion width.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    void polygons_covered_by_width(Polygons &out, const float scaled_epsilon) const;
    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion spacing.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    // Useful to calculate area of an infill, which has been really filled in by a 100% rectilinear infill.
    void polygons_covered_by_spacing(Polygons &out, const float scaled_epsilon) const;
    Polygons polygons_covered_by_width(const float scaled_epsilon = 0.f) const
        { Polygons out; this->polygons_covered_by_width(out, scaled_epsilon); return out; }
    Polygons polygons_covered_by_spacing(const float scaled_epsilon = 0.f) const
        { Polygons out; this->polygons_covered_by_spacing(out, scaled_epsilon); return out; }
    // Minimum volumetric velocity of this extrusion entity. Used by the constant nozzle pressure algorithm.
    double min_mm3_per_mm() const { return this->mm3_per_mm; }
    Polyline as_polyline() const { return this->polyline; }
    void   collect_polylines(Polylines &dst) const override { if (! this->polyline.empty()) dst.emplace_back(this->polyline); }
    double total_volume() const override { return mm3_per_mm * unscale<double>(length()); }

private:
    void _inflate_collection(const Polylines &polylines, ExtrusionEntityCollection* collection) const;

    ExtrusionRole m_role;
};

typedef std::vector<ExtrusionPath> ExtrusionPaths;

// Single continuous extrusion path, possibly with varying extrusion thickness, extrusion height or bridging / non bridging.
class ExtrusionMultiPath : public ExtrusionEntity
{
public:
    ExtrusionPaths paths;
    
    ExtrusionMultiPath() {};
    ExtrusionMultiPath(const ExtrusionMultiPath &rhs) : paths(rhs.paths) {}
    ExtrusionMultiPath(ExtrusionMultiPath &&rhs) : paths(std::move(rhs.paths)) {}
    ExtrusionMultiPath(const ExtrusionPaths &paths) : paths(paths) {};
    ExtrusionMultiPath(const ExtrusionPath &path) { this->paths.push_back(path); }

    ExtrusionMultiPath& operator=(const ExtrusionMultiPath &rhs) { this->paths = rhs.paths; return *this; }
    ExtrusionMultiPath& operator=(ExtrusionMultiPath &&rhs) { this->paths = std::move(rhs.paths); return *this; }

    bool is_loop() const { return false; }
    bool can_reverse() const { return true; }
    ExtrusionMultiPath* clone() const { return new ExtrusionMultiPath(*this); }
    void reverse();
    Point first_point() const override { return this->paths.front().polyline.points.front(); }
    Point last_point() const override { return this->paths.back().polyline.points.back(); }
    double length() const override;
    ExtrusionRole role() const override { return this->paths.empty() ? erNone : this->paths.front().role(); }
    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion width.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    void polygons_covered_by_width(Polygons &out, const float scaled_epsilon) const;
    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion spacing.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    // Useful to calculate area of an infill, which has been really filled in by a 100% rectilinear infill.
    void polygons_covered_by_spacing(Polygons &out, const float scaled_epsilon) const;
    Polygons polygons_covered_by_width(const float scaled_epsilon = 0.f) const
        { Polygons out; this->polygons_covered_by_width(out, scaled_epsilon); return out; }
    Polygons polygons_covered_by_spacing(const float scaled_epsilon = 0.f) const
        { Polygons out; this->polygons_covered_by_spacing(out, scaled_epsilon); return out; }
    // Minimum volumetric velocity of this extrusion entity. Used by the constant nozzle pressure algorithm.
    double min_mm3_per_mm() const;
    Polyline as_polyline() const;
    void   collect_polylines(Polylines &dst) const override { Polyline pl = this->as_polyline(); if (! pl.empty()) dst.emplace_back(std::move(pl)); }
    double total_volume() const override { double volume =0.; for (const auto& path : paths) volume += path.total_volume(); return volume; }
};

// Single continuous extrusion loop, possibly with varying extrusion thickness, extrusion height or bridging / non bridging.
class ExtrusionLoop : public ExtrusionEntity
{
public:
    ExtrusionPaths paths;
    
    ExtrusionLoop(ExtrusionLoopRole role = elrDefault) : m_loop_role(role) {};
    ExtrusionLoop(const ExtrusionPaths &paths, ExtrusionLoopRole role = elrDefault) : paths(paths), m_loop_role(role) {};
    ExtrusionLoop(ExtrusionPaths &&paths, ExtrusionLoopRole role = elrDefault) : paths(std::move(paths)), m_loop_role(role) {};
    ExtrusionLoop(const ExtrusionPath &path, ExtrusionLoopRole role = elrDefault) : m_loop_role(role) 
        { this->paths.push_back(path); };
    ExtrusionLoop(const ExtrusionPath &&path, ExtrusionLoopRole role = elrDefault) : m_loop_role(role)
        { this->paths.emplace_back(std::move(path)); };
    bool is_loop() const { return true; }
    bool can_reverse() const { return false; }
    ExtrusionLoop* clone() const { return new ExtrusionLoop (*this); }
    bool make_clockwise();
    bool make_counter_clockwise();
    void reverse();
    Point first_point() const override { return this->paths.front().polyline.points.front(); }
    Point last_point() const override { assert(first_point() == this->paths.back().polyline.points.back()); return first_point(); }
    Polygon polygon() const;
    double length() const override;
    bool split_at_vertex(const Point &point);
    void split_at(const Point &point, bool prefer_non_overhang);
    void clip_end(double distance, ExtrusionPaths* paths) const;
    // Test, whether the point is extruded by a bridging flow.
    // This used to be used to avoid placing seams on overhangs, but now the EdgeGrid is used instead.
    bool has_overhang_point(const Point &point) const;
    ExtrusionRole role() const override { return this->paths.empty() ? erNone : this->paths.front().role(); }
    ExtrusionLoopRole loop_role() const { return m_loop_role; }
    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion width.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    void polygons_covered_by_width(Polygons &out, const float scaled_epsilon) const;
    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion spacing.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    // Useful to calculate area of an infill, which has been really filled in by a 100% rectilinear infill.
    void polygons_covered_by_spacing(Polygons &out, const float scaled_epsilon) const;
    Polygons polygons_covered_by_width(const float scaled_epsilon = 0.f) const
        { Polygons out; this->polygons_covered_by_width(out, scaled_epsilon); return out; }
    Polygons polygons_covered_by_spacing(const float scaled_epsilon = 0.f) const
        { Polygons out; this->polygons_covered_by_spacing(out, scaled_epsilon); return out; }
    // Minimum volumetric velocity of this extrusion entity. Used by the constant nozzle pressure algorithm.
    double min_mm3_per_mm() const;
    Polyline as_polyline() const { return this->polygon().split_at_first_point(); }
    void   collect_polylines(Polylines &dst) const override { Polyline pl = this->as_polyline(); if (! pl.empty()) dst.emplace_back(std::move(pl)); }
    double total_volume() const override { double volume =0.; for (const auto& path : paths) volume += path.total_volume(); return volume; }

private:
    ExtrusionLoopRole m_loop_role;
};

inline void extrusion_paths_append(ExtrusionPaths &dst, Polylines &polylines, ExtrusionRole role, double mm3_per_mm, float width, float height)
{
    dst.reserve(dst.size() + polylines.size());
    for (Polyline &polyline : polylines)
        if (polyline.is_valid()) {
            dst.push_back(ExtrusionPath(role, mm3_per_mm, width, height));
            dst.back().polyline = polyline;
        }
}

inline void extrusion_paths_append(ExtrusionPaths &dst, Polylines &&polylines, ExtrusionRole role, double mm3_per_mm, float width, float height)
{
    dst.reserve(dst.size() + polylines.size());
    for (Polyline &polyline : polylines)
        if (polyline.is_valid()) {
            dst.push_back(ExtrusionPath(role, mm3_per_mm, width, height));
            dst.back().polyline = std::move(polyline);
        }
    polylines.clear();
}

inline void extrusion_entities_append_paths(ExtrusionEntitiesPtr &dst, Polylines &polylines, ExtrusionRole role, double mm3_per_mm, float width, float height)
{
    dst.reserve(dst.size() + polylines.size());
    for (Polyline &polyline : polylines)
        if (polyline.is_valid()) {
            ExtrusionPath *extrusion_path = new ExtrusionPath(role, mm3_per_mm, width, height);
            dst.push_back(extrusion_path);
            extrusion_path->polyline = polyline;
        }
}

inline void extrusion_entities_append_paths(ExtrusionEntitiesPtr &dst, Polylines &&polylines, ExtrusionRole role, double mm3_per_mm, float width, float height)
{
    dst.reserve(dst.size() + polylines.size());
    for (Polyline &polyline : polylines)
        if (polyline.is_valid()) {
            ExtrusionPath *extrusion_path = new ExtrusionPath(role, mm3_per_mm, width, height);
            dst.push_back(extrusion_path);
            extrusion_path->polyline = std::move(polyline);
        }
    polylines.clear();
}

inline void extrusion_entities_append_loops(ExtrusionEntitiesPtr &dst, Polygons &&loops, ExtrusionRole role, double mm3_per_mm, float width, float height)
{
    dst.reserve(dst.size() + loops.size());
    for (Polygon &poly : loops) {
        if (poly.is_valid()) {
            ExtrusionPath path(role, mm3_per_mm, width, height);
            path.polyline.points = std::move(poly.points);
            path.polyline.points.push_back(path.polyline.points.front());
            dst.emplace_back(new ExtrusionLoop(std::move(path)));
        }
    }
    loops.clear();
}

}

#endif
