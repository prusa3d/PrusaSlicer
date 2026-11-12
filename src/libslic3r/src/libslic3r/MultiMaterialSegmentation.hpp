#ifndef slic3r_MultiMaterialSegmentation_hpp_
#define slic3r_MultiMaterialSegmentation_hpp_

#include <boost/polygon/polygon.hpp>
#include <utility>
#include <vector>
#include <functional>

#include "Slic3r/Domain/FacetsAnnotation.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Line.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/libslic3r.h"
#include "Slic3r/Biz/Algorithms/ColoredLine.hpp"

namespace Slic3r {

class PrintObject;

enum class IncludeTopAndBottomLayers {
    Yes,
    No
};

struct ModelVolumeFacetsInfo {
    const Domain::FacetsAnnotation &facets_annotation;
    // Indicate if model volume is painted.
    const bool                      is_painted;
    // Indicate if the default extruder (TriangleStateType::NONE) should be replaced with the volume extruder.
    const bool                      replace_default_extruder;
};

BoundingBox get_extents(const std::vector<Biz::Algorithms::ColoredLines> &colored_polygons);

// Returns segmentation based on painting in segmentation gizmos.
std::vector<std::vector<ExPolygons>> segmentation_by_painting(const PrintObject                                                       &print_object,
                                                              const std::function<ModelVolumeFacetsInfo(const Domain::ModelVolume &)> &extract_facets_info,
                                                              size_t                                                                   num_facets_states,
                                                              float                                                                    segmentation_max_width,
                                                              float                                                                    segmentation_interlocking_depth,
                                                              IncludeTopAndBottomLayers                                                include_top_and_bottom_layers,
                                                              const std::function<void()>                                             &throw_on_cancel_callback);

// Returns multi-material segmentation based on painting in multi-material segmentation gizmo
std::vector<std::vector<ExPolygons>> multi_material_segmentation_by_painting(const PrintObject &print_object, const std::function<void()> &throw_on_cancel_callback);

// Returns fuzzy skin segmentation based on painting in fuzzy skin segmentation gizmo
std::vector<std::vector<ExPolygons>> fuzzy_skin_segmentation_by_painting(const PrintObject &print_object, const std::function<void()> &throw_on_cancel_callback);

} // namespace Slic3r

#endif // slic3r_MultiMaterialSegmentation_hpp_
