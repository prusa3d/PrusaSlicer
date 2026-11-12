#include "Slic3r/Biz/Algorithms/HealPolygon.hpp"

#include <numeric> // iota

#include "Slic3r/Domain/ExPolygonsIndex.hpp"
#include "Slic3r/Biz/Algorithms/AABBTreeLines.hpp" // search structure for found close points
#include "Slic3r/Biz/Algorithms/Point.hpp"
#include "Slic3r/Biz/Algorithms/Polygon.hpp"
#include "Slic3r/Biz/Algorithms/ExPolygon.hpp"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Biz/Algorithms/ClipperUtils.hpp"
#include "Slic3r/Biz/Algorithms/IntersectionPoints.hpp" // IntersectionsLines + IntersectionLines

#include "Slic3r/Utils.hpp" // append(ExPolygons)

using namespace Slic3r;

// NOTE: approach to heal shape by Clipper::Closing is not working

// functionality to remove all spikes from shape
// Potentionaly useable for eliminate spike in layer
// #define REMOVE_SPIKES

// function to remove useless islands and holes
// #define REMOVE_SMALL_ISLANDS
#ifdef REMOVE_SMALL_ISLANDS
namespace {
void remove_small_islands(Domain::ExPolygons& shape, double minimal_area);
} // namespace
#endif // REMOVE_SMALL_ISLANDS

// #define VISUALIZE_HEAL
#ifdef VISUALIZE_HEAL
namespace {
// for debug purpose only
// NOTE: check scale when store svg !!
#include "Slic3r/Biz/Algorithms/SVG.hpp" // for visualize_heal

Domain::Points get_unique_intersections(const Slic3r::IntersectionsLines& intersections); // fast forward declaration
static std::string visualize_heal_svg_filepath = "C:/data/temp/heal.svg";

void visualize_heal(const std::string& svg_filepath, const Domain::ExPolygons& expolygons)
{
    Domain::Points pts = to_points(expolygons);
    Domain::BoundingBox2crd bb(pts);
    // double svg_scale = SHAPE_SCALE / unscale<double>(1.);
    // bb.scale(svg_scale);
    Biz::Algorithms::SVG::SVG svg(svg_filepath, bb);
    svg.draw(expolygons);

    Domain::Points duplicits = collect_duplicates(pts);
    int black_size           = std::max(bb.size().x(), bb.size().y()) / 20;
    svg.draw(duplicits, "black", black_size);

    Slic3r::IntersectionsLines intersections_f = get_intersections(expolygons);
    Domain::Points intersections               = get_unique_intersections(intersections_f);
    svg.draw(intersections, "red", black_size * 1.2);
}
} // namespace
#endif // VISUALIZE_HEAL

// do not expose out of this file stbtt_ data types
namespace {
// TODO: it is connected only with emboss font to scale font glyphs
static constexpr double SHAPE_SCALE = 0.001; // SCALING_FACTOR promile is fine enough

// Try to remove self intersection by subtracting rect 2x2 px
Domain::ExPolygon create_bounding_rect(const Domain::ExPolygons& shape);

// Heal duplicates points and self intersections
bool heal_dupl_inter(Domain::ExPolygons& shape, unsigned max_iteration);

const Domain::Points pts_2x2(
    {Domain::Point(0, 0), Domain::Point(1, 0), Domain::Point(1, 1), Domain::Point(0, 1)}
);
const Domain::Points pts_3x3(
    {Domain::Point(-1, -1), Domain::Point(1, -1), Domain::Point(1, 1), Domain::Point(-1, 1)}
);

struct SpikeDesc
{
    // cosinus of max spike angle
    double cos_angle; // speed up to skip acos

    // Half of Wanted bevel size
    double half_bevel;

    /// <summary>
    /// Calculate spike description
    /// </summary>
    /// <param name="bevel_size">Size of spike width after cut of the tip, has to be grater than 2.5</param>
    /// <param name="pixel_spike_length">When spike has same or more pixels with width less than 1 pixel</param>
    SpikeDesc(double bevel_size, double pixel_spike_length = 6) :
        // create min angle given by spike_length
        // Use it as minimal height of 1 pixel base spike
        cos_angle(
            std::fabs(
                std::cos(
                    /*angle*/ 2. * std::atan2(pixel_spike_length, .5)
                )
            )
        ),

        // When remove spike this angle is set.
        // Value must be grater than min_angle
        half_bevel(bevel_size / 2)
    {}
};

// return TRUE when remove point. It could create polygon with 2 points.
bool remove_when_spike(Domain::Polygon& polygon, size_t index, const SpikeDesc& spike_desc);
void remove_spikes_in_duplicates(Domain::ExPolygons& expolygons, const Domain::Points& duplicates);

#ifdef REMOVE_SPIKES
// Remove long sharp corners aka spikes
// by adding points to bevel tip of spikes - Not printable parts
// Try to not modify long sides of spike and add points on it's side
void remove_spikes(Domain::Polygon& polygon, const SpikeDesc& spike_desc);
void remove_spikes(Domain::Polygons& polygons, const SpikeDesc& spike_desc);
void remove_spikes(Domain::ExPolygons& expolygons, const SpikeDesc& spike_desc);
#endif

// spike ... very sharp corner - when not removed cause iteration of heal process
// index ... index of duplicit point in polygon
bool remove_when_spike(Domain::Polygon& polygon, size_t index, const SpikeDesc& spike_desc)
{
    std::optional<Domain::Point> add;
    bool do_erase       = false;
    Domain::Points& pts = polygon.points;
    {
        size_t pts_size = pts.size();
        if (pts_size < 3)
            return false;

        const Domain::Point& a = (index == 0) ? pts.back() : pts[index - 1];
        const Domain::Point& b = pts[index];
        const Domain::Point& c = (index == (pts_size - 1)) ? pts.front() : pts[index + 1];

        // calc sides
        Domain::Vec2d ba = (a - b).cast<double>();
        Domain::Vec2d bc = (c - b).cast<double>();

        double dot_product = ba.dot(bc);

        // sqrt together after multiplication save one sqrt
        double ba_size_sq = ba.squaredNorm();
        double bc_size_sq = bc.squaredNorm();
        double norm       = sqrt(ba_size_sq * bc_size_sq);
        double cos_angle  = dot_product / norm;

        // small angle are around 1 --> cos(0) = 1
        if (cos_angle < spike_desc.cos_angle)
            return false; // not a spike

        // has to be in range <-1, 1>
        // Due to preccission of floating point number could be sligtly out of range
        if (cos_angle > 1.)
            cos_angle = 1.;
        // if (cos_angle < -1.)
        // cos_angle = -1.;

        // Current Spike angle
        double angle          = acos(cos_angle);
        double wanted_size    = spike_desc.half_bevel / cos(angle / 2.);
        double wanted_size_sq = wanted_size * wanted_size;

        bool is_ba_short = ba_size_sq < wanted_size_sq;
        bool is_bc_short = bc_size_sq < wanted_size_sq;

        auto a_side = [&b, &ba, &ba_size_sq, &wanted_size]() -> Domain::Point {
            Domain::Vec2d ba_norm = ba / sqrt(ba_size_sq);
            return b + (wanted_size * ba_norm).cast<Domain::coord_t>();
        };
        auto c_side = [&b, &bc, &bc_size_sq, &wanted_size]() -> Domain::Point {
            Domain::Vec2d bc_norm = bc / sqrt(bc_size_sq);
            return b + (wanted_size * bc_norm).cast<Domain::coord_t>();
        };

        if (is_ba_short && is_bc_short) {
            // remove short spike
            do_erase = true;
        } else if (is_ba_short) {
            // move point B on C-side
            pts[index] = c_side();
        } else if (is_bc_short) {
            // move point B on A-side
            pts[index] = a_side();
        } else {
            // move point B on C-side and add point on A-side(left - before)
            pts[index] = c_side();
            add        = a_side();
            if (*add == pts[index]) {
                // should be very rare, when SpikeDesc has small base
                // will be fixed by remove B point
                add.reset();
                do_erase = true;
            }
        }
    }
    if (do_erase) {
        pts.erase(pts.begin() + index);
        return true;
    }
    if (add.has_value())
        pts.insert(pts.begin() + index, *add);
    return false;
}
} // namespace

namespace Slic3r::Biz::Algorithms::HealPolygon {

Domain::HealedExPolygons heal_polygons(const Domain::Polygons& shape, bool is_non_zero, unsigned max_iteration)
{
    const double clean_distance        = 1.415; // little grater than sqrt(2)
    ClipperLib::PolyFillType fill_type = is_non_zero ? ClipperLib::pftNonZero :
                                                       ClipperLib::pftEvenOdd;

    // When edit this code check that font 'ALIENATE.TTF' and glyph 'i' still work
    // fix of self intersections
    // http://www.angusj.com/delphi/clipper/documentation/Docs/Units/ClipperLib/Functions/SimplifyPolygon.htm
    ClipperLib::Paths paths = ClipperLib::SimplifyPolygons(
        ClipperUtils::PolygonsProvider(shape),
        fill_type
    );
    ClipperLib::CleanPolygons(paths, clean_distance);
    Domain::Polygons polygons = Polygon::to_polygons(paths);
    polygons.erase(
        std::remove_if(
            polygons.begin(),
            polygons.end(),
            [](const Domain::Polygon& p) { return p.size() < 3; }
        ),
        polygons.end()
    );

    if (polygons.empty())
        return {{}, false};

    // Do not remove all duplicates but do it better way
    // Overlap all duplicit points by rectangle 3x3
    Domain::Points duplicits = Point::collect_duplicates(Polygon::to_points(polygons));
    if (!duplicits.empty()) {
        polygons.reserve(polygons.size() + duplicits.size());
        for (const Domain::Point& p : duplicits) {
            Domain::Polygon rect_3x3(pts_3x3);
            rect_3x3.translate(p);
            polygons.push_back(rect_3x3);
        }
    }
    Domain::ExPolygons res = ClipperUtils::union_ex(polygons, fill_type);
    bool is_healed         = heal_expolygons(res, max_iteration);
    return {res, is_healed};
}

bool heal_expolygons(Domain::ExPolygons& shape, unsigned max_iteration)
{
    return ::heal_dupl_inter(shape, max_iteration);
}

bool divide_segments_for_close_point(Domain::ExPolygons& expolygons, double distance)
{
    if (expolygons.empty())
        return false;
    if (distance < 0.)
        return false;

    // Domain::ExPolygons can't contain same neigbours
    Algorithms::ExPolygon::remove_consecutive_duplicate_points(expolygons);

    // IMPROVE: use int(insted of double) lines and tree
    const Domain::ExPolygonsIndices ids(expolygons);
    const Domain::Line2ds lines = Algorithms::ExPolygon::to_linesf(expolygons, ids.get_count());
    AABBTreeIndirect::Tree<2, double>
        tree  = Algorithms::AABBTreeLines::build_aabb_tree_over_indexed_lines(lines);
    using Div = std::pair<Domain::Point, size_t>;
    std::vector<Div> divs;
    size_t point_index = 0;
    auto check_points =
        [&divs, &point_index, &lines, &tree, &distance, &ids, &expolygons](const Domain::Points& pts) {
            for (const Domain::Point& p : pts) {
                Domain::Vec2d p_d               = p.cast<double>();
                std::vector<size_t> close_lines = Algorithms::AABBTreeLines::all_lines_in_radius(
                    lines,
                    tree,
                    p_d,
                    distance
                );
                for (size_t index : close_lines) {
                    // skip point neighbour lines indices
                    if (index == point_index)
                        continue;
                    if (&p != &pts.front()) {
                        if (index == point_index - 1)
                            continue;
                    } else if (index == (pts.size() - 1))
                        continue;

                    // do not doubled side point of segment
                    const Domain::ExPolygonsIndex id = ids.cvt(index);
                    const Domain::ExPolygon& expoly  = expolygons[id.expolygons_index];
                    const Domain::Polygon& poly      = id.is_contour() ? expoly.contour :
                                                                         expoly.holes[id.hole_index()];
                    const Domain::Points& poly_pts   = poly.points;
                    const Domain::Point& line_a      = poly_pts[id.point_index];
                    const Domain::Point& line_b      = (!ids.is_last_point(id)) ?
                             poly_pts[id.point_index + 1] :
                             poly_pts.front();
                    assert(line_a == lines[index].a.cast<int>());
                    assert(line_b == lines[index].b.cast<int>());
                    if (p == line_a || p == line_b)
                        continue;

                    divs.emplace_back(p, index);
                }
                ++point_index;
            }
        };
    for (const Domain::ExPolygon& expoly : expolygons) {
        check_points(expoly.contour.points);
        for (const Domain::Polygon& hole : expoly.holes)
            check_points(hole.points);
    }

    // check if exist division
    if (divs.empty())
        return false;

    // sort from biggest index to zero
    // to be able add points and not interupt indices
    std::sort(divs.begin(), divs.end(), [](const Div& d1, const Div& d2) {
        return d1.second > d2.second;
    });

    auto it = divs.begin();
    // divide close line
    while (it != divs.end()) {
        // colect division of a line segmen
        size_t index = it->second;
        auto it2     = it + 1;
        while (it2 != divs.end() && it2->second == index)
            ++it2;

        Domain::ExPolygonsIndex id = ids.cvt(index);
        Domain::ExPolygon& expoly  = expolygons[id.expolygons_index];
        Domain::Polygon& poly = id.is_contour() ? expoly.contour : expoly.holes[id.hole_index()];
        Domain::Points& pts   = poly.points;
        size_t count          = it2 - it;

        // add points into polygon to divide in place of near point
        if (count == 1) {
            pts.insert(pts.begin() + id.point_index + 1, it->first);
            ++it;
        } else {
            // collect points to add into polygon
            Domain::Points points;
            points.reserve(count);
            for (; it < it2; ++it)
                points.push_back(it->first);

            // need sort by line direction
            const Domain::Line2d& line = lines[index];
            Domain::Vec2d dir          = line.b - line.a;
            // select mayorit direction
            int axis  = (abs(dir.x()) > abs(dir.y())) ? 0 : 1;
            using Fnc = std::function<bool(const Domain::Point&, const Domain::Point&)>;
            Fnc fnc   = (dir[axis] < 0) ?
                  Fnc([axis](const Domain::Point& p1, const Domain::Point& p2) {
                    return p1[axis] > p2[axis];
                }) :
                  Fnc([axis](const Domain::Point& p1, const Domain::Point& p2) {
                    return p1[axis] < p2[axis];
                });
            std::sort(points.begin(), points.end(), fnc);

            // use only unique points
            points.erase(std::unique(points.begin(), points.end()), points.end());

            // divide line by adding points into polygon
            pts.insert(pts.begin() + id.point_index + 1, points.begin(), points.end());
        }
        assert(it == it2);
    }
    return true;
}

} // namespace Slic3r::Biz::Algorithms::HealPolygon

namespace {

// bad is contour smaller than 3 points
void remove_bad(Domain::Polygons& polygons)
{
    polygons.erase(
        std::remove_if(
            polygons.begin(),
            polygons.end(),
            [](const Domain::Polygon& p) { return p.size() < 3; }
        ),
        polygons.end()
    );
}

void remove_bad(Domain::ExPolygons& expolygons)
{
    expolygons.erase(
        std::remove_if(
            expolygons.begin(),
            expolygons.end(),
            [](const Domain::ExPolygon& p) { return p.contour.size() < 3; }
        ),
        expolygons.end()
    );

    for (Domain::ExPolygon& expolygon : expolygons)
        remove_bad(expolygon.holes);
}

Domain::Points get_unique_intersections(const Biz::Algorithms::IntersectionsLines& intersections)
{
    Domain::Points result;
    if (intersections.empty())
        return result;

    // convert intersections into Points
    result.reserve(intersections.size());
    std::transform(
        intersections.begin(),
        intersections.end(),
        std::back_inserter(result),
        [](const Biz::Algorithms::IntersectionLines& i) {
            return Domain::Point(
                static_cast<Domain::coord_t>(std::floor(i.intersection.x())),
                static_cast<Domain::coord_t>(std::floor(i.intersection.y()))
            );
        }
    );
    // intersections should be unique poits
    std::sort(result.begin(), result.end());
    auto it = std::unique(result.begin(), result.end());
    result.erase(it, result.end());
    return result;
}

Domain::Polygons get_holes_with_points(const Domain::Polygons& holes, const Domain::Points& points)
{
    Domain::Polygons result;
    for (const Domain::Polygon& hole : holes)
        for (const Domain::Point& p : points)
            for (const Domain::Point& h : hole)
                if (p == h) {
                    result.push_back(hole);
                    break;
                }
    return result;
}

/// <summary>
/// Fill holes which create duplicits or intersections
/// When healing hole creates trouble in shape again try to heal by an union instead of diff_ex
/// </summary>
/// <param name="holes">Holes which was substracted from shape previous</param>
/// <param name="duplicates">Current duplicates in shape</param>
/// <param name="intersections">Current intersections in shape</param>
/// <param name="shape">Partialy healed shape[could be modified]</param>
/// <returns>True when modify shape otherwise False</returns>
bool fill_trouble_holes(
    const Domain::Polygons& holes,
    const Domain::Points& duplicates,
    const Domain::Points& intersections,
    Domain::ExPolygons& shape
)
{
    if (holes.empty())
        return false;
    if (duplicates.empty() && intersections.empty())
        return false;

    Domain::Polygons fill = get_holes_with_points(holes, duplicates);
    append(fill, get_holes_with_points(holes, intersections));
    if (fill.empty())
        return false;

    shape = Biz::Algorithms::ClipperUtils::union_ex(shape, fill);
    return true;
}

// extend functionality from Points.cpp --> collect_duplicates
// with address of duplicated points
struct Duplicate
{
    Domain::Point point;
    std::vector<uint32_t> indices;
};

using Duplicates = std::vector<Duplicate>;

Duplicates collect_duplicit_indices(const Domain::ExPolygons& expoly)
{
    Domain::Points pts = Biz::Algorithms::ExPolygon::to_points(expoly);

    // initialize original index locations
    std::vector<uint32_t> idx(pts.size());
    std::iota(idx.begin(), idx.end(), 0);
    std::sort(idx.begin(), idx.end(), [&pts](uint32_t i1, uint32_t i2) { return pts[i1] < pts[i2]; });

    Duplicates result;
    const Domain::Point* prev = &pts[idx.front()];
    for (size_t i = 1; i < idx.size(); ++i) {
        uint32_t index           = idx[i];
        const Domain::Point* act = &pts[index];
        if (*prev == *act) {
            // duplicit point
            if (!result.empty() && result.back().point == *act) {
                // more than 2 points with same coordinate
                result.back().indices.push_back(index);
            } else {
                uint32_t prev_index = idx[i - 1];
                result.push_back({*act, {prev_index, index}});
            }
            continue;
        }
        prev = act;
    }
    return result;
}

Domain::Points get_points(const Duplicates& duplicate_indices)
{
    Domain::Points result;
    if (duplicate_indices.empty())
        return result;

    // convert intersections into Points
    result.reserve(duplicate_indices.size());
    std::transform(
        duplicate_indices.begin(),
        duplicate_indices.end(),
        std::back_inserter(result),
        [](const Duplicate& d) { return d.point; }
    );
    return result;
}

Domain::ExPolygon create_bounding_rect(const Domain::ExPolygons& shape)
{
    Domain::BoundingBox2crd bb = Biz::Algorithms::ExPolygon::get_extents(shape);
    Domain::Point size         = Biz::Algorithms::BoundingBox::sizes(bb);
    if (size.x() < 10)
        bb.max.x() += 10;
    if (size.y() < 10)
        bb.max.y() += 10;

    Domain::Polygon rect(
        {// CCW
         bb.min,
         {bb.max.x(), bb.min.y()},
         bb.max,
         {bb.min.x(), bb.max.y()}
        }
    );

    Domain::Point offset = Biz::Algorithms::BoundingBox::sizes(bb) * 0.1;
    Domain::Polygon hole(
        {// CW
         bb.min + offset,
         {bb.min.x() + offset.x(), bb.max.y() - offset.y()},
         bb.max - offset,
         {bb.max.x() - offset.x(), bb.min.y() + offset.y()}
        }
    );

    return Domain::ExPolygon(rect, hole);
}

#ifdef REMOVE_SMALL_ISLANDS
void remove_small_islands(Domain::ExPolygons& expolygons, double minimal_area)
{
    if (expolygons.empty())
        return;

    // remove small expolygons contours
    auto expoly_it = std::remove_if(
        expolygons.begin(),
        expolygons.end(),
        [&minimal_area](const Domain::ExPolygon& p) { return p.contour.area() < minimal_area; }
    );
    expolygons.erase(expoly_it, expolygons.end());

    // remove small holes in expolygons
    for (Domain::ExPolygon& expoly : expolygons) {
        Domain::Polygons& holes = expoly.holes;
        auto it = std::remove_if(holes.begin(), holes.end(), [&minimal_area](const Domain::Polygon& p) {
            return -p.area() < minimal_area;
        });
        holes.erase(it, holes.end());
    }
}
#endif // REMOVE_SMALL_ISLANDS

void remove_spikes_in_duplicates(Domain::ExPolygons& expolygons, const Domain::Points& duplicates)
{
    if (duplicates.empty())
        return;
    auto check = [](Domain::Polygon& polygon, const Domain::Point& d) -> bool {
        double spike_bevel  = 1 / SHAPE_SCALE;
        double spike_length = 5.;
        const static SpikeDesc sd(spike_bevel, spike_length);
        Domain::Points& pts = polygon.points;
        bool exist_remove   = false;
        for (size_t i = 0; i < pts.size(); i++) {
            if (pts[i] != d)
                continue;
            exist_remove |= remove_when_spike(polygon, i, sd);
        }
        return exist_remove && pts.size() < 3;
    };

    bool exist_remove = false;
    for (Domain::ExPolygon& expolygon : expolygons) {
        Domain::BoundingBox2crd bb = Biz::Algorithms::BoundingBox::construct(
            Biz::Algorithms::Polygon::to_points(expolygon.contour)
        );
        for (const Domain::Point& d : duplicates) {
            if (!bb.contains(d))
                continue;
            exist_remove |= check(expolygon.contour, d);
            for (Domain::Polygon& hole : expolygon.holes)
                exist_remove |= check(hole, d);
        }
    }

    if (exist_remove)
        remove_bad(expolygons);
}

bool heal_dupl_inter(Domain::ExPolygons& shape, unsigned max_iteration)
{
    if (shape.empty())
        return true;
    Biz::Algorithms::ExPolygon::remove_consecutive_duplicate_points(shape);

    // create loop permanent memory
    Domain::Polygons holes;
    while (--max_iteration) {
        Duplicates duplicate_indices = collect_duplicit_indices(shape);
        // Points duplicates = collect_duplicates(to_points(shape));
        Biz::Algorithms::IntersectionsLines intersections = Biz::Algorithms::get_intersections(shape);

        // Check whether shape is already healed
        if (intersections.empty() && duplicate_indices.empty())
            return true;

        Domain::Points duplicate_points    = get_points(duplicate_indices);
        Domain::Points intersection_points = get_unique_intersections(intersections);

        if (fill_trouble_holes(holes, duplicate_points, intersection_points, shape)) {
            holes.clear();
            continue;
        }

        holes.clear();
        holes.reserve(intersections.size() + duplicate_points.size());

        remove_spikes_in_duplicates(shape, duplicate_points);

        // Fix self intersection in result by subtracting hole 2x2
        for (const Domain::Point& p : intersection_points) {
            Domain::Polygon hole(pts_2x2);
            hole.translate(p);
            holes.push_back(hole);
        }

        // Fix duplicit points by hole 3x3 around duplicit point
        for (const Domain::Point& p : duplicate_points) {
            Domain::Polygon hole(pts_3x3);
            hole.translate(p);
            holes.push_back(hole);
        }

        shape = Biz::Algorithms::ClipperUtils::diff_ex(
            shape,
            holes,
            Biz::Algorithms::ClipperUtils::ApplySafetyOffset::No
        );
        // ApplySafetyOffset::Yes is incompatible with function fill_trouble_holes
    }

    // Create partialy healed output
    Duplicates duplicates                             = collect_duplicit_indices(shape);
    Biz::Algorithms::IntersectionsLines intersections = Biz::Algorithms::get_intersections(shape);
    if (duplicates.empty() && intersections.empty()) {
        // healed in the last loop
        return true;
    }

#ifdef VISUALIZE_HEAL
    visualize_heal(visualize_heal_svg_filepath, shape);
#endif // VISUALIZE_HEAL

    assert(false); // Can not heal this shape
    // investigate how to heal better way

    Domain::ExPolygonsIndices ei(shape);
    std::vector<bool> is_healed(shape.size(), {true});
    for (const Duplicate& duplicate : duplicates) {
        for (uint32_t i : duplicate.indices)
            is_healed[ei.cvt(i).expolygons_index] = false;
    }
    for (const Biz::Algorithms::IntersectionLines& intersection : intersections) {
        is_healed[ei.cvt(intersection.line_index1).expolygons_index] = false;
        is_healed[ei.cvt(intersection.line_index2).expolygons_index] = false;
    }

    for (size_t shape_index = 0; shape_index < shape.size(); shape_index++) {
        if (!is_healed[shape_index]) {
            // exchange non healed expoly with bb rect
            Domain::ExPolygon& expoly = shape[shape_index];
            expoly                    = create_bounding_rect({expoly});
        }
    }

    // After insert bounding box unify and heal
    shape = Biz::Algorithms::ClipperUtils::union_ex(shape);
    heal_dupl_inter(shape, 1);
    return false;
}

} // namespace
