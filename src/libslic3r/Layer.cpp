///|/ Copyright (c) Prusa Research 2016 - 2023 Vojtěch Bubník @bubnikv, Pavel Mikuš @Godrak, Lukáš Hejl @hejllukas
///|/ Copyright (c) Slic3r 2014 - 2015 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2015 Maksim Derbasov @ntfshard
///|/
///|/ ported from lib/Slic3r/Layer.pm:
///|/ Copyright (c) Prusa Research 2016 - 2022 Vojtěch Bubník @bubnikv
///|/ Copyright (c) Slic3r 2011 - 2016 Alessandro Ranellucci @alranel
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "Layer.hpp"

#include <boost/log/trivial.hpp>
#include <clipper/clipper_z.hpp>
#include <cstdint>
#include <iterator>
#include <numeric>
#include <tuple>
#include <cassert>

#include "ClipperZUtils.hpp"
#include "ClipperUtils.hpp"
#include "Point.hpp"
#include "Polygon.hpp"
#include "Print.hpp"
#include "ShortestPath.hpp"
#include "SVG.hpp"
#include "BoundingBox.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/LayerRegion.hpp"
#include "libslic3r/PerimeterGenerator.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Surface.hpp"
#include "libslic3r/SurfaceCollection.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r {

Layer::~Layer()
{
    this->lower_layer = this->upper_layer = nullptr;
    for (LayerRegion *region : m_regions)
        delete region;
    m_regions.clear();
}

// Test whether whether there are any slices assigned to this layer.
bool Layer::empty() const
{
	for (const LayerRegion *layerm : m_regions)
        if (layerm != nullptr && ! layerm->slices().empty())
            // Non empty layer.
            return false;
    return true;
}

LayerRegion* Layer::add_region(const PrintRegion *print_region)
{
    m_regions.emplace_back(new LayerRegion(this, print_region));
    return m_regions.back();
}

// merge all regions' slices to get islands
void Layer::make_slices()
{
    {
        ExPolygons slices;
        if (m_regions.size() == 1) {
            // optimization: if we only have one region, take its slices
            slices = to_expolygons(m_regions.front()->slices().surfaces);
        } else {
            Polygons slices_p;
            for (LayerRegion *layerm : m_regions)
                polygons_append(slices_p, to_polygons(layerm->slices().surfaces));
            slices = union_safety_offset_ex(slices_p);
        }
        // lslices are sorted by topological order from outside to inside from the clipper union used above
        this->lslices = slices;
    }

    this->lslice_indices_sorted_by_print_order = chain_expolygons(this->lslices);
}

// used by Layer::build_up_down_graph()
// Shrink source polygons one by one, so that they will be separated if they were touching
// at vertices (non-manifold situation).
// Then convert them to Z-paths with Z coordinate indicating index of the source expolygon.
[[nodiscard]] static ClipperLib_Z::Paths expolygons_to_zpaths_shrunk(const ExPolygons &expolygons, coord_t isrc)
{
    size_t num_paths = 0;
    for (const ExPolygon &expolygon : expolygons)
        num_paths += expolygon.num_contours();

    ClipperLib_Z::Paths out;
    out.reserve(num_paths);

    ClipperLib::Paths           contours;
    ClipperLib::Paths           holes;
    ClipperLib::Clipper         clipper;
    ClipperLib::ClipperOffset   co;
    ClipperLib::Paths           out2;

    // Top / bottom surfaces must overlap more than 2um to be chained into a Z graph.
    // Also a larger offset will likely be more robust on non-manifold input polygons.
    static constexpr const float delta = scaled<float>(0.001);
    // Don't scale the miter limit, it is a factor, not an absolute length!
    co.MiterLimit = 3.;
// Use the default zero edge merging distance. For this kind of safety offset the accuracy of normal direction is not important.
//    co.ShortestEdgeLength = delta * ClipperOffsetShortestEdgeFactor;
//    static constexpr const double accept_area_threshold_ccw = sqr(scaled<double>(0.1 * delta));
    // Such a small hole should not survive the shrinkage, it should grow over 
//    static constexpr const double accept_area_threshold_cw  = sqr(scaled<double>(0.2 * delta));

    for (const ExPolygon &expoly : expolygons) {
        contours.clear();
        co.Clear();
        co.AddPath(expoly.contour.points, ClipperLib::jtMiter, ClipperLib::etClosedPolygon);
        co.Execute(contours, - delta);
//        size_t num_prev = out.size();
        if (! contours.empty()) {
            holes.clear();
            for (const Polygon &hole : expoly.holes) {
                co.Clear();
                co.AddPath(hole.points, ClipperLib::jtMiter, ClipperLib::etClosedPolygon);
                // Execute reorients the contours so that the outer most contour has a positive area. Thus the output
                // contours will be CCW oriented even though the input paths are CW oriented.
                // Offset is applied after contour reorientation, thus the signum of the offset value is reversed.
                out2.clear();
                co.Execute(out2, delta);
                append(holes, std::move(out2));
            }
            // Subtract holes from the contours.
            if (! holes.empty()) {
                clipper.Clear();
                clipper.AddPaths(contours, ClipperLib::ptSubject, true);
                clipper.AddPaths(holes, ClipperLib::ptClip, true);
                contours.clear();
                clipper.Execute(ClipperLib::ctDifference, contours, ClipperLib::pftNonZero, ClipperLib::pftNonZero);
            }
            for (const auto &contour : contours) {
                bool accept = true;
                // Trying to get rid of offset artifacts, that may be created due to numerical issues in offsetting algorithm
                // or due to self-intersections in the source polygons.
                //FIXME how reliable is it? Is it helpful or harmful? It seems to do more harm than good as it tends to punch holes
                // into existing ExPolygons.
#if 0
                if (contour.size() < 8) {
                    // Only accept contours with area bigger than some threshold.
                    double a = ClipperLib::Area(contour);
                    // Polygon has to be bigger than some threshold to be accepted.
                    // Hole to be accepted has to have an area slightly bigger than the non-hole, so it will not happen due to rounding errors,
                    // that a hole will be accepted without its outer contour.
                    accept = a > 0 ? a > accept_area_threshold_ccw : a < - accept_area_threshold_cw;
                }
#endif
                if (accept) {
                    out.emplace_back();
                    ClipperLib_Z::Path &path = out.back();
                    path.reserve(contour.size());
                    for (const Point &p : contour)
                        path.push_back({ p.x(), p.y(), isrc });
                }
            }
        }
#if 0 // #ifndef NDEBUG
        // Test whether the expolygons in a single layer overlap.
        Polygons test;
        for (size_t i = num_prev; i < out.size(); ++ i)
            test.emplace_back(ClipperZUtils::from_zpath(out[i]));
        Polygons outside = diff(test, to_polygons(expoly));
        if (! outside.empty()) {
            BoundingBox bbox(get_extents(expoly));
            bbox.merge(get_extents(test));
            SVG svg(debug_out_path("expolygons_to_zpaths_shrunk-self-intersections.svg").c_str(), bbox);
            svg.draw(expoly, "blue");
            svg.draw(test, "green");
            svg.draw(outside, "red");
        }
        assert(outside.empty());
#endif // NDEBUG
        ++ isrc;
    }

    return out;
}

// used by Layer::build_up_down_graph()
static void connect_layer_slices(
    Layer                                           &below,
    Layer                                           &above,
    const ClipperLib_Z::PolyTree                    &polytree,
    const std::vector<std::pair<coord_t, coord_t>>  &intersections,
    const coord_t                                    offset_below,
    const coord_t                                    offset_above
#ifndef NDEBUG
    , const coord_t                                  offset_end
#endif // NDEBUG
    )
{
    class Visitor {
    public:
        Visitor(const std::vector<std::pair<coord_t, coord_t>> &intersections, 
            Layer &below, Layer &above, const coord_t offset_below, const coord_t offset_above
#ifndef NDEBUG
            , const coord_t offset_end
#endif // NDEBUG
            ) :
            m_intersections(intersections), m_below(below), m_above(above), m_offset_below(offset_below), m_offset_above(offset_above)
#ifndef NDEBUG
            , m_offset_end(offset_end) 
#endif // NDEBUG
            {}

        void visit(const ClipperLib_Z::PolyNode &polynode)
        {
#ifndef NDEBUG
            auto assert_intersection_valid = [this](int i, int j) {
                assert(i < j);
                assert(i >= m_offset_below);
                assert(i < m_offset_above);
                assert(j >= m_offset_above);
                assert(j < m_offset_end);
                return true;
            };
#endif // NDEBUG
            if (polynode.Contour.size() >= 3) {
                // If there is an intersection point, it should indicate which contours (one from layer below, the other from layer above) intersect.
                // Otherwise the contour is fully inside another contour.
                auto [i, j] = this->find_top_bottom_contour_ids_strict(polynode);
                bool found = false;
                if (i < 0 && j < 0) {
                    // This should not happen. It may only happen if the source contours had just self intersections or intersections with contours at the same layer.
                    // We may safely ignore such cases where the intersection area is meager.
                    double a = ClipperLib_Z::Area(polynode.Contour);
                    if (a < sqr(scaled<double>(0.001))) {
                        // Ignore tiny overlaps. They are not worth resolving.
                    } else {
                        // We should not ignore large cases. Try to resolve the conflict by a majority of references.
                        std::tie(i, j) = this->find_top_bottom_contour_ids_approx(polynode);
                        // At least top or bottom should be resolved.
                        assert(i >= 0 || j >= 0);
                    }
                }
                if (j < 0) {
                    if (i < 0) {
                        // this->find_top_bottom_contour_ids_approx() shoudl have made sure this does not happen.
                        assert(false);
                    } else {
                        assert(i >= m_offset_below && i < m_offset_above);
                        i -= m_offset_below;
                        j = this->find_other_contour_costly(polynode, m_above, j == -2);
                        found = j >= 0;
                    }
                } else if (i < 0) {
                    assert(j >= m_offset_above && j < m_offset_end);
                    j -= m_offset_above;
                    i = this->find_other_contour_costly(polynode, m_below, i == -2);
                    found = i >= 0;
                } else {
                    assert(assert_intersection_valid(i, j));
                    i -= m_offset_below;
                    j -= m_offset_above;
                    assert(i >= 0 && i < m_below.lslices_ex.size());
                    assert(j >= 0 && j < m_above.lslices_ex.size());
                    found = true;
                }
                if (found) {
                    assert(i >= 0 && i < m_below.lslices_ex.size());
                    assert(j >= 0 && j < m_above.lslices_ex.size());
                    // Subtract area of holes from the area of outer contour.
                    double area = ClipperLib_Z::Area(polynode.Contour);
                    for (int icontour = 0; icontour < polynode.ChildCount(); ++ icontour)
                        area -= ClipperLib_Z::Area(polynode.Childs[icontour]->Contour);
                    // Store the links and area into the contours.
                    LayerSlice::Links &links_below = m_below.lslices_ex[i].overlaps_above;
                    LayerSlice::Links &links_above = m_above.lslices_ex[j].overlaps_below;
                    LayerSlice::Link key{ j };
                    auto it_below = std::lower_bound(links_below.begin(), links_below.end(), key, [](auto &l, auto &r){ return l.slice_idx < r.slice_idx; });
                    if (it_below != links_below.end() && it_below->slice_idx == j) {
                        it_below->area += area;
                    } else {
                        auto it_above = std::lower_bound(links_above.begin(), links_above.end(), key, [](auto &l, auto &r){ return l.slice_idx < r.slice_idx; });
                        if (it_above != links_above.end() && it_above->slice_idx == i) {
                            it_above->area += area;
                        } else {
                            // Insert into one of the two vectors.
                            bool take_below = false;
                            if (links_below.size() < LayerSlice::LinksStaticSize)
                                take_below = false;
                            else if (links_above.size() >= LayerSlice::LinksStaticSize) {
                                size_t shift_below = links_below.end() - it_below;
                                size_t shift_above = links_above.end() - it_above;
                                take_below = shift_below < shift_above;
                            }
                            if (take_below)
                                links_below.insert(it_below, { j, float(area) });
                            else
                                links_above.insert(it_above, { i, float(area) });
                        }
                    }
                }
            }
            for (int i = 0; i < polynode.ChildCount(); ++ i)
                for (int j = 0; j < polynode.Childs[i]->ChildCount(); ++ j)
                    this->visit(*polynode.Childs[i]->Childs[j]);
        }

    private:
        // Find the indices of the contour below & above for an expolygon created as an intersection of two expolygons, one below, the other above.
        // Returns -1 if there is no point on the intersection refering bottom resp. top source expolygon.
        // Returns -2 if the intersection refers to multiple source expolygons on bottom resp. top layers.
        std::pair<int32_t, int32_t> find_top_bottom_contour_ids_strict(const ClipperLib_Z::PolyNode &polynode) const
        {
            // If there is an intersection point, it should indicate which contours (one from layer below, the other from layer above) intersect.
            // Otherwise the contour is fully inside another contour.
            int32_t i = -1, j = -1;
            auto process_i = [&i, &j](coord_t k) {
                if (i == -1)
                    i = k;
                else if (i >= 0) {
                    if (i != k) {
                        // Error: Intersection contour contains points of two or more source bottom contours.
                        i = -2;
                        if (j == -2)
                            // break
                            return true;
                    }
                } else
                    assert(i == -2);
                return false;
            };
            auto process_j = [&i, &j](coord_t k) {
                if (j == -1)
                    j = k;
                else if (j >= 0) {
                    if (j != k) {
                        // Error: Intersection contour contains points of two or more source top contours.
                        j = -2;
                        if (i == -2)
                            // break
                            return true;
                    }
                } else
                    assert(j == -2);
                return false;
            };
            for (int icontour = 0; icontour <= polynode.ChildCount(); ++ icontour) {
                const ClipperLib_Z::Path &contour = icontour == 0 ? polynode.Contour : polynode.Childs[icontour - 1]->Contour;
                if (contour.size() >= 3) {
                    for (const ClipperLib_Z::IntPoint &pt : contour)
                        if (coord_t k = pt.z(); k < 0) {
                            const auto &intersection = m_intersections[-k - 1];
                            assert(intersection.first <= intersection.second);
                            if (intersection.first < m_offset_above ? process_i(intersection.first) : process_j(intersection.first))
                                goto end;
                            if (intersection.second < m_offset_above ? process_i(intersection.second) : process_j(intersection.second))
                                goto end;
                        } else if (k < m_offset_above ? process_i(k) : process_j(k))
                            goto end;
                }
            }
        end:
            return { i, j };
        }

        // Find the indices of the contour below & above for an expolygon created as an intersection of two expolygons, one below, the other above.
        // This variant expects that the source expolygon assingment is not unique, it counts the majority.
        // Returns -1 if there is no point on the intersection refering bottom resp. top source expolygon.
        // Returns -2 if the intersection refers to multiple source expolygons on bottom resp. top layers.
        std::pair<int32_t, int32_t> find_top_bottom_contour_ids_approx(const ClipperLib_Z::PolyNode &polynode) const
        {
            // 1) Collect histogram of contour references.
            struct HistoEl {
                int32_t id;
                int32_t count;
            };
            std::vector<HistoEl> histogram;
            {
                auto increment_counter = [&histogram](const int32_t i) {
                    auto it = std::lower_bound(histogram.begin(), histogram.end(), i, [](auto l, auto r){ return l.id < r; });
                    if (it == histogram.end() || it->id != i)
                        histogram.insert(it, HistoEl{ i, int32_t(1) });
                    else
                        ++ it->count;
                };
                for (int icontour = 0; icontour <= polynode.ChildCount(); ++ icontour) {
                    const ClipperLib_Z::Path &contour = icontour == 0 ? polynode.Contour : polynode.Childs[icontour - 1]->Contour;
                    if (contour.size() >= 3) {
                        for (const ClipperLib_Z::IntPoint &pt : contour)
                            if (coord_t k = pt.z(); k < 0) {
                                const auto &intersection = m_intersections[-k - 1];
                                assert(intersection.first <= intersection.second);
                                increment_counter(intersection.first);
                                increment_counter(intersection.second);
                            } else
                                increment_counter(k);
                    }
                }
                assert(! histogram.empty());
            }
            int32_t i = -1;
            int32_t j = -1;
            if (! histogram.empty()) {
                // 2) Split the histogram to bottom / top.
                auto mid          = std::upper_bound(histogram.begin(), histogram.end(), m_offset_above, [](auto l, auto r){ return l < r.id; });
                // 3) Sort the bottom / top parts separately.
                auto bottom_begin = histogram.begin();
                auto bottom_end   = mid;
                auto top_begin    = mid;
                auto top_end      = histogram.end();
                std::sort(bottom_begin, bottom_end, [](auto l, auto r) { return l.count > r.count; });
                std::sort(top_begin,    top_end,    [](auto l, auto r) { return l.count > r.count; });
                double i_quality = 0;
                double j_quality = 0;
                if (bottom_begin != bottom_end) {
                    i = bottom_begin->id;
                    i_quality = std::next(bottom_begin) == bottom_end ? std::numeric_limits<double>::max() : double(bottom_begin->count) / std::next(bottom_begin)->count;
                }
                if (top_begin != top_end) {
                    j = top_begin->id;
                    j_quality = std::next(top_begin) == top_end ? std::numeric_limits<double>::max() : double(top_begin->count) / std::next(top_begin)->count;
                }
                // Expected to be called only if there are duplicate references to be resolved by the histogram.
                assert(i >= 0 || j >= 0);
                assert(i_quality < std::numeric_limits<double>::max() || j_quality < std::numeric_limits<double>::max());
                if (i >= 0 && i_quality < j_quality) {
                    // Force the caller to resolve the bottom references the costly but robust way.
                    assert(j >= 0);
                    // Twice the number of references for the best contour.
                    assert(j_quality >= 2.);
                    i = -2;
                } else if (j >= 0) {
                    // Force the caller to resolve the top reference the costly but robust way.
                    assert(i >= 0);
                    // Twice the number of references for the best contour.
                    assert(i_quality >= 2.);
                    j = -2;
                }

            }
            return { i, j };
        }

        static int32_t find_other_contour_costly(const ClipperLib_Z::PolyNode &polynode, const Layer &other_layer, bool other_has_duplicates)
        {
            if (! other_has_duplicates) {
                // The contour below is likely completely inside another contour above. Look-it up in the island above.
                Point pt(polynode.Contour.front().x(), polynode.Contour.front().y());
                for (int i = int(other_layer.lslices_ex.size()) - 1; i >= 0; -- i)
                    if (other_layer.lslices_ex[i].bbox.contains(pt) && other_layer.lslices[i].contains(pt))
                        return i;
                // The following shall not happen now as the source expolygons are being shrunk a bit before intersecting,
                // thus each point of each intersection polygon should fit completely inside one of the original (unshrunk) expolygons.
                assert(false);
            }
            // The comment below may not be valid anymore, see the comment above. However the code is used in case the polynode contains multiple references 
            // to other_layer expolygons, thus the references are not unique.
            //
            // The check above might sometimes fail when the polygons overlap only on points, which causes the clipper to detect no intersection.
            // The problem happens rarely, mostly on simple polygons (in terms of number of points), but regardless of size!
            // example of failing link on two layers, each with single polygon without holes.
            // layer A = Polygon{(-24931238,-11153865),(-22504249,-8726874),(-22504249,11477151),(-23261469,12235585),(-23752371,12727276),(-25002495,12727276),(-27502745,10227026),(-27502745,-12727274),(-26504645,-12727274)}
            // layer B = Polygon{(-24877897,-11100524),(-22504249,-8726874),(-22504249,11477151),(-23244827,12218916),(-23752371,12727276),(-25002495,12727276),(-27502745,10227026),(-27502745,-12727274),(-26504645,-12727274)}
            // note that first point is not identical, and the check above picks (-24877897,-11100524) as the first contour point (polynode.Contour.front()).
            // that point is sadly slightly outisde of the layer A, so no link is detected, eventhough they are overlaping "completely"
            Polygons contour_poly{ Polygon{ClipperZUtils::from_zpath(polynode.Contour)} };
            BoundingBox contour_aabb{contour_poly.front().points};
            int32_t i_largest = -1;
            double  a_largest = 0;
            for (int i = int(other_layer.lslices_ex.size()) - 1; i >= 0; -- i)
                if (contour_aabb.overlap(other_layer.lslices_ex[i].bbox))
                    // it is potentially slow, but should be executed rarely
                    if (Polygons overlap = intersection(contour_poly, other_layer.lslices[i]); ! overlap.empty()) {
                        if (other_has_duplicates) {
                            // Find the contour with the largest overlap. It is expected that the other overlap will be very small.
                            double a = area(overlap);
                            if (a > a_largest) {
                                a_largest = a;
                                i_largest = i;
                            }
                        } else {
                            // Most likely there is just one contour that overlaps, however it is not guaranteed.
                            i_largest = i;
                            break;
                        }
                    }
            assert(i_largest >= 0);
            return i_largest;
        }

        const std::vector<std::pair<coord_t, coord_t>> &m_intersections;
        Layer                                          &m_below;
        Layer                                          &m_above;
        const coord_t                                   m_offset_below;
        const coord_t                                   m_offset_above;
#ifndef NDEBUG
        const coord_t                                   m_offset_end;
#endif // NDEBUG
    } visitor(intersections, below, above, offset_below, offset_above
#ifndef NDEBUG
        , offset_end
#endif // NDEBUG
    );

    for (int i = 0; i < polytree.ChildCount(); ++ i)
        visitor.visit(*polytree.Childs[i]);

#ifndef NDEBUG
    // Verify that only one directional link is stored: either from bottom slice up or from upper slice down.
    for (int32_t islice = 0; islice < below.lslices_ex.size(); ++ islice) {
        LayerSlice::Links &links1 = below.lslices_ex[islice].overlaps_above;
        for (LayerSlice::Link &link1 : links1) {
            LayerSlice::Links &links2 = above.lslices_ex[link1.slice_idx].overlaps_below;
            assert(! std::binary_search(links2.begin(), links2.end(), link1, [](auto &l, auto &r){ return l.slice_idx < r.slice_idx; }));
        }
    }
    for (int32_t islice = 0; islice < above.lslices_ex.size(); ++ islice) {
        LayerSlice::Links &links1 = above.lslices_ex[islice].overlaps_below;
        for (LayerSlice::Link &link1 : links1) {
            LayerSlice::Links &links2 = below.lslices_ex[link1.slice_idx].overlaps_above;
            assert(! std::binary_search(links2.begin(), links2.end(), link1, [](auto &l, auto &r){ return l.slice_idx < r.slice_idx; }));
        }
    }
#endif // NDEBUG

    // Scatter the links, but don't sort them yet.
    for (int32_t islice = 0; islice < int32_t(below.lslices_ex.size()); ++ islice)
        for (LayerSlice::Link &link : below.lslices_ex[islice].overlaps_above)
            above.lslices_ex[link.slice_idx].overlaps_below.push_back({ islice, link.area });
    for (int32_t islice = 0; islice < int32_t(above.lslices_ex.size()); ++ islice)
        for (LayerSlice::Link &link : above.lslices_ex[islice].overlaps_below)
            below.lslices_ex[link.slice_idx].overlaps_above.push_back({ islice, link.area });
    // Sort the links.
    for (LayerSlice &lslice : below.lslices_ex)
        std::sort(lslice.overlaps_above.begin(), lslice.overlaps_above.end(), [](const LayerSlice::Link &l, const LayerSlice::Link &r){ return l.slice_idx < r.slice_idx; });
    for (LayerSlice &lslice : above.lslices_ex)
        std::sort(lslice.overlaps_below.begin(), lslice.overlaps_below.end(), [](const LayerSlice::Link &l, const LayerSlice::Link &r){ return l.slice_idx < r.slice_idx; });
}

void Layer::build_up_down_graph(Layer& below, Layer& above)
{
    coord_t             paths_below_offset = 0;
    ClipperLib_Z::Paths paths_below = expolygons_to_zpaths_shrunk(below.lslices, paths_below_offset);
    coord_t             paths_above_offset = paths_below_offset + coord_t(below.lslices.size());
    ClipperLib_Z::Paths paths_above = expolygons_to_zpaths_shrunk(above.lslices, paths_above_offset);
#ifndef NDEBUG
    coord_t             paths_end = paths_above_offset + coord_t(above.lslices.size());
#endif // NDEBUG

    ClipperLib_Z::Clipper  clipper;
    ClipperLib_Z::PolyTree result;
    ClipperZUtils::ClipperZIntersectionVisitor::Intersections intersections;
    ClipperZUtils::ClipperZIntersectionVisitor visitor(intersections);
    clipper.ZFillFunction(visitor.clipper_callback());
    clipper.AddPaths(paths_below, ClipperLib_Z::ptSubject, true);
    clipper.AddPaths(paths_above, ClipperLib_Z::ptClip, true);
    clipper.Execute(ClipperLib_Z::ctIntersection, result, ClipperLib_Z::pftNonZero, ClipperLib_Z::pftNonZero);

    connect_layer_slices(below, above, result, intersections, paths_below_offset, paths_above_offset
#ifndef NDEBUG
        , paths_end
#endif // NDEBUG
        );
}

static inline bool layer_needs_raw_backup(const Layer *layer)
{
    return ! (layer->regions().size() == 1 && (layer->id() > 0 || layer->object()->config().elefant_foot_compensation.value == 0));
}

void Layer::backup_untyped_slices()
{
    if (layer_needs_raw_backup(this)) {
        for (LayerRegion *layerm : m_regions)
            layerm->m_raw_slices = to_expolygons(layerm->slices().surfaces);
    } else {
        assert(m_regions.size() == 1);
        m_regions.front()->m_raw_slices.clear();
    }
}

void Layer::restore_untyped_slices()
{
    if (layer_needs_raw_backup(this)) {
        for (LayerRegion *layerm : m_regions)
            layerm->m_slices.set(layerm->m_raw_slices, stInternal);
    } else {
        assert(m_regions.size() == 1);
        m_regions.front()->m_slices.set(this->lslices, stInternal);
    }
}

// Similar to Layer::restore_untyped_slices()
// To improve robustness of detect_surfaces_type() when reslicing (working with typed slices), see GH issue #7442.
// Only resetting layerm->slices if Slice::extra_perimeters is always zero or it will not be used anymore
// after the perimeter generator.
void Layer::restore_untyped_slices_no_extra_perimeters()
{
    if (layer_needs_raw_backup(this)) {
        for (LayerRegion *layerm : m_regions)
        	if (! layerm->region().config().extra_perimeters.value)
            	layerm->m_slices.set(layerm->m_raw_slices, stInternal);
    } else {
    	assert(m_regions.size() == 1);
    	LayerRegion *layerm = m_regions.front();
    	// This optimization is correct, as extra_perimeters are only reused by prepare_infill() with multi-regions.
        //if (! layerm->region().config().extra_perimeters.value)
        	layerm->m_slices.set(this->lslices, stInternal);
    }
}

ExPolygons Layer::merged(float offset_scaled) const
{
	assert(offset_scaled >= 0.f);
    // If no offset is set, apply EPSILON offset before union, and revert it afterwards.
	float offset_scaled2 = 0;
	if (offset_scaled == 0.f) {
		offset_scaled  = float(  EPSILON);
		offset_scaled2 = float(- EPSILON);
    }
    Polygons polygons;
	for (LayerRegion *layerm : m_regions) {
		const PrintRegionConfig &config = layerm->region().config();
		// Our users learned to bend Slic3r to produce empty volumes to act as subtracters. Only add the region if it is non-empty.
		if (config.bottom_solid_layers > 0 || config.top_solid_layers > 0 || config.fill_density > 0. || config.perimeters > 0)
			append(polygons, offset(layerm->slices().surfaces, offset_scaled));
	}
    ExPolygons out = union_ex(polygons);
	if (offset_scaled2 != 0.f)
		out = offset_ex(out, offset_scaled2);
    return out;
}

// If there is any incompatibility, separate LayerRegions have to be created.
inline bool has_compatible_dynamic_overhang_speed(const PrintRegionConfig &config, const PrintRegionConfig &other_config)
{
    bool dynamic_overhang_speed_compatibility = config.enable_dynamic_overhang_speeds == other_config.enable_dynamic_overhang_speeds;
    if (dynamic_overhang_speed_compatibility && config.enable_dynamic_overhang_speeds) {
        dynamic_overhang_speed_compatibility = config.overhang_speed_0 == other_config.overhang_speed_0 &&
                                               config.overhang_speed_1 == other_config.overhang_speed_1 &&
                                               config.overhang_speed_2 == other_config.overhang_speed_2 &&
                                               config.overhang_speed_3 == other_config.overhang_speed_3;
    }

    return dynamic_overhang_speed_compatibility;
}

// If there is any incompatibility, separate LayerRegions have to be created.
inline bool has_compatible_layer_regions(const PrintRegionConfig &config, const PrintRegionConfig &other_config)
{
    return config.perimeter_extruder                                    == other_config.perimeter_extruder &&
           config.perimeters                                            == other_config.perimeters &&
           config.perimeter_speed                                       == other_config.perimeter_speed &&
           config.external_perimeter_speed                              == other_config.external_perimeter_speed &&
           (config.gap_fill_enabled ? config.gap_fill_speed.value : 0.) == (other_config.gap_fill_enabled ? other_config.gap_fill_speed.value : 0.) &&
           config.overhangs                                             == other_config.overhangs &&
           config.opt_serialize("perimeter_extrusion_width")     == other_config.opt_serialize("perimeter_extrusion_width") &&
           config.thin_walls                                            == other_config.thin_walls &&
           config.external_perimeters_first                             == other_config.external_perimeters_first &&
           config.infill_overlap                                        == other_config.infill_overlap &&
           has_compatible_dynamic_overhang_speed(config, other_config);
}

// Here the perimeters are created cummulatively for all layer regions sharing the same parameters influencing the perimeters.
// The perimeter paths and the thin fills (ExtrusionEntityCollection) are assigned to the first compatible layer region.
// The resulting fill surface is split back among the originating regions.
void Layer::make_perimeters()
{
    BOOST_LOG_TRIVIAL(trace) << "Generating perimeters for layer " << this->id();
    
    // keep track of regions whose perimeters we have already generated
    std::vector<unsigned char>                              done(m_regions.size(), false);
    std::vector<uint32_t>                                   layer_region_ids;
    std::vector<std::pair<ExtrusionRange, ExtrusionRange>>  perimeter_and_gapfill_ranges;
    ExPolygons                                              fill_expolygons;
    std::vector<ExPolygonRange>                             fill_expolygons_ranges;
    SurfacesPtr                                             surfaces_to_merge;
    SurfacesPtr                                             surfaces_to_merge_temp;

    auto layer_region_reset_perimeters = [](LayerRegion &layerm) {
        layerm.m_perimeters.clear();
        layerm.m_fills.clear();
        layerm.m_thin_fills.clear();
        layerm.m_fill_expolygons.clear();
        layerm.m_fill_expolygons_bboxes.clear();
        layerm.m_fill_expolygons_composite.clear();
        layerm.m_fill_expolygons_composite_bboxes.clear();
    };

    // Remove layer islands, remove references to perimeters and fills from these layer islands to LayerRegion ExtrusionEntities.
    for (LayerSlice &lslice : this->lslices_ex)
        lslice.islands.clear();

    for (auto it_curr_region = m_regions.cbegin(); it_curr_region != m_regions.cend(); ++it_curr_region) {
        const size_t curr_region_id = std::distance(m_regions.cbegin(), it_curr_region);
        if (done[curr_region_id]) {
            continue;
        }

        LayerRegion &curr_region = **it_curr_region;
        layer_region_reset_perimeters(curr_region);
        if (curr_region.slices().empty()) {
            continue;
        }

        BOOST_LOG_TRIVIAL(trace) << "Generating perimeters for layer " << this->id() << ", region " << curr_region_id;
        done[curr_region_id]                 = true;
        const PrintRegionConfig &curr_config = curr_region.region().config();

        perimeter_and_gapfill_ranges.clear();
        fill_expolygons.clear();
        fill_expolygons_ranges.clear();
        surfaces_to_merge.clear();

        // Find compatible regions.
        layer_region_ids.clear();
        layer_region_ids.push_back(curr_region_id);

        PerimeterRegions perimeter_regions;
        for (auto it_next_region = std::next(it_curr_region); it_next_region != m_regions.cend(); ++it_next_region) {
            const size_t             next_region_id = std::distance(m_regions.cbegin(), it_next_region);
            LayerRegion             &next_region    = **it_next_region;
            const PrintRegionConfig &next_config    = next_region.region().config();
            if (next_region.slices().empty()) {
                continue;
            }

            if (!has_compatible_layer_regions(curr_config, next_config)) {
                continue;
            }

            // Now, we are sure that we want to merge LayerRegions in any case.
            layer_region_reset_perimeters(next_region);
            layer_region_ids.push_back(next_region_id);
            done[next_region_id] = true;

            // If any parameters affecting just perimeters are incompatible, then we also create PerimeterRegion.
            if (!PerimeterRegion::has_compatible_perimeter_regions(curr_config, next_config)) {
                perimeter_regions.emplace_back(next_region);
            }
        }

        if (layer_region_ids.size() == 1) { // Optimization.
            curr_region.make_perimeters(curr_region.slices(), perimeter_regions, perimeter_and_gapfill_ranges, fill_expolygons, fill_expolygons_ranges);
            this->sort_perimeters_into_islands(curr_region.slices(), curr_region_id, perimeter_and_gapfill_ranges, std::move(fill_expolygons), fill_expolygons_ranges, layer_region_ids);
        } else {
            SurfaceCollection new_slices;
            // Use the region with highest infill rate, as the make_perimeters() function below decides on the gap fill based on the infill existence.
            uint32_t     region_id_config = layer_region_ids.front();
            LayerRegion *layerm_config    = m_regions[region_id_config];
            {
                // Merge slices (surfaces) according to number of extra perimeters.
                for (uint32_t region_id : layer_region_ids) {
                    LayerRegion &layerm = *m_regions[region_id];
                    for (const Surface &surface : layerm.slices())
                        surfaces_to_merge.emplace_back(&surface);
                    if (layerm.region().config().fill_density > layerm_config->region().config().fill_density) {
                        region_id_config = region_id;
                        layerm_config    = &layerm;
                    }
                }

                std::sort(surfaces_to_merge.begin(), surfaces_to_merge.end(), [](const Surface *l, const Surface *r) { return l->extra_perimeters < r->extra_perimeters; });
                for (size_t i = 0; i < surfaces_to_merge.size();) {
                    size_t         j                = i;
                    const Surface &first            = *surfaces_to_merge[i];
                    size_t         extra_perimeters = first.extra_perimeters;
                    for (; j < surfaces_to_merge.size() && surfaces_to_merge[j]->extra_perimeters == extra_perimeters; ++j);

                    if (i + 1 == j) {
                        // Nothing to merge, just copy.
                        new_slices.surfaces.emplace_back(*surfaces_to_merge[i]);
                    } else {
                        surfaces_to_merge_temp.assign(surfaces_to_merge.begin() + i, surfaces_to_merge.begin() + j);
                        new_slices.append(offset_ex(surfaces_to_merge_temp, ClipperSafetyOffset), first);
                    }

                    i = j;
                }
            }

            // Try to merge compatible PerimeterRegions.
            if (perimeter_regions.size() > 1) {
                PerimeterRegion::merge_compatible_perimeter_regions(perimeter_regions);
            }

            // Make perimeters.
            layerm_config->make_perimeters(new_slices, perimeter_regions, perimeter_and_gapfill_ranges, fill_expolygons, fill_expolygons_ranges);
            this->sort_perimeters_into_islands(new_slices, region_id_config, perimeter_and_gapfill_ranges, std::move(fill_expolygons), fill_expolygons_ranges, layer_region_ids);
        }
    }

    BOOST_LOG_TRIVIAL(trace) << "Generating perimeters for layer " << this->id() << " - Done";
}

void Layer::sort_perimeters_into_islands(
    // Slices for which perimeters and fill_expolygons were just created.
    // The slices may have been created by merging multiple source slices with the same perimeter parameters.
    const SurfaceCollection                                         &slices,
    // Region where the perimeters, gap fills and fill expolygons are stored.
    const uint32_t                                                   region_id,
    // Perimeters and gap fills produced by the perimeter generator for the slices,
    // sorted by the source slices.
    const std::vector<std::pair<ExtrusionRange, ExtrusionRange>>    &perimeter_and_gapfill_ranges,
    // Fill expolygons produced for all source slices above.
    ExPolygons                                                      &&fill_expolygons,
    // Fill expolygon ranges sorted by the source slices.
    const std::vector<ExPolygonRange>                               &fill_expolygons_ranges,
    // If the current layer consists of multiple regions, then the fill_expolygons above are split by the source LayerRegion surfaces.
    const std::vector<uint32_t>                                     &layer_region_ids)
{
    assert(perimeter_and_gapfill_ranges.size() == fill_expolygons_ranges.size());
    assert(! layer_region_ids.empty());

    LayerRegion &this_layer_region = *m_regions[region_id];

    // Bounding boxes of fill_expolygons.
    BoundingBoxes fill_expolygons_bboxes;
    fill_expolygons_bboxes.reserve(fill_expolygons.size());
    for (const ExPolygon &expolygon : fill_expolygons)
        fill_expolygons_bboxes.emplace_back(get_extents(expolygon));

    // Take one sample point for each source slice, to be used to sort source slices into layer slices.
    // source slice index + its sample.
    std::vector<std::pair<uint32_t, Point>> perimeter_slices_queue;
    perimeter_slices_queue.reserve(slices.size());
    for (uint32_t islice = 0; islice < uint32_t(slices.size()); ++ islice) {
        const std::pair<ExtrusionRange, ExtrusionRange> &extrusions = perimeter_and_gapfill_ranges[islice];
        Point sample;
        bool  sample_set = false;
        // Take a sample deep inside its island if available. Infills are usually quite far from the island boundary.
        for (uint32_t iexpoly : fill_expolygons_ranges[islice])
            if (const ExPolygon &expoly = fill_expolygons[iexpoly]; ! expoly.empty()) {
                sample     = expoly.contour.points[expoly.contour.points.size() / 2];
                sample_set = true;
                break;
            }
        if (! sample_set) {
            // If there is no infill, take a sample of some inner perimeter.
            for (uint32_t iperimeter : extrusions.first) {
                const ExtrusionEntity &ee = *this_layer_region.perimeters().entities[iperimeter];
                if (ee.is_collection()) {
                    for (const ExtrusionEntity *ee2 : dynamic_cast<const ExtrusionEntityCollection&>(ee).entities)
                        if (! ee2->role().is_external()) {
                            sample     = ee2->middle_point();
                            sample_set = true;
                            goto loop_end;
                        }
                } else if (! ee.role().is_external()) {
                    sample = ee.middle_point();
                    sample_set = true;
                    break;
                }
            }
        loop_end:
            if (! sample_set) {
                if (! extrusions.second.empty()) {
                    // If there is no inner perimeter, take a sample of some gap fill extrusion.
                    sample     = this_layer_region.thin_fills().entities[*extrusions.second.begin()]->middle_point();
                    sample_set = true;
                }
                if (! sample_set && ! extrusions.first.empty()) {
                    // As a last resort, take a sample of some external perimeter.
                    sample     = this_layer_region.perimeters().entities[*extrusions.first.begin()]->middle_point();
                    sample_set = true;
                }
            }
        }
        // There may be a valid empty island.
        // assert(sample_set);
        if (sample_set)
            perimeter_slices_queue.emplace_back(islice, sample);
    }

    // Map of source fill_expolygon into region and fill_expolygon of that region.
    // -1: not set
    struct RegionWithFillIndex {
        int region_id{ -1 };
        int fill_in_region_id{ -1 };
    };
    std::vector<RegionWithFillIndex> map_expolygon_to_region_and_fill;
    const bool                       has_multiple_regions = layer_region_ids.size() > 1;
    assert(has_multiple_regions || layer_region_ids.size() == 1);
    // assign fill_surfaces to each layer
    if (! fill_expolygons.empty()) {
        if (has_multiple_regions) {
            // Sort the bounding boxes lexicographically.
            std::vector<uint32_t> fill_expolygons_bboxes_sorted(fill_expolygons_bboxes.size());
            std::iota(fill_expolygons_bboxes_sorted.begin(), fill_expolygons_bboxes_sorted.end(), 0);
            std::sort(fill_expolygons_bboxes_sorted.begin(), fill_expolygons_bboxes_sorted.end(), [&fill_expolygons_bboxes](uint32_t lhs, uint32_t rhs){
                const BoundingBox &bbl = fill_expolygons_bboxes[lhs];
                const BoundingBox &bbr = fill_expolygons_bboxes[rhs];
                return bbl.min < bbr.min || (bbl.min == bbr.min && bbl.max < bbr.max);
            });
            map_expolygon_to_region_and_fill.assign(fill_expolygons.size(), {});
            for (uint32_t region_idx : layer_region_ids) {
                LayerRegion &l = *m_regions[region_idx];
                l.m_fill_expolygons = intersection_ex(l.slices().surfaces, fill_expolygons);
                l.m_fill_expolygons_bboxes.reserve(l.fill_expolygons().size());
                for (const ExPolygon &expolygon : l.fill_expolygons()) {
                    BoundingBox bbox = get_extents(expolygon);
                    l.m_fill_expolygons_bboxes.emplace_back(bbox);
                    auto it_bbox = std::lower_bound(fill_expolygons_bboxes_sorted.begin(), fill_expolygons_bboxes_sorted.end(), bbox, [&fill_expolygons_bboxes](uint32_t lhs, const BoundingBox &bbr){
                        const BoundingBox &bbl = fill_expolygons_bboxes[lhs];
                        return bbl.min < bbr.min || (bbl.min == bbr.min && bbl.max < bbr.max);
                    });
                    if (it_bbox != fill_expolygons_bboxes_sorted.end())
                        if (uint32_t fill_id = *it_bbox; fill_expolygons_bboxes[fill_id] == bbox) {
                            // With a very high probability the two expolygons match exactly. Confirm that.
                            if (expolygons_match(expolygon, fill_expolygons[fill_id])) {
                                RegionWithFillIndex &ref = map_expolygon_to_region_and_fill[fill_id];
                                // Only one expolygon produced by intersection with LayerRegion surface may match an expolygon of fill_expolygons.
                                assert(ref.region_id == -1 && ref.fill_in_region_id == -1);
                                ref.region_id         = region_idx;
                                ref.fill_in_region_id = int(&expolygon - l.fill_expolygons().data());
                            }
                        }
                }
            }
            // Check whether any island contains multiple fills that fall into the same region, but not they are not contiguous.
            // If so, sort fills in that particular region so that fills of an island become contiguous.
            // Index of a region to sort.
            int              sort_region_id = -1;
            // Temporary vector of fills for reordering.
            ExPolygons       fills_temp;
            // Temporary vector of fill_bboxes for reordering.
            BoundingBoxes    fill_bboxes_temp;
            // Vector of new positions of the above.
            std::vector<int> new_positions;
            do {
                sort_region_id = -1;
                for (size_t source_slice_idx = 0; source_slice_idx < fill_expolygons_ranges.size(); ++ source_slice_idx)
                    if (const ExPolygonRange fill_range = fill_expolygons_ranges[source_slice_idx]; fill_range.size() > 1) {
                        // More than one expolygon exists for a single island. Check whether they are contiguous inside a single LayerRegion::fill_expolygons() vector.
                        uint32_t fill_idx = *fill_range.begin();
                        if (const int fill_regon_id = map_expolygon_to_region_and_fill[fill_idx].region_id; fill_regon_id != -1) {
                            int fill_in_region_id = map_expolygon_to_region_and_fill[fill_idx].fill_in_region_id;
                            bool needs_sorting = false;
                            for (++ fill_idx; fill_idx != *fill_range.end(); ++ fill_idx) {
                                if (const RegionWithFillIndex &ref = map_expolygon_to_region_and_fill[fill_idx]; ref.region_id != fill_regon_id) {
                                    // This island has expolygons split among multiple regions.
                                    needs_sorting = false;
                                    break;
                                } else if (ref.fill_in_region_id != ++ fill_in_region_id) {
                                    // This island has all expolygons stored inside the same region, but not sorted.
                                    needs_sorting = true;
                                }
                            }
                            if (needs_sorting) {
                                sort_region_id = fill_regon_id;
                                break;
                            }
                        }
                    }
                if (sort_region_id != -1) {
                    // Reorder fills in region with sort_region index.
                    LayerRegion &layerm = *m_regions[sort_region_id];
                    new_positions.assign(layerm.fill_expolygons().size(), -1);
                    int last = 0;
                    for (RegionWithFillIndex &ref : map_expolygon_to_region_and_fill)
                        if (ref.region_id == sort_region_id) {
                            new_positions[ref.fill_in_region_id] = last;
                            ref.fill_in_region_id = last ++;
                        }
                    for (auto &new_pos : new_positions)
                        if (new_pos == -1)
                            // Not referenced by any map_expolygon_to_region_and_fill.
                            new_pos = last ++;
                    // Move just the content of m_fill_expolygons to fills_temp, but don't move the container vector.
                    auto &fills       = layerm.m_fill_expolygons;
                    auto &fill_bboxes = layerm.m_fill_expolygons_bboxes;

                    assert(fills.size() == fill_bboxes.size());
                    assert(last == int(fills.size()));

                    fills_temp.resize(fills.size());
                    fills_temp.assign(std::make_move_iterator(fills.begin()), std::make_move_iterator(fills.end()));

                    fill_bboxes_temp.resize(fill_bboxes.size());
                    fill_bboxes_temp.assign(std::make_move_iterator(fill_bboxes.begin()), std::make_move_iterator(fill_bboxes.end()));

                    // Move / reorder the ExPolygons and BoundingBoxes back into m_fill_expolygons and m_fill_expolygons_bboxes.
                    for (size_t old_pos = 0; old_pos < new_positions.size(); ++old_pos) {
                        fills[new_positions[old_pos]]       = std::move(fills_temp[old_pos]);
                        fill_bboxes[new_positions[old_pos]] = std::move(fill_bboxes_temp[old_pos]);
                    }
                }
            } while (sort_region_id != -1);
        } else {
            this_layer_region.m_fill_expolygons        = std::move(fill_expolygons);
            this_layer_region.m_fill_expolygons_bboxes = std::move(fill_expolygons_bboxes);
        }
    }

    auto insert_into_island = [
        // Region where the perimeters, gap fills and fill expolygons are stored.
        region_id,
        // Whether there are infills with different regions generated for this LayerSlice.
        has_multiple_regions,
        // Layer split into surfaces
        &slices,
        // Perimeters and gap fills to be sorted into islands.
        &perimeter_and_gapfill_ranges,
        // Infill regions to be sorted into islands.
        &fill_expolygons, &fill_expolygons_bboxes, &fill_expolygons_ranges,
        // Mapping of fill_expolygon to region and its infill.
        &map_expolygon_to_region_and_fill,
        // Output
        &regions = m_regions, &lslices_ex = this->lslices_ex]
        (int lslice_idx, int source_slice_idx) {
        lslices_ex[lslice_idx].islands.push_back({});
        LayerIsland &island = lslices_ex[lslice_idx].islands.back();
        island.perimeters = LayerExtrusionRange(region_id, perimeter_and_gapfill_ranges[source_slice_idx].first);
        island.boundary = slices.surfaces[source_slice_idx].expolygon;
        island.thin_fills = perimeter_and_gapfill_ranges[source_slice_idx].second;
        if (ExPolygonRange fill_range = fill_expolygons_ranges[source_slice_idx]; ! fill_range.empty()) {
            if (has_multiple_regions) {
                // Check whether the fill expolygons of this island were split into multiple regions.
                island.fill_region_id = LayerIsland::fill_region_composite_id;
                for (uint32_t fill_idx : fill_range) {
                    if (const int fill_regon_id = map_expolygon_to_region_and_fill[fill_idx].region_id;
                        fill_regon_id == -1 || (island.fill_region_id != LayerIsland::fill_region_composite_id && int(island.fill_region_id) != fill_regon_id)) {
                        island.fill_region_id = LayerIsland::fill_region_composite_id;
                        break;
                    } else
                        island.fill_region_id = fill_regon_id;
                }
                if (island.fill_expolygons_composite()) {
                    // They were split, thus store the unsplit "composite" expolygons into the region of perimeters.
                    LayerRegion &this_layer_region = *regions[region_id];
                    auto begin = uint32_t(this_layer_region.fill_expolygons_composite().size());
                    this_layer_region.m_fill_expolygons_composite.reserve(this_layer_region.fill_expolygons_composite().size() + fill_range.size());
                    std::move(fill_expolygons.begin() + *fill_range.begin(), fill_expolygons.begin() + *fill_range.end(), std::back_inserter(this_layer_region.m_fill_expolygons_composite));
                    this_layer_region.m_fill_expolygons_composite_bboxes.insert(this_layer_region.m_fill_expolygons_composite_bboxes.end(), 
                        fill_expolygons_bboxes.begin() + *fill_range.begin(), fill_expolygons_bboxes.begin() + *fill_range.end());
                    island.fill_expolygons = ExPolygonRange(begin, uint32_t(this_layer_region.fill_expolygons_composite().size()));
                } else {
                    // All expolygons are stored inside a single LayerRegion in a contiguous range.
                    island.fill_expolygons = ExPolygonRange(
                        map_expolygon_to_region_and_fill[*fill_range.begin()].fill_in_region_id,
                        map_expolygon_to_region_and_fill[*fill_range.end() - 1].fill_in_region_id + 1);
                }
            } else {
                // Layer island is made of one fill region only.
                island.fill_expolygons = fill_range;
                island.fill_region_id  = region_id;
            }
        }
    };

    // First sort into islands using exact fit.
    // Traverse the slices in an increasing order of bounding box size, so that the islands inside another islands are tested first,
    // so we can just test a point inside ExPolygon::contour and we may skip testing the holes.
    auto point_inside_surface = [&lslices = this->lslices, &lslices_ex = this->lslices_ex](size_t lslice_idx, const Point &point) {
        const BoundingBox &bbox = lslices_ex[lslice_idx].bbox;
        return point.x() >= bbox.min.x() && point.x() < bbox.max.x() &&
               point.y() >= bbox.min.y() && point.y() < bbox.max.y() &&
               // Exact match: Don't just test whether a point is inside the outer contour of an island,
               // test also whether the point is not inside some hole of the same expolygon.
               // This is unfortunatelly necessary because the point may be inside an expolygon of one of this expolygon's hole
               // and missed due to numerical issues.
               lslices[lslice_idx].contains(point);
    };
    for (int lslice_idx = int(lslices_ex.size()) - 1; lslice_idx >= 0 && ! perimeter_slices_queue.empty(); -- lslice_idx)
        for (auto it_source_slice = perimeter_slices_queue.begin(); it_source_slice != perimeter_slices_queue.end(); ++ it_source_slice)
            if (point_inside_surface(lslice_idx, it_source_slice->second)) {
                insert_into_island(lslice_idx, it_source_slice->first);
                if (std::next(it_source_slice) != perimeter_slices_queue.end())
                    // Remove the current slice & point pair from the queue.
                    *it_source_slice = perimeter_slices_queue.back();
                perimeter_slices_queue.pop_back();
                break;
            }
    if (! perimeter_slices_queue.empty()) {
        // If the slice sample was not fitted into any slice using exact fit, try to find a closest island as a last resort.
        // This should be a rare event especially if the sample point was taken from infill or inner perimeter,
        // however we may land here for external perimeter only islands with fuzzy skin applied.
        // Check whether fuzzy skin was enabled and adjust the bounding box accordingly.
        const PrintConfig       &print_config  = this->object()->print()->config();
        const PrintRegionConfig &region_config = this_layer_region.region().config();
        const auto               bbox_eps      = scaled<coord_t>(
            EPSILON + print_config.gcode_resolution.value +
            (region_config.fuzzy_skin.value == FuzzySkinType::None ? 0. : region_config.fuzzy_skin_thickness.value 
                //FIXME it looks as if Arachne could extend open lines by fuzzy_skin_point_dist, which does not seem right.
                + region_config.fuzzy_skin_point_dist.value));
        auto point_inside_surface_dist2 =
            [&lslices = this->lslices, &lslices_ex = this->lslices_ex, bbox_eps]
            (const size_t lslice_idx, const Point &point) {
            const BoundingBox &bbox = lslices_ex[lslice_idx].bbox;
            return 
                point.x() < bbox.min.x() - bbox_eps || point.x() > bbox.max.x() + bbox_eps ||
                point.y() < bbox.min.y() - bbox_eps || point.y() > bbox.max.y() + bbox_eps ?
                std::numeric_limits<double>::max() :
                (lslices[lslice_idx].point_projection(point) - point).cast<double>().squaredNorm();
        };
        for (auto it_source_slice  = perimeter_slices_queue.begin(); it_source_slice != perimeter_slices_queue.end(); ++ it_source_slice) {
            double d2min = std::numeric_limits<double>::max();
            int    lslice_idx_min = -1;
            for (int lslice_idx = int(lslices_ex.size()) - 1; lslice_idx >= 0; -- lslice_idx)
                if (double d2 = point_inside_surface_dist2(lslice_idx, it_source_slice->second); d2 < d2min) {
                    d2min = d2;
                    lslice_idx_min = lslice_idx;
                }
            if (lslice_idx_min == -1) {
                // This should not happen, but Arachne seems to produce a perimeter point far outside its source contour.
                // As a last resort, find the closest source contours to the sample point.
                for (int lslice_idx = int(lslices_ex.size()) - 1; lslice_idx >= 0; -- lslice_idx)
                    if (double d2 = (lslices[lslice_idx].point_projection(it_source_slice->second) - it_source_slice->second).cast<double>().squaredNorm(); d2 < d2min) {
                        d2min = d2;
                        lslice_idx_min = lslice_idx;
                    }
            }
            assert(lslice_idx_min != -1);
            insert_into_island(lslice_idx_min, it_source_slice->first);
        }
    }
}

void Layer::export_region_slices_to_svg(const char *path) const
{
    BoundingBox bbox;
    for (const auto *region : m_regions)
        for (const auto &surface : region->slices())
            bbox.merge(get_extents(surface.expolygon));
    Point legend_size = export_surface_type_legend_to_svg_box_size();
    Point legend_pos(bbox.min(0), bbox.max(1));
    bbox.merge(Point(std::max(bbox.min(0) + legend_size(0), bbox.max(0)), bbox.max(1) + legend_size(1)));

    SVG svg(path, bbox);
    const float transparency = 0.5f;
    for (const auto *region : m_regions)
        for (const auto &surface : region->slices())
            svg.draw(surface.expolygon, surface_type_to_color_name(surface.surface_type), transparency);
    export_surface_type_legend_to_svg(svg, legend_pos);
    svg.Close(); 
}

// Export to "out/LayerRegion-name-%d.svg" with an increasing index with every export.
void Layer::export_region_slices_to_svg_debug(const char *name) const
{
    static size_t idx = 0;
    this->export_region_slices_to_svg(debug_out_path("Layer-slices-%s-%d.svg", name, idx ++).c_str());
}

void Layer::export_region_fill_surfaces_to_svg(const char *path) const
{
    BoundingBox bbox;
    for (const auto *region : m_regions)
        for (const auto &surface : region->slices())
            bbox.merge(get_extents(surface.expolygon));
    Point legend_size = export_surface_type_legend_to_svg_box_size();
    Point legend_pos(bbox.min(0), bbox.max(1));
    bbox.merge(Point(std::max(bbox.min(0) + legend_size(0), bbox.max(0)), bbox.max(1) + legend_size(1)));

    SVG svg(path, bbox);
    const float transparency = 0.5f;
    for (const auto *region : m_regions)
        for (const auto &surface : region->slices())
            svg.draw(surface.expolygon, surface_type_to_color_name(surface.surface_type), transparency);
    export_surface_type_legend_to_svg(svg, legend_pos);
    svg.Close();
}

// Export to "out/LayerRegion-name-%d.svg" with an increasing index with every export.
void Layer::export_region_fill_surfaces_to_svg_debug(const char *name) const
{
    static size_t idx = 0;
    this->export_region_fill_surfaces_to_svg(debug_out_path("Layer-fill_surfaces-%s-%d.svg", name, idx ++).c_str());
}

BoundingBox get_extents(const LayerRegion &layer_region)
{
    BoundingBox bbox;
    if (! layer_region.slices().empty()) {
        bbox = get_extents(layer_region.slices().surfaces.front());
        for (auto it = layer_region.slices().surfaces.cbegin() + 1; it != layer_region.slices().surfaces.cend(); ++ it)
            bbox.merge(get_extents(*it));
    }
    return bbox;
}

BoundingBox get_extents(const LayerRegionPtrs &layer_regions)
{
    BoundingBox bbox;
    if (!layer_regions.empty()) {
        bbox = get_extents(*layer_regions.front());
        for (auto it = layer_regions.begin() + 1; it != layer_regions.end(); ++it)
            bbox.merge(get_extents(**it));
    }
    return bbox;
}

}
