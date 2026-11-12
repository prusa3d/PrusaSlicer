#ifndef slic3r_SLA_SuppotstIslands_ExpandNeighbor_hpp_
#define slic3r_SLA_SuppotstIslands_ExpandNeighbor_hpp_

#include "Slic3r/Biz/Algorithms/IStackFunction.hpp"
#include "Slic3r/Biz/CGAL/Algorithms/VoronoiGraph.hpp"
#include "Slic3r/Biz/CGAL/Algorithms/PostProcessNeighbor.hpp"
#include "Slic3r/Biz/CGAL/Algorithms/EvaluateNeighbor.hpp"

namespace Slic3r::Biz::CGAL::Algorithms {

/// <summary>
/// Expand neighbor to
///  - PostProcessNeighbor
///  - EvaluateNeighbor
/// </summary>
class ExpandNeighbor : public Biz::Algorithms::IStackFunction
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
    virtual void process(Biz::Algorithms::CallStack &call_stack);
};

} // namespace Slic3r::sla
#endif // slic3r_SLA_SuppotstIslands_ExpandNeighbor_hpp_
