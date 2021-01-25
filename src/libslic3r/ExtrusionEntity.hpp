#ifndef slic3r_ExtrusionEntity_hpp_
#define slic3r_ExtrusionEntity_hpp_

#include "libslic3r.h"
#include "Polygon.hpp"
#include "Polyline.hpp"

#include <assert.h>
#include <string_view>

namespace Slic3r {

class ExPolygonCollection;
class ExtrusionEntityCollection;
class Extruder;

// Each ExtrusionRole value identifies a distinct set of { extruder, speed }
/*
enum ExtrusionRoleModifier : uint16_t {
    ermPerimeter = (1 << 0),
    ermInfill = (2 << 1),
    ermThin = (2 << 2),
    ermSkirt = (2 << 3),
    ermOther = (2 << 4),
    ermInternal = (1 << 10),
    ermExternal = (1 << 11),
    ermSolid = (1 << 12),
    ermBridge = (1 << 13),
    ermSupport = (1 << 13)
};
enum ExtrusionRole : uint16_t {
    erNone = 0,
    erPerimeter = ermPerimeter | ermInternal,
    erExternalPerimeter = ermPerimeter | ermExternal,
    erOverhangPerimeter = ermPerimeter | ermBridge,
    erInternalInfill = ermInfill | ermInternal,
    erSolidInfill = ermInfill | ermSolid | ermInternal,
    erTopSolidInfill = ermInfill | ermSolid | ermExternal,
    erBridgeInfill = ermInfill | ermSolid | ermBridge | ermExternal,
    erInternalBridgeInfill = ermInfill | ermSolid | ermBridge,
    erThinWall = ermThin | ermInternal,
    erGapFill = ermThin | ermExternal,
    erSkirt = ermSkirt,
    erSupportMaterial = ermInfill | ermSupport | ermInternal,
    erSupportMaterialInterface = ermInfill | ermSupport | ermExternal,
    erWipeTower = ermSkirt | ermSupport,
    erMilling = ermOther | ermPerimeter,
    erCustom = ermOther | ermSkirt,
    // Extrusion role for a collection with multiple extrusion roles.
    erMixed = ermOther
};
*/
enum ExtrusionRole : uint8_t {
    erNone,
    erPerimeter,
    erExternalPerimeter,
    erOverhangPerimeter,
    erInternalInfill,
    erSolidInfill,
    erTopSolidInfill,
    erIroning,
    erBridgeInfill,
    erInternalBridgeInfill,
    erThinWall,
    erGapFill,
    erSkirt,
    erSupportMaterial,
    erSupportMaterialInterface,
    erWipeTower,
    erMilling,
    erCustom,
    // Extrusion role for a collection with multiple extrusion roles.
    erMixed,
    erCount
};

// perimeter / infill / support / skirt / gapfill / wipetower / custom / mixed
// side / internal / top / bottom
// bridge

// Special flags describing loop
enum ExtrusionLoopRole : uint16_t {
    elrDefault = 1 << 0, //1
    // doesn't contains more contour: it's the most internal one
    elrInternal = 1 << 1, //2
    elrSkirt =    1 << 2,  //4
    //it's a modifier that indicate that the loop is around a hole, not around the infill
    elrHole = 1 << 3, // 8
    //it's a modifier that indicate that the loop should be printed as vase
    elrVase = 1 << 4, //16
    //it's a modifier that indicate that the loop does not contains an inner loop, used for random seam
    elrFirstLoop = 1 << 5, //32
};


inline bool is_perimeter(ExtrusionRole role)
{
    return role == erPerimeter
        || role == erExternalPerimeter
        || role == erThinWall
        || role == erOverhangPerimeter;
}

inline bool is_infill(ExtrusionRole role)
{
    return role == erBridgeInfill
        || role == erInternalInfill
        || role == erSolidInfill
        || role == erTopSolidInfill
        || role == erIroning;
}

inline bool is_solid_infill(ExtrusionRole role)
{
    return role == erBridgeInfill
        || role == erSolidInfill
        || role == erTopSolidInfill
        || role == erIroning;
}

inline bool is_bridge(ExtrusionRole role) {
    return role == erBridgeInfill;
}


class ExtrusionEntity;
class ExtrusionPath;
class ExtrusionPath3D;
class ExtrusionMultiPath;
class ExtrusionMultiPath3D;
class ExtrusionLoop;
//
//class ExtrusionVisitor {
//public:
//    virtual void default_use(ExtrusionEntity &entity) { assert(false); };
//    virtual void use(ExtrusionPath &path) { ExtrusionEntity &entity = path; default_use(entity); };
//    virtual void use(ExtrusionPath3D &path3D) { ExtrusionPath &path = path3D;  use(path); };
//    virtual void use(ExtrusionMultiPath &multipath) { ExtrusionEntity &entity = multipath; default_use(entity); };
//    virtual void use(ExtrusionMultiPath3D &multipath3D) { ExtrusionEntity &entity = multipath3D; default_use(entity); };
//    virtual void use(ExtrusionLoop &loop) { ExtrusionEntity &entity = loop; default_use(entity); };
//    virtual void use(ExtrusionEntityCollection &collection) { ExtrusionEntity &entity = collection;  default_use(entity); };
//};
//class ExtrusionVisitorConst {
//public:
//    virtual void default_use(const ExtrusionEntity &entity) { assert(false); };
//    virtual void use(const ExtrusionPath &path) { const ExtrusionEntity &entity = path; default_use(entity); };
//    virtual void use(const ExtrusionPath3D &path3D) { const ExtrusionPath &path = path3D;  use(path); };
//    virtual void use(const ExtrusionMultiPath &multipath) { const ExtrusionEntity &entity = multipath; default_use(entity); };
//    virtual void use(const ExtrusionMultiPath3D &multipath3D) { const ExtrusionEntity &entity = multipath3D; default_use(entity); };
//    virtual void use(const ExtrusionLoop &loop) { const ExtrusionEntity &entity = loop; default_use(entity); };
//    virtual void use(const ExtrusionEntityCollection &collection) { const ExtrusionEntity &entity = collection;  default_use(entity); };
//};

class ExtrusionVisitor {
public:
    virtual void default_use(ExtrusionEntity &entity) { assert(false); };
    virtual void use(ExtrusionPath &path);
    virtual void use(ExtrusionPath3D &path3D);
    virtual void use(ExtrusionMultiPath &multipath);
    virtual void use(ExtrusionMultiPath3D &multipath3D);
    virtual void use(ExtrusionLoop &loop);
    virtual void use(ExtrusionEntityCollection &collection);
};
class ExtrusionVisitorConst {
public:
    virtual void default_use(const ExtrusionEntity &entity) { assert(false); };
    virtual void use(const ExtrusionPath &path);
    virtual void use(const ExtrusionPath3D &path3D);
    virtual void use(const ExtrusionMultiPath &multipath);
    virtual void use(const ExtrusionMultiPath3D &multipath3D);
    virtual void use(const ExtrusionLoop &loop);
    virtual void use(const ExtrusionEntityCollection &collection);
};

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
    virtual ~ExtrusionEntity() {}
    virtual void reverse() = 0;
    virtual const Point& first_point() const = 0;
    virtual const Point& last_point() const = 0;
    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion width.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    virtual void polygons_covered_by_width(Polygons &out, const float scaled_epsilon) const = 0;
    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion spacing.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    // Useful to calculate area of an infill, which has been really filled in by a 100% rectilinear infill.
    virtual void polygons_covered_by_spacing(Polygons &out, const float scaled_epsilon) const = 0;
    virtual Polygons polygons_covered_by_width(const float scaled_epsilon = 0.f) const
        { Polygons out; this->polygons_covered_by_width(out, scaled_epsilon); return out; }
    virtual Polygons polygons_covered_by_spacing(const float scaled_epsilon = 0.f) const
        { Polygons out; this->polygons_covered_by_spacing(out, scaled_epsilon); return out; }
    // Minimum volumetric velocity of this extrusion entity. Used by the constant nozzle pressure algorithm.
    virtual double min_mm3_per_mm() const = 0;
    virtual Polyline as_polyline() const = 0;
    virtual void   collect_polylines(Polylines &dst) const = 0;
    virtual Polylines as_polylines() const { Polylines dst; this->collect_polylines(dst); return dst; }
    virtual double length() const = 0;
    virtual double total_volume() const = 0;
    virtual void visit(ExtrusionVisitor &visitor) = 0;
    virtual void visit(ExtrusionVisitorConst &visitor) const = 0;

    static std::string role_to_string(ExtrusionRole role);
    static ExtrusionRole string_to_role(const std::string_view role);
};

typedef std::vector<ExtrusionEntity*> ExtrusionEntitiesPtr;

class ExtrusionPath : public ExtrusionEntity
{
public:
    Polyline polyline;
    // Volumetric velocity. mm^3 of plastic per mm of linear head motion. Used by the G-code generator.
    double mm3_per_mm;
    // Width of the extrusion, used for visualization purposes. Unscaled
    float width;
    // Height of the extrusion, used for visualization purposes. Unscaled
    float height;

    ExtrusionPath(ExtrusionRole role) : mm3_per_mm(-1), width(-1), height(-1), m_role(role) {}
    ExtrusionPath(ExtrusionRole role, double mm3_per_mm, float width, float height) : mm3_per_mm(mm3_per_mm), width(width), height(height), m_role(role) {}
    ExtrusionPath(const ExtrusionPath& rhs) : polyline(rhs.polyline), mm3_per_mm(rhs.mm3_per_mm), width(rhs.width), height(rhs.height), m_role(rhs.m_role) {}
    ExtrusionPath(ExtrusionPath&& rhs) : polyline(std::move(rhs.polyline)), mm3_per_mm(rhs.mm3_per_mm), width(rhs.width), height(rhs.height), m_role(rhs.m_role) {}
    ExtrusionPath(const Polyline &polyline, const ExtrusionPath &rhs) : polyline(polyline), mm3_per_mm(rhs.mm3_per_mm), width(rhs.width), height(rhs.height), m_role(rhs.m_role) {}
    ExtrusionPath(Polyline &&polyline, const ExtrusionPath &rhs) : polyline(std::move(polyline)), mm3_per_mm(rhs.mm3_per_mm), width(rhs.width), height(rhs.height), m_role(rhs.m_role) {}

    ExtrusionPath& operator=(const ExtrusionPath& rhs) { m_role = rhs.m_role; this->mm3_per_mm = rhs.mm3_per_mm; this->width = rhs.width; this->height = rhs.height; this->polyline = rhs.polyline; return *this; }
    ExtrusionPath& operator=(ExtrusionPath&& rhs) { m_role = rhs.m_role; this->mm3_per_mm = rhs.mm3_per_mm; this->width = rhs.width; this->height = rhs.height; this->polyline = std::move(rhs.polyline); return *this; }

    virtual ExtrusionPath* clone() const override { return new ExtrusionPath(*this); }
    // Create a new object, initialize it with this object using the move semantics.
    virtual ExtrusionPath* clone_move() override { return new ExtrusionPath(std::move(*this)); }
    void reverse() override { this->polyline.reverse(); }
    const Point& first_point() const override { return this->polyline.points.front(); }
    const Point& last_point() const override { return this->polyline.points.back(); }
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
    virtual void simplify(double tolerance);
    double length() const override;
    ExtrusionRole role() const override { return m_role; }
    void set_role(ExtrusionRole new_role) { m_role = new_role; }
    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion width.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    void polygons_covered_by_width(Polygons &out, const float scaled_epsilon) const override;
    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion spacing.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    // Useful to calculate area of an infill, which has been really filled in by a 100% rectilinear infill.
    void polygons_covered_by_spacing(Polygons &out, const float scaled_epsilon) const override;
    virtual Polygons polygons_covered_by_width(const float scaled_epsilon = 0.f) const
        { Polygons out; this->polygons_covered_by_width(out, scaled_epsilon); return out; }
    virtual Polygons polygons_covered_by_spacing(const float scaled_epsilon = 0.f) const
        { Polygons out; this->polygons_covered_by_spacing(out, scaled_epsilon); return out; }
    // Minimum volumetric velocity of this extrusion entity. Used by the constant nozzle pressure algorithm.
    double min_mm3_per_mm() const override { return this->mm3_per_mm; }
    Polyline as_polyline() const override { return this->polyline; }
    void   collect_polylines(Polylines &dst) const override { if (! this->polyline.empty()) dst.emplace_back(this->polyline); }
    double total_volume() const override { return mm3_per_mm * unscale<double>(length()); }
    virtual void visit(ExtrusionVisitor &visitor) override { visitor.use(*this); };
    virtual void visit(ExtrusionVisitorConst &visitor) const override { visitor.use(*this); };

protected:
    void _inflate_collection(const Polylines &polylines, ExtrusionEntityCollection* collection) const;

    ExtrusionRole m_role;
};
typedef std::vector<ExtrusionPath> ExtrusionPaths;

class ExtrusionPath3D : public ExtrusionPath {
public:
    std::vector<coord_t> z_offsets;

    ExtrusionPath3D(ExtrusionRole role) : ExtrusionPath(role) { /*std::cout << "new path3D\n"; */};
    ExtrusionPath3D(ExtrusionRole role, double mm3_per_mm, float width, float height) : ExtrusionPath(role, mm3_per_mm, width, height) { /*std::cout << "new path3D++\n";*/ };
    ExtrusionPath3D(const ExtrusionPath &rhs) : ExtrusionPath(rhs) { /*std::cout << "new path3D from path "<<size()<<"?"<<z_offsets.size()<<"\n";*/ }
    ExtrusionPath3D(ExtrusionPath &&rhs) : ExtrusionPath(rhs) { /*std::cout << "new path3D from path " << size() << "?" << z_offsets.size()<<"\n";*/ }
    ExtrusionPath3D(const ExtrusionPath3D &rhs) : ExtrusionPath(rhs), z_offsets(rhs.z_offsets) { /*std::cout << "new path3D from path3D " << size() << "?" << z_offsets.size()<<"\n";*/ }
    ExtrusionPath3D(ExtrusionPath3D &&rhs) : ExtrusionPath(rhs), z_offsets(std::move(rhs.z_offsets)) { /*std::cout << "new2 path3D from path3D " << size() << "?" << z_offsets.size()<<"\n";*/ }
    //    ExtrusionPath(ExtrusionRole role, const Flow &flow) : m_role(role), mm3_per_mm(flow.mm3_per_mm()), width(flow.width), height(flow.height), feedrate(0.0f), extruder_id(0) {};

    ExtrusionPath3D& operator=(const ExtrusionPath3D &rhs) { m_role = rhs.m_role; this->mm3_per_mm = rhs.mm3_per_mm; this->width = rhs.width; this->height = rhs.height; 
        this->polyline = rhs.polyline; z_offsets = rhs.z_offsets; return *this;
    }
    ExtrusionPath3D& operator=(ExtrusionPath3D &&rhs) { m_role = rhs.m_role; this->mm3_per_mm = rhs.mm3_per_mm; this->width = rhs.width; this->height = rhs.height; 
        this->polyline = std::move(rhs.polyline); z_offsets = std::move(rhs.z_offsets); return *this;
    }
    virtual ExtrusionPath3D* clone() const override { return new ExtrusionPath3D(*this); }
    virtual ExtrusionPath3D* clone_move() override { return new ExtrusionPath3D(std::move(*this)); }
    virtual void visit(ExtrusionVisitor &visitor) override { visitor.use(*this); };
    virtual void visit(ExtrusionVisitorConst &visitor) const override { visitor.use(*this); };

    void push_back(Point p, coord_t z_offset) { polyline.points.push_back(p); z_offsets.push_back(z_offset); }

    //TODO: simplify only for points that have the same z-offset
    void simplify(double tolerance) override {}
};
typedef std::vector<ExtrusionPath3D> ExtrusionPaths3D;

// Single continuous extrusion path, possibly with varying extrusion thickness, extrusion height or bridging / non bridging.
template <typename THING = ExtrusionEntity>
class ExtrusionMultiEntity : public ExtrusionEntity {
public:
    std::vector<THING> paths;

    ExtrusionMultiEntity() {};
    ExtrusionMultiEntity(const ExtrusionMultiEntity &rhs) : paths(rhs.paths) {}
    ExtrusionMultiEntity(ExtrusionMultiEntity &&rhs) : paths(std::move(rhs.paths)) {}
    ExtrusionMultiEntity(const std::vector<THING> &paths) : paths(paths) {};
    ExtrusionMultiEntity(const THING &path) { this->paths.push_back(path); }

    ExtrusionMultiEntity& operator=(const ExtrusionMultiEntity &rhs) { this->paths = rhs.paths; return *this; }
    ExtrusionMultiEntity& operator=(ExtrusionMultiEntity &&rhs) { this->paths = std::move(rhs.paths); return *this; }

    bool is_loop() const override { return false; }
    ExtrusionRole role() const override { return this->paths.empty() ? erNone : this->paths.front().role(); }
    virtual const Point& first_point() const override { return this->paths.back().as_polyline().points.back(); }
    virtual const Point& last_point() const override { return this->paths.back().as_polyline().points.back(); }

    virtual void reverse() override {
        for (THING &entity : this->paths)
            entity.reverse();
        std::reverse(this->paths.begin(), this->paths.end());
    }

    double length() const override {
        double len = 0;
        for (const THING &entity : this->paths)
            len += entity.length();
        return len;
    }

    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion width.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    void polygons_covered_by_width(Polygons &out, const float scaled_epsilon) const override {
        for (const THING &entity : this->paths)
            entity.polygons_covered_by_width(out, scaled_epsilon);
    }

    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion spacing.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    // Useful to calculate area of an infill, which has been really filled in by a 100% rectilinear infill.
    void polygons_covered_by_spacing(Polygons &out, const float scaled_epsilon) const override {
        for (const THING &entity : this->paths)
            entity.polygons_covered_by_spacing(out, scaled_epsilon);
    }

    // Minimum volumetric velocity of this extrusion entity. Used by the constant nozzle pressure algorithm.
    double min_mm3_per_mm() const override {
        double min_mm3_per_mm = std::numeric_limits<double>::max();
        for (const THING &entity : this->paths)
            if (entity.role() != erGapFill && entity.role() != erThinWall && entity.role() != erMilling)
                min_mm3_per_mm = std::min(min_mm3_per_mm, entity.min_mm3_per_mm());
        return min_mm3_per_mm;
    }

    Polyline as_polyline() const override {
        Polyline out;
        if (!paths.empty()) {
            size_t len = 0;
            for (size_t i_path = 0; i_path < paths.size(); ++i_path) {
                assert(!paths[i_path].as_polyline().points.empty());
                assert(i_path == 0 || paths[i_path - 1].polyline.points.back() == paths[i_path].as_polyline().points.front());
                len += paths[i_path].as_polyline().points.size();
            }
            // The connecting points between the segments are equal.
            len -= paths.size() - 1;
            assert(len > 0);
            out.points.reserve(len);
            out.points.push_back(paths.front().as_polyline().points.front());
            for (size_t i_path = 0; i_path < paths.size(); ++i_path) {
                Polyline poly_i = paths[i_path].as_polyline();
                if (poly_i.size() > 1)
                    out.points.insert(out.points.end(), poly_i.points.begin() + 1, poly_i.points.end());
            }
        }
        return out;
    }
    Polygons polygons_covered_by_width(const float scaled_epsilon = 0.f) const override{ Polygons out; this->polygons_covered_by_width(out, scaled_epsilon); return out; }
    Polygons polygons_covered_by_spacing(const float scaled_epsilon = 0.f) const override { Polygons out; this->polygons_covered_by_spacing(out, scaled_epsilon); return out; }
    void collect_polylines(Polylines &dst) const override { Polyline pl = this->as_polyline(); if (!pl.empty()) dst.emplace_back(std::move(pl)); }
    double total_volume() const override { double volume = 0.; for (const auto& path : paths) volume += path.total_volume(); return volume; }
};

// Single continuous extrusion path, possibly with varying extrusion thickness, extrusion height or bridging / non bridging.
class ExtrusionMultiPath : public ExtrusionMultiEntity<ExtrusionPath> {
public:

    ExtrusionMultiPath() {};
    ExtrusionMultiPath(const ExtrusionMultiPath &rhs) : ExtrusionMultiEntity(rhs) {}
    ExtrusionMultiPath(ExtrusionMultiPath &&rhs) : ExtrusionMultiEntity(rhs) {}
    ExtrusionMultiPath(const ExtrusionPaths &paths) : ExtrusionMultiEntity(paths) {};
    ExtrusionMultiPath(const ExtrusionPath &path) :ExtrusionMultiEntity(path) {}

    virtual ExtrusionMultiPath* clone() const override { return new ExtrusionMultiPath(*this); }
    virtual ExtrusionMultiPath* clone_move() override { return new ExtrusionMultiPath(std::move(*this)); }

    virtual void visit(ExtrusionVisitor &visitor) override { visitor.use(*this); };
    virtual void visit(ExtrusionVisitorConst &visitor) const override { visitor.use(*this); };
};
// Single continuous extrusion path, possibly with varying extrusion thickness, extrusion height or bridging / non bridging.
class ExtrusionMultiPath3D : public ExtrusionMultiEntity<ExtrusionPath3D> {
public:

    ExtrusionMultiPath3D() {};
    ExtrusionMultiPath3D(const ExtrusionMultiPath3D &rhs) : ExtrusionMultiEntity(rhs) {}
    ExtrusionMultiPath3D(ExtrusionMultiPath3D &&rhs) : ExtrusionMultiEntity(rhs) {}
    ExtrusionMultiPath3D(const ExtrusionPaths3D &paths) : ExtrusionMultiEntity(paths) {};
    ExtrusionMultiPath3D(const ExtrusionPath3D &path) :ExtrusionMultiEntity(path) {}

    virtual ExtrusionMultiPath3D* clone() const override { return new ExtrusionMultiPath3D(*this); }
    virtual ExtrusionMultiPath3D* clone_move() override { return new ExtrusionMultiPath3D(std::move(*this)); }

    virtual void visit(ExtrusionVisitor &visitor) override { visitor.use(*this); };
    virtual void visit(ExtrusionVisitorConst &visitor) const override { visitor.use(*this); };

    virtual bool can_reverse() const override { return false; }
    virtual void reverse() override {
        std::cout << "I SAID NO REVERSE!!!FFFS\n";
    }
};

// Single continuous extrusion loop, possibly with varying extrusion thickness, extrusion height or bridging / non bridging.
class ExtrusionLoop : public ExtrusionEntity
{
public:
    ExtrusionPaths paths;
    
    ExtrusionLoop(ExtrusionLoopRole role = elrDefault) : m_loop_role(role) {}
    ExtrusionLoop(const ExtrusionPaths &paths, ExtrusionLoopRole role = elrDefault) : paths(paths), m_loop_role(role) {}
    ExtrusionLoop(ExtrusionPaths &&paths, ExtrusionLoopRole role = elrDefault) : paths(std::move(paths)), m_loop_role(role) {}
    ExtrusionLoop(const ExtrusionPath &path, ExtrusionLoopRole role = elrDefault) : m_loop_role(role) 
        { this->paths.push_back(path); }
    ExtrusionLoop(const ExtrusionPath &&path, ExtrusionLoopRole role = elrDefault) : m_loop_role(role)
        { this->paths.emplace_back(std::move(path)); }
    virtual bool is_loop() const override{ return true; }
    virtual bool can_reverse() const override { return false; }
    virtual ExtrusionEntity* clone() const override{ return new ExtrusionLoop (*this); }
    // Create a new object, initialize it with this object using the move semantics.
    virtual ExtrusionEntity* clone_move() override { return new ExtrusionLoop(std::move(*this)); }
    bool make_clockwise();
    bool make_counter_clockwise();
    virtual void reverse() override;
    const Point& first_point() const override { return this->paths.front().polyline.points.front(); }
    const Point& last_point() const override { assert(this->first_point() == this->paths.back().polyline.points.back()); return this->first_point(); }
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
    double total_volume() const override { double volume = 0.; for (const auto& path : paths) volume += path.total_volume(); return volume; }
    virtual void visit(ExtrusionVisitor &visitor) override { visitor.use(*this); };
    virtual void visit(ExtrusionVisitorConst &visitor) const override { visitor.use(*this); };

    //static inline std::string role_to_string(ExtrusionLoopRole role);

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
            if (polyline.points.back() == polyline.points.front()) {
                ExtrusionPath path(role, mm3_per_mm, width, height);
                path.polyline.points = polyline.points;
                dst.emplace_back(new ExtrusionLoop(std::move(path)));
            } else {
                ExtrusionPath *extrusion_path = new ExtrusionPath(role, mm3_per_mm, width, height);
                dst.push_back(extrusion_path);
                extrusion_path->polyline = polyline;
            }
        }
}

inline void extrusion_entities_append_paths(ExtrusionEntitiesPtr &dst, Polylines &&polylines, ExtrusionRole role, double mm3_per_mm, float width, float height)
{
    dst.reserve(dst.size() + polylines.size());
    for (Polyline &polyline : polylines)
        if (polyline.is_valid()) {
            if (polyline.points.back() == polyline.points.front()) {
                ExtrusionPath path(role, mm3_per_mm, width, height);
                path.polyline.points = polyline.points;
                dst.emplace_back(new ExtrusionLoop(std::move(path)));
            } else {
                ExtrusionPath *extrusion_path = new ExtrusionPath(role, mm3_per_mm, width, height);
                dst.push_back(extrusion_path);
                extrusion_path->polyline = std::move(polyline);
            }
        }
    polylines.clear();
}

inline void extrusion_entities_append_loops(ExtrusionEntitiesPtr &dst, Polygons &loops, ExtrusionRole role, double mm3_per_mm, float width, float height) {
    dst.reserve(dst.size() + loops.size());
    for (Polygon &poly : loops) {
        if (poly.is_valid()) {
            ExtrusionPath path(role, mm3_per_mm, width, height);
            path.polyline.points = poly.points;
            path.polyline.points.push_back(path.polyline.points.front());
            dst.emplace_back(new ExtrusionLoop(std::move(path)));
        }
    }
}

inline void extrusion_entities_append_loops(ExtrusionEntitiesPtr &dst, Polygons &&loops, ExtrusionRole role, double mm3_per_mm, float width, float height)
{
    dst.reserve(dst.size() + loops.size());
    for (Polygon &poly : loops) {
        if (poly.is_valid()) {
            ExtrusionPath path(role, mm3_per_mm, width, height);
            path.polyline.points = std::move(poly.points);
            path.polyline.points.push_back(path.polyline.points.front());
            ExtrusionLoop *loop = new ExtrusionLoop(std::move(path));
            //default to ccw
            loop->make_counter_clockwise();
            dst.emplace_back(loop);
        }
    }
    loops.clear();
}

class ExtrusionPrinter : public ExtrusionVisitorConst {
    std::stringstream ss;
    double mult;
    bool trunc;
public:
    ExtrusionPrinter(double mult = 0.0001, bool trunc = false) : mult(mult), trunc(trunc) { }
    virtual void use(const ExtrusionPath& path) override;
    virtual void use(const ExtrusionPath3D& path3D) override;
    virtual void use(const ExtrusionMultiPath& multipath) override;
    virtual void use(const ExtrusionMultiPath3D& multipath) override;
    virtual void use(const ExtrusionLoop& loop) override;
    virtual void use(const ExtrusionEntityCollection& collection) override;
    std::string str() { return ss.str(); }
    std::string print(const ExtrusionEntity& entity)&& {
        entity.visit(*this);
        return ss.str();
    }
};

class ExtrusionLength : public ExtrusionVisitorConst {
    double dist;
public:
    ExtrusionLength() : dist(0){ }
    virtual void default_use(const ExtrusionEntity& path) override;
    virtual void use(const ExtrusionEntityCollection& collection) override;
    double get() { return dist; }
    double length(const ExtrusionEntity& entity)&& {
        entity.visit(*this);
        return get();
    }
};

}

#endif
