#ifndef slic3r_SLA_SuppotstIslands_EvaluateNeighbor_hpp_
#define slic3r_SLA_SuppotstIslands_EvaluateNeighbor_hpp_

#include <memory>

#include "Slic3r/Biz/Algorithms/IStackFunction.hpp"
#include "Slic3r/Biz/CGAL/Algorithms/PostProcessNeighbors.hpp"
#include "Slic3r/Biz/CGAL/Algorithms/VoronoiGraph.hpp"

namespace Slic3r::Biz::CGAL::Algorithms {

/// <summary>
/// create on stack
///  1 * PostProcessNeighbors
///  N * ExpandNode
/// </summary>
class EvaluateNeighbor : public Biz::Algorithms::IStackFunction
{
    std::unique_ptr<PostProcessNeighbors> post_process_neighbor;
public:
    EvaluateNeighbor(
        VoronoiGraph::ExPath &    result,
        const VoronoiGraph::Node *node,
        double                    distance_to_node = 0.,
        const VoronoiGraph::Path &prev_path = VoronoiGraph::Path({}, 0.));

    /// <summary>
    /// create on stack
    ///  1 * PostProcessNeighbors
    ///  N * ExpandNode
    /// </summary>
    virtual void process(Biz::Algorithms::CallStack &call_stack);
};

} // namespace Slic3r::sla
#endif // slic3r_SLA_SuppotstIslands_EvaluateNeighbor_hpp_
