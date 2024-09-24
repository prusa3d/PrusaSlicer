/**
 * @file
 * @brief Utility functions for travel gcode generation.
 */

#ifndef slic3r_GCode_Travels_hpp_
#define slic3r_GCode_Travels_hpp_

#include <tcbspan/span.hpp>
#include <boost/functional/hash.hpp>
#include <boost/math/special_functions/pow.hpp>
#include <boost/container_hash/hash.hpp>
#include <vector>
#include <functional>
#include <optional>
#include <cstddef>
#include <unordered_set>

#include "libslic3r/AABBTreeLines.hpp"
#include "libslic3r/Line.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/GCode/ExtrusionOrder.hpp"

// Forward declarations.
namespace Slic3r {
class Layer;
class Point;
class Linef;
class Polyline;
class FullPrintConfig;
class ExtrusionEntity;

} // namespace Slic3r

namespace Slic3r::GCode {
struct ObjectLayerToPrint;

using ObjectsLayerToPrint = std::vector<ObjectLayerToPrint>;

class ObjectOrExtrusionLinef : public Linef
{
public:
    ObjectOrExtrusionLinef() = delete;
    ObjectOrExtrusionLinef(const Vec2d &a, const Vec2d &b) : Linef(a, b) {}
    explicit ObjectOrExtrusionLinef(const Vec2d &a, const Vec2d &b, size_t object_layer_idx, size_t instance_idx)
        : Linef(a, b), object_layer_idx(int(object_layer_idx)), instance_idx(int(instance_idx)) {}
    ObjectOrExtrusionLinef(const Vec2d &a, const Vec2d &b, size_t object_layer_idx, size_t instance_idx, const ExtrusionEntity *extrusion_entity)
        : Linef(a, b), object_layer_idx(int(object_layer_idx)), instance_idx(int(instance_idx)), extrusion_entity(extrusion_entity) {}

    virtual ~ObjectOrExtrusionLinef() = default;

    const int              object_layer_idx = -1;
    const int              instance_idx     = -1;
    const ExtrusionEntity *extrusion_entity = nullptr;
};

struct ExtrudedExtrusionEntity
{
    const int              object_layer_idx = -1;
    const int              instance_idx     = -1;
    const ExtrusionEntity *extrusion_entity = nullptr;

    bool operator==(const ExtrudedExtrusionEntity &other) const
    {
        return extrusion_entity == other.extrusion_entity && object_layer_idx == other.object_layer_idx &&
               instance_idx == other.instance_idx;
    }
};

struct ExtrudedExtrusionEntityHash
{
    size_t operator()(const ExtrudedExtrusionEntity &eee) const noexcept
    {
        std::size_t seed = std::hash<const ExtrusionEntity *>{}(eee.extrusion_entity);
        boost::hash_combine(seed, std::hash<int>{}(eee.object_layer_idx));
        boost::hash_combine(seed, std::hash<int>{}(eee.instance_idx));
        return seed;
    }
};

class TravelObstacleTracker
{
public:
    void init_layer(const Layer &layer, const ObjectsLayerToPrint &objects_to_print);

    void mark_extruded(const ExtrusionEntity *extrusion_entity, size_t object_layer_idx, size_t instance_idx);

    bool is_extruded(const ObjectOrExtrusionLinef &line) const;

    const AABBTreeLines::LinesDistancer<ObjectOrExtrusionLinef> &previous_layer_distancer() const { return m_previous_layer_distancer; }

    const AABBTreeLines::LinesDistancer<ObjectOrExtrusionLinef> &current_layer_distancer() const { return m_current_layer_distancer; }

    const ObjectsLayerToPrint &objects_to_print() const { return m_objects_to_print; }

private:
    ObjectsLayerToPrint                                                      m_objects_to_print;
    AABBTreeLines::LinesDistancer<ObjectOrExtrusionLinef>                    m_previous_layer_distancer;

    AABBTreeLines::LinesDistancer<ObjectOrExtrusionLinef>                    m_current_layer_distancer;
    std::unordered_set<ExtrudedExtrusionEntity, ExtrudedExtrusionEntityHash> m_extruded_extrusion;
};
} // namespace Slic3r::GCode

namespace Slic3r::GCode::Impl::Travels {
/**
 * @brief A point on a curve with a distance from start.
 */
struct DistancedPoint
{
    Point point;
    double distance_from_start;
};

struct ElevatedTravelParams
{
    /** Maximal value of nozzle lift. */
    double lift_height{};

    /** Distance from travel to the middle of the smoothing parabola. */
    double slope_end{};

    /** Width of the smoothing parabola */
    double blend_width{};

    /** How many points should be used to approximate the parabola */
    unsigned parabola_points_count{};
};

/**
 * @brief A mathematical formula for a smooth function.
 *
 * It starts lineary increasing than there is a parabola part and
 * at the end it is flat.
 */
struct ElevatedTravelFormula
{
    ElevatedTravelFormula(const ElevatedTravelParams &params);
    double operator()(const double distance_from_start) const;

private:
    double slope_function(double distance_from_start) const;

    double smoothing_from;
    double smoothing_to;
    double blend_width;
    double lift_height;
    double slope_end;
};

/**
 * @brief Takes a path described as a list of points and adds points to it.
 *
 * @param xy_path A list of points describing a path in xy.
 * @param sorted_distances A sorted list of distances along the path.
 * @return Sliced path.
 *
 * The algorithm travels along the path segments and adds points to
 * the segments in such a way that the points have specified distances
 * from the xy_path start. **Any distances over the xy_path end will
 * be simply ignored.**
 *
 * Example usage - simplified for clarity:
 * @code
 * std::vector<double> distances{0.5, 1.5};
 * std::vector<Points> xy_path{{0, 0}, {1, 0}};
 * // produces
 * {{0, 0}, {0, 0.5}, {1, 0}}
 * // notice that 1.5 is omitted
 * @endcode
 */
std::vector<DistancedPoint> slice_xy_path(
    tcb::span<const Point> xy_path, tcb::span<const double> sorted_distances
);

/**
 * @brief Generate regulary spaced points on 1 axis. Includes both from and to.
 *
 * If count is 1, the point is in the middle of the range.
 */
std::vector<double> linspace(const double from, const double to, const unsigned count);

ElevatedTravelParams get_elevated_traval_params(
    const Polyline& xy_path,
    const FullPrintConfig &config,
    const unsigned extruder_id,
    const GCode::TravelObstacleTracker &obstacle_tracker
);

/**
 * @brief Simply return the xy_path with z coord set to elevation.
 */
Points3 generate_flat_travel(tcb::span<const Point> xy_path, const float elevation);

/**
 * @brief Take xy_path and genrate a travel acording to elevation.
 *
 * @param xy_path A list of points describing a path in xy.
 * @param ensure_points_at_distances See slice_xy_path sorted_distances.
 * @param elevation  A function taking current distance in mm as input and returning elevation in mm
 * as output.
 *
 * **Be aweare** that the elevation function operates in mm, while xy_path and returned travel are
 * in scaled coordinates.
 */
Points3 generate_elevated_travel(
    const tcb::span<const Point> xy_path,
    const std::vector<double> &ensure_points_at_distances,
    const double initial_elevation,
    const std::function<double(double)> &elevation
);

/**
 * @brief Given a AABB tree over lines find intersection with xy_path closest to the xy_path start.
 *
 * @param xy_path A path in 2D.
 * @param distancer AABB Tree over lines.
 * @param objects_to_print Objects to print are used to determine in which object xy_path starts.

 * @param ignore_starting_object_intersection When it is true, then the first intersection during traveling from the object out is ignored.
 * @return Distance to the first intersection if there is one.
 *
 * **Ignores intersection with xy_path starting point.**
 */
double get_first_crossed_line_distance(
    tcb::span<const Line> xy_path,
    const AABBTreeLines::LinesDistancer<ObjectOrExtrusionLinef> &distancer,
    const ObjectsLayerToPrint &objects_to_print = {},
    const std::function<bool(const ObjectOrExtrusionLinef &)> &predicate = [](const ObjectOrExtrusionLinef &) { return true; },
    bool ignore_starting_object_intersection = true);

/**
 * @brief Extract parameters and decide wheather the travel can be elevated.
 * Then generate the whole travel 3D path - elevated if possible.
 */
Points3 generate_travel_to_extrusion(
    const Polyline &xy_path,
    const FullPrintConfig &config,
    const unsigned extruder_id,
    const double initial_elevation,
    const GCode::TravelObstacleTracker &obstacle_tracker,
    const Point &xy_path_coord_origin
);
} // namespace Slic3r::GCode::Impl::Travels

#endif // slic3r_GCode_Travels_hpp_
