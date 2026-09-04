#ifndef slic3r_SLA_SuppotstIslands_SampleConfig_hpp_
#define slic3r_SLA_SuppotstIslands_SampleConfig_hpp_

#include "Slic3r/Biz/Algorithms/Scaling.hpp"
#include "Slic3r/Domain/Types.hpp"

#define OPTION_TO_STORE_ISLAND

namespace Slic3r::sla {

/**
@brief Configure how to prepare data for SupportPointGenerator
*/
struct PrepareSupportConfig
{
    // Size of the steps between discretized samples on the overhanging part of layer
    // Smaller value means more point to investigate in support process,
    // but smaller divergence of support distances
    double discretize_overhang_step = 2.;  // [in mm]

    // Detection of peninsula(half island)
    // Peninsula contain wider one layer overhang than this value
    float peninsula_min_width = static_cast<float>(Biz::Algorithms::Scaling::scaled(2.)); // [in scaled mm]

    // Distance from previous layer part to still supported
    float peninsula_self_supported_width = static_cast<float>(Biz::Algorithms::Scaling::scaled(1.5)); // [in scaled mm]

    // To be able support same 2d area multipletimes,
    // It is neccessary to remove support point form near KDTree structure
    // Must be greater than surface texture and lower than self supporting area
    // May be use maximal island distance
    float removing_delta = static_cast<float>(Biz::Algorithms::Scaling::scaled(5.));

    // Define minimal size of separable model part which will be filtered out
    // Half of support head diameter is impossible to print other than sphere from support head
    float minimal_bounding_sphere_radius = 0.2f; // [in mm]
};

/**
@brief Configuration DTO
Define where is neccessary to put support point on island
Mainly created by SampleConfigFactory
*/
struct SampleConfig
{
    // Maximal distance of support points on thin island's part
    // MUST be bigger than zero
    Domain::coord_t thin_max_distance = static_cast<Domain::coord_t>(Biz::Algorithms::Scaling::scaled(5.));

    // Maximal distance of support points inside of thick island's part
    // MUST be bigger than zero
    Domain::coord_t thick_inner_max_distance = static_cast<Domain::coord_t>(Biz::Algorithms::Scaling::scaled(5.));

    // Maximal distance of support points on outline of thick island's part
    // Sample outline of Field by this value
    // MUST be bigger than zero
    Domain::coord_t thick_outline_max_distance = static_cast<Domain::coord_t>(Biz::Algorithms::Scaling::scaled(5. * 3. / 4.));

    // Support point head radius
    // MUST be bigger than zero
    Domain::coord_t head_radius = static_cast<Domain::coord_t>(Biz::Algorithms::Scaling::scaled(.4)); // [nano meter]

    // When it is possible, there will be this minimal distance from outline.
    // zero when head should be on outline
    Domain::coord_t minimal_distance_from_outline = 0; // [nano meter]

    // Measured as sum of VD edge length from outline
    // Used only when there is no space for outline offset on first/last point
    // Must be bigger than minimal_distance_from_outline
    Domain::coord_t maximal_distance_from_outline = static_cast<Domain::coord_t>(Biz::Algorithms::Scaling::scaled(1.));// [nano meter]

    // Maximal length of longest path in voronoi diagram to be island
    // supported only by one single support point this point will be in center of path.
    Domain::coord_t max_length_for_one_support_point = static_cast<Domain::coord_t>(Biz::Algorithms::Scaling::scaled(1.));

    // Maximal length of island supported by 2 points
    Domain::coord_t max_length_for_two_support_points = static_cast<Domain::coord_t>(Biz::Algorithms::Scaling::scaled(1.));
    // Maximal ratio of path length for island supported by 2 points
    // Used only in case when maximal_distance_from_outline is bigger than 
    // current island longest_path * this ratio
    // Note: Preven for tiny island to contain overlapped support points
    // must be smaller than 0.5 and bigger than zero
    float max_length_ratio_for_two_support_points = 0.25; // |--25%--Sup----50%----Sup--25%--|

    // Maximal width of line island supported in the middle of line
    // Must be greater or equal to thick_min_width
    Domain::coord_t thin_max_width = static_cast<Domain::coord_t>(Biz::Algorithms::Scaling::scaled(1.));

    // Minimal width to be supported by outline
    // Must be smaller or equal to thin_max_width
    Domain::coord_t thick_min_width = static_cast<Domain::coord_t>(Biz::Algorithms::Scaling::scaled(1.));

    // Minimal length of island's part to create tiny&thick interface
    Domain::coord_t min_part_length = static_cast<Domain::coord_t>(Biz::Algorithms::Scaling::scaled(1.));

    // Term criteria for end of alignment
    // Minimal change in manhatn move of support position before termination
    Domain::coord_t minimal_move = static_cast<Domain::coord_t>(Biz::Algorithms::Scaling::scaled(.01)); // devide from print resolution to quater pixel

    // Maximal count of align iteration
    size_t count_iteration = 100;
    
    // Maximal distance over Voronoi diagram edges to find closest point during aligning Support point
    Domain::coord_t max_align_distance = 0; // [scaled mm -> nanometers]

    // There is no need to calculate with precisse island
    // NOTE: Slice of Cylinder bottom has tip of trinagles on contour
    // (neighbor coordinate - create issue in voronoi)
    double simplification_tolerance = static_cast<double>(Biz::Algorithms::Scaling::scaled(0.05 /*mm*/));

#ifdef OPTION_TO_STORE_ISLAND
    // Only for debug purposes
    std::string path = ""; // when set to empty string, no debug output is generated
#endif // OPTION_TO_STORE_ISLAND

    // Configuration for data preparation
    PrepareSupportConfig prepare_config;
};
} // namespace Slic3r::sla
#endif // slic3r_SLA_SuppotstIslands_SampleConfig_hpp_
