#ifndef slic3r_SLA_SuppotstIslands_Parabola_hpp_
#define slic3r_SLA_SuppotstIslands_Parabola_hpp_

#include <libslic3r/Line.hpp>
#include <libslic3r/Point.hpp>

namespace Slic3r::sla {

/// <summary>
/// DTO store prabola params
/// A parabola can be defined geometrically as a set of points (locus of points) in the Euclidean plane:
/// Where distance from focus point is same as distance from line(directrix).
/// </summary>
struct Parabola
{
    Line  directrix;
    Point focus;

    Parabola(Line directrix, Point focus)
        : directrix(std::move(directrix)), focus(std::move(focus))
    {}
};


/// <summary>
/// DTO store segment of parabola
/// Parabola with start(from) and end(to) point lay on parabola
/// </summary>
struct ParabolaSegment: public Parabola
{
    Point from;
    Point to;

    ParabolaSegment(Parabola parabola, Point from, Point to) : 
        Parabola(std::move(parabola)), from(from), to(to)
    {}
    ParabolaSegment(Line directrix, Point focus, Point from, Point to)
        : Parabola(directrix, focus), from(from), to(to)
    {}
};

} // namespace Slic3r::sla
#endif // slic3r_SLA_SuppotstIslands_Parabola_hpp_
