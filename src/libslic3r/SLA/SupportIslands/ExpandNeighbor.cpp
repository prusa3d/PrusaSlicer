#include "ExpandNeighbor.hpp"
#include "VoronoiGraphUtils.hpp"

using namespace Slic3r::sla;

ExpandNeighbor::ExpandNeighbor(
    NodeDataWithResult &                data,
    const VoronoiGraph::Node::Neighbor &neighbor)
    : data(data)
    , neighbor(neighbor)
{}

void ExpandNeighbor::process(CallStack &call_stack)
{
    if (data.skip_nodes.find(neighbor.node) != data.skip_nodes.end()) return;

    // detection of circle
    auto circle_opt = VoronoiGraphUtils::create_circle(data.act_path,
                                                       neighbor);
    if (circle_opt.has_value()) {
        size_t circle_index = data.result.circles.size();
        data.circle_indexes.push_back(circle_index);
        data.result.circles.push_back(*circle_opt);
        return;
    }

    // create copy of path(not circles, not side_branches)
    const VoronoiGraph::Node &next_node = *neighbor.node;
    // is next node leaf ?
    if (next_node.neighbors.size() == 1) {
        VoronoiGraph::Path side_branch({&next_node}, neighbor.length());
        data.side_branches.push(std::move(side_branch));
        return;
    }

    auto post_process_neighbor = std::make_unique<PostProcessNeighbor>(data);
    VoronoiGraph::ExPath &neighbor_path = post_process_neighbor->neighbor_path;

    call_stack.emplace(std::move(post_process_neighbor));
    call_stack.emplace(
        std::make_unique<EvaluateNeighbor>(neighbor_path, neighbor.node,
                                           neighbor.length(),
                                           data.act_path));
}