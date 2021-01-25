#ifndef slic3r_MedialAxis_hpp_
#define slic3r_MedialAxis_hpp_

#include "libslic3r.h"
#include "ExPolygon.hpp"
#include "Polyline.hpp"
#include "Geometry.hpp"
#include "ExtrusionEntityCollection.hpp"
#include "Flow.hpp"
#include <vector>

#define BOOST_VORONOI_USE_GMP 1
#include "boost/polygon/voronoi.hpp"
using boost::polygon::voronoi_builder;
using boost::polygon::voronoi_diagram;

namespace Slic3r {

/// This class is used to create single-line extrusion pattern with variable width to cover a ExPolygon.
/// The ends can enter a boundary area if neded, and can have a taper at each end.
/// The constructor initialize the mandatory variable.
/// you must use the setter to add the opptional settings before calling build().
class MedialAxis {
    public:
        /// _expolygon: the polygon to fill
        /// _max_width : maximum width of the extrusion. _expolygon shouldn't have a spot where a circle diameter is higher than that (or almost).
        /// _min_width : minimum width of the extrusion, every spot where a circle diameter is lower than that will be ignored (unless it's the tip of the extrusion)
        /// _height: height of the extrusion, used to compute the difference between width and spacing.
        MedialAxis(const ExPolygon &_expolygon, const coord_t _max_width, const coord_t _min_width, const coord_t _height)
            : surface(_expolygon), max_width(_max_width), min_width(_min_width), height(_height),
            bounds(&_expolygon), nozzle_diameter(_min_width), taper_size(0), stop_at_min_width(true){};

        /// create the polylines_out collection of variable-width polyline to extrude.
        void build(ThickPolylines &polylines_out);
        /// You shouldn't use this method as it doesn't give you the variable width. Can be useful for debugging.
        void build(Polylines &polylines);

        /// optional parameter: anchor area in which the extrusion should extends into. Default : expolygon (no bound)
        MedialAxis& use_bounds(const ExPolygon & _bounds) { this->bounds = &_bounds; return *this; }
        /// optional parameter: the real minimum width : it will grow the width of every extrusion that has a width lower than that. Default : min_width (same min)
        MedialAxis& use_min_real_width(const coord_t nozzle_diameter) { this->nozzle_diameter = nozzle_diameter; return *this; }
        /// optional parameter: create a taper of this length at each end (inside a bound or not). Default : 0 (no taper)
        MedialAxis& use_tapers(const coord_t taper_size) { this->taper_size = taper_size; return *this; }
        /// optional parameter: if true, the entension inside the bounds can be cut if the width is too small. Default : true
        MedialAxis& set_stop_at_min_width(const bool stop_at_min_width) { this->stop_at_min_width = stop_at_min_width; return *this; }

    private:
        /// Cache value: lines is here only to avoid passing it in argument of many methods. Initialized in polyline_from_voronoi.
        Lines lines;

        /// input polygon to fill
        const ExPolygon& surface;
        /// the copied expolygon from surface, it's modified in build() to simplify it. It's then used to create the voronoi diagram.
        ExPolygon expolygon;
        const ExPolygon* bounds;
        /// maximum width of the extrusion. _expolygon shouldn't have a spot where a circle diameter is higher than that (or almost).
        const coord_t max_width;
        /// minimum width of the extrusion, every spot where a circle diameter is lower than that will be ignored (unless it's the tip of the extrusion)
        const coord_t min_width;
        /// height of the extrusion, used to compute the diufference between width and spacing.
        const coord_t height;
        /// Used to compute the real minimum width we can extrude. if != min_width, it activate grow_to_nozzle_diameter(). 
        coord_t nozzle_diameter;
        /// if != , it activates taper_ends(). Can use nozzle_diameter.
        coord_t taper_size;
        //if true, remove_too_* can shorten the bits created by extends_line.
        bool stop_at_min_width;

        //voronoi stuff
        class VD : public voronoi_diagram<double> {
        public:
            typedef double                                          coord_type;
            typedef boost::polygon::point_data<coordinate_type>     point_type;
            typedef boost::polygon::segment_data<coordinate_type>   segment_type;
            typedef boost::polygon::rectangle_data<coordinate_type> rect_type;
        };
        VD vd;
        std::set<const VD::edge_type*> edges, valid_edges;
        std::map<const VD::edge_type*, std::pair<coordf_t, coordf_t> > thickness;
        void process_edge_neighbors(const VD::edge_type* edge, ThickPolyline* polyline);
        bool validate_edge(const VD::edge_type* edge);
        const Line& retrieve_segment(const VD::cell_type* cell) const;
        const Point& retrieve_endpoint(const VD::cell_type* cell) const;
        void polyline_from_voronoi(const Lines& voronoi_edges, ThickPolylines* polylines_out);

        // functions called by build:

        /// create a simplied version of surface, store it in expolygon
        void simplify_polygon_frontier();
        /// fusion little polylines created (by voronoi) on the external side of a curve inside the main polyline.
        void fusion_curve(ThickPolylines &pp);
        /// fusion polylines created by voronoi, where needed.
        void main_fusion(ThickPolylines& pp);
        /// like fusion_curve but for sharp angles like a square corner.
        void fusion_corners(ThickPolylines &pp);
        /// extends the polylines inside bounds, use extends_line on both end
        void extends_line_both_side(ThickPolylines& pp);
        /// extends the polylines inside bounds (anchors)
        void extends_line(ThickPolyline& polyline, const ExPolygons& anchors, const coord_t join_width);
        /// remove too thin bits at start & end of polylines
        void remove_too_thin_extrusion(ThickPolylines& pp);
        /// instead of keeping polyline split at each corssing, we try to create long strait polylines that can cross each other.
        void concatenate_polylines_with_crossing(ThickPolylines& pp);
        /// remove bits around points that are too thin (can be inside the polyline)
        void remove_too_thin_points(ThickPolylines& pp);
        /// delete polylines that are too short
        void remove_too_short_polylines(ThickPolylines& pp, const coord_t min_size);
        /// be sure we didn't try to push more plastic than the volume defined by surface * height can receive. If overextruded, reduce all widths by the correct %.
        void ensure_not_overextrude(ThickPolylines& pp);
        /// if nozzle_diameter > min_width, grow bits that are < width(nozzle_diameter) to width(nozzle_diameter) (don't activate that for gapfill)
        void grow_to_nozzle_diameter(ThickPolylines& pp, const ExPolygons& anchors);
        /// taper the ends of polylines (don't activate that for gapfill)
        void taper_ends(ThickPolylines& pp);
        //cleaning method
        void check_width(ThickPolylines& pp, double max_width, std::string msg);
        //removing small extrusion that won't be useful and will harm print. A bit like fusion_corners but more lenient and with just del.
        void remove_bits(ThickPolylines& pp);
};
    
    /// create a ExtrusionEntityCollection from ThickPolylines, discretizing the variable width into little sections (of 4*SCALED_RESOLUTION length) where needed.
    ExtrusionEntityCollection thin_variable_width(const ThickPolylines &polylines, ExtrusionRole role, Flow flow);
}


#endif
