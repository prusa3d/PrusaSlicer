#ifndef slic3r_SLA_SuppotstIslands_ExpandNeighbor_hpp_
#define slic3r_SLA_SuppotstIslands_ExpandNeighbor_hpp_

#include "IStackFunction.hpp"
#include "VoronoiGraph.hpp"
#include "PostProcessNeighbor.hpp"
#include "EvaluateNeighbor.hpp"

namespace Slic3r::sla {

/// <summary>
/// Expand neighbor to
///  - PostProcessNeighbor
///  - EvaluateNeighbor
/// </summary>
class ExpandNeighbor : public IStackFunction
{
    NodeDataWithResult &                data;
    const VoronoiGraph::Node::Neighbor &neighbor;

public:
    ExpandNeighbor(NodeDataWithResult &                data,
                   const VoronoiGraph::Node::Neighbor &neighbor);

    /// <summary>
    /// Expand neighbor to
    ///  - PostProcessNeighbor
    ///  - EvaluateNeighbor
    /// </summary>
    /// <param name="call_stack">Output callStack</param>
    virtual void process(CallStack &call_stack);
};

} // namespace Slic3r::sla
#endif // slic3r_SLA_SuppotstIslands_ExpandNeighbor_hpp_
