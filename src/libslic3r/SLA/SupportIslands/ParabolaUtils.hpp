#ifndef slic3r_SLA_SuppotstIslands_ParabolaUtils_hpp_
#define slic3r_SLA_SuppotstIslands_ParabolaUtils_hpp_

#include "Parabola.hpp"
#include <libslic3r/SVG.hpp>

namespace Slic3r::sla {

/// <summary>
/// Class which contain collection of static function
/// for work with Parabola.
/// </summary>
class ParabolaUtils
{
public:
    ParabolaUtils() = delete;

    /// <summary>
    /// Integrate length over interval defined by points from and to
    /// </summary>
    /// <param name="parabola">Input segment of parabola</param>
    /// <returns>Length of parabola arc</returns>
    static double length(const ParabolaSegment &parabola);

    /// <summary>
    /// Sample parabola between points from and to by step.
    /// </summary>
    /// <param name="parabola">Input segment of parabola</param>
    /// <param name="discretization_step">Define sampling</param>
    /// <returns>Length of parabola arc</returns>
    static double length_by_sampling(const ParabolaSegment &parabola,
                                     double discretization_step = 200);

    /// <summary>
    /// calculate focal length of parabola
    /// </summary>
    /// <param name="parabola">input parabola</param>
    /// <returns>Focal length</returns>
    static double focal_length(const Parabola &parabola);

    /// <summary>
    /// Check if parabola interval (from, to) contains top of parabola
    /// </summary>
    /// <param name="parabola">Input segment of parabola</param>
    /// <returns>True when interval contain top of parabola otherwise False</returns>
    static bool is_over_zero(const ParabolaSegment &parabola);

    /// <summary>
    /// Connvert parabola to svg by sampling
    /// </summary>
    /// <param name="svg">outputfile</param>
    /// <param name="parabola">parabola to draw</param>
    /// <param name="color">color</param>
    /// <param name="width">width</param>
    /// <param name="discretization_step">step between discretized lines</param>
    static void draw(SVG &                  svg,
                     const ParabolaSegment &parabola,
                     const char *           color,
                     coord_t                width,
                     double                 discretization_step = 1e3);

private:
    /// <summary>
    /// Integral of parabola: y = x^2 from zero to point x
    /// https://ocw.mit.edu/courses/mathematics/18-01sc-single-variable-calculus-fall-2010/unit-4-techniques-of-integration/part-b-partial-fractions-integration-by-parts-arc-length-and-surface-area/session-78-computing-the-length-of-a-curve/MIT18_01SCF10_Ses78d.pdf
    /// </summary>
    /// <param name="x">x coordinate of parabola, Positive number</param>
    /// <returns>Length of parabola from zero to x</returns> 
    static double parabola_arc_length(double x);
};

} // namespace Slic3r::sla
#endif // slic3r_SLA_SuppotstIslands_ParabolaUtils_hpp_
