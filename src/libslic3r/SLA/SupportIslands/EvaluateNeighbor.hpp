#ifndef slic3r_SLA_SuppotstIslands_EvaluateNeighbor_hpp_
#define slic3r_SLA_SuppotstIslands_EvaluateNeighbor_hpp_

#include <memory>

#include "IStackFunction.hpp"
#include "PostProcessNeighbors.hpp"
#include "VoronoiGraph.hpp"

namespace Slic3r::sla {

/// <summary>
/// create on stack
///  1 * PostProcessNeighbors
///  N * ExpandNode
/// </summary>
class EvaluateNeighbor : public IStackFunction
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
    virtual void process(CallStack &call_stack);
};

} // namespace Slic3r::sla
#endif // slic3r_SLA_SuppotstIslands_EvaluateNeighbor_hpp_
