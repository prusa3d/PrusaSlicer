#ifndef slic3r_FillBase_hpp_
#define slic3r_FillBase_hpp_

#include <assert.h>
#include <memory.h>
#include <float.h>
#include <stdint.h>
#include <stdexcept>

#include <type_traits>
#include <boost/log/trivial.hpp>

#include "../libslic3r.h"
#include "../BoundingBox.hpp"
#include "../PrintConfig.hpp"
#include "../Exception.hpp"
#include "../Utils.hpp"
#include "../Flow.hpp"
#include "../ExtrusionEntity.hpp"
#include "../ExtrusionEntityCollection.hpp"

namespace Slic3r {

class ExPolygon;
class Surface;

namespace FillAdaptive {
    struct Octree;
};

// Infill shall never fail, therefore the error is classified as RuntimeError, not SlicingError.
class InfillFailedException : public Slic3r::RuntimeError {
public:
    InfillFailedException() : Slic3r::RuntimeError("Infill failed") {}
};

struct FillParams
{
    bool        full_infill() const { return density > 0.9999f && density < 1.0001f; }
    // Don't connect the fill lines around the inner perimeter.
    bool        dont_connect() const { return connection == InfillConnection::icNotConnected; }

    // Fill density, fraction in <0, 1>
    float       density     { 0.f };

    // Fill extruding flow multiplier, fraction in <0, 1>. Used by "over bridge compensation"
    float       flow_mult   { 1.0f };

    // Don't connect the fill lines around the inner perimeter.
    InfillConnection connection{ icConnected };

    // Length of an infill anchor along the perimeter.
    // 1000mm is roughly the maximum length line that fits into a 32bit coord_t.
    float       anchor_length   { 1000.f };
    float       anchor_length_max   { 1000.f };

    // Don't adjust spacing to fill the space evenly.
    bool        dont_adjust { true };

    // Monotonic infill - strictly left to right for better surface quality of top infills.
    bool        monotonic  { false };

    // Try to extrude the exact amount of plastic to fill the volume requested
    bool        fill_exactly{ false };

    // For Honeycomb.
    // we were requested to complete each loop;
    // in this case we don't try to make more continuous paths
    bool        complete    { false };

    // if role == erNone or ERCustom, this method have to choose the best role itself, else it must use the argument's role.
    ExtrusionRole role      { erNone };

    //flow to use
    Flow          flow      = Flow(0.f, 0.f, 0.f, false);

    //full configuration for the region, to avoid copying every bit that is needed. Use this for process-specific parameters.
    PrintRegionConfig const *config{ nullptr };
};
static_assert(IsTriviallyCopyable<FillParams>::value, "FillParams class is not POD (and it should be - see constructor).");

class Fill
{
public:
    // Index of the layer.
    size_t      layer_id;
    // Z coordinate of the top print surface, in unscaled coordinates
    coordf_t    z;
    // infill / perimeter overlap, in unscaled coordinates 
    coordf_t    overlap;
    ExPolygons  no_overlap_expolygons;
    // in radians, ccw, 0 = East
    float       angle;
    // In scaled coordinates. Maximum lenght of a perimeter segment connecting two infill lines.
    // Used by the FillRectilinear2, FillGrid2, FillTriangles, FillStars and FillCubic.
    // If left to zero, the links will not be limited.
    coord_t     link_max_length;
    // In scaled coordinates. Used by the concentric infill pattern to clip the loops to create extrusion paths.
    coord_t     loop_clipping;
    // In scaled coordinates. Bounding box of the 2D projection of the object.
    BoundingBox bounding_box;

    // Octree builds on mesh for usage in the adaptive cubic infill
    FillAdaptive::Octree* adapt_fill_octree = nullptr;
protected:
    // in unscaled coordinates, please use init (after settings all others settings) as some algos want to modify the value
    coordf_t    spacing_priv;

public:
    virtual ~Fill() {}
    virtual Fill* clone() const = 0;

    static Fill* new_from_type(const InfillPattern type);
    static Fill* new_from_type(const std::string &type);

    void         set_bounding_box(const Slic3r::BoundingBox &bbox) { bounding_box = bbox; }
    virtual void init_spacing(coordf_t spacing, const FillParams &params) { this->spacing_priv = spacing;  }
    coordf_t get_spacing() const { return spacing_priv; }

    // Do not sort the fill lines to optimize the print head path?
    virtual bool no_sort() const { return false; }

    // This method have to fill the ExtrusionEntityCollection. It call fill_surface by default
    virtual void fill_surface_extrusion(const Surface *surface, const FillParams &params, ExtrusionEntitiesPtr &out) const;
    
    // Perform the fill.
    virtual Polylines fill_surface(const Surface *surface, const FillParams &params) const;

protected:
    Fill() :
        layer_id(size_t(-1)),
        z(0.),
        spacing_priv(0.),
        // Infill / perimeter overlap.
        overlap(0.),
        // Initial angle is undefined.
        angle(FLT_MAX),
        link_max_length(0),
        loop_clipping(0),
        // The initial bounding box is empty, therefore undefined.
        bounding_box(Point(0, 0), Point(-1, -1))
        {}

    // The expolygon may be modified by the method to avoid a copy.
    virtual void _fill_surface_single(
        const FillParams                & /* params */, 
        unsigned int                      /* thickness_layers */,
        const std::pair<float, Point>   & /* direction */, 
        ExPolygon                         /* expolygon */,
        Polylines                       & /* polylines_out */) const {
        BOOST_LOG_TRIVIAL(error)<<"Error, the fill isn't implemented";
    };

    virtual float _layer_angle(size_t idx) const { return (idx & 1) ? float(M_PI/2.) : 0; }

    virtual coord_t _line_spacing_for_density(float density) const;

    virtual std::pair<float, Point> _infill_direction(const Surface *surface) const;

    void do_gap_fill(const ExPolygons& gapfill_areas, const FillParams& params, ExtrusionEntitiesPtr& coll_out) const;

    double compute_unscaled_volume_to_fill(const Surface* surface, const FillParams& params) const;

    ExtrusionRole getRoleFromSurfaceType(const FillParams &params, const Surface *surface) const {
        if (params.role == erNone || params.role == erCustom) {
            return params.flow.bridge ?
                (surface->has_pos_bottom() ? erBridgeInfill :erInternalBridgeInfill) :
                           (surface->has_fill_solid() ?
                           ((surface->has_pos_top()) ? erTopSolidInfill : erSolidInfill) :
                           erInternalInfill);
        }
        return params.role;
    }

public:
    static void connect_infill(Polylines&& infill_ordered, const ExPolygon& boundary, Polylines& polylines_out, const double spacing, const FillParams& params);
    //for rectilinear
    static void connect_infill(Polylines&& infill_ordered, const ExPolygon& boundary, const Polygons& polygons_src, Polylines& polylines_out, const double spacing, const FillParams& params);

    static coord_t  _adjust_solid_spacing(const coord_t width, const coord_t distance);

    // Align a coordinate to a grid. The coordinate may be negative,
    // the aligned value will never be bigger than the original one.
    static coord_t _align_to_grid(const coord_t coord, const coord_t spacing) {
        // Current C++ standard defines the result of integer division to be rounded to zero,
        // for both positive and negative numbers. Here we want to round down for negative
        // numbers as well.
        coord_t aligned = (coord < 0) ?
                ((coord - spacing + 1) / spacing) * spacing :
                (coord / spacing) * spacing;
        assert(aligned <= coord);
        return aligned;
    }
    static Point   _align_to_grid(Point   coord, Point   spacing) 
        { return Point(_align_to_grid(coord(0), spacing(0)), _align_to_grid(coord(1), spacing(1))); }
    static coord_t _align_to_grid(coord_t coord, coord_t spacing, coord_t base)
        { return base + _align_to_grid(coord - base, spacing); }
    static Point   _align_to_grid(Point   coord, Point   spacing, Point   base)
        { return Point(_align_to_grid(coord(0), spacing(0), base(0)), _align_to_grid(coord(1), spacing(1), base(1))); }
};

namespace FakePerimeterConnect {
    void connect_infill(Polylines&& infill_ordered, const ExPolygon& boundary, Polylines& polylines_out, const double spacing, const FillParams& params);
    void connect_infill(Polylines&& infill_ordered, const Polygons& boundary, const BoundingBox& bbox, Polylines& polylines_out, const double spacing, const FillParams& params);
    void connect_infill(Polylines&& infill_ordered, const std::vector<const Polygon*>& boundary, const BoundingBox& bbox, Polylines& polylines_out, double spacing, const FillParams& params);
}
namespace PrusaSimpleConnect {
    void connect_infill(Polylines& infill_ordered, const ExPolygon& boundary, Polylines& polylines_out, const double spacing, const FillParams& params);
}
namespace NaiveConnect {
    void connect_infill(Polylines&& infill_ordered, const ExPolygon& boundary, Polylines& polylines_out, const double spacing, const FillParams& params);
}

class ExtrusionSetRole : public ExtrusionVisitor {
    ExtrusionRole new_role;
public:
    ExtrusionSetRole(ExtrusionRole role) : new_role(role) {}
    void use(ExtrusionPath &path) override { path.set_role(new_role); }
    void use(ExtrusionPath3D &path3D) override { path3D.set_role(new_role); }
    void use(ExtrusionMultiPath &multipath) override { for (ExtrusionPath path : multipath.paths) path.set_role(new_role); }
    void use(ExtrusionMultiPath3D &multipath) override { for (ExtrusionPath path : multipath.paths) path.set_role(new_role); }
    void use(ExtrusionLoop &loop) override { for (ExtrusionPath path : loop.paths) path.set_role(new_role); }
    void use(ExtrusionEntityCollection &collection) override { for (ExtrusionEntity *entity : collection.entities) entity->visit(*this); }
};

} // namespace Slic3r

#endif // slic3r_FillBase_hpp_
