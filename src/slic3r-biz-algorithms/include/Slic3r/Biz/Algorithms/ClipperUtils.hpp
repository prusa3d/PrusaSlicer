
#pragma once


#include <assert.h>
#include <cstddef>
#include <iterator>
#include <utility>
#include <vector>
#include <cassert>

#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Domain/Polygon.hpp"
#include "Slic3r/Domain/Line.hpp"
#include "Slic3r/Domain/Point.hpp"
#include "Slic3r/Domain/Polyline.hpp"
#include "Slic3r/Domain/BoundingBox.hpp"

#ifdef SLIC3R_USE_CLIPPER2

#include <clipper2.clipper.h>

#else /* SLIC3R_USE_CLIPPER2 */

#include "clipper/clipper.hpp"

// import these wherever we're included
using ClipperLib::jtMiter;
using ClipperLib::jtRound;
using ClipperLib::jtSquare;

#endif /* SLIC3R_USE_CLIPPER2 */

namespace Slic3r::Biz::Algorithms::ClipperUtils {

static constexpr const float                        ClipperSafetyOffset     = 10.f;

static constexpr const ClipperLib::JoinType DefaultJoinType         = ClipperLib::jtMiter;

static constexpr const ClipperLib::EndType DefaultEndType           = ClipperLib::etOpenButt;

//FIXME evaluate the default miter limit. 3 seems to be extreme, Cura uses 1.2.
// Mitter Limit 3 is useful for perimeter generator, where sharp corners are extruded without needing a gap fill.
// However such a high limit causes issues with large positive or negative offsets, where a sharp corner
// is extended excessively.
static constexpr const double                       DefaultMiterLimit       = 3.;

static constexpr const ClipperLib::JoinType DefaultLineJoinType     = ClipperLib::jtSquare;
// Miter limit is ignored for jtSquare.
static constexpr const double                       DefaultLineMiterLimit   = 0.;

// Decimation factor applied on input contour when doing offset, multiplied by the offset distance.
static constexpr const double                       ClipperOffsetShortestEdgeFactor = 0.005;

enum class ApplySafetyOffset {
    No,
    Yes
};

class PathsProviderIteratorBase {
public:
    using value_type        = Domain::Points;
    using difference_type   = std::ptrdiff_t;
    using pointer           = const Domain::Points*;
    using reference         = const Domain::Points&;
    using iterator_category = std::input_iterator_tag;
};

class EmptyPathsProvider {
public:
    struct iterator : public PathsProviderIteratorBase {
    public:
        const Domain::Points& operator*() { assert(false); return s_empty_points; }
        // all iterators point to end.
        constexpr bool operator==(const iterator &rhs) const { return true; }
        constexpr bool operator!=(const iterator &rhs) const { return false; }
        const Domain::Points& operator++(int) { assert(false); return s_empty_points; }
        const iterator& operator++() { assert(false); return *this; }
    };

    constexpr EmptyPathsProvider() {}
    static constexpr iterator cend()   throw() { return iterator{}; }
    static constexpr iterator end()    throw() { return cend(); }
    static constexpr iterator cbegin() throw() { return cend(); }
    static constexpr iterator begin()  throw() { return cend(); }
    static constexpr size_t   size()   throw() { return 0; }

    static Domain::Points s_empty_points;
};

class SinglePathProvider {
public:
    SinglePathProvider(const Domain::Points &points) : m_points(points) {}

    struct iterator : public PathsProviderIteratorBase {
    public:
        explicit iterator(const Domain::Points &points) : m_ptr(&points) {}
        const Domain::Points& operator*() const { return *m_ptr; }
        bool operator==(const iterator &rhs) const { return m_ptr == rhs.m_ptr; }
        bool operator!=(const iterator &rhs) const { return !(*this == rhs); }
        const Domain::Points& operator++(int) { auto out = m_ptr; m_ptr = &s_end; return *out; }
        iterator& operator++() { m_ptr = &s_end; return *this; }
    private:
        const Domain::Points *m_ptr;
    };

    iterator cbegin() const { return iterator(m_points); }
    iterator begin()  const { return this->cbegin(); }
    iterator cend()   const { return iterator(s_end); }
    iterator end()    const { return this->cend(); }
    size_t   size()   const { return 1; }

private:
    const  Domain::Points &m_points;
    static Domain::Points  s_end;
};

template<typename PathType>
class PathsProvider {
public:
    PathsProvider(const std::vector<PathType> &paths) : m_paths(paths) {}

    struct iterator : public PathsProviderIteratorBase {
    public:
        explicit iterator(typename std::vector<PathType>::const_iterator it) : m_it(it) {}
        const Domain::Points& operator*() const { return *m_it; }
        bool operator==(const iterator &rhs) const { return m_it == rhs.m_it; }
        bool operator!=(const iterator &rhs) const { return !(*this == rhs); }
        const Domain::Points& operator++(int) { return *(m_it ++); }
        iterator& operator++() { ++ m_it; return *this; }
    private:
        typename std::vector<PathType>::const_iterator m_it;
    };

    iterator cbegin() const { return iterator(m_paths.begin()); }
    iterator begin()  const { return this->cbegin(); }
    iterator cend()   const { return iterator(m_paths.end()); }
    iterator end()    const { return this->cend(); }
    size_t   size()   const { return m_paths.size(); }

private:
    const std::vector<PathType> &m_paths;
};

template<typename MultiPointsType>
class MultiPointsProvider {
public:
    MultiPointsProvider(const MultiPointsType &multipoints) : m_multipoints(multipoints) {}

    struct iterator : public PathsProviderIteratorBase {
    public:
        explicit iterator(typename MultiPointsType::const_iterator it) : m_it(it) {}
        const Domain::Points& operator*() const { return m_it->points; }
        bool operator==(const iterator &rhs) const { return m_it == rhs.m_it; }
        bool operator!=(const iterator &rhs) const { return !(*this == rhs); }
        const Domain::Points& operator++(int) { return (m_it ++)->points; }
        iterator& operator++() { ++ m_it; return *this; }
    private:
        typename MultiPointsType::const_iterator m_it;
    };

    iterator cbegin() const { return iterator(m_multipoints.begin()); }
    iterator begin()  const { return this->cbegin(); }
    iterator cend()   const { return iterator(m_multipoints.end()); }
    iterator end()    const { return this->cend(); }
    size_t   size()   const { return m_multipoints.size(); }

private:
    const MultiPointsType &m_multipoints;
};

using PolygonsProvider  = MultiPointsProvider<Domain::Polygons>;
using PolylinesProvider = MultiPointsProvider<Domain::Polylines>;

struct ExPolygonProvider {
    ExPolygonProvider(const Domain::ExPolygon &expoly) : m_expoly(expoly) {}

    struct iterator : public PathsProviderIteratorBase {
    public:
        explicit iterator(const Domain::ExPolygon &expoly, int idx) : m_expoly(expoly), m_idx(idx) {}
        const Domain::Points& operator*() const { return (m_idx == 0) ? m_expoly.contour.points : m_expoly.holes[m_idx - 1].points; }
        bool operator==(const iterator &rhs) const { assert(m_expoly == rhs.m_expoly); return m_idx == rhs.m_idx; }
        bool operator!=(const iterator &rhs) const { return !(*this == rhs); }
        const Domain::Points& operator++(int) { const Domain::Points &out = **this; ++ m_idx; return out; }
        iterator& operator++() { ++ m_idx; return *this; }
    private:
        const Domain::ExPolygon &m_expoly;
        int              m_idx;
    };

    iterator cbegin() const { return iterator(m_expoly, 0); }
    iterator begin()  const { return this->cbegin(); }
    iterator cend()   const { return iterator(m_expoly, m_expoly.holes.size() + 1); }
    iterator end()    const { return this->cend(); }
    size_t   size()   const { return m_expoly.holes.size() + 1; }

private:
    const Domain::ExPolygon &m_expoly;
};

struct ExPolygonsProvider {
    ExPolygonsProvider(const Domain::ExPolygons &expolygons) : m_expolygons(expolygons) {
        m_size = 0;
        for (const Domain::ExPolygon &expoly : expolygons)
            m_size += expoly.holes.size() + 1;
    }

    struct iterator : public PathsProviderIteratorBase {
    public:
        explicit iterator(Domain::ExPolygons::const_iterator it) : m_it_expolygon(it), m_idx_contour(0) {}
        const Domain::Points& operator*() const { return (m_idx_contour == 0) ? m_it_expolygon->contour.points : m_it_expolygon->holes[m_idx_contour - 1].points; }
        bool operator==(const iterator &rhs) const { return m_it_expolygon == rhs.m_it_expolygon && m_idx_contour == rhs.m_idx_contour; }
        bool operator!=(const iterator &rhs) const { return !(*this == rhs); }
        iterator& operator++() { 
            if (++ m_idx_contour == m_it_expolygon->holes.size() + 1) {
                ++ m_it_expolygon;
                m_idx_contour = 0;
            }
            return *this;
        }
        const Domain::Points& operator++(int) { 
            const Domain::Points &out = **this;
            ++ (*this);
            return out;
        }
    private:
        Domain::ExPolygons::const_iterator  m_it_expolygon;
        size_t                      m_idx_contour;
    };

    iterator cbegin() const { return iterator(m_expolygons.cbegin()); }
    iterator begin()  const { return this->cbegin(); }
    iterator cend()   const { return iterator(m_expolygons.cend()); }
    iterator end()    const { return this->cend(); }
    size_t   size()   const { return m_size; }

private:
    const Domain::ExPolygons &m_expolygons;
    size_t            m_size;
};

// For ClipperLib with Z coordinates.
using ZPoint = Domain::Vec3crd;
using ZPoints = std::vector<ZPoint>;

// Clip source polygon to be used as a clipping polygon with a bouding box around the source (to be clipped) polygon.
// Useful as an optimization for expensive ClipperLib operations, for example when clipping source polygons one by one
// with a set of polygons covering the whole layer below.
void                    clip_clipper_polygon_with_subject_bbox(const Domain::Points &src, const Domain::BoundingBox2crd &bbox, Domain::Points &out);
void                    clip_clipper_polygon_with_subject_bbox(const ZPoints &src, const Domain::BoundingBox2crd &bbox, ZPoints &out);
[[nodiscard]] Domain::Points    clip_clipper_polygon_with_subject_bbox(const Domain::Points &src, const Domain::BoundingBox2crd &bbox);
[[nodiscard]] ZPoints   clip_clipper_polygon_with_subject_bbox(const ZPoints &src, const Domain::BoundingBox2crd &bbox);
void                    clip_clipper_polygon_with_subject_bbox(const Domain::Polygon &src, const Domain::BoundingBox2crd &bbox, Domain::Polygon &out);
[[nodiscard]] Domain::Polygon   clip_clipper_polygon_with_subject_bbox(const Domain::Polygon &src, const Domain::BoundingBox2crd &bbox);
[[nodiscard]] Domain::Polygons  clip_clipper_polygons_with_subject_bbox(const Domain::Polygons &src, const Domain::BoundingBox2crd &bbox);
[[nodiscard]] Domain::Polygons  clip_clipper_polygons_with_subject_bbox(const Domain::ExPolygon &src, const Domain::BoundingBox2crd &bbox);
[[nodiscard]] Domain::Polygons  clip_clipper_polygons_with_subject_bbox(const Domain::ExPolygons &src, const Domain::BoundingBox2crd &bbox);


// offset Polygons
// Wherever applicable, please use the expand() / shrink() variants instead, they convey their purpose better.
Domain::Polygons offset(const Domain::Polygon &polygon, const float delta, ClipperLib::JoinType joinType = DefaultJoinType, double miterLimit = DefaultMiterLimit);

// offset Polylines
// Wherever applicable, please use the expand() / shrink() variants instead, they convey their purpose better.
// Input polygons for negative offset shall be "normalized": There must be no overlap / intersections between the input polygons.
Domain::Polygons   offset(const Domain::Polyline &polyline, const float delta, ClipperLib::JoinType joinType = DefaultLineJoinType, double miterLimit = DefaultLineMiterLimit, ClipperLib::EndType end_type = DefaultEndType);
Domain::Polygons   offset(const Domain::Polylines &polylines, const float delta, ClipperLib::JoinType joinType = DefaultLineJoinType, double miterLimit = DefaultLineMiterLimit, ClipperLib::EndType end_type = DefaultEndType);
Domain::Polygons   offset(const Domain::Polygons &polygons, const float delta, ClipperLib::JoinType joinType = DefaultJoinType, double miterLimit = DefaultMiterLimit);
Domain::Polygons   offset(const Domain::ExPolygon &expolygon, const float delta, ClipperLib::JoinType joinType = DefaultJoinType, double miterLimit = DefaultMiterLimit);
Domain::Polygons   offset(const Domain::ExPolygons &expolygons, const float delta, ClipperLib::JoinType joinType = DefaultJoinType, double miterLimit = DefaultMiterLimit);
Domain::ExPolygons offset_ex(const Domain::Polygons &polygons, const float delta, ClipperLib::JoinType joinType = DefaultJoinType, double miterLimit = DefaultMiterLimit);
Domain::ExPolygons offset_ex(const Domain::ExPolygon &expolygon, const float delta, ClipperLib::JoinType joinType = DefaultJoinType, double miterLimit = DefaultMiterLimit);
Domain::ExPolygons offset_ex(const Domain::ExPolygons &expolygons, const float delta, ClipperLib::JoinType joinType = DefaultJoinType, double miterLimit = DefaultMiterLimit);

// convert stroke to path by offsetting of contour
Domain::Polygons contour_to_polygons(const Domain::Polygon &polygon, const float line_width, ClipperLib::JoinType join_type = DefaultJoinType, double miter_limit = DefaultMiterLimit);
Domain::Polygons contour_to_polygons(const Domain::Polygons &polygon, const float line_width, ClipperLib::JoinType join_type = DefaultJoinType, double miter_limit = DefaultMiterLimit);

inline Domain::Polygons   union_safety_offset   (const Domain::Polygons   &polygons)   { return offset   (polygons,   ClipperSafetyOffset); }
inline Domain::Polygons   union_safety_offset   (const Domain::ExPolygons &expolygons) { return offset   (expolygons, ClipperSafetyOffset); }
inline Domain::ExPolygons union_safety_offset_ex(const Domain::Polygons   &polygons)   { return offset_ex(polygons,   ClipperSafetyOffset); }
inline Domain::ExPolygons union_safety_offset_ex(const Domain::ExPolygons &expolygons) { return offset_ex(expolygons, ClipperSafetyOffset); }

Domain::Polygons   union_safety_offset(const Domain::Polygons &expolygons);
Domain::Polygons   union_safety_offset(const Domain::ExPolygons &expolygons);
Domain::ExPolygons union_safety_offset_ex(const Domain::Polygons &polygons);
Domain::ExPolygons union_safety_offset_ex(const Domain::ExPolygons &expolygons);

// Aliases for the various offset(...) functions, conveying the purpose of the offset.
inline Domain::Polygons   expand(const Domain::Polygon &polygon, const float delta, ClipperLib::JoinType joinType = DefaultJoinType, double miterLimit = DefaultMiterLimit) 
    { assert(delta > 0); return offset(polygon, delta, joinType, miterLimit); }
inline Domain::Polygons   expand(const Domain::Polygons &polygons, const float delta, ClipperLib::JoinType joinType = DefaultJoinType, double miterLimit = DefaultMiterLimit) 
    { assert(delta > 0); return offset(polygons, delta, joinType, miterLimit); }
inline Domain::Polygons   expand(const Domain::ExPolygons &polygons, const float delta, ClipperLib::JoinType joinType = DefaultJoinType, double miterLimit = DefaultMiterLimit) 
    { assert(delta > 0); return offset(polygons, delta, joinType, miterLimit); }
inline Domain::ExPolygons expand_ex(const Domain::Polygons &polygons, const float delta, ClipperLib::JoinType joinType = DefaultJoinType, double miterLimit = DefaultMiterLimit) 
    { assert(delta > 0); return offset_ex(polygons, delta, joinType, miterLimit); }
// Input polygons for shrinking shall be "normalized": There must be no overlap / intersections between the input polygons.
inline Domain::Polygons   shrink(const Domain::Polygons &polygons, const float delta, ClipperLib::JoinType joinType = DefaultJoinType, double miterLimit = DefaultMiterLimit) 
    { assert(delta > 0); return offset(polygons, -delta, joinType, miterLimit); }
inline Domain::ExPolygons shrink_ex(const Domain::Polygons &polygons, const float delta, ClipperLib::JoinType joinType = DefaultJoinType, double miterLimit = DefaultMiterLimit) 
    { assert(delta > 0); return offset_ex(polygons, -delta, joinType, miterLimit); }
inline Domain::ExPolygons shrink_ex(const Domain::ExPolygons &polygons, const float delta, ClipperLib::JoinType joinType = DefaultJoinType, double miterLimit = DefaultMiterLimit) 
    { assert(delta > 0); return offset_ex(polygons, -delta, joinType, miterLimit); }

// Wherever applicable, please use the opening() / closing() variants instead, they convey their purpose better.
// Input polygons for negative offset shall be "normalized": There must be no overlap / intersections between the input polygons.
Domain::Polygons   offset2(const Domain::ExPolygons &expolygons, const float delta1, const float delta2, ClipperLib::JoinType joinType = DefaultJoinType, double miterLimit = DefaultMiterLimit);
Domain::ExPolygons offset2_ex(const Domain::ExPolygons &expolygons, const float delta1, const float delta2, ClipperLib::JoinType joinType = DefaultJoinType, double miterLimit = DefaultMiterLimit);

// Offset outside, then inside produces morphological closing. All deltas should be positive.
Domain::Polygons          closing(const Domain::Polygons &polygons, const float delta1, const float delta2, ClipperLib::JoinType joinType = DefaultJoinType, double miterLimit = DefaultMiterLimit);
inline Domain::Polygons   closing(const Domain::Polygons &polygons, const float delta, ClipperLib::JoinType joinType = DefaultJoinType, double miterLimit = DefaultMiterLimit) 
    { return closing(polygons, delta, delta, joinType, miterLimit); }
Domain::ExPolygons        closing_ex(const Domain::Polygons &polygons, const float delta1, const float delta2, ClipperLib::JoinType joinType = DefaultJoinType, double miterLimit = DefaultMiterLimit);
inline Domain::ExPolygons closing_ex(const Domain::Polygons &polygons, const float delta, ClipperLib::JoinType joinType = DefaultJoinType, double miterLimit = DefaultMiterLimit) 
    { return closing_ex(polygons, delta, delta, joinType, miterLimit); }
inline Domain::ExPolygons closing_ex(const Domain::ExPolygons &polygons, const float delta, ClipperLib::JoinType joinType = DefaultJoinType, double miterLimit = DefaultMiterLimit) 
    { assert(delta > 0); return offset2_ex(polygons, delta, - delta, joinType, miterLimit); }
inline Domain::ExPolygons closing_ex(const Domain::ExPolygons &polygons, const float delta1, const float delta2, ClipperLib::JoinType joinType = DefaultJoinType, double miterLimit = DefaultMiterLimit) 
    { assert(delta1 > 0); assert(delta2 > 0); return offset2_ex(polygons, delta1, - delta2, joinType, miterLimit); }

// Offset inside, then outside produces morphological opening. All deltas should be positive.
// Input polygons for opening shall be "normalized": There must be no overlap / intersections between the input polygons.
Domain::Polygons          opening(const Domain::Polygons &polygons, const float delta1, const float delta2, ClipperLib::JoinType joinType = DefaultJoinType, double miterLimit = DefaultMiterLimit);
Domain::Polygons          opening(const Domain::ExPolygons &expolygons, const float delta1, const float delta2, ClipperLib::JoinType joinType = DefaultJoinType, double miterLimit = DefaultMiterLimit);
inline Domain::Polygons   opening(const Domain::Polygons &polygons, const float delta, ClipperLib::JoinType joinType = DefaultJoinType, double miterLimit = DefaultMiterLimit) 
    { return opening(polygons, delta, delta, joinType, miterLimit); }
inline Domain::Polygons   opening(const Domain::ExPolygons &expolygons, const float delta, ClipperLib::JoinType joinType = DefaultJoinType, double miterLimit = DefaultMiterLimit) 
    { return opening(expolygons, delta, delta, joinType, miterLimit); }
inline Domain::ExPolygons opening_ex(const Domain::ExPolygons &polygons, const float delta, ClipperLib::JoinType joinType = DefaultJoinType, double miterLimit = DefaultMiterLimit) 
    { assert(delta > 0); return offset2_ex(polygons, - delta, delta, joinType, miterLimit); }

Domain::Lines _clipper_ln(ClipperLib::ClipType clipType, const Domain::Lines &subject, const Domain::Polygons &clip);

// Safety offset is applied to the clipping polygons only.
Domain::Polygons   diff(const Domain::Polygon &subject, const Domain::Polygon &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Domain::Polygons   diff(const Domain::Polygons &subject, const Domain::Polygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Domain::Polygons   diff(const Domain::Polygons &subject, const Domain::ExPolygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
// Optimized version clipping the "clipping" polygon using clip_clipper_polygon_with_subject_bbox().
// To be used with complex clipping polygons, where majority of the clipping polygons are outside of the source polygon.
Domain::Polygons   diff_clipped(const Domain::Polygons &src, const Domain::Polygons &clipping, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Domain::Polygons   diff(const Domain::ExPolygons &subject, const Domain::Polygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Domain::Polygons   diff(const Domain::ExPolygons &subject, const Domain::ExPolygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Domain::ExPolygons diff_ex(const Domain::Polygons &subject, const Domain::Polygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Domain::ExPolygons diff_ex(const Domain::Polygons &subject, const Domain::ExPolygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Domain::ExPolygons diff_ex(const Domain::Polygon &subject, const Domain::ExPolygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Domain::ExPolygons diff_ex(const Domain::ExPolygon &subject, const Domain::Polygon &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Domain::ExPolygons diff_ex(const Domain::ExPolygon &subject, const Domain::Polygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Domain::ExPolygons diff_ex(const Domain::ExPolygon &subject, const Domain::ExPolygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Domain::ExPolygons diff_ex(const Domain::ExPolygons &subject, const Domain::Polygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Domain::ExPolygons diff_ex(const Domain::ExPolygons &subject, const Domain::ExPolygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Domain::Polylines  diff_pl(const Domain::Polyline &subject, const Domain::Polygons &clip);
Domain::Polylines  diff_pl(const Domain::Polylines &subject, const Domain::Polygons &clip);
Domain::Polylines  diff_pl(const Domain::Polyline &subject, const Domain::ExPolygon &clip);
Domain::Polylines  diff_pl(const Domain::Polyline &subject, const Domain::ExPolygons &clip);
Domain::Polylines  diff_pl(const Domain::Polylines &subject, const Domain::ExPolygon &clip);
Domain::Polylines  diff_pl(const Domain::Polylines &subject, const Domain::ExPolygons &clip);
Domain::Polylines  diff_pl(const Domain::Polygons &subject, const Domain::Polygons &clip);

inline Domain::Lines diff_ln(const Domain::Lines &subject, const Domain::Polygons &clip)
{
    return _clipper_ln(ClipperLib::ctDifference, subject, clip);
}

// Safety offset is applied to the clipping polygons only.
Domain::Polygons   intersection(const Domain::Polygon &subject, const Domain::Polygon &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Domain::Polygons   intersection(const Domain::Polygon &subject, const Domain::Polygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Domain::Polygons   intersection(const Domain::Polygon &subject, const Domain::ExPolygon &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Domain::Polygons   intersection(const Domain::Polygons &subject, const Domain::ExPolygon &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Domain::Polygons   intersection(const Domain::Polygons &subject, const Domain::Polygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
// Optimized version clipping the "clipping" polygon using clip_clipper_polygon_with_subject_bbox().
// To be used with complex clipping polygons, where majority of the clipping polygons are outside of the source polygon.
Domain::Polygons   intersection_clipped(const Domain::Polygons &subject, const Domain::Polygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Domain::Polygons   intersection(const Domain::ExPolygon &subject, const Domain::ExPolygon &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Domain::Polygons   intersection(const Domain::ExPolygons &subject, const Domain::Polygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Domain::Polygons   intersection(const Domain::ExPolygons &subject, const Domain::ExPolygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Domain::ExPolygons intersection_ex(const Domain::Polygons &subject, const Domain::Polygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Domain::ExPolygons intersection_ex(const Domain::ExPolygon &subject, const Domain::Polygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Domain::ExPolygons intersection_ex(const Domain::Polygons &subject, const Domain::ExPolygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Domain::ExPolygons intersection_ex(const Domain::ExPolygons &subject, const Domain::Polygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Domain::ExPolygons intersection_ex(const Domain::ExPolygons &subject, const Domain::ExPolygon &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Domain::ExPolygons intersection_ex(const Domain::ExPolygons &subject, const Domain::ExPolygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Domain::Polylines  intersection_pl(const Domain::Polylines &subject, const Domain::Polygon &clip);
Domain::Polylines  intersection_pl(const Domain::Polyline &subject, const Domain::ExPolygon &clip);
Domain::Polylines  intersection_pl(const Domain::Polylines &subject, const Domain::ExPolygon &clip);
Domain::Polylines  intersection_pl(const Domain::Polyline &subject, const Domain::Polygons &clip);
Domain::Polylines  intersection_pl(const Domain::Polyline &subject, const Domain::ExPolygons &clip);
Domain::Polylines  intersection_pl(const Domain::Polylines &subject, const Domain::Polygons &clip);
Domain::Polylines  intersection_pl(const Domain::Polylines &subject, const Domain::ExPolygons &clip);
Domain::Polylines  intersection_pl(const Domain::Polygons &subject, const Domain::Polygons &clip);

inline Domain::Lines intersection_ln(const Domain::Lines &subject, const Domain::Polygons &clip)
{
    return _clipper_ln(ClipperLib::ctIntersection, subject, clip);
}

inline Domain::Lines intersection_ln(const Domain::Line &subject, const Domain::Polygons &clip)
{
    Domain::Lines lines;
    lines.emplace_back(subject);
    return _clipper_ln(ClipperLib::ctIntersection, lines, clip);
}

Domain::Polygons union_(const Domain::Polygons &subject);
Domain::Polygons union_(const Domain::ExPolygons &subject);
Domain::Polygons union_(const Domain::Polygons &subject, const ClipperLib::PolyFillType fillType);
Domain::Polygons union_(const Domain::Polygons &subject, const Domain::Polygon &subject2);
Domain::Polygons union_(const Domain::Polygons &subject, const Domain::Polygons &subject2);
Domain::Polygons union_(const Domain::Polygons &subject, const Domain::ExPolygon &subject2);
// May be used to "heal" unusual models (3DLabPrints etc.) by providing fill_type (pftEvenOdd, pftNonZero, pftPositive, pftNegative).
Domain::ExPolygons union_ex(const Domain::Polygons &subject, ClipperLib::PolyFillType fill_type = ClipperLib::pftNonZero);
Domain::ExPolygons union_ex(const Domain::Polygons &subject, const Domain::Polygons &subject2, ClipperLib::PolyFillType fill_type = ClipperLib::pftNonZero);
Domain::ExPolygons union_ex(const Domain::ExPolygons &subject);
Domain::ExPolygons union_ex(const Domain::ExPolygons &subject, const Domain::ExPolygons &subject2);
Domain::ExPolygons union_ex(const Domain::ExPolygons &subject, const Domain::Polygons &subject2);
Domain::ExPolygons union_ex(const Domain::Polygons &subject, const Domain::ExPolygons &subject2);

// Convert polygons / expolygons into ClipperLib::PolyTree using ClipperLib::pftEvenOdd, thus union will NOT be performed.
// If the contours are not intersecting, their orientation shall not be modified by union_pt().
ClipperLib::PolyTree union_pt(const Domain::Polygons &subject);
ClipperLib::PolyTree union_pt(const Domain::ExPolygons &subject);

Domain::Polygons union_pt_chained_outside_in(const Domain::Polygons &subject);

// Perform union operation on Polygons using parallel reduction to merge Polygons one by one.
// When many detailed Polygons overlap, performing union over all Polygons at once can be quite slow.
// However, performing the union operation incrementally can be significantly faster in such cases.
Domain::Polygons union_parallel_reduce(const Domain::Polygons &subject);

Domain::ExPolygons xor_ex(const Domain::ExPolygons &subject, const Domain::ExPolygon &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Domain::ExPolygons xor_ex(const Domain::ExPolygons &subject, const Domain::ExPolygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);

ClipperLib::PolyNodes order_nodes(const ClipperLib::PolyNodes &nodes);

// Implementing generalized loop (foreach) over a list of nodes which can be
// ordered or unordered (performance gain) based on template parameter
enum class e_ordering {
    ON,
    OFF
};

// Create a template struct, template functions can not be partially specialized
template<e_ordering o, class Fn> struct _foreach_node {
    void operator()(const ClipperLib::PolyNodes &nodes, Fn &&fn);
};

// Specialization with NO ordering
template<class Fn> struct _foreach_node<e_ordering::OFF, Fn> {
    void operator()(const ClipperLib::PolyNodes &nodes, Fn &&fn)
    {
        for (auto &n : nodes) fn(n);    
    }
};

// Specialization with ordering
template<class Fn> struct _foreach_node<e_ordering::ON, Fn> {
    void operator()(const ClipperLib::PolyNodes &nodes, Fn &&fn)
    {
        auto ordered_nodes = order_nodes(nodes);
        for (auto &n : nodes) fn(n);    
    }
};

// Wrapper function for the foreach_node which can deduce arguments automatically
template<e_ordering o, class Fn>
void foreach_node(const ClipperLib::PolyNodes &nodes, Fn &&fn)
{
    _foreach_node<o, Fn>()(nodes, std::forward<Fn>(fn));
}

template<e_ordering o, class ExOrJustPolygons>
void traverse_pt(const ClipperLib::PolyNodes &nodes, ExOrJustPolygons *retval);

// Collecting polygons of the tree into a list of Polygons, holes have clockwise
// orientation.
template<e_ordering ordering = e_ordering::OFF>
void traverse_pt(const ClipperLib::PolyNode *tree, Domain::Polygons *out)
{
    if (!tree) return; // terminates recursion
    
    // Push the contour of the current level
    out->emplace_back(tree->Contour);
    
    // Do the recursion for all the children.
    traverse_pt<ordering>(tree->Childs, out);
}

// Collecting polygons of the tree into a list of ExPolygons.
template<e_ordering ordering = e_ordering::OFF>
void traverse_pt(const ClipperLib::PolyNode *tree, Domain::ExPolygons *out)
{
    if (!tree) return;
    else if(tree->IsHole()) {
        // Levels of holes are skipped and handled together with the
        // contour levels.
        traverse_pt<ordering>(tree->Childs, out);
        return;
    }
    
    Domain::ExPolygon level;
    level.contour.points = tree->Contour;
    
    foreach_node<ordering>(tree->Childs, 
                           [out, &level] (const ClipperLib::PolyNode *node) {
        
        // Holes are collected here. 
        level.holes.emplace_back(node->Contour);
        
        // By doing a recursion, a new level expoly is created with the contour
        // and holes of the lower level. Doing this for all the childs.
        traverse_pt<ordering>(node->Childs, out);
    }); 
    
    out->emplace_back(level);
}

template<e_ordering o = e_ordering::OFF, class ExOrJustPolygons>
void traverse_pt(const ClipperLib::PolyNodes &nodes, ExOrJustPolygons *retval)
{
    foreach_node<o>(nodes, [&retval](const ClipperLib::PolyNode *node) {
        traverse_pt<o>(node, retval);
    });
}


/* OTHER */
Domain::Polygons simplify_polygons(const Domain::Polygons &subject);

Domain::Polygons top_level_islands(const Domain::Polygons &polygons);

ClipperLib::Path mittered_offset_path_scaled(const Domain::Points &contour, const std::vector<float> &deltas, double miter_limit);
Domain::Polygons  variable_offset_inner(const Domain::ExPolygon &expoly, const std::vector<std::vector<float>> &deltas, double miter_limit = 2.);
Domain::Polygons  variable_offset_outer(const Domain::ExPolygon &expoly, const std::vector<std::vector<float>> &deltas, double miter_limit = 2.);
Domain::ExPolygons variable_offset_outer_ex(const Domain::ExPolygon &expoly, const std::vector<std::vector<float>> &deltas, double miter_limit = 2.);
Domain::ExPolygons variable_offset_inner_ex(const Domain::ExPolygon &expoly, const std::vector<std::vector<float>> &deltas, double miter_limit = 2.);

} // namespace Slic3r::Biz::Algorithms::ClipperUtils
