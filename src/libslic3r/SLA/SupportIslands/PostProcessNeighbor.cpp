#include "PostProcessNeighbor.hpp"

#include "VoronoiGraphUtils.hpp"

using namespace Slic3r::sla;

void PostProcessNeighbor::process()
{
    bool is_circle_neighbor = false;
    if (neighbor_path.nodes.empty()) { // neighbor node is on circle
        for (VoronoiGraph::Circle &circle : neighbor_path.circles) {
            const auto &circle_item = std::find(circle.nodes.begin(),
                                                circle.nodes.end(), data.node);
            if (circle_item == circle.nodes.end())
                continue; // node is NOT on circle

            size_t next_circle_index = &circle -
                                       &neighbor_path.circles.front();
            size_t circle_index = data.result.circles.size() +
                                  next_circle_index;
            data.circle_indexes.push_back(circle_index);

            // check if this node is end of circle
            if (circle_item == circle.nodes.begin()) {
                data.end_circle_indexes.push_back(circle_index);

                // !! this FIX circle lenght because at detection of
                // circle it will cost time to calculate it
                circle.length -= data.act_path.length;

                // skip twice checking of circle
                data.skip_nodes.insert(circle.nodes.back());
            }
            is_circle_neighbor = true;
        }
    }
    VoronoiGraphUtils::append_neighbor_branch(data.result, neighbor_path);
    if (!is_circle_neighbor)
        data.side_branches.push(std::move(neighbor_path));
}