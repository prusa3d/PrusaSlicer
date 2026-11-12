#ifndef slic3r_SLA_SuppotstIslands_Parabola_hpp_
#define slic3r_SLA_SuppotstIslands_Parabola_hpp_

#include "Slic3r/Domain/Line.hpp"
#include "Slic3r/Domain/Point.hpp"

namespace Slic3r::Biz::Algorithms {

/// <summary>
/// DTO store prabola params
/// A parabola can be defined geometrically as a set of points (locus of points) in the Euclidean plane:
/// Where distance from focus point is same as distance from line(directrix).
/// </summary>
struct Parabola
{
    Domain::Line  directrix;
    Domain::Point focus;

    Parabola(Domain::Line directrix, Domain::Point focus)
        : directrix(std::move(directrix)), focus(std::move(focus))
    {}
};


/// <summary>
/// DTO store segment of parabola
/// Parabola with start(from) and end(to) point lay on parabola
/// </summary>
struct ParabolaSegment: public Parabola
{
    Domain::Point from;
    Domain::Point to;

    ParabolaSegment(Parabola parabola, Domain::Point from, Domain::Point to) : 
        Parabola(std::move(parabola)), from(from), to(to)
    {}
    ParabolaSegment(Domain::Line directrix, Domain::Point focus, Domain::Point from, Domain::Point to)
        : Parabola(directrix, focus), from(from), to(to)
    {}
};

} // namespace Slic3r::sla
#endif // slic3r_SLA_SuppotstIslands_Parabola_hpp_
