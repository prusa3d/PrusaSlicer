#ifndef slic3r_FillBase_hpp_
#define slic3r_FillBase_hpp_

#include <memory.h>
#include <stdexcept>
#include <type_traits>
#include <string>
#include <utility>
#include <vector>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cassert>
#include <cstdint>

#include "Slic3r/Domain/BoundingBox.hpp"
#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Domain/Point.hpp"
#include "Slic3r/Domain/Polygon.hpp"
#include "Slic3r/Domain/Polyline.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Exception.hpp"
#include "Slic3r/Domain/ConfigDefsFDM.hpp"

#include "libslic3r/Utils.hpp"
#include "libslic3r/ConfigViews.hpp"

namespace Slic3r {

class Surface;
class PrintConfigView;
class PrintObjectConfigView;
struct ThickPolyline;
using ThickPolylines = std::vector<ThickPolyline>;

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
    bool        full_infill() const { return density > 0.9999f; }
    // Don't connect the fill lines around the inner perimeter.
    bool        dont_connect() const { return anchor_length_max < 0.05f; }

    // Fill density, fraction in <0, 1>
    float       density 		{ 0.f };

    // Length of an infill anchor along the perimeter.
    // 1000mm is roughly the maximum length line that fits into a 32bit coord_t.
    float       anchor_length       { 1000.f };
    float       anchor_length_max   { 1000.f };

    // G-code resolution.
    double      resolution          { 0.0125 };

    // Don't adjust spacing to fill the space evenly.
    bool        dont_adjust 	{ true };

    // Monotonic infill - strictly left to right for better surface quality of top infills.
    bool 		monotonic		{ false };

    // For Honeycomb.
    // we were requested to complete each loop;
    // in this case we don't try to make more continuous paths
    bool        complete 		{ false };

    // For Concentric infill, to switch between Classic and Arachne.
    bool        use_arachne     { false };
    // Layer height for Concentric infill with Arachne.
    double    layer_height    { 0.f };

    // For infills that produce closed loops to force printing those loops clockwise.
    bool        prefer_clockwise_movements { false };

    unsigned extruder_id{std::numeric_limits<unsigned>::max()};
};
static_assert(std::is_trivially_copyable_v<FillParams>, "FillParams class is not POD (and it should be - see constructor).");

class Fill
{
public:
    // Index of the layer.
    size_t      layer_id;
    // Z coordinate of the top print surface, in unscaled coordinates
    double    z;
    // in unscaled coordinates
    double    spacing;
    // infill / perimeter overlap, in unscaled coordinates
    double    overlap;
    // in radians, ccw, 0 = East
    float       angle;
    // In scaled coordinates. Maximum lenght of a perimeter segment connecting two infill lines.
    // Used by the FillRectilinear2, FillGrid2, FillTriangles, FillStars and FillCubic.
    // If left to zero, the links will not be limited.
    Domain::coord_t link_max_length;
    // In scaled coordinates. Used by the concentric infill pattern to clip the loops to create extrusion paths.
    Domain::coord_t loop_clipping;
    // In scaled coordinates. Bounding box of the 2D projection of the object.
    Domain::BoundingBox2crd bounding_box;

    // Octree builds on mesh for usage in the adaptive cubic infill
    FillAdaptive::Octree* adapt_fill_octree = nullptr;

    // PrintConfig and PrintObjectConfig are used by infills that use Arachne (Concentric and FillEnsuring).
    PrintRegionConfigView region_config;

public:
    virtual ~Fill() {}
    virtual Fill* clone() const = 0;

    static Fill* new_from_type(const Domain::InfillPattern type);
    static bool  use_bridge_flow(const Domain::InfillPattern type);

    void         set_bounding_box(const Domain::BoundingBox2crd &bbox) { bounding_box = bbox; }

    // Use bridge flow for the fill?
    virtual bool use_bridge_flow() const { return false; }

    // Do not sort the fill lines to optimize the print head path?
    virtual bool no_sort() const { return false; }

    virtual bool is_self_crossing() = 0;

    // Return true if infill has a consistent pattern between layers.
    virtual bool has_consistent_pattern() const { return false; }

    // Perform the fill.
    virtual Domain::Polylines fill_surface(const Surface *surface, const FillParams &params);
    virtual ThickPolylines fill_surface_arachne(const Surface *surface, const FillParams &params);

protected:
    Fill() :
        layer_id(size_t(-1)),
        z(0.),
        spacing(0.),
        // Infill / perimeter overlap.
        overlap(0.),
        // Initial angle is undefined.
        angle(FLT_MAX),
        link_max_length(0),
        loop_clipping(0),
        // The initial bounding box is empty, therefore undefined.
        bounding_box(Domain::Point(0, 0), Domain::Point(-1, -1))
        {}

    // The expolygon may be modified by the method to avoid a copy.
    virtual void    _fill_surface_single(
        const FillParams                & /* params */,
        unsigned int                      /* thickness_layers */,
        const std::pair<float, Domain::Point>   & /* direction */,
        Domain::ExPolygon                         /* expolygon */,
        Domain::Polylines                       & /* polylines_out */) {}

    // Used for concentric infill to generate ThickPolylines using Arachne.
    virtual void _fill_surface_single(const FillParams              &params,
                                      unsigned int                   thickness_layers,
                                      const std::pair<float, Domain::Point> &direction,
                                      Domain::ExPolygon              expolygon,
                                      ThickPolylines                &thick_polylines_out) {}

    virtual float _layer_angle(size_t idx) const { return (idx & 1) ? float(M_PI/2.) : 0; }


public:
    virtual std::pair<float, Domain::Point> _infill_direction(const Surface *surface) const;
    static void connect_infill(Domain::Polylines &&infill_ordered, const Domain::ExPolygon &boundary, Domain::Polylines &polylines_out, const double spacing, const FillParams &params);
    static void connect_infill(Domain::Polylines &&infill_ordered, const Domain::Polygons &boundary, const Domain::BoundingBox2crd& bbox, Domain::Polylines &polylines_out, const double spacing, const FillParams &params);
    static void connect_infill(Domain::Polylines &&infill_ordered, const std::vector<const Domain::Polygon*> &boundary, const Domain::BoundingBox2crd &bbox, Domain::Polylines &polylines_out, double spacing, const FillParams &params);

    static void connect_base_support(Domain::Polylines &&infill_ordered, const std::vector<const Domain::Polygon*> &boundary_src, const Domain::BoundingBox2crd &bbox, Domain::Polylines &polylines_out, const double spacing, const FillParams &params);
    static void connect_base_support(Domain::Polylines &&infill_ordered, const Domain::Polygons &boundary_src, const Domain::BoundingBox2crd &bbox, Domain::Polylines &polylines_out, const double spacing, const FillParams &params);

    static Domain::coord_t _adjust_solid_spacing(const Domain::coord_t width, const Domain::coord_t distance);
};

} // namespace Slic3r

#endif // slic3r_FillBase_hpp_
