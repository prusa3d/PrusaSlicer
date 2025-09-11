#include "SupportIslandPoint.hpp"
#include "VoronoiGraphUtils.hpp"
#include "LineUtils.hpp"

using namespace Slic3r::sla;

SupportIslandPoint::SupportIslandPoint(Slic3r::Point point, Type type)
    : point(std::move(point)), type(type)
{}

bool SupportIslandPoint::can_move(const Type &type)
{
    static const std::set<Type> cant_move({
        Type::one_bb_center_point,
        Type::one_center_point,
        Type::two_points,
    });
    return cant_move.find(type) == cant_move.end();
}

bool SupportIslandPoint::can_move() const { return can_move(type); }

coord_t SupportIslandPoint::move(const Point &destination)
{
    Point diff = destination - point;
    point      = destination;
    return abs(diff.x()) + abs(diff.y()); // Manhatn distance
}

std::string SupportIslandPoint::to_string(const Type &type)
{
    static std::map<Type, std::string> type_to_string=
        {{Type::one_center_point,   "one_center_point"},
         {Type::two_points,         "two_points"},
         {Type::two_points_backup,  "two_points_backup"},
         {Type::one_bb_center_point,"one_bb_center_point"},
         {Type::thin_part,          "thin_part"},
         {Type::thin_part_change,   "thin_part_change"},
         {Type::thin_part_loop,     "thin_part_loop"},
         {Type::thick_part_outline, "thick_part_outline"},
         {Type::thick_part_inner,   "thick_part_inner"},
         {Type::bad_shape_for_vd,   "bad_shape_for_vd"},
         {Type::permanent,          "permanent"},
         {Type::undefined,          "undefined"}};
    auto it = type_to_string.find(type);
    if (it == type_to_string.end()) 
        return to_string(Type::undefined);
    return it->second;
}

///////////////
// Point on VD
///////////////

SupportCenterIslandPoint::SupportCenterIslandPoint(
    VoronoiGraph::Position         position,
    const SampleConfig *   configuration,
    Type                           type)
    : SupportIslandPoint(VoronoiGraphUtils::create_edge_point(position), type)
    , configuration(configuration)
    , position(position)
{}

coord_t SupportCenterIslandPoint::move(const Point &destination)
{        
    // move only along VD
    // TODO: Start respect minimum distance from outline !!
    position =
        VoronoiGraphUtils::align(position, destination,
                                 configuration->max_align_distance);
    Point new_point  = VoronoiGraphUtils::create_edge_point(position);
    return SupportIslandPoint::move(new_point);
}

///////////////
// Point on Outline
///////////////

SupportOutlineIslandPoint::SupportOutlineIslandPoint(
    Position position, std::shared_ptr<Restriction> restriction, Type type)
    : SupportIslandPoint(calc_point(position, *restriction), type)
    , position(position)
    , restriction(std::move(restriction))
{}

bool SupportOutlineIslandPoint::can_move() const { return true; }

coord_t SupportOutlineIslandPoint::move(const Point &destination)
{
    size_t index   = position.index;
    MoveResult closest = create_result(index, destination);

    const double &length = restriction->lengths[position.index];
    double distance = (1.0 - position.ratio) * length;
    while (distance < restriction->max_align_distance) {
        auto next_index = restriction->next_index(index);
        if (!next_index.has_value()) break;
        index = *next_index;
        update_result(closest, index, destination);
        distance += restriction->lengths[index];
    }

    index    = position.index;
    distance = static_cast<coord_t>(position.ratio) * length;
    while (distance < restriction->max_align_distance) {
        auto prev_index = restriction->prev_index(index);
        if (!prev_index.has_value()) break;
        index         = *prev_index;
        update_result(closest, index, destination);
        distance += restriction->lengths[index];
    }

    // apply closest result of move
    this->position = closest.position;
    return SupportIslandPoint::move(closest.point);
}

Slic3r::Point SupportOutlineIslandPoint::calc_point(const Position &position, const Restriction &restriction)
{
    const Line &line = restriction.lines[position.index];
    Point direction = LineUtils::direction(line);
    return line.a + direction * position.ratio;
}

SupportOutlineIslandPoint::MoveResult SupportOutlineIslandPoint::create_result(
    size_t index, const Point &destination)
{
    const Line &line       = restriction->lines[index];
    double      line_ratio_full = LineUtils::foot(line, destination);
    double      line_ratio      = std::clamp(line_ratio_full, 0., 1.);
    Position    position(index, line_ratio);
    Point       point = calc_point(position, *restriction);
    double distance_double = (point - destination).cast<double>().norm();
    coord_t     distance        = static_cast<coord_t>(distance_double);
    return MoveResult(position, point, distance);
}

void SupportOutlineIslandPoint::update_result(MoveResult & result,
                                              size_t       index,
                                              const Point &destination)
{
    const Line &line       = restriction->lines[index];
    double      line_ratio_full = LineUtils::foot(line, destination);
    double      line_ratio = std::clamp(line_ratio_full, 0., 1.);
    Position    position(index, line_ratio);
    Point       point = calc_point(position, *restriction);
    Point       diff      = point - destination;
    if (abs(diff.x()) > result.distance) return;
    if (abs(diff.y()) > result.distance) return;
    double distance_double = diff.cast<double>().norm();
    coord_t distance = static_cast<coord_t>(distance_double);
    if (result.distance > distance) {
        result.distance = distance;
        result.position = position;
        result.point    = point;
    }
}

////////////////////
/// Inner Point
/////////////////////// 

SupportIslandInnerPoint::SupportIslandInnerPoint(
    Point point, std::shared_ptr<ExPolygons> inner, Type type)
    : SupportIslandPoint(point, type), inner(std::move(inner))
{}

coord_t SupportIslandInnerPoint::move(const Point &destination) {

    // IMPROVE: Do not move over island hole if there is no connected island. 
    // Can cause bad supported area in very special case.
    for (const ExPolygon& inner_expolygon: *inner)
        if (inner_expolygon.contains(destination))
            return SupportIslandPoint::move(destination);

    // find closest line cross area border
    Vec2d v1 = (destination-point).cast<double>();
    double closest_ratio = 1.;
    Lines lines = to_lines(*inner);
    for (const Line &line : lines) {
        // line intersection       
        const Vec2d v2 = LineUtils::direction(line).cast<double>();
        double denom = cross2(v1, v2);
        // is line parallel
        if (fabs(denom) < std::numeric_limits<float>::epsilon()) continue;

        const Vec2d v12  = (point - line.a).cast<double>();
        double      nume1 = cross2(v2, v12);
        double      t1    = nume1 / denom;
        if (t1 < 0. || t1 > closest_ratio) continue; // out of line

        double nume2 = cross2(v1, v12);
        double t2     = nume2 / denom;
        if (t2 < 0. || t2 > 1.0) continue; // out of contour

        closest_ratio = t1;
    }
    // no correct closest point --> almost parallel cross
    if (closest_ratio >= 1.) return 0;
    Point new_point = point + (closest_ratio * v1).cast<coord_t>();
    return SupportIslandPoint::move(new_point);
}