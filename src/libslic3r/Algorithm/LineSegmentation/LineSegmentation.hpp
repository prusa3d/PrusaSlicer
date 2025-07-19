#ifndef libslic3r_LineSegmentation_hpp_
#define libslic3r_LineSegmentation_hpp_

#include <vector>

#include "libslic3r/Arachne/utils/ExtrusionLine.hpp"

namespace Slic3r {
class ExPolygon;
class Polyline;
class Polygon;
class PrintRegionConfig;

struct PerimeterRegion;

using ExPolygons       = std::vector<ExPolygon>;
using PerimeterRegions = std::vector<PerimeterRegion>;
} // namespace Slic3r

namespace Slic3r::Arachne {
struct ExtrusionLine;
}

namespace Slic3r::Algorithm::LineSegmentation {

struct PolylineSegment
{
    Polyline polyline;
    size_t   clip_idx;
};

struct PolylineRegionSegment
{
    Polyline                 polyline;
    const PrintRegionConfig &config;

    PolylineRegionSegment(const Polyline &polyline, const PrintRegionConfig &config) : polyline(polyline), config(config) {}
};

struct ExtrusionSegment
{
    Arachne::ExtrusionLine extrusion;
    size_t                 clip_idx;
};

struct ExtrusionRegionSegment
{
    Arachne::ExtrusionLine   extrusion;
    const PrintRegionConfig &config;

    ExtrusionRegionSegment(const Arachne::ExtrusionLine &extrusion, const PrintRegionConfig &config) : extrusion(extrusion), config(config) {}
};

using PolylineSegments        = std::vector<PolylineSegment>;
using ExtrusionSegments       = std::vector<ExtrusionSegment>;
using PolylineRegionSegments  = std::vector<PolylineRegionSegment>;
using ExtrusionRegionSegments = std::vector<ExtrusionRegionSegment>;

PolylineSegments polyline_segmentation(const Polyline &subject, const std::vector<ExPolygons> &expolygons_clips, size_t default_clip_idx = 0);
PolylineSegments polygon_segmentation(const Polygon &subject, const std::vector<ExPolygons> &expolygons_clips, size_t default_clip_idx = 0);
ExtrusionSegments extrusion_segmentation(const Arachne::ExtrusionLine &subject, const std::vector<ExPolygons> &expolygons_clips, size_t default_clip_idx = 0);

PolylineRegionSegments polyline_segmentation(const Polyline &subject, const PrintRegionConfig &base_config, const PerimeterRegions &perimeter_regions_clips);
PolylineRegionSegments polygon_segmentation(const Polygon &subject, const PrintRegionConfig &base_config, const PerimeterRegions &perimeter_regions_clips);
ExtrusionRegionSegments extrusion_segmentation(const Arachne::ExtrusionLine &subject, const PrintRegionConfig &base_config, const PerimeterRegions &perimeter_regions_clips);

} // namespace Slic3r::Algorithm::LineSegmentation

#endif // libslic3r_LineSegmentation_hpp_
