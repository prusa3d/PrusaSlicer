#ifndef slic3r_SLA_SuppotstIslands_SupportIslandPoint_hpp_
#define slic3r_SLA_SuppotstIslands_SupportIslandPoint_hpp_

#include <set>
#include <memory>
#include <libslic3r/Point.hpp>
#include "VoronoiGraph.hpp"
#include "SampleConfig.hpp"

namespace Slic3r::sla {

/// <summary>
/// DTO position with information about source of support point
/// </summary>
class SupportIslandPoint
{
public:
    enum class Type: unsigned char {
        one_bb_center_point,  // for island smaller than head radius
        one_center_point,     // small enough to support only by one support point
        two_points,           // island stretched between two points
        two_points_backup,    // same as before but forced after divide to thin&thick with small amoutn of points
        thin_part,            // point on thin part of island lay on VD
        thin_part_change,     // on the first edge -> together with change to thick part of island
        thin_part_loop,       // on the last edge -> loop into itself part of island
        thick_part_outline,   // keep position align with island outline 
        thick_part_inner,     // point inside wide part, without restriction on move

        bad_shape_for_vd,     // can't make a Voronoi diagram on the shape

        permanent,            // permanent support point with static position
        undefined
    };

    Type type; 
    Point point;

public:
    /// <summary>
    /// constructor
    /// </summary>
    /// <param name="point">coordinate point inside a layer (in one slice)</param>
    /// <param name="type">type of support point</param>
    SupportIslandPoint(Point point, Type type = Type::undefined);

    /// <summary>
    /// virtual destructor to be inheritable
    /// </summary>
    virtual ~SupportIslandPoint() = default;

    /// <summary>
    /// static function to decide if type is possible to move or not
    /// </summary>
    /// <param name="type">type to distinguish</param>
    /// <returns>True when is possible to move, otherwise FALSE</returns>
    static bool can_move(const Type &type);
        
    /// <summary>
    /// static function to decide if type is possible to move or not
    /// </summary>
    /// <param name="type">type to distinguish</param>
    /// <returns>True when is possible to move, otherwise FALSE</returns>
    virtual bool can_move() const;

    /// <summary>
    /// Move position of support point close to destination
    /// with support point restrictions
    /// </summary>
    /// <param name="destination">Wanted position</param>
    /// <returns>Move distance</returns>
    virtual coord_t move(const Point &destination);

    /// <summary>
    /// Convert type to string value
    /// </summary>
    /// <param name="type">Input type</param>
    /// <returns>String type</returns>
    static std::string to_string(const Type &type);
};

using SupportIslandPointPtr = std::unique_ptr<SupportIslandPoint>;
using SupportIslandPoints = std::vector<SupportIslandPointPtr>;

/// <summary>
/// Support point with no move during aligning
/// </summary>
class SupportIslandNoMovePoint : public SupportIslandPoint
{
public:
    //constructor
    using SupportIslandPoint::SupportIslandPoint;

    /// <summary>
    /// Can move?
    /// </summary>
    /// <returns>FALSE</returns>
    bool can_move() const override { return false; }

    /// <summary>
    /// No move!
    /// Should not be call.
    /// </summary>
    /// <param name="destination">Wanted position</param>
    /// <returns>No move means zero distance</returns>
    coord_t move(const Point &destination) override { return 0; }
};

/// <summary>
/// DTO Support point laying on voronoi graph edge
/// Restriction to move only on Voronoi graph
/// </summary>
class SupportCenterIslandPoint : public SupportIslandPoint
{
public:
    // Define position on voronoi graph
    // FYI: Lose data when voronoi graph does NOT exist
    VoronoiGraph::Position position;

    // hold pointer to configuration
    // FYI: Lose data when configuration destruct
    const SampleConfig *configuration;
public:
    SupportCenterIslandPoint(VoronoiGraph::Position position,
                             const SampleConfig *configuration,
                             Type                   type = Type::thin_part);
    
    bool can_move() const override{ return true; }
    coord_t move(const Point &destination) override;
};

/// <summary>
/// Support point laying on Outline of island
/// Restriction to move only on outline
/// </summary>
class SupportOutlineIslandPoint : public SupportIslandPoint
{
public:
    // definition of restriction
    class Restriction;

    struct Position
    {
        // index of line form island outline - index into Restriction
        // adress line inside inner polygon --> SupportOutline
        size_t index;

        // define position on line by ratio 
        // from 0 (line point a) 
        // to   1 (line point b)
        float ratio;

        Position(size_t index, float ratio) : index(index), ratio(ratio) {}
    };    
    Position position;


    // store lines for allowed move - with distance from island source lines
    std::shared_ptr<Restriction> restriction;

public:
    SupportOutlineIslandPoint(Position                     position,
                              std::shared_ptr<Restriction> restriction,
                              Type type = Type::thick_part_outline);
    // return true
    bool can_move() const override;

    /// <summary>
    /// Move nearest to destination point
    /// only along restriction lines
    /// + change current position
    /// </summary>
    /// <param name="destination">Wanted support position</param>
    /// <returns>move distance manhatn</returns>
    coord_t move(const Point &destination) override;

    /// <summary>
    /// Calculate 2d point belong to line position
    /// </summary>
    /// <param name="position">Define position on line from restriction</param>
    /// <param name="restriction">Hold lines</param>
    /// <returns>Position in 2d</returns>
    static Point calc_point(const Position &   position,
                            const Restriction &restriction);

    /// <summary>
    /// Keep data for align support point on bordred of island
    /// Define possible move of point along outline
    /// IMPROVE: Should contain list of Points on outline.
    /// (to keep maximal distance of neighbor points on outline)
    /// </summary>
    class Restriction
    {
    public:
        // line restriction
        // must be connected line.a == prev_line.b && line.b == next_line.a
        Lines lines;

        // keep stored line lengths
        // same size as lines
        std::vector<double> lengths;

        // maximal distance for search nearest line to destination point during aligning
        coord_t max_align_distance;

        Restriction(Lines               lines,
                    std::vector<double> lengths,
                    coord_t             max_align_distance)
            : lines(lines)
            , lengths(lengths)
            , max_align_distance(max_align_distance)
        {
            assert(lines.size() == lengths.size());
        }
        virtual ~Restriction() = default;
        virtual std::optional<size_t> next_index(size_t index) const = 0;
        virtual std::optional<size_t> prev_index(size_t index) const = 0;
    };
    
    class RestrictionLineSequence: public Restriction
    {
    public:
        // inherit constructors
        using Restriction::Restriction;
        
        virtual std::optional<size_t> next_index(size_t index) const override
        {
            assert(index < lines.size());
            ++index;
            if (index >= lines.size()) return {}; // index out of range
            return index;
        }

        virtual std::optional<size_t> prev_index(size_t index) const override
        {
            assert(index < lines.size());
            if (index >= lines.size()) return {}; // index out of range
            if (index == 0) return {}; // no prev line
            return index - 1;
        }
    };

    class RestrictionCircleSequence : public Restriction
    {
    public:
        // inherit constructors
        using Restriction::Restriction;

        virtual std::optional<size_t> next_index(size_t index) const override
        {
            assert(index < lines.size());
            if (index >= lines.size()) return {}; // index out of range
            ++index;
            if (index == lines.size()) return 0;
            return index;
        }

        virtual std::optional<size_t> prev_index(size_t index) const override
        {
            assert(index < lines.size());
            if (index >= lines.size()) return {}; // index out of range
            if (index == 0) return lines.size() - 1;
            return index - 1;
        }
    };

private:
    // DTO for result of move
    struct MoveResult
    {
        // define position on restriction line
        Position position;
        // point laying on restricted line
        Point point;
        // distance point on restricted line from destination point
        coord_t distance;

        MoveResult(Position position, Point point, coord_t distance)
            : position(position), point(point), distance(distance)
        {}
    };
    MoveResult create_result(size_t index, const Point &destination);
    void update_result(MoveResult& result, size_t index, const Point &destination);
};

/// <summary>
/// Store pointer to inner ExPolygon for allowed move across island area
/// Give an option to move with point
/// </summary>
class SupportIslandInnerPoint: public SupportIslandPoint
{    
    // define inner area of island where inner point could move during aligning
    std::shared_ptr<ExPolygons> inner;
public:
    SupportIslandInnerPoint(Point     point,
                            std::shared_ptr<ExPolygons> inner,
                            Type      type = Type::thick_part_inner);
    
    bool can_move() const override { return true; };

    /// <summary>
    /// Move nearest to destination point
    /// only inside inner area
    /// + change current position
    /// </summary>
    /// <param name="destination">Wanted support position</param>
    /// <returns>move distance euclidean</returns>
    coord_t move(const Point &destination) override;
};

} // namespace Slic3r::sla
#endif // slic3r_SLA_SuppotstIslands_SupportIslandPoint_hpp_
