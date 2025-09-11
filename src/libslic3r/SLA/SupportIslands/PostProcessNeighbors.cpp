#include "PostProcessNeighbors.hpp"

#include "VoronoiGraphUtils.hpp"

using namespace Slic3r::sla;

void PostProcessNeighbors::process()
{
    // remember connected circle
    if (circle_indexes.size() > 1) {
        for (size_t circle_index : circle_indexes) {
            for (size_t circle_index2 : circle_indexes) {
                if (circle_index == circle_index2) continue;
                result.connected_circle[circle_index].insert(circle_index2);
            }
        }
    }

    // detect end of circles in this node
    if (!end_circle_indexes.empty() &&
        end_circle_indexes.size() == circle_indexes.size()) {
        size_t circle_index = circle_indexes.front(); // possible any of them
        side_branches.push(
            VoronoiGraphUtils::find_longest_path_on_circles(*node,
                                                            circle_index,
                                                            result));

        circle_indexes.clear(); // resolved circles
    }

    // simple node on circle --> only input and output neighbor
    if (side_branches.empty()) return;

    // is node on unresolved circle?
    if (!circle_indexes.empty()) {
        // not search for longest path, it will eval on end of circle
        result.side_branches[node] = side_branches;
        return;
    }

    // create result longest path from longest side branch
    VoronoiGraph::Path longest_path(std::move(side_branches.top()));
    side_branches.pop();
    if (!side_branches.empty()) {
        result.side_branches[node] = side_branches;
    }
    longest_path.nodes.insert(longest_path.nodes.begin(), node);
    result.nodes   = std::move(longest_path.nodes);
    result.length = distance_to_node + longest_path.length;
}