#ifndef slic3r_SLA_SuppotstIslands_UniformSupportIsland_hpp_
#define slic3r_SLA_SuppotstIslands_UniformSupportIsland_hpp_

#include <libslic3r/ExPolygon.hpp>
#include "SampleConfig.hpp"
#include "SupportIslandPoint.hpp"
#include "libslic3r/SLA/SupportPointGenerator.hpp" // Peninsula

namespace Slic3r::sla {

/// <summary>
/// Distribute support points across island area defined by ExPolygon.
/// </summary>
/// <param name="island">Shape of island</param>
/// <param name="permanent">Place supported by already existing supports</param>
/// <param name="config">Configuration of support density</param>
/// <returns>Support points laying inside of the island</returns>
SupportIslandPoints uniform_support_island(
    const ExPolygon &island, const Points &permanent, const SampleConfig &config);

/// <summary>
/// Distribute support points across peninsula
/// </summary>
/// <param name="peninsula">half island with anotation of the coast and land outline</param>
/// <param name="permanent">Place supported by already existing supports</param>
/// <param name="config">Density distribution parameters</param>
/// <returns>Support points laying inside of the peninsula</returns>
SupportIslandPoints uniform_support_peninsula(
    const Peninsula &peninsula, const Points& permanent, const SampleConfig &config);

/// <summary>
/// Check for tests that developer do not forget disable visualization after debuging.
/// </summary>
bool is_uniform_support_island_visualization_disabled();

} // namespace Slic3r::sla
#endif // slic3r_SLA_SuppotstIslands_UniformSupportIsland_hpp_
