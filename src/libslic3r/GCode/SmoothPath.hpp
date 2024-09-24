#ifndef slic3r_GCode_SmoothPath_hpp_
#define slic3r_GCode_SmoothPath_hpp_

#include <ankerl/unordered_dense.h>
#include <optional>
#include <vector>

#include "../ExtrusionEntity.hpp"
#include "../Geometry/ArcWelder.hpp"

namespace Slic3r {

class ExtrusionEntityCollection;
class Point;
class Polyline;

namespace GCode {

struct SmoothPathElement
{
    ExtrusionAttributes         path_attributes;
    Geometry::ArcWelder::Path   path;
};

using SmoothPath = std::vector<SmoothPathElement>;

// Length of a smooth path.
double length(const SmoothPath &path);
// Returns true if the smooth path is longer than a threshold.
bool longer_than(const SmoothPath &path, const double length);

std::optional<Point> sample_path_point_at_distance_from_start(const SmoothPath &path, double distance);
std::optional<Point> sample_path_point_at_distance_from_end(const SmoothPath &path, double distance);

// Clip end of a smooth path, for seam hiding.
// When clipping the end of a path, don't create segments shorter than min_point_distance_threshold, 
// rather discard such a degenerate segment.
double clip_end(SmoothPath &path, double distance, double min_point_distance_threshold);

void reverse(SmoothPath &path);

class SmoothPathCache
{
public:
    struct InterpolationParameters {
        double tolerance;
        double fit_circle_tolerance;
    };

    void interpolate_add(const ExtrusionPath             &ee,  const InterpolationParameters &params);
    void interpolate_add(const ExtrusionMultiPath        &ee,  const InterpolationParameters &params);
    void interpolate_add(const ExtrusionLoop             &ee,  const InterpolationParameters &params);
    void interpolate_add(const ExtrusionEntityCollection &eec, const InterpolationParameters &params);

    const Geometry::ArcWelder::Path* resolve(const Polyline      *pl) const;
    const Geometry::ArcWelder::Path* resolve(const ExtrusionPath &path) const;

    // Look-up a smooth representation of path in the cache. If it does not exist, produce a simplified polyline.
    Geometry::ArcWelder::Path        resolve_or_fit(const ExtrusionPath &path, bool reverse, double resolution) const;

    // Look-up a smooth representation of path in the cache. If it does not exist, produce a simplified polyline.
    SmoothPath                       resolve_or_fit(const ExtrusionPaths &paths, bool reverse, double resolution) const;
    SmoothPath                       resolve_or_fit(const ExtrusionMultiPath &path, bool reverse, double resolution) const;

    // Look-up a smooth representation of path in the cache. If it does not exist, produce a simplified polyline.
    SmoothPath                       resolve_or_fit_split_with_seam(
        const ExtrusionLoop &path, const bool reverse, const double resolution,
        const Point &seam_point, const double seam_point_merge_distance_threshold) const;

private:
    ankerl::unordered_dense::map<const Polyline*, Geometry::ArcWelder::Path>    m_cache;
};

// Encapsulates references to global and layer local caches of smooth extrusion paths.
class SmoothPathCaches final
{
public:
    SmoothPathCaches() = delete;
    SmoothPathCaches(const SmoothPathCache &global, const SmoothPathCache &layer_local) : 
        m_global(&global), m_layer_local(&layer_local) {}
    SmoothPathCaches operator=(const SmoothPathCaches &rhs)
        { m_global = rhs.m_global; m_layer_local = rhs.m_layer_local; return *this; }

    const SmoothPathCache& global() const { return *m_global; }
    const SmoothPathCache& layer_local() const { return *m_layer_local; }

private:
    const SmoothPathCache *m_global;
    const SmoothPathCache *m_layer_local;
};

} // namespace GCode
} // namespace Slic3r

#endif // slic3r_GCode_SmoothPath_hpp_
