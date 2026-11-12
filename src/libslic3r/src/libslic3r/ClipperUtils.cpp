///Pavel Mikuš @Godrak, Lukáš Matěna @lukasmatena, Lukáš Hejl @hejllukas, Filip Sykala @Jony01
#include "ClipperUtils.hpp"

#include <cmath>

#include "ShortestPath.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Surface.hpp"
#include "libslic3r/libslic3r.h"

#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/parallel_reduce.h>

namespace Slic3r {

Slic3r::Polygons offset(
    const Slic3r::Surfaces& surfaces, const float delta, ClipperLib::JoinType joinType, double miterLimit
)
{
    const ExPolygons expolygons{to_expolygons(surfaces)};
    return offset(expolygons, delta, joinType, miterLimit);
}

Slic3r::Polygons offset(
    const Slic3r::SurfacesPtr& surfaces,
    const float delta,
    ClipperLib::JoinType joinType,
    double miterLimit
)
{
    const ExPolygons expolygons{to_expolygons(surfaces)};
    return offset(expolygons, delta, joinType, miterLimit);
}

Slic3r::ExPolygons offset_ex(
    const Slic3r::Surfaces& surfaces, const float delta, ClipperLib::JoinType joinType, double miterLimit
)
{
    const ExPolygons expolygons{to_expolygons(surfaces)};
    return offset_ex(expolygons, delta, joinType, miterLimit);
}

Slic3r::ExPolygons offset_ex(
    const Slic3r::SurfacesPtr& surfaces,
    const float delta,
    ClipperLib::JoinType joinType,
    double miterLimit
)
{
    const ExPolygons expolygons{to_expolygons(surfaces)};
    return offset_ex(expolygons, delta, joinType, miterLimit);
}

ExPolygons offset2_ex(
    const Surfaces& surfaces,
    const float delta1,
    const float delta2,
    ClipperLib::JoinType joinType,
    double miterLimit
)
{
    const ExPolygons expolygons{to_expolygons(surfaces)};
    return offset2_ex(expolygons, delta1, delta2, joinType, miterLimit);
}

Slic3r::ExPolygons closing_ex(
    const Slic3r::Surfaces& surfaces,
    const float delta1,
    const float delta2,
    ClipperLib::JoinType joinType,
    double miterLimit
)
{
    const ExPolygons expolygons{to_expolygons(surfaces)};
    return closing_ex(expolygons, delta1, delta2, joinType, miterLimit);
}

Slic3r::Polygons opening(
    const Slic3r::Surfaces& surfaces,
    const float delta1,
    const float delta2,
    ClipperLib::JoinType joinType,
    double miterLimit
)
{
    const ExPolygons expolygons{to_expolygons(surfaces)};
    return opening(expolygons, delta1, delta2, joinType, miterLimit);
}

Slic3r::Polygons diff(
    const Slic3r::Surfaces& subject, const Slic3r::Polygons& clip, ApplySafetyOffset do_safety_offset
)
{
    const ExPolygons expolygons{to_expolygons(subject)};
    return diff(expolygons, clip, do_safety_offset);
}

Slic3r::Polygons intersection(
    const Slic3r::Surfaces& subject, const Slic3r::Polygons& clip, ApplySafetyOffset do_safety_offset
)
{
    const ExPolygons expolygons{to_expolygons(subject)};
    return intersection(expolygons, clip, do_safety_offset);
}

Slic3r::Polygons intersection(
    const Slic3r::Surfaces& subject, const Slic3r::ExPolygons& clip, ApplySafetyOffset do_safety_offset
)
{
    const ExPolygons expolygons{to_expolygons(subject)};
    return intersection(expolygons, clip, do_safety_offset);
}

Slic3r::ExPolygons diff_ex(const Slic3r::Polygons &subject, const Slic3r::Surfaces &clip, ApplySafetyOffset do_safety_offset) {
    const ExPolygons clip_expolygons{to_expolygons(clip)};
    return diff_ex(subject, clip_expolygons, do_safety_offset);
}

Slic3r::ExPolygons diff_ex(
    const Slic3r::Surfaces& subject, const Slic3r::Polygons& clip, ApplySafetyOffset do_safety_offset
)
{
    const ExPolygons expolygons{to_expolygons(subject)};
    return diff_ex(expolygons, clip, do_safety_offset);
}

Slic3r::ExPolygons diff_ex(
    const Slic3r::Surfaces& subject, const Slic3r::ExPolygons& clip, ApplySafetyOffset do_safety_offset
)
{
    const ExPolygons expolygons{to_expolygons(subject)};
    return diff_ex(expolygons, clip, do_safety_offset);
}

Slic3r::ExPolygons diff_ex(const Slic3r::ExPolygons &subject, const Slic3r::Surfaces &clip, ApplySafetyOffset do_safety_offset) {
    const ExPolygons clip_expolygons{to_expolygons(clip)};
    return diff_ex(subject, clip_expolygons, do_safety_offset);
}

Slic3r::ExPolygons diff_ex(
    const Slic3r::Surfaces& subject, const Slic3r::Surfaces& clip, ApplySafetyOffset do_safety_offset
)
{
    const ExPolygons expolygons{to_expolygons(subject)};
    return diff_ex(expolygons, clip, do_safety_offset);
}

Slic3r::ExPolygons diff_ex(
    const Slic3r::SurfacesPtr& subject,
    const Slic3r::Polygons& clip,
    ApplySafetyOffset do_safety_offset
)
{
    const ExPolygons expolygons{to_expolygons(subject)};
    return diff_ex(expolygons, clip, do_safety_offset);
}

Slic3r::ExPolygons diff_ex(
    const Slic3r::SurfacesPtr& subject,
    const Slic3r::ExPolygons& clip,
    ApplySafetyOffset do_safety_offset
)
{
    const ExPolygons expolygons{to_expolygons(subject)};
    return diff_ex(expolygons, clip, do_safety_offset);
}

Slic3r::ExPolygons intersection_ex(
    const Slic3r::Surfaces& subject, const Slic3r::Polygons& clip, ApplySafetyOffset do_safety_offset
)
{
    const ExPolygons expolygons{to_expolygons(subject)};
    return intersection_ex(expolygons, clip, do_safety_offset);
}
Slic3r::ExPolygons intersection_ex(
    const Slic3r::Surfaces& subject, const Slic3r::ExPolygons& clip, ApplySafetyOffset do_safety_offset
)
{
    const ExPolygons expolygons{to_expolygons(subject)};
    return intersection_ex(expolygons, clip, do_safety_offset);
}
Slic3r::ExPolygons intersection_ex(
    const Slic3r::Surfaces& subject, const Slic3r::Surfaces& clip, ApplySafetyOffset do_safety_offset
)
{
    const ExPolygons expolygons{to_expolygons(subject)};
    const ExPolygons clip_expolygons{to_expolygons(clip)};
    return intersection_ex(expolygons, clip_expolygons, do_safety_offset);
}
Slic3r::ExPolygons intersection_ex(
    const Slic3r::SurfacesPtr& subject,
    const Slic3r::ExPolygons& clip,
    ApplySafetyOffset do_safety_offset
)
{
    const ExPolygons expolygons{to_expolygons(subject)};
    return intersection_ex(expolygons, clip, do_safety_offset);
}

Slic3r::ExPolygons union_ex(const Slic3r::Surfaces& subject)
{
    const ExPolygons expolygons{to_expolygons(subject)};
    return union_ex(expolygons);
}

} // namespace Slic3r
