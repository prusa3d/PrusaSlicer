#include "Slic3r/Biz/CGAL/Algorithms/EvaluateNeighbor.hpp"
#include "ExpandNeighbor.hpp"

namespace Slic3r::Biz::CGAL::Algorithms {

EvaluateNeighbor::EvaluateNeighbor(VoronoiGraph::ExPath &    result,
                                   const VoronoiGraph::Node *node,
                                   double                    distance_to_node,
                                   const VoronoiGraph::Path &prev_path)
    : post_process_neighbor(
          std::make_unique<PostProcessNeighbors>(result,
                                                 node,
                                                 distance_to_node,
                                                 prev_path))
{}

void EvaluateNeighbor::process(Biz::Algorithms::CallStack &call_stack)
{
    NodeDataWithResult &data = *post_process_neighbor;
    call_stack.emplace(std::move(post_process_neighbor));
    for (const VoronoiGraph::Node::Neighbor &neighbor : data.node->neighbors)
        call_stack.emplace(std::make_unique<ExpandNeighbor>(data, neighbor));
}
}
