#ifndef slic3r_ClipperUtils_hpp_
#define slic3r_ClipperUtils_hpp_

//#define SLIC3R_USE_CLIPPER2

#include <assert.h>
#include <cstddef>
#include <iterator>
#include <utility>
#include <vector>
#include <cassert>

#include "libslic3r.h"
#include "ExPolygon.hpp"
#include "Polygon.hpp"
#include "Surface.hpp"
#include "libslic3r/Line.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polyline.hpp"
#include "Slic3r/Biz/Algorithms/ClipperUtils.hpp"

namespace Slic3r {

using Slic3r::Biz::Algorithms::ClipperUtils::ClipperSafetyOffset;
using Slic3r::Biz::Algorithms::ClipperUtils::DefaultJoinType;
using Slic3r::Biz::Algorithms::ClipperUtils::DefaultEndType;
using Slic3r::Biz::Algorithms::ClipperUtils::DefaultMiterLimit;
using Slic3r::Biz::Algorithms::ClipperUtils::DefaultLineJoinType;
using Slic3r::Biz::Algorithms::ClipperUtils::DefaultLineMiterLimit;
using Slic3r::Biz::Algorithms::ClipperUtils::ClipperOffsetShortestEdgeFactor;
using Slic3r::Biz::Algorithms::ClipperUtils::ApplySafetyOffset;

namespace ClipperUtils {
using Slic3r::Biz::Algorithms::ClipperUtils::EmptyPathsProvider;
using Slic3r::Biz::Algorithms::ClipperUtils::ExPolygonProvider;
using Slic3r::Biz::Algorithms::ClipperUtils::ExPolygonsProvider;
using Slic3r::Biz::Algorithms::ClipperUtils::MultiPointsProvider;
using Slic3r::Biz::Algorithms::ClipperUtils::PathsProvider;
using Slic3r::Biz::Algorithms::ClipperUtils::PathsProviderIteratorBase;
using Slic3r::Biz::Algorithms::ClipperUtils::PolygonsProvider;
using Slic3r::Biz::Algorithms::ClipperUtils::PolylinesProvider;
using Slic3r::Biz::Algorithms::ClipperUtils::SinglePathProvider;

using Slic3r::Biz::Algorithms::ClipperUtils::ZPoint;
using Slic3r::Biz::Algorithms::ClipperUtils::ZPoints;

using Slic3r::Biz::Algorithms::ClipperUtils::clip_clipper_polygon_with_subject_bbox;
using Slic3r::Biz::Algorithms::ClipperUtils::clip_clipper_polygons_with_subject_bbox;
}

using Slic3r::Biz::Algorithms::ClipperUtils::offset;
using Slic3r::Biz::Algorithms::ClipperUtils::offset_ex;
Slic3r::Polygons   offset(const Slic3r::Surfaces &surfaces, const float delta, ClipperLib::JoinType joinType = DefaultJoinType, double miterLimit = DefaultMiterLimit);
Slic3r::Polygons   offset(const Slic3r::SurfacesPtr &surfaces, const float delta, ClipperLib::JoinType joinType = DefaultJoinType, double miterLimit = DefaultMiterLimit);

Slic3r::ExPolygons offset_ex(const Slic3r::Surfaces &surfaces, const float delta, ClipperLib::JoinType joinType = DefaultJoinType, double miterLimit = DefaultMiterLimit);
Slic3r::ExPolygons offset_ex(const Slic3r::SurfacesPtr &surfaces, const float delta, ClipperLib::JoinType joinType = DefaultJoinType, double miterLimit = DefaultMiterLimit);

using Slic3r::Biz::Algorithms::ClipperUtils::contour_to_polygons;

using Slic3r::Biz::Algorithms::ClipperUtils::union_safety_offset;
using Slic3r::Biz::Algorithms::ClipperUtils::union_safety_offset_ex;

using Slic3r::Biz::Algorithms::ClipperUtils::expand;
using Slic3r::Biz::Algorithms::ClipperUtils::expand_ex;
using Slic3r::Biz::Algorithms::ClipperUtils::shrink;
using Slic3r::Biz::Algorithms::ClipperUtils::shrink_ex;

using Slic3r::Biz::Algorithms::ClipperUtils::offset2;
using Slic3r::Biz::Algorithms::ClipperUtils::offset2_ex;

Slic3r::ExPolygons offset2_ex(const Slic3r::Surfaces &surfaces, const float delta1, const float delta2, ClipperLib::JoinType joinType = DefaultJoinType, double miterLimit = DefaultMiterLimit);

using Slic3r::Biz::Algorithms::ClipperUtils::closing;
using Slic3r::Biz::Algorithms::ClipperUtils::closing_ex;

inline Slic3r::ExPolygons closing_ex(const Slic3r::Surfaces &surfaces, const float delta, ClipperLib::JoinType joinType = DefaultJoinType, double miterLimit = DefaultMiterLimit) 
    { assert(delta > 0); return offset2_ex(surfaces, delta, - delta, joinType, miterLimit); }


using Slic3r::Biz::Algorithms::ClipperUtils::opening;
using Slic3r::Biz::Algorithms::ClipperUtils::opening_ex;
Slic3r::Polygons          opening(const Slic3r::Surfaces &surfaces, const float delta1, const float delta2, ClipperLib::JoinType joinType = DefaultJoinType, double miterLimit = DefaultMiterLimit);
inline Slic3r::Polygons   opening(const Slic3r::Surfaces &surfaces, const float delta, ClipperLib::JoinType joinType = DefaultJoinType, double miterLimit = DefaultMiterLimit) 
    { return opening(surfaces, delta, delta, joinType, miterLimit); }
inline Slic3r::ExPolygons opening_ex(const Slic3r::Surfaces &surfaces, const float delta, ClipperLib::JoinType joinType = DefaultJoinType, double miterLimit = DefaultMiterLimit) 
    { assert(delta > 0); return offset2_ex(surfaces, - delta, delta, joinType, miterLimit); }

using Slic3r::Biz::Algorithms::ClipperUtils::_clipper_ln;

using Slic3r::Biz::Algorithms::ClipperUtils::diff;
using Slic3r::Biz::Algorithms::ClipperUtils::diff_clipped;
using Slic3r::Biz::Algorithms::ClipperUtils::diff_ex;
using Slic3r::Biz::Algorithms::ClipperUtils::diff_pl;
Slic3r::Polygons   diff(const Slic3r::Surfaces &subject, const Slic3r::Polygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons diff_ex(const Slic3r::Polygons &subject, const Slic3r::Surfaces &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons diff_ex(const Slic3r::Surfaces &subject, const Slic3r::Polygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons diff_ex(const Slic3r::Surfaces &subject, const Slic3r::ExPolygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons diff_ex(const Slic3r::ExPolygons &subject, const Slic3r::Surfaces &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons diff_ex(const Slic3r::Surfaces &subject, const Slic3r::Surfaces &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons diff_ex(const Slic3r::SurfacesPtr &subject, const Slic3r::Polygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons diff_ex(const Slic3r::SurfacesPtr &subject, const Slic3r::ExPolygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);

using Slic3r::Biz::Algorithms::ClipperUtils::diff_ln;

using Slic3r::Biz::Algorithms::ClipperUtils::intersection;
using Slic3r::Biz::Algorithms::ClipperUtils::intersection_clipped;
using Slic3r::Biz::Algorithms::ClipperUtils::intersection_ex;
using Slic3r::Biz::Algorithms::ClipperUtils::intersection_pl;
Slic3r::Polygons   intersection(const Slic3r::Surfaces &subject, const Slic3r::Polygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Slic3r::Polygons   intersection(const Slic3r::Surfaces &subject, const Slic3r::ExPolygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons intersection_ex(const Slic3r::Surfaces &subject, const Slic3r::Polygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons intersection_ex(const Slic3r::Surfaces &subject, const Slic3r::ExPolygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons intersection_ex(const Slic3r::Surfaces &subject, const Slic3r::Surfaces &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons intersection_ex(const Slic3r::SurfacesPtr &subject, const Slic3r::ExPolygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);

using Slic3r::Biz::Algorithms::ClipperUtils::intersection_ln;

using Slic3r::Biz::Algorithms::ClipperUtils::union_;
using Slic3r::Biz::Algorithms::ClipperUtils::union_ex;
Slic3r::ExPolygons union_ex(const Slic3r::Surfaces &subject);
using Slic3r::Biz::Algorithms::ClipperUtils::union_pt;

using Slic3r::Biz::Algorithms::ClipperUtils::union_pt_chained_outside_in;
using Slic3r::Biz::Algorithms::ClipperUtils::union_parallel_reduce;

using Slic3r::Biz::Algorithms::ClipperUtils::xor_ex;
using Slic3r::Biz::Algorithms::ClipperUtils::order_nodes;

using Slic3r::Biz::Algorithms::ClipperUtils::e_ordering;
using Slic3r::Biz::Algorithms::ClipperUtils::foreach_node;
using Slic3r::Biz::Algorithms::ClipperUtils::traverse_pt;

using Slic3r::Biz::Algorithms::ClipperUtils::simplify_polygons;

using Slic3r::Biz::Algorithms::ClipperUtils::top_level_islands;

using Slic3r::Biz::Algorithms::ClipperUtils::mittered_offset_path_scaled;
using Slic3r::Biz::Algorithms::ClipperUtils::variable_offset_inner;
using Slic3r::Biz::Algorithms::ClipperUtils::variable_offset_outer;
using Slic3r::Biz::Algorithms::ClipperUtils::variable_offset_inner_ex;
using Slic3r::Biz::Algorithms::ClipperUtils::variable_offset_outer_ex;

} // namespace Slic3r

#endif // slic3r_ClipperUtils_hpp_
