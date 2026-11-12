#ifndef slic3r_SLA_SuppotstIslands_PostProcessNeighbors_hpp_
#define slic3r_SLA_SuppotstIslands_PostProcessNeighbors_hpp_

#include "Slic3r/Biz/Algorithms/IStackFunction.hpp"
#include "Slic3r/Biz/CGAL/Algorithms/VoronoiGraph.hpp"
#include "Slic3r/Biz/CGAL/Algorithms/NodeDataWithResult.hpp"

namespace Slic3r::Biz::CGAL::Algorithms {

/// <summary>
/// call after all neighbors are processed
/// </summary>
class PostProcessNeighbors : public NodeDataWithResult, public Biz::Algorithms::IStackFunction
{
public:
    PostProcessNeighbors(VoronoiGraph::ExPath &    result,
                         const VoronoiGraph::Node *node,
                         double                    distance_to_node = 0.,
                         const VoronoiGraph::Path &prev_path =
                             VoronoiGraph::Path({}, 0.) // make copy
                         )
        : NodeDataWithResult(
            result, node, distance_to_node, 
            prev_path.extend(node, distance_to_node),
            prepare_skip_nodes(prev_path)
            )
    {}

    virtual void process([[maybe_unused]] Biz::Algorithms::CallStack &call_stack)
    {
        process();
    }

private:
    static std::set<const VoronoiGraph::Node *> prepare_skip_nodes(
        const VoronoiGraph::Path &prev_path)
    {
        if (prev_path.nodes.empty()) return {};
        const VoronoiGraph::Node *prev_node = prev_path.nodes.back();
        return {prev_node};
    }

    void process();
};

} // namespace Slic3r::sla
#endif // slic3r_SLA_SuppotstIslands_PostProcessNeighbors_hpp_
