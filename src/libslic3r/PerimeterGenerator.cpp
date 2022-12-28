#include "PerimeterGenerator.hpp"
#include "ClipperUtils.hpp"
#include "ExtrusionEntityCollection.hpp"
#include "ShortestPath.hpp"
#include "PNGReadWrite.hpp"
#include "Print.hpp" // PrintObject declaration etc

#include <cmath>
#include <cassert>
// typedef unsigned char Byte;

// Get the correct sleep function:
#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#endif
#include <cstdlib>

namespace Slic3r {

static ExtrusionPaths thick_polyline_to_extrusion_paths(const ThickPolyline &thick_polyline, ExtrusionRole role, const Flow &flow, const float tolerance, const float merge_tolerance)
{
    ExtrusionPaths paths;
    ExtrusionPath path(role);
    ThickLines lines = thick_polyline.thicklines();
    
    for (int i = 0; i < (int)lines.size(); ++i) {
        const ThickLine& line = lines[i];
        
        const coordf_t line_len = line.length();
        if (line_len < SCALED_EPSILON) continue;
        
        double thickness_delta = fabs(line.a_width - line.b_width);
        if (thickness_delta > tolerance) {
            const unsigned int segments = (unsigned int)ceil(thickness_delta / tolerance);
            const coordf_t seg_len = line_len / segments;
            Points pp;
            std::vector<coordf_t> width;
            {
                pp.push_back(line.a);
                width.push_back(line.a_width);
                for (size_t j = 1; j < segments; ++j) {
                    pp.push_back((line.a.cast<double>() + (line.b - line.a).cast<double>().normalized() * (j * seg_len)).cast<coord_t>());
                    
                    coordf_t w = line.a_width + (j*seg_len) * (line.b_width-line.a_width) / line_len;
                    width.push_back(w);
                    width.push_back(w);
                }
                pp.push_back(line.b);
                width.push_back(line.b_width);
                
                assert(pp.size() == segments + 1u);
                assert(width.size() == segments*2);
            }
            
            // delete this line and insert new ones
            lines.erase(lines.begin() + i);
            for (size_t j = 0; j < segments; ++j) {
                ThickLine new_line(pp[j], pp[j+1]);
                new_line.a_width = width[2*j];
                new_line.b_width = width[2*j+1];
                lines.insert(lines.begin() + i + j, new_line);
            }
            
            -- i;
            continue;
        }
        
        const double w = fmax(line.a_width, line.b_width);
        if (path.polyline.points.empty()) {
            path.polyline.append(line.a);
            path.polyline.append(line.b);
            // Convert from spacing to extrusion width based on the extrusion model
            // of a square extrusion ended with semi circles.
            Flow new_flow = flow.with_width(unscale<float>(w) + flow.height() * float(1. - 0.25 * PI));
            #ifdef SLIC3R_DEBUG
            printf("  filling %f gap\n", flow.width);
            #endif
            path.mm3_per_mm  = new_flow.mm3_per_mm();
            path.width       = new_flow.width();
            path.height      = new_flow.height();
        } else {
            thickness_delta = fabs(scale_(flow.width()) - w);
            if (thickness_delta <= merge_tolerance) {
                // the width difference between this line and the current flow width is 
                // within the accepted tolerance
                path.polyline.append(line.b);
            } else {
                // we need to initialize a new line
                paths.emplace_back(std::move(path));
                path = ExtrusionPath(role);
                -- i;
            }
        }
    }
    if (path.polyline.is_valid())
        paths.emplace_back(std::move(path));
    return paths;
}

static void variable_width(const ThickPolylines& polylines, ExtrusionRole role, const Flow &flow, std::vector<ExtrusionEntity*> &out)
{
	// This value determines granularity of adaptive width, as G-code does not allow
	// variable extrusion within a single move; this value shall only affect the amount
	// of segments, and any pruning shall be performed before we apply this tolerance.
	const float tolerance = float(scale_(0.05));
	for (const ThickPolyline &p : polylines) {
		ExtrusionPaths paths = thick_polyline_to_extrusion_paths(p, role, flow, tolerance, tolerance);
		// Append paths to collection.
		if (! paths.empty()) {
			if (paths.front().first_point() == paths.back().last_point())
				out.emplace_back(new ExtrusionLoop(std::move(paths)));
			else {
				for (ExtrusionPath &path : paths)
					out.emplace_back(new ExtrusionPath(std::move(path)));
			}
		}
	}
}

// Hierarchy of perimeters.
class PerimeterGeneratorLoop {
public:
    // Polygon of this contour.
    Polygon                             polygon;
    // Is it a contour or a hole?
    // Contours are CCW oriented, holes are CW oriented.
    bool                                is_contour;
    // Depth in the hierarchy. External perimeter has depth = 0. An external perimeter could be both a contour and a hole.
    unsigned short                      depth;
    // Should this contur be fuzzyfied on path generation?
    bool                                fuzzify;
    // Children contour, may be both CCW and CW oriented (outer contours or holes).
    std::vector<PerimeterGeneratorLoop> children;
    
    PerimeterGeneratorLoop(const Polygon &polygon, unsigned short depth, bool is_contour, bool fuzzify) : 
        polygon(polygon), is_contour(is_contour), depth(depth), fuzzify(fuzzify) {}
    // External perimeter. It may be CCW or CW oriented (outer contour or hole contour).
    bool is_external() const { return this->depth == 0; }
    // An island, which may have holes, but it does not have another internal island.
    bool is_internal_contour() const;
};

static inline double surface_offset(double offset, double max_offset, double fuzzy_skin_thickness) {
    return (offset) * (fuzzy_skin_thickness * 2.) / max_offset - fuzzy_skin_thickness;
}

/*!
Map a surface point to a 2d U (horizontal) value on a texture map, but expressed in millimeters due to the value's usage in PrusaSlicer.
The side matters since the texture should be wrapped around the whole object, not just one side, starting with the left,
to match the behavior of cube maps as used in the graphics field.
To reduce the number of calculations, the U value is not conformed to 0.0 to 1.0 like usual U values.
It is not 3D cube mapping but it will work for any point that is neither on the top nor bottom
(It would work but visibly behave like square mapping rather than cube mapping in those cases).

@param center The center of the bounding_box, pre-calculated for caching or customization.
@param bounding_box The extents of the model from a top view.
@param flat_point The surface point from a top view (variance in depth relative to center only matters if it puts the point in a different side).
@param normal_radians The normal of the flat_point expressed in radians from the top view (X-Y plane).
*/
static inline double cubemap_side_u(const Point& center, const BoundingBox& bounding_box, const Point& flat_point, const double normal_radians) {
    // double normal_radians = atan2(flat_point.y() - center.y(), flat_point.x() - center.x());
    double angle_deg = normal_radians * 180. / 3.14159;
    // -180 < angle_deg <= 180.
    // int side = 0;
    double previous_sides_total_length = 0.0;
    double relative_offset;
    if (angle_deg > 135. || angle_deg <= -135.) { // left (side 0)
        relative_offset = bounding_box.size().y() - (flat_point.y() - bounding_box.min.y());
        // ^ inverse since the left side *of* the left side is at the back, which has a larger y value than the front.
    } else if (angle_deg < -45.) { // front (2nd side in cube mapping)
        // side = 1;
        previous_sides_total_length = bounding_box.size().y(); // left's length is its y size.
        relative_offset = flat_point.x() - bounding_box.min.x();
    }
    else if (angle_deg <= 45.) { // right
        // side = 2;
        previous_sides_total_length =
            bounding_box.size().y() // left's length is its y size.
            + bounding_box.size().x() // front's length is its x size (width).
        ;
        relative_offset = flat_point.y() - bounding_box.min.y();
    } else { // (angle_deg > 45. && angle_deg <= 135) { // back
        // side = 3;
        previous_sides_total_length =
            bounding_box.size().y() * 2. // length of left + right (same, so * 2)
            + bounding_box.size().x() // front's length is its x size (width).
        ;
        relative_offset = bounding_box.size().x() - (flat_point.x() - bounding_box.min.x());
        // ^ inverse since the left side *of* the back is the right.
    }
    return previous_sides_total_length + relative_offset;
}

/*!
\brief Create a fuzzy polygon from an existing polygon.

Create a fuzzy polygon from an existing polygon. If a displacement map is used
(object->config().displacement_img().IsOk()), the spacing is fuzzy_skin_point_dist per pixel.
In that case z is used with fuzzy_skin_point_dist to determine the y pixel in the displacement map.
Otherwise, distance between points gets a random change of +- 1/4 and z is ignored.

Thanks Cura developers for this function. PrusaSlicer community member Poikilos initially implemented displacement_img and cube mapping.

@param poly The original polygon (a single perimeter).
@param fuzzy_skin_thickness The maximum random amount to affect the depth of the surface (total of in or out change).
@param z The location of the perimeter starting from the bottom of the model (used only for mapping).
@param object The PrintObject* used to access the bounding_box. Both that and displacement_img are used for mapping, but only if
              displacement_img->IsOk() (loaded/unloaded from fuzzy_skin_displacement_map path by apply_config).
@param displacement_img The map used when fuzzy map is enabled in PrintRegionConfig (not in PrintObjectConfig in object)
                        requiring this extra argument in every deeper call.
*/
static void fuzzy_polygon(Polygon &poly, double fuzzy_skin_thickness, double fuzzy_skin_point_dist, const double z, const PrintObject* object, const png::BackendPng* displacement_img)
{
    // This function must recieve scaled<double>(value) for each double argument, such as to improve float accuracy.
    const double min_dist_between_points = fuzzy_skin_point_dist * 3. / 4.; // hardcoded: the point distance may vary between 3/4 and 5/4 the supplied value
    const double range_random_point_dist = fuzzy_skin_point_dist / 2.;
    double dist_left_over = 0.0; // randomized below if there is no displacement map to align.
    Point* p0 = &poly.points.back();
    Points out;
    out.reserve(poly.points.size());
    // In graphics terms, U wraps around the layer (and is mapped to the X-axis of the image).
    // In PrusaSlicer the vertical axis is Z, but when working with images the vertical axis is V (mapped to Y in the image).
    // Normally U and V are from 0 to 1, but in this case they are pixel locations (from 0 to width or height minus 1).
    // TODO: Offset U such that the left edge of the image is at a good seam (probably point closest to 0,0, or
    //   in other words the front left corner, since the seam matters most in the case of box-like things like miniature buildings).
    double pixel_u = 0.0;
    double resolution = fuzzy_skin_point_dist;
    double pixel_v = z / fuzzy_skin_point_dist;  // Match v and u scale to fix y to x proportions (Each fuzzy_skin_point_dist spans 1 pixel on x).
    // ^ Note: z and fuzzy_skin_point_dist must both have been scaled using scale<double>(value) for this to work correctly.
    double pixel_x = 0.0;
    double pixel_y = 0.0;
    BoundingBox bounding_box;
    bool mapped = (displacement_img != nullptr) && displacement_img->IsOk();  // Check if the option is being used at all.
    if (object == nullptr) {
        if (mapped) {
            mapped = false;
            std::cerr << "Error: object is nullptr but displacement_img IsOk. This should never happen"
                " (and object should only be nullptr in test(s)), and mapping has been set back to false." << std::endl;
        }
    }

    Point center;
    if (mapped) {
        // Only access `object` if mapped (or tests that set object to nullptr will cause an exception).
        bounding_box = object->bounding_box();
        center = bounding_box.center();
        resolution = fuzzy_skin_point_dist; // A lower value can sharpen edges, but if a 1024x1024 image uses too high of a divisor (like 32 for a 200mm high model using 16MB RAM) the program will have increased slicing time by 32x (and likely have an OOM crash)!
        pixel_y = pixel_v;
        pixel_y = static_cast<double>((static_cast<int>(pixel_v+.5)) % displacement_img->GetHeight()); // +.5 to round; "Clamp" the texture using the "repeat" method (in graphics terms).
        pixel_y = displacement_img->GetHeight() - pixel_y; // Flip it so the bottom pixel (GetHeight()-1) is at the first layer(s) (z=~0) of the print.
        // std::cerr << "z=" << z << " / fuzzy_skin_point_dist=" << fuzzy_skin_point_dist << " and mapped becomes " << pixel_y << "" << std::endl; // debug only (and messy since multithreaded)
    }
    else {
        dist_left_over = double(rand()) * (min_dist_between_points / 2.) / double(RAND_MAX); // the distance to be traversed on the line before making the first new point
    }
    double total_dist = 0.0; // Keep track of total travel for displacement_img mapping.
    const double rand_max_d = double(RAND_MAX);
    for (Point &p1 : poly.points)
    { // 'a' is the (next) new point between p0 and p1
        Vec2d  p0p1      = (p1 - *p0).cast<double>();
        double p0p1_size = p0p1.norm();
        // so that p0p1_size - dist_last_point evaluates to dist_left_over - p0p1_size
        double dist_last_point = dist_left_over + p0p1_size * 2.;
        if (mapped) {
            for (double p0pa_dist = dist_left_over; p0pa_dist < p0p1_size;
                p0pa_dist += resolution)
            {
                // First get the side for cubic mapping.
                // a. Get the flat (non-fuzzy, or .5 offset) point first, to determine the 2D in-between point for mapping.
                double radius = surface_offset(.5, 1.0, fuzzy_skin_thickness); // The initial value is a "neutral" radius, *only* for calculating flat_point.
                Point flat_point = *p0 + (p0p1 * (p0pa_dist / p0p1_size) + perp(p0p1).cast<double>().normalized() * radius).cast<coord_t>();
                Point normal_point = *p0 + (p0p1 * (p0pa_dist / p0p1_size) + perp(p0p1).cast<double>().normalized() * (radius+1.0)).cast<coord_t>();
                // ^ should be the same math as below.
                double normal_radians = atan2(normal_point.y() - flat_point.y(), normal_point.x() - flat_point.x());
                // b. determine the face:
                pixel_u = cubemap_side_u(center, bounding_box, flat_point, normal_radians) / fuzzy_skin_point_dist;
                pixel_x = (double)((int)(pixel_u+.5) % displacement_img->GetWidth()); // +.5 to round; "Clamp" the texture using the "repeat" method (in graphics terms).
                radius = surface_offset(255.0 - double(displacement_img->GetLuma((int)(pixel_x+.5), (int)(pixel_y+.5))), 255.0, fuzzy_skin_thickness);
                // ^ Adding +.5 before casting to int is effectively the same as rounding.
                // ^ Using GetBlue, GetRed or GetGreen doesn't matter since the image should have been loaded as gray.
                // ^ max is 255 since it is image data (assumes 1 byte per channel as is expected in most cases including all known wx cases).
                // ^ inverse (255 - luma) due to existing behavior of surface_offset (code was moved to there unmodified from 'else' case below).
                out.emplace_back(*p0 + (p0p1 * (p0pa_dist / p0p1_size) + perp(p0p1).cast<double>().normalized() * radius).cast<coord_t>());
                dist_last_point = p0pa_dist;
                // pixel_u = total_dist / fuzzy_skin_point_dist; // This would always start at the seam, which is only reliable in models with double two-way symmetry where seam is "Aligned".
                total_dist += resolution;
            }
        }
        else {
            for (double p0pa_dist = dist_left_over; p0pa_dist < p0p1_size;
                p0pa_dist += min_dist_between_points + double(rand()) * range_random_point_dist / double(RAND_MAX))
            {
                double radius = surface_offset(rand(), rand_max_d, fuzzy_skin_thickness);
                out.emplace_back(*p0 + (p0p1 * (p0pa_dist / p0p1_size) + perp(p0p1).cast<double>().normalized() * radius).cast<coord_t>());
                dist_last_point = p0pa_dist;
            }
        }
        dist_left_over = p0p1_size - dist_last_point;
        p0 = &p1;
    }
    while (out.size() < 3) {
        size_t point_idx = poly.size() - 2;
        out.emplace_back(poly[point_idx]);
        if (point_idx == 0)
            break;
        -- point_idx;
    }
    if (out.size() >= 3)
        poly.points = std::move(out);
}

using PerimeterGeneratorLoops = std::vector<PerimeterGeneratorLoop>;

static ExtrusionEntityCollection traverse_loops(const PerimeterGenerator &perimeter_generator, const PerimeterGeneratorLoops &loops, ThickPolylines &thin_walls, const PrintObject* object)
{
    // loops is an arrayref of ::Loop objects
    // turn each one into an ExtrusionLoop object
    ExtrusionEntityCollection   coll;
    Polygon                     fuzzified;
    for (const PerimeterGeneratorLoop &loop : loops) {
        bool is_external = loop.is_external();
        
        ExtrusionRole role;
        ExtrusionLoopRole loop_role;
        role = is_external ? erExternalPerimeter : erPerimeter;
        if (loop.is_internal_contour()) {
            // Note that we set loop role to ContourInternalPerimeter
            // also when loop is both internal and external (i.e.
            // there's only one contour loop).
            loop_role = elrContourInternalPerimeter;
        } else {
            loop_role = elrDefault;
        }
        
        // detect overhanging/bridging perimeters
        ExtrusionPaths paths;
        const Polygon &polygon = loop.fuzzify ? fuzzified : loop.polygon;
        if (loop.fuzzify) {
            fuzzified = loop.polygon;
            fuzzy_polygon(
                fuzzified,
                scaled<double>(perimeter_generator.config->fuzzy_skin_thickness.value),
                scaled<double>(perimeter_generator.config->fuzzy_skin_point_dist.value),
                scaled<double>(perimeter_generator.z_of_current_layer),
                object,
                perimeter_generator.config->opt_image("fuzzy_skin_displacement_map", false)
            );
        }
        if (perimeter_generator.config->overhangs && perimeter_generator.layer_id > perimeter_generator.object_config->raft_layers
            && ! ((perimeter_generator.object_config->support_material || perimeter_generator.object_config->support_material_enforce_layers > 0) && 
                  perimeter_generator.object_config->support_material_contact_distance.value == 0)) {
            // get non-overhang paths by intersecting this loop with the grown lower slices
            extrusion_paths_append(
                paths,
                intersection_pl({ polygon }, perimeter_generator.lower_slices_polygons()),
                role,
                is_external ? perimeter_generator.ext_mm3_per_mm()           : perimeter_generator.mm3_per_mm(),
                is_external ? perimeter_generator.ext_perimeter_flow.width() : perimeter_generator.perimeter_flow.width(),
                (float)perimeter_generator.layer_height);
            
            // get overhang paths by checking what parts of this loop fall 
            // outside the grown lower slices (thus where the distance between
            // the loop centerline and original lower slices is >= half nozzle diameter
            extrusion_paths_append(
                paths,
                diff_pl({ polygon }, perimeter_generator.lower_slices_polygons()),
                erOverhangPerimeter,
                perimeter_generator.mm3_per_mm_overhang(),
                perimeter_generator.overhang_flow.width(),
                perimeter_generator.overhang_flow.height());
            
            // Reapply the nearest point search for starting point.
            // We allow polyline reversal because Clipper may have randomly reversed polylines during clipping.
            chain_and_reorder_extrusion_paths(paths, &paths.front().first_point());
        } else {
            ExtrusionPath path(role);
            path.polyline   = polygon.split_at_first_point();
            path.mm3_per_mm = is_external ? perimeter_generator.ext_mm3_per_mm()           : perimeter_generator.mm3_per_mm();
            path.width      = is_external ? perimeter_generator.ext_perimeter_flow.width() : perimeter_generator.perimeter_flow.width();
            path.height     = (float)perimeter_generator.layer_height;
            paths.push_back(path);
        }
        
        coll.append(ExtrusionLoop(std::move(paths), loop_role));
    }
    
    // Append thin walls to the nearest-neighbor search (only for first iteration)
    if (! thin_walls.empty()) {
        variable_width(thin_walls, erExternalPerimeter, perimeter_generator.ext_perimeter_flow, coll.entities);
        thin_walls.clear();
    }
    
    // Traverse children and build the final collection.
	Point zero_point(0, 0);
	std::vector<std::pair<size_t, bool>> chain = chain_extrusion_entities(coll.entities, &zero_point);
    ExtrusionEntityCollection out;
    for (const std::pair<size_t, bool> &idx : chain) {
		assert(coll.entities[idx.first] != nullptr);
        if (idx.first >= loops.size()) {
            // This is a thin wall.
			out.entities.reserve(out.entities.size() + 1);
            out.entities.emplace_back(coll.entities[idx.first]);
			coll.entities[idx.first] = nullptr;
            if (idx.second)
				out.entities.back()->reverse();
        } else {
            const PerimeterGeneratorLoop &loop = loops[idx.first];
            assert(thin_walls.empty());
            ExtrusionEntityCollection children = traverse_loops(perimeter_generator, loop.children, thin_walls, object);
            out.entities.reserve(out.entities.size() + children.entities.size() + 1);
            ExtrusionLoop *eloop = static_cast<ExtrusionLoop*>(coll.entities[idx.first]);
            coll.entities[idx.first] = nullptr;
            if (loop.is_contour) {
                eloop->make_counter_clockwise();
                out.append(std::move(children.entities));
                out.entities.emplace_back(eloop);
            } else {
                eloop->make_clockwise();
                out.entities.emplace_back(eloop);
                out.append(std::move(children.entities));
            }
        }
    }
    return out;
}

void PerimeterGenerator::process()
{
    // other perimeters
    m_mm3_per_mm               		= this->perimeter_flow.mm3_per_mm();
    coord_t perimeter_width         = this->perimeter_flow.scaled_width();
    coord_t perimeter_spacing       = this->perimeter_flow.scaled_spacing();
    
    // external perimeters
    m_ext_mm3_per_mm           		= this->ext_perimeter_flow.mm3_per_mm();
    coord_t ext_perimeter_width     = this->ext_perimeter_flow.scaled_width();
    coord_t ext_perimeter_spacing   = this->ext_perimeter_flow.scaled_spacing();
    coord_t ext_perimeter_spacing2  = scaled<coord_t>(0.5f * (this->ext_perimeter_flow.spacing() + this->perimeter_flow.spacing()));
    
    // overhang perimeters
    m_mm3_per_mm_overhang      		= this->overhang_flow.mm3_per_mm();
    
    // solid infill
    coord_t solid_infill_spacing    = this->solid_infill_flow.scaled_spacing();
    
    // Calculate the minimum required spacing between two adjacent traces.
    // This should be equal to the nominal flow spacing but we experiment
    // with some tolerance in order to avoid triggering medial axis when
    // some squishing might work. Loops are still spaced by the entire
    // flow spacing; this only applies to collapsing parts.
    // For ext_min_spacing we use the ext_perimeter_spacing calculated for two adjacent
    // external loops (which is the correct way) instead of using ext_perimeter_spacing2
    // which is the spacing between external and internal, which is not correct
    // and would make the collapsing (thus the details resolution) dependent on 
    // internal flow which is unrelated.
    coord_t min_spacing         = coord_t(perimeter_spacing      * (1 - INSET_OVERLAP_TOLERANCE));
    coord_t ext_min_spacing     = coord_t(ext_perimeter_spacing  * (1 - INSET_OVERLAP_TOLERANCE));
    bool    has_gap_fill 		= this->config->gap_fill_enabled.value && this->config->gap_fill_speed.value > 0;

    // prepare grown lower layer slices for overhang detection
    if (this->lower_slices != NULL && this->config->overhangs) {
        // We consider overhang any part where the entire nozzle diameter is not supported by the
        // lower layer, so we take lower slices and offset them by half the nozzle diameter used 
        // in the current layer
        double nozzle_diameter = this->print_config->nozzle_diameter.get_at(this->config->perimeter_extruder-1);
        m_lower_slices_polygons = offset(*this->lower_slices, float(scale_(+nozzle_diameter/2)));
    }

    // We need to process each island separately because we might have different
    // extra perimeters for each one.
    // - this->object should only be accessed if a map is used, otherwise test(s) must be modified
    //   to generate an object and ensure `object` param of the PerimeterGenerator constructor is not nullptr.
    for (const Surface &surface : this->slices->surfaces) {
        // detect how many perimeters must be generated for this island
        int        loop_number = this->config->perimeters + surface.extra_perimeters - 1;  // 0-indexed loops
        ExPolygons last        = union_ex(surface.expolygon.simplify_p(m_scaled_resolution));
        ExPolygons gaps;
        if (loop_number >= 0) {
            // In case no perimeters are to be generated, loop_number will equal to -1.
            std::vector<PerimeterGeneratorLoops> contours(loop_number+1);    // depth => loops
            std::vector<PerimeterGeneratorLoops> holes(loop_number+1);       // depth => loops
            ThickPolylines thin_walls;
            // we loop one time more than needed in order to find gaps after the last perimeter was applied
            for (int i = 0;; ++ i) {  // outer loop is 0
                // Calculate next onion shell of perimeters.
                ExPolygons offsets;
                if (i == 0) {
                    // the minimum thickness of a single loop is:
                    // ext_width/2 + ext_spacing/2 + spacing/2 + width/2
                    offsets = this->config->thin_walls ? 
                        offset2_ex(
                            last,
                            - float(ext_perimeter_width / 2. + ext_min_spacing / 2. - 1),
                            + float(ext_min_spacing / 2. - 1)) :
                        offset_ex(last, - float(ext_perimeter_width / 2.));
                    // look for thin walls
                    if (this->config->thin_walls) {
                        // the following offset2 ensures almost nothing in @thin_walls is narrower than $min_width
                        // (actually, something larger than that still may exist due to mitering or other causes)
                        coord_t min_width = coord_t(scale_(this->ext_perimeter_flow.nozzle_diameter() / 3));
                        ExPolygons expp = opening_ex(
                            // medial axis requires non-overlapping geometry
                            diff_ex(last, offset(offsets, float(ext_perimeter_width / 2.) + ClipperSafetyOffset)),
                            float(min_width / 2.));
                        // the maximum thickness of our thin wall area is equal to the minimum thickness of a single loop
                        for (ExPolygon &ex : expp)
                            ex.medial_axis(ext_perimeter_width + ext_perimeter_spacing2, min_width, &thin_walls);
                    }
                    if (m_spiral_vase && offsets.size() > 1) {
                    	// Remove all but the largest area polygon.
                    	keep_largest_contour_only(offsets);
                    }
                } else {
                    //FIXME Is this offset correct if the line width of the inner perimeters differs
                    // from the line width of the infill?
                    coord_t distance = (i == 1) ? ext_perimeter_spacing2 : perimeter_spacing;
                    offsets = this->config->thin_walls ?
                        // This path will ensure, that the perimeters do not overfill, as in 
                        // prusa3d/Slic3r GH #32, but with the cost of rounding the perimeters
                        // excessively, creating gaps, which then need to be filled in by the not very 
                        // reliable gap fill algorithm.
                        // Also the offset2(perimeter, -x, x) may sometimes lead to a perimeter, which is larger than
                        // the original.
                        offset2_ex(last,
                                - float(distance + min_spacing / 2. - 1.),
                                float(min_spacing / 2. - 1.)) :
                        // If "detect thin walls" is not enabled, this paths will be entered, which 
                        // leads to overflows, as in prusa3d/Slic3r GH #32
                        offset_ex(last, - float(distance));
                    // look for gaps
                    if (has_gap_fill)
                        // not using safety offset here would "detect" very narrow gaps
                        // (but still long enough to escape the area threshold) that gap fill
                        // won't be able to fill but we'd still remove from infill area
                        append(gaps, diff_ex(
                            offset(last,    - float(0.5 * distance)),
                            offset(offsets,   float(0.5 * distance + 10))));  // safety offset
                }
                if (offsets.empty()) {
                    // Store the number of loops actually generated.
                    loop_number = i - 1;
                    // No region left to be filled in.
                    last.clear();
                    break;
                } else if (i > loop_number) {
                    // If i > loop_number, we were looking just for gaps.
                    break;
                }
                {
                    const bool fuzzify_contours = this->config->fuzzy_skin != FuzzySkinType::None && i == 0 && this->layer_id > 0;
                    const bool fuzzify_holes    = fuzzify_contours && this->config->fuzzy_skin == FuzzySkinType::All;
                    for (const ExPolygon &expolygon : offsets) {
    	                // Outer contour may overlap with an inner contour,
    	                // inner contour may overlap with another inner contour,
    	                // outer contour may overlap with itself.
    	                //FIXME evaluate the overlaps, annotate each point with an overlap depth,
                        // compensate for the depth of intersection.
                        contours[i].emplace_back(expolygon.contour, i, true, fuzzify_contours);

                        if (! expolygon.holes.empty()) {
                            holes[i].reserve(holes[i].size() + expolygon.holes.size());
                            for (const Polygon &hole : expolygon.holes)
                                holes[i].emplace_back(hole, i, false, fuzzify_holes);
                        }
                    }
                }
                last = std::move(offsets);
                if (i == loop_number && (! has_gap_fill || this->config->fill_density.value == 0)) {
                	// The last run of this loop is executed to collect gaps for gap fill.
                	// As the gap fill is either disabled or not 
                	break;
                }
            }

            // nest loops: holes first
            for (int d = 0; d <= loop_number; ++ d) {
                PerimeterGeneratorLoops &holes_d = holes[d];
                // loop through all holes having depth == d
                for (int i = 0; i < (int)holes_d.size(); ++ i) {
                    const PerimeterGeneratorLoop &loop = holes_d[i];
                    // find the hole loop that contains this one, if any
                    for (int t = d + 1; t <= loop_number; ++ t) {
                        for (int j = 0; j < (int)holes[t].size(); ++ j) {
                            PerimeterGeneratorLoop &candidate_parent = holes[t][j];
                            if (candidate_parent.polygon.contains(loop.polygon.first_point())) {
                                candidate_parent.children.push_back(loop);
                                holes_d.erase(holes_d.begin() + i);
                                -- i;
                                goto NEXT_LOOP;
                            }
                        }
                    }
                    // if no hole contains this hole, find the contour loop that contains it
                    for (int t = loop_number; t >= 0; -- t) {
                        for (int j = 0; j < (int)contours[t].size(); ++ j) {
                            PerimeterGeneratorLoop &candidate_parent = contours[t][j];
                            if (candidate_parent.polygon.contains(loop.polygon.first_point())) {
                                candidate_parent.children.push_back(loop);
                                holes_d.erase(holes_d.begin() + i);
                                -- i;
                                goto NEXT_LOOP;
                            }
                        }
                    }
                    NEXT_LOOP: ;
                }
            }
            // nest contour loops
            for (int d = loop_number; d >= 1; -- d) {
                PerimeterGeneratorLoops &contours_d = contours[d];
                // loop through all contours having depth == d
                for (int i = 0; i < (int)contours_d.size(); ++ i) {
                    const PerimeterGeneratorLoop &loop = contours_d[i];
                    // find the contour loop that contains it
                    for (int t = d - 1; t >= 0; -- t) {
                        for (size_t j = 0; j < contours[t].size(); ++ j) {
                            PerimeterGeneratorLoop &candidate_parent = contours[t][j];
                            if (candidate_parent.polygon.contains(loop.polygon.first_point())) {
                                candidate_parent.children.push_back(loop);
                                contours_d.erase(contours_d.begin() + i);
                                -- i;
                                goto NEXT_CONTOUR;
                            }
                        }
                    }
                    NEXT_CONTOUR: ;
                }
            }
            // at this point, all loops should be in contours[0]
            ExtrusionEntityCollection entities = traverse_loops(*this, contours.front(), thin_walls, this->object);
            // if brim will be printed, reverse the order of perimeters so that
            // we continue inwards after having finished the brim
            // TODO: add test for perimeter order
            if (this->config->external_perimeters_first || 
                (this->layer_id == 0 && this->object_config->brim_width.value > 0))
                entities.reverse();
            // append perimeters for this slice as a collection
            if (! entities.empty())
                this->loops->append(entities);
        } // for each loop of an island

        // fill gaps
        if (! gaps.empty()) {
            // collapse 
            double min = 0.2 * perimeter_width * (1 - INSET_OVERLAP_TOLERANCE);
            double max = 2. * perimeter_spacing;
            ExPolygons gaps_ex = diff_ex(
                //FIXME offset2 would be enough and cheaper.
                opening_ex(gaps, float(min / 2.)),
                offset2_ex(gaps, - float(max / 2.), float(max / 2. + ClipperSafetyOffset)));
            ThickPolylines polylines;
            for (const ExPolygon &ex : gaps_ex)
                ex.medial_axis(max, min, &polylines);
            if (! polylines.empty()) {
				ExtrusionEntityCollection gap_fill;
				variable_width(polylines, erGapFill, this->solid_infill_flow, gap_fill.entities);
                /*  Make sure we don't infill narrow parts that are already gap-filled
                    (we only consider this surface's gaps to reduce the diff() complexity).
                    Growing actual extrusions ensures that gaps not filled by medial axis
                    are not subtracted from fill surfaces (they might be too short gaps
                    that medial axis skips but infill might join with other infill regions
                    and use zigzag).  */
                //FIXME Vojtech: This grows by a rounded extrusion width, not by line spacing,
                // therefore it may cover the area, but no the volume.
                last = diff_ex(last, gap_fill.polygons_covered_by_width(10.f));
				this->gap_fill->append(std::move(gap_fill.entities));
			}
        }

        // create one more offset to be used as boundary for fill
        // we offset by half the perimeter spacing (to get to the actual infill boundary)
        // and then we offset back and forth by half the infill spacing to only consider the
        // non-collapsing regions
        coord_t inset = 
            (loop_number < 0) ? 0 :
            (loop_number == 0) ?
                // one loop
                ext_perimeter_spacing / 2 :
                // two or more loops?
                perimeter_spacing / 2;
        // only apply infill overlap if we actually have one perimeter
        if (inset > 0)
            inset -= coord_t(scale_(this->config->get_abs_value("infill_overlap", unscale<double>(inset + solid_infill_spacing / 2))));
        // simplify infill contours according to resolution
        Polygons pp;
        for (ExPolygon &ex : last)
            ex.simplify_p(m_scaled_resolution, &pp);
        // collapse too narrow infill areas
        coord_t min_perimeter_infill_spacing = coord_t(solid_infill_spacing * (1. - INSET_OVERLAP_TOLERANCE));
        // append infill areas to fill_surfaces
        this->fill_surfaces->append(
            offset2_ex(
                union_ex(pp),
                float(- inset - min_perimeter_infill_spacing / 2.),
                float(min_perimeter_infill_spacing / 2.)),
            stInternal);
    } // for each island
}

bool PerimeterGeneratorLoop::is_internal_contour() const
{
    // An internal contour is a contour containing no other contours
    if (! this->is_contour)
        return false;
    for (const PerimeterGeneratorLoop &loop : this->children)
        if (loop.is_contour)
            return false;
    return true;
}

}
