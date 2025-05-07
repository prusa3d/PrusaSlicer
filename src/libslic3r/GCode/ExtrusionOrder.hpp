#ifndef slic3r_GCode_ExtrusionOrder_hpp_
#define slic3r_GCode_ExtrusionOrder_hpp_

#include <vector>
#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <utility>

#include "libslic3r/GCode/SmoothPath.hpp"
#include "libslic3r/GCode/WipeTowerIntegration.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/GCode/SeamPlacer.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/ShortestPath.hpp"
#include "libslic3r/GCode/ToolOrdering.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r {
class ExtrusionEntity;
class ExtrusionEntityReference;
class Print;
class PrintObject;
class PrintRegion;

namespace GCode {
class WipeTowerIntegration;
}  // namespace GCode
struct PrintInstance;
}  // namespace Slic3r

namespace Slic3r::GCode {
// Object and support extrusions of the same PrintObject at the same print_z.
// public, so that it could be accessed by free helper functions from GCode.cpp
struct ObjectLayerToPrint
{
    ObjectLayerToPrint() : object_layer(nullptr), support_layer(nullptr) {}
    const Layer *object_layer;
    const SupportLayer *support_layer;
    const Layer *layer() const { return (object_layer != nullptr) ? object_layer : support_layer; }
    const PrintObject *object() const {
        return (this->layer() != nullptr) ? this->layer()->object() : nullptr;
    }
    coordf_t print_z() const {
        return (object_layer != nullptr && support_layer != nullptr) ?
            0.5 * (object_layer->print_z + support_layer->print_z) :
            this->layer()->print_z;
    }
};

using ObjectsLayerToPrint = std::vector<GCode::ObjectLayerToPrint>;

struct InstanceToPrint
{
    InstanceToPrint(
        size_t object_layer_to_print_id, const PrintObject &print_object, size_t instance_id
    )
        : object_layer_to_print_id(object_layer_to_print_id)
        , print_object(print_object)
        , instance_id(instance_id) {}

    // Index into std::vector<ObjectLayerToPrint>, which contains Object and Support layers for the
    // current print_z, collected for a single object, or for possibly multiple objects with
    // multiple instances.
    const size_t object_layer_to_print_id;
    const PrintObject &print_object;
    // Instance idx of the copy of a print object.
    const size_t instance_id;
};
} // namespace Slic3r::GCode

namespace Slic3r::GCode::ExtrusionOrder {

struct InfillRange {
    std::vector<SmoothPath> items;
    const PrintRegion *region;
};

struct Perimeter {
    GCode::SmoothPath smooth_path;
    bool reversed;
    const ExtrusionEntity *extrusion_entity;
    std::size_t wipe_offset;
};

struct IslandExtrusions {
    const PrintRegion *region;
    std::vector<Perimeter> perimeters;
    std::vector<InfillRange> infill_ranges;
    bool infill_first{false};
};

struct SliceExtrusions {
    std::vector<IslandExtrusions> common_extrusions;
    std::vector<InfillRange> ironing_extrusions;
};

struct SupportPath {
    SmoothPath path;
    bool is_interface;
};

struct NormalExtrusions {
    Point instance_offset;
    std::vector<SupportPath> support_extrusions;
    std::vector<SliceExtrusions> slices_extrusions;
};

struct OverridenExtrusions {
    Point instance_offset;
    std::vector<SliceExtrusions> slices_extrusions;
};

/**
 * Intentionally strong type representing a point in a coordinate system
 * of an instance. Introduced to avoid confusion between local and
 * global coordinate systems.
 */
struct InstancePoint {
    Point local_point;
};

using PathSmoothingResult = std::pair<GCode::SmoothPath, std::size_t>;
using PathSmoothingFunction = std::function<PathSmoothingResult(
    const Layer *, const PrintRegion *, const ExtrusionEntityReference &, const unsigned extruder_id, std::optional<InstancePoint> &previous_position
)>;

struct BrimPath {
    SmoothPath path;
    bool is_loop;
};

struct ExtruderExtrusions {
    unsigned extruder_id;
    std::vector<std::pair<std::size_t, GCode::SmoothPath>> skirt;
    std::vector<BrimPath> brim;
    std::vector<OverridenExtrusions> overriden_extrusions;
    std::vector<NormalExtrusions> normal_extrusions;
    std::optional<Point> wipe_tower_start;
};

static constexpr const double min_gcode_segment_length = 0.002;

bool is_empty(const std::vector<SliceExtrusions> &extrusions);

std::vector<ExtruderExtrusions> get_extrusions(
    const Print &print,
    const GCode::WipeTowerIntegration *wipe_tower,
    const GCode::ObjectsLayerToPrint &layers,
    const bool is_first_layer,
    const LayerTools &layer_tools,
    const std::vector<InstanceToPrint> &instances_to_print,
    const std::map<unsigned int, std::pair<size_t, size_t>> &skirt_loops_per_extruder,
    unsigned current_extruder_id,
    const PathSmoothingFunction &smooth_path,
    bool get_brim,
    std::optional<Point> previous_position
);

std::optional<Geometry::ArcWelder::Segment> get_first_point(const std::vector<ExtruderExtrusions> &extrusions);

const PrintInstance * get_first_instance(
    const std::vector<ExtruderExtrusions> &extrusions,
    const std::vector<InstanceToPrint> &instances_to_print
);
}

#endif // slic3r_GCode_ExtrusionOrder_hpp_
