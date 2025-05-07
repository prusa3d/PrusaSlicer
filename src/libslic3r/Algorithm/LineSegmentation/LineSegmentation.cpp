#include <cassert>
#include <limits>
#include <vector>

#include "clipper/clipper_z.hpp"
#include "libslic3r/Arachne/utils/ExtrusionLine.hpp"
#include "libslic3r/Arachne/utils/ExtrusionJunction.hpp"
#include "libslic3r/ClipperZUtils.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/PerimeterGenerator.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/libslic3r.h"

#include "LineSegmentation.hpp"

namespace Slic3r::Algorithm::LineSegmentation {

const constexpr coord_t POINT_IS_ON_LINE_THRESHOLD_SQR = Slic3r::sqr(scaled<coord_t>(EPSILON));

struct ZAttributes
{
    bool     is_clip_point = false;
    bool     is_new_point  = false;
    uint32_t point_index   = 0;

    ZAttributes() = default;

    explicit ZAttributes(const uint32_t clipper_coord) :
        is_clip_point((clipper_coord >> 31) & 0x1), is_new_point((clipper_coord >> 30) & 0x1), point_index(clipper_coord & 0x3FFFFFFF) {}

    explicit ZAttributes(const ClipperLib_Z::IntPoint &clipper_pt) : ZAttributes(clipper_pt.z()) {}

    ZAttributes(const bool is_clip_point, const bool is_new_point, const uint32_t point_index) :
        is_clip_point(is_clip_point), is_new_point(is_new_point), point_index(point_index)
    {
        assert(this->point_index < (1u << 30) && "point_index exceeds 30 bits!");
    }

    // Encode the structure to uint32_t.
    constexpr uint32_t encode() const
    {
        assert(this->point_index < (1u << 30) && "point_index exceeds 30 bits!");
        return (this->is_clip_point << 31) | (this->is_new_point << 30) | (this->point_index & 0x3FFFFFFF);
    }

    // Decode the uint32_t to the structure.
    static ZAttributes decode(const uint32_t clipper_coord)
    {
        return { bool((clipper_coord >> 31) & 0x1), bool((clipper_coord >> 30) & 0x1), clipper_coord & 0x3FFFFFFF };
    }

    static ZAttributes decode(const ClipperLib_Z::IntPoint &clipper_pt) { return ZAttributes::decode(clipper_pt.z()); }
};

struct LineRegionRange
{
    size_t begin_idx;         // Index of the line on which the region begins.
    double begin_t;           // Scalar position on the begin_idx line in which the region begins. The value is from range <0., 1.>.
    size_t end_idx;           // Index of the line on which the region ends.
    double end_t;             // Scalar position on the end_idx line in which the region ends. The value is from range <0., 1.>.
    size_t clip_idx;          // Index of clipping ExPolygons to identified which ExPolygons group contains this line.

    LineRegionRange(size_t begin_idx, double begin_t, size_t end_idx, double end_t, size_t clip_idx)
        : begin_idx(begin_idx), begin_t(begin_t), end_idx(end_idx), end_t(end_t), clip_idx(clip_idx) {}

    // Check if 'other' overlaps with this LineRegionRange.
    bool is_overlap(const LineRegionRange &other) const
    {
        if (this->end_idx < other.begin_idx || this->begin_idx > other.end_idx) {
            return false;
        } else if (this->end_idx == other.begin_idx && this->end_t <= other.begin_t) {
            return false;
        } else if (this->begin_idx == other.end_idx && this->begin_t >= other.end_t) {
            return false;
        }

        return true;
    }

    // Check if 'inner' is whole inside this LineRegionRange.
    bool is_inside(const LineRegionRange &inner) const
    {
        if (!this->is_overlap(inner)) {
            return false;
        }

        const bool starts_after = (this->begin_idx < inner.begin_idx) || (this->begin_idx == inner.begin_idx && this->begin_t <= inner.begin_t);
        const bool ends_before  = (this->end_idx > inner.end_idx) || (this->end_idx == inner.end_idx && this->end_t >= inner.end_t);

        return starts_after && ends_before;
    }

    bool is_zero_length() const { return this->begin_idx == this->end_idx && this->begin_t == this->end_t; }

    bool operator<(const LineRegionRange &rhs) const
    {
        return this->begin_idx < rhs.begin_idx || (this->begin_idx == rhs.begin_idx && this->begin_t < rhs.begin_t);
    }
};

using LineRegionRanges = std::vector<LineRegionRange>;

inline Point make_point(const ClipperLib_Z::IntPoint &clipper_pt) { return { clipper_pt.x(), clipper_pt.y() }; }

inline ClipperLib_Z::Paths to_clip_zpaths(const ExPolygons &clips) { return ClipperZUtils::expolygons_to_zpaths_with_same_z<false>(clips, coord_t(ZAttributes(true, false, 0).encode())); }

static ClipperLib_Z::Path subject_to_zpath(const Points &subject, const bool is_closed)
{
    ZAttributes z_attributes(false, false, 0);

    ClipperLib_Z::Path out;
    if (!subject.empty()) {
        out.reserve((subject.size() + is_closed) ? 1 : 0);
        for (const Point &p : subject) {
            out.emplace_back(p.x(), p.y(), z_attributes.encode());
            ++z_attributes.point_index;
        }

        if (is_closed) {
            // If it is closed, then duplicate the first point at the end to make a closed path open.
            out.emplace_back(subject.front().x(), subject.front().y(), z_attributes.encode());
        }
    }

    return out;
}

static ClipperLib_Z::Path subject_to_zpath(const Arachne::ExtrusionLine &subject)
{
    // Closed Arachne::ExtrusionLine already has duplicated the last point.
    ZAttributes z_attributes(false, false, 0);

    ClipperLib_Z::Path out;
    if (!subject.empty()) {
        out.reserve(subject.size());
        for (const Arachne::ExtrusionJunction &junction : subject) {
            out.emplace_back(junction.p.x(), junction.p.y(), z_attributes.encode());
            ++z_attributes.point_index;
        }
    }

    return out;
}

static ClipperLib_Z::Path subject_to_zpath(const Polyline &subject) { return subject_to_zpath(subject.points, false); }

[[maybe_unused]] static ClipperLib_Z::Path subject_to_zpath(const Polygon &subject) { return subject_to_zpath(subject.points, true); }

struct ProjectionInfo
{
    double projected_t;
    double distance_sqr;
};

static ProjectionInfo project_point_on_line(const Point &line_from_pt, const Point &line_to_pt, const Point &query_pt)
{
    const Vec2d  line_vec        = (line_to_pt - line_from_pt).template cast<double>();
    const Vec2d  query_vec       = (query_pt - line_from_pt).template cast<double>();
    const double line_length_sqr = line_vec.squaredNorm();

    if (line_length_sqr <= 0.) {
        return { std::numeric_limits<double>::max(), std::numeric_limits<double>::max() };
    }

    const double projected_t            = query_vec.dot(line_vec);
    const double projected_t_normalized = std::clamp(projected_t / line_length_sqr, 0., 1.);
    // Projected point have to line on the line.
    if (projected_t < 0. || projected_t > line_length_sqr) {
        return { projected_t_normalized, std::numeric_limits<double>::max() };
    }

    const Vec2d  projected_vec = projected_t_normalized * line_vec;
    const double distance_sqr  = (projected_vec - query_vec).squaredNorm();

    return { projected_t_normalized, distance_sqr };
}

static int32_t find_closest_line_to_point(const ClipperLib_Z::Path &subject, const ClipperLib_Z::IntPoint &query)
{
    auto   it_min           = subject.end();
    double distance_sqr_min = std::numeric_limits<double>::max();

    const Point query_pt = make_point(query);
    Point       prev_pt  = make_point(subject.front());
    for (auto it_curr = std::next(subject.begin()); it_curr != subject.end(); ++it_curr) {
        const Point curr_pt = make_point(*it_curr);

        const double distance_sqr = project_point_on_line(prev_pt, curr_pt, query_pt).distance_sqr;
        if (distance_sqr <= POINT_IS_ON_LINE_THRESHOLD_SQR) {
            return int32_t(std::distance(subject.begin(), std::prev(it_curr)));
        }

        if (distance_sqr < distance_sqr_min) {
            distance_sqr_min = distance_sqr;
            it_min           = std::prev(it_curr);
        }

        prev_pt = curr_pt;
    }

    if (it_min != subject.end()) {
        return int32_t(std::distance(subject.begin(), it_min));
    }

    return -1;
}

std::optional<LineRegionRange> create_line_region_range(ClipperLib_Z::Path &&intersection, const ClipperLib_Z::Path &subject, const size_t region_idx)
{
    if (intersection.size() < 2) {
        return std::nullopt;
    }

    auto need_reverse = [&subject](const ClipperLib_Z::Path &intersection) -> bool {
        for (size_t curr_idx = 1; curr_idx < intersection.size(); ++curr_idx) {
            ZAttributes prev_z(intersection[curr_idx - 1]);
            ZAttributes curr_z(intersection[curr_idx]);

            if (!prev_z.is_clip_point && !curr_z.is_clip_point) {
                if (prev_z.point_index > curr_z.point_index) {
                    return true;
                } else if (curr_z.point_index == prev_z.point_index) {
                    assert(curr_z.point_index < subject.size());
                    const Point subject_pt = make_point(subject[curr_z.point_index]);
                    const Point prev_pt    = make_point(intersection[curr_idx - 1]);
                    const Point curr_pt    = make_point(intersection[curr_idx]);

                    const double prev_dist = (prev_pt - subject_pt).cast<double>().squaredNorm();
                    const double curr_dist = (curr_pt - subject_pt).cast<double>().squaredNorm();
                    if (prev_dist > curr_dist) {
                        return true;
                    }
                }
            }
        }

        return false;
    };

    for (ClipperLib_Z::IntPoint &clipper_pt : intersection) {
        const ZAttributes clipper_pt_z(clipper_pt);
        if (!clipper_pt_z.is_clip_point) {
            continue;
        }

        // FIXME @hejllukas: We could save searing for the source line in some cases using other intersection points,
        //                   but in reality, the clip point will be inside the intersection in very rare cases.
        if (int32_t subject_line_idx = find_closest_line_to_point(subject, clipper_pt); subject_line_idx != -1) {
            clipper_pt.z() = coord_t(ZAttributes(false, true, subject_line_idx).encode());
        }

        assert(!ZAttributes(clipper_pt).is_clip_point);
        if (ZAttributes(clipper_pt).is_clip_point) {
            return std::nullopt;
        }
    }

    // Ensure that indices of source input are ordered in increasing order.
    if (need_reverse(intersection)) {
        std::reverse(intersection.begin(), intersection.end());
    }

    ZAttributes begin_z(intersection.front());
    ZAttributes end_z(intersection.back());

    assert(begin_z.point_index <= subject.size() && end_z.point_index <= subject.size());
    const size_t begin_idx = begin_z.point_index;
    const size_t end_idx   = end_z.point_index;
    const double begin_t   = begin_z.is_new_point ? project_point_on_line(make_point(subject[begin_idx]), make_point(subject[begin_idx + 1]), make_point(intersection.front())).projected_t : 0.;
    const double end_t     = end_z.is_new_point ? project_point_on_line(make_point(subject[end_idx]), make_point(subject[end_idx + 1]), make_point(intersection.back())).projected_t : 0.;

    if (begin_t == std::numeric_limits<double>::max() || end_t == std::numeric_limits<double>::max()) {
        return std::nullopt;
    }

    return LineRegionRange{ begin_idx, begin_t, end_idx, end_t, region_idx };
}

LineRegionRanges intersection_with_region(const ClipperLib_Z::Path &subject, const ClipperLib_Z::Paths &clips, const size_t region_config_idx)
{
    ClipperLib_Z::Clipper clipper;
    clipper.PreserveCollinear(true); // Especially with Arachne, we don't want to remove collinear edges.
    clipper.ZFillFunction([](const ClipperLib_Z::IntPoint &e1bot, const ClipperLib_Z::IntPoint &e1top,
                             const ClipperLib_Z::IntPoint &e2bot, const ClipperLib_Z::IntPoint &e2top,
                             ClipperLib_Z::IntPoint &new_pt) {
        const ZAttributes e1bot_z(e1bot), e1top_z(e1top), e2bot_z(e2bot), e2top_z(e2top);

        assert(e1bot_z.is_clip_point == e1top_z.is_clip_point);
        assert(e2bot_z.is_clip_point == e2top_z.is_clip_point);

        if (!e1bot_z.is_clip_point && !e1top_z.is_clip_point) {
            assert(e1bot_z.point_index + 1 == e1top_z.point_index || e1bot_z.point_index == e1top_z.point_index + 1);
            new_pt.z() = coord_t(ZAttributes(false, true, std::min(e1bot_z.point_index, e1top_z.point_index)).encode());
        } else if (!e2bot_z.is_clip_point && !e2top_z.is_clip_point) {
            assert(e2bot_z.point_index + 1 == e2top_z.point_index || e2bot_z.point_index == e2top_z.point_index + 1);
            new_pt.z() = coord_t(ZAttributes(false, true, std::min(e2bot_z.point_index, e2top_z.point_index)).encode());
        } else {
            assert(false && "At least one of the conditions above has to be met.");
        }
    });

    clipper.AddPath(subject, ClipperLib_Z::ptSubject, false);
    clipper.AddPaths(clips, ClipperLib_Z::ptClip, true);

    ClipperLib_Z::Paths intersections;
    {
        ClipperLib_Z::PolyTree clipped_polytree;
        clipper.Execute(ClipperLib_Z::ctIntersection, clipped_polytree, ClipperLib_Z::pftNonZero, ClipperLib_Z::pftNonZero);
        ClipperLib_Z::PolyTreeToPaths(std::move(clipped_polytree), intersections);
    }

    LineRegionRanges line_region_ranges;
    line_region_ranges.reserve(intersections.size());
    for (ClipperLib_Z::Path &intersection : intersections) {
        if (std::optional<LineRegionRange> region_range = create_line_region_range(std::move(intersection), subject, region_config_idx); region_range.has_value()) {
            line_region_ranges.emplace_back(*region_range);
        }
    }

    return line_region_ranges;
}

LineRegionRanges create_continues_line_region_ranges(LineRegionRanges &&line_region_ranges, const size_t default_clip_idx, const size_t total_lines_cnt)
{
    if (line_region_ranges.empty()) {
        return line_region_ranges;
    }

    std::sort(line_region_ranges.begin(), line_region_ranges.end());

    // Resolve overlapping regions if it happens, but it should never happen.
    for (size_t region_range_idx = 1; region_range_idx < line_region_ranges.size(); ++region_range_idx) {
        LineRegionRange &prev_range = line_region_ranges[region_range_idx - 1];
        LineRegionRange &curr_range = line_region_ranges[region_range_idx];

        assert(!prev_range.is_overlap(curr_range));
        if (prev_range.is_inside(curr_range)) {
            // Make the previous range zero length to remove it later.
            curr_range           = prev_range;
            prev_range.begin_idx = curr_range.begin_idx;
            prev_range.begin_t   = curr_range.begin_t;
            prev_range.end_idx   = curr_range.begin_idx;
            prev_range.end_t     = curr_range.begin_t;
        } else if (prev_range.is_overlap(curr_range)) {
            curr_range.begin_idx = prev_range.end_idx;
            curr_range.begin_t   = prev_range.end_t;
        }
    }

    // Fill all gaps between regions with the default region.
    LineRegionRanges line_region_ranges_out;
    size_t           prev_line_idx = 0.;
    double           prev_t        = 0.;
    for (const LineRegionRange &curr_line_region : line_region_ranges) {
        if (curr_line_region.is_zero_length()) {
            continue;
        }

        assert(prev_line_idx < curr_line_region.begin_idx || (prev_line_idx == curr_line_region.begin_idx && prev_t <= curr_line_region.begin_t));

        // Fill the gap if it is necessary.
        if (prev_line_idx != curr_line_region.begin_idx || prev_t != curr_line_region.begin_t) {
            line_region_ranges_out.emplace_back(prev_line_idx, prev_t, curr_line_region.begin_idx, curr_line_region.begin_t, default_clip_idx);
        }

        // Add the current region.
        line_region_ranges_out.emplace_back(curr_line_region);
        prev_line_idx = curr_line_region.end_idx;
        prev_t        = curr_line_region.end_t;
    }

    // Fill the last remaining gap if it exists.
    const size_t last_line_idx = total_lines_cnt - 1;
    if ((prev_line_idx == last_line_idx && prev_t == 1.) || ((prev_line_idx == total_lines_cnt && prev_t == 0.))) {
        // There is no gap at the end.
        return line_region_ranges_out;
    }

    // Fill the last remaining gap.
    line_region_ranges_out.emplace_back(prev_line_idx, prev_t, last_line_idx, 1., default_clip_idx);

    return line_region_ranges_out;
}

LineRegionRanges subject_segmentation(const ClipperLib_Z::Path &subject, const std::vector<ExPolygons> &expolygons_clips, const size_t default_clip_idx = 0)
{
    LineRegionRanges line_region_ranges;
    for (const ExPolygons &expolygons_clip : expolygons_clips) {
        const size_t              expolygons_clip_idx = &expolygons_clip - expolygons_clips.data();
        const ClipperLib_Z::Paths clips               = to_clip_zpaths(expolygons_clip);
        Slic3r::append(line_region_ranges, intersection_with_region(subject, clips, expolygons_clip_idx + default_clip_idx + 1));
    }

    return create_continues_line_region_ranges(std::move(line_region_ranges), default_clip_idx, subject.size() - 1);
}

PolylineSegment create_polyline_segment(const LineRegionRange &line_region_range, const Polyline &subject)
{
    Polyline polyline_out;
    if (line_region_range.begin_t == 0.) {
        polyline_out.points.emplace_back(subject[line_region_range.begin_idx]);
    } else {
        assert(line_region_range.begin_idx <= subject.size());
        Point interpolated_start_pt = lerp(subject[line_region_range.begin_idx], subject[line_region_range.begin_idx + 1], line_region_range.begin_t);
        polyline_out.points.emplace_back(interpolated_start_pt);
    }

    for (size_t line_idx = line_region_range.begin_idx + 1; line_idx <= line_region_range.end_idx; ++line_idx) {
        polyline_out.points.emplace_back(subject[line_idx]);
    }

    if (line_region_range.end_t == 0.) {
        polyline_out.points.emplace_back(subject[line_region_range.end_idx]);
    } else if (line_region_range.end_t == 1.) {
        assert(line_region_range.end_idx <= subject.size());
        polyline_out.points.emplace_back(subject[line_region_range.end_idx + 1]);
    } else {
        assert(line_region_range.end_idx <= subject.size());
        Point interpolated_end_pt = lerp(subject[line_region_range.end_idx], subject[line_region_range.end_idx + 1], line_region_range.end_t);
        polyline_out.points.emplace_back(interpolated_end_pt);
    }

    return { polyline_out, line_region_range.clip_idx };
}

PolylineSegments create_polyline_segments(const LineRegionRanges &line_region_ranges, const Polyline &subject)
{
    PolylineSegments polyline_segments;
    polyline_segments.reserve(line_region_ranges.size());
    for (const LineRegionRange &region_range : line_region_ranges) {
        polyline_segments.emplace_back(create_polyline_segment(region_range, subject));
    }

    return polyline_segments;
}

ExtrusionSegment create_extrusion_segment(const LineRegionRange &line_region_range, const Arachne::ExtrusionLine &subject)
{
    // When we call this function, we split ExtrusionLine into at least two segments, so none of those segments are closed.
    Arachne::ExtrusionLine extrusion_out(subject.inset_idx, subject.is_odd);
    if (line_region_range.begin_t == 0.) {
        extrusion_out.junctions.emplace_back(subject[line_region_range.begin_idx]);
    } else {
        assert(line_region_range.begin_idx <= subject.size());
        const Arachne::ExtrusionJunction &junction_from = subject[line_region_range.begin_idx];
        const Arachne::ExtrusionJunction &junction_to   = subject[line_region_range.begin_idx + 1];

        const Point   interpolated_start_pt = lerp(junction_from.p, junction_to.p, line_region_range.begin_t);
        const coord_t interpolated_start_w  = lerp(junction_from.w, junction_to.w, line_region_range.begin_t);

        assert(junction_from.perimeter_index == junction_to.perimeter_index);
        extrusion_out.junctions.emplace_back(interpolated_start_pt, interpolated_start_w, junction_from.perimeter_index);
    }

    for (size_t line_idx = line_region_range.begin_idx + 1; line_idx <= line_region_range.end_idx; ++line_idx) {
        extrusion_out.junctions.emplace_back(subject[line_idx]);
    }

    if (line_region_range.end_t == 0.) {
        extrusion_out.junctions.emplace_back(subject[line_region_range.end_idx]);
    } else if (line_region_range.end_t == 1.) {
        assert(line_region_range.end_idx <= subject.size());
        extrusion_out.junctions.emplace_back(subject[line_region_range.end_idx + 1]);
    } else {
        assert(line_region_range.end_idx <= subject.size());
        const Arachne::ExtrusionJunction &junction_from = subject[line_region_range.end_idx];
        const Arachne::ExtrusionJunction &junction_to   = subject[line_region_range.end_idx + 1];

        const Point   interpolated_end_pt = lerp(junction_from.p, junction_to.p, line_region_range.end_t);
        const coord_t interpolated_end_w  = lerp(junction_from.w, junction_to.w, line_region_range.end_t);

        assert(junction_from.perimeter_index == junction_to.perimeter_index);
        extrusion_out.junctions.emplace_back(interpolated_end_pt, interpolated_end_w, junction_from.perimeter_index);
    }

    return { extrusion_out, line_region_range.clip_idx };
}

ExtrusionSegments create_extrusion_segments(const LineRegionRanges &line_region_ranges, const Arachne::ExtrusionLine &subject)
{
    ExtrusionSegments extrusion_segments;
    extrusion_segments.reserve(line_region_ranges.size());
    for (const LineRegionRange &region_range : line_region_ranges) {
        extrusion_segments.emplace_back(create_extrusion_segment(region_range, subject));
    }

    return extrusion_segments;
}

PolylineSegments polyline_segmentation(const Polyline &subject, const std::vector<ExPolygons> &expolygons_clips, const size_t default_clip_idx)
{
    const LineRegionRanges line_region_ranges = subject_segmentation(subject_to_zpath(subject), expolygons_clips, default_clip_idx);
    if (line_region_ranges.empty()) {
        return { PolylineSegment{subject, default_clip_idx} };
    } else if (line_region_ranges.size() == 1) {
        return { PolylineSegment{subject, line_region_ranges.front().clip_idx} };
    }

    return create_polyline_segments(line_region_ranges, subject);
}

PolylineSegments polygon_segmentation(const Polygon &subject, const std::vector<ExPolygons> &expolygons_clips, const size_t default_clip_idx)
{
    return polyline_segmentation(to_polyline(subject), expolygons_clips, default_clip_idx);
}

ExtrusionSegments extrusion_segmentation(const Arachne::ExtrusionLine &subject, const std::vector<ExPolygons> &expolygons_clips, const size_t default_clip_idx)
{
    const LineRegionRanges line_region_ranges = subject_segmentation(subject_to_zpath(subject), expolygons_clips, default_clip_idx);
    if (line_region_ranges.empty()) {
        return { ExtrusionSegment{subject, default_clip_idx} };
    } else if (line_region_ranges.size() == 1) {
        return { ExtrusionSegment{subject, line_region_ranges.front().clip_idx} };
    }

    return create_extrusion_segments(line_region_ranges, subject);
}

inline std::vector<ExPolygons> to_expolygons_clips(const PerimeterRegions &perimeter_regions_clips)
{
    std::vector<ExPolygons> expolygons_clips;
    expolygons_clips.reserve(perimeter_regions_clips.size());
    for (const PerimeterRegion &perimeter_region_clip : perimeter_regions_clips) {
        expolygons_clips.emplace_back(perimeter_region_clip.expolygons);
    }

    return expolygons_clips;
}

PolylineRegionSegments polyline_segmentation(const Polyline &subject, const PrintRegionConfig &base_config, const PerimeterRegions &perimeter_regions_clips)
{
    const LineRegionRanges line_region_ranges = subject_segmentation(subject_to_zpath(subject), to_expolygons_clips(perimeter_regions_clips));
    if (line_region_ranges.empty()) {
        return { PolylineRegionSegment{subject, base_config} };
    } else if (line_region_ranges.size() == 1) {
        return { PolylineRegionSegment{subject, perimeter_regions_clips[line_region_ranges.front().clip_idx - 1].region->config()} };
    }

    PolylineRegionSegments segments_out;
    for (PolylineSegment &segment : create_polyline_segments(line_region_ranges, subject)) {
        const PrintRegionConfig &config = segment.clip_idx == 0 ? base_config : perimeter_regions_clips[segment.clip_idx - 1].region->config();
        segments_out.emplace_back(std::move(segment.polyline), config);
    }

    return segments_out;
}

PolylineRegionSegments polygon_segmentation(const Polygon &subject, const PrintRegionConfig &base_config, const PerimeterRegions &perimeter_regions_clips)
{
    return polyline_segmentation(to_polyline(subject), base_config, perimeter_regions_clips);
}

ExtrusionRegionSegments extrusion_segmentation(const Arachne::ExtrusionLine &subject, const PrintRegionConfig &base_config, const PerimeterRegions &perimeter_regions_clips)
{
    const LineRegionRanges line_region_ranges = subject_segmentation(subject_to_zpath(subject), to_expolygons_clips(perimeter_regions_clips));
    if (line_region_ranges.empty()) {
        return { ExtrusionRegionSegment{subject, base_config} };
    } else if (line_region_ranges.size() == 1) {
        return { ExtrusionRegionSegment{subject, perimeter_regions_clips[line_region_ranges.front().clip_idx - 1].region->config()} };
    }

    ExtrusionRegionSegments segments_out;
    for (ExtrusionSegment &segment : create_extrusion_segments(line_region_ranges, subject)) {
        const PrintRegionConfig &config = segment.clip_idx == 0 ? base_config : perimeter_regions_clips[segment.clip_idx - 1].region->config();
        segments_out.emplace_back(std::move(segment.extrusion), config);
    }

    return segments_out;
}

} // namespace Slic3r::Algorithm::LineSegmentation
