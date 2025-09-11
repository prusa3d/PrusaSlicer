#ifndef slic3r_SLA_SuppotstIslands_NodeDataWithResult_hpp_
#define slic3r_SLA_SuppotstIslands_NodeDataWithResult_hpp_

#include <vector>
#include <set>
#include "VoronoiGraph.hpp"

namespace Slic3r::sla {

/// <summary>
/// DTO for process node during depth search
/// which create longest path in voronoi graph
/// </summary>
struct NodeDataWithResult
{
    // result for this node
    VoronoiGraph::ExPath &result;

    // actual proccessed node
    const VoronoiGraph::Node *node;
    // distance to this node from input node
    double distance_to_node;

    // path from start point to this node
    // last one is actual node
    VoronoiGraph::Path act_path;

    // skip node when circle start - must end at this node
    // set --> multiple cirle could start at same node
    // previous node should be skiped to so it is initialized with it
    std::set<const VoronoiGraph::Node *> skip_nodes; // circle

    // store all circle indexes this node is lay on
    // used to create connected circles structure
    std::vector<size_t> circle_indexes;
    // When circle_indexes is not empty node lays on circle
    // and in this node is not searching for longest path only store side
    // branches(not part of circle)

    // indexes of circle ending in this node(could be more than one)
    std::vector<size_t> end_circle_indexes;
    // When end_circle_indexes == circle_indexes
    // than it is end of circle (multi circle structure) and it is processed

    // contain possible continue path
    // possible empty
    VoronoiGraph::ExPath::SideBranches side_branches;

public:
    // append node to act path
    NodeDataWithResult(
        VoronoiGraph::ExPath &    result,
        const VoronoiGraph::Node *node,
        double                    distance_to_node,
        VoronoiGraph::Path &&act_path,        
        std::set<const VoronoiGraph::Node *> &&skip_nodes
    )
        : result(result)
        , node(node)
        , distance_to_node(distance_to_node)
        , act_path(std::move(act_path)) // copy prev and append actual node
        , skip_nodes(std::move(skip_nodes))
    {
        //prev_path.extend(node, distance_to_node)
        //const VoronoiGraph::Node *prev_node = (prev_path.nodes.size() >= 1) ?
        //                                          prev_path.nodes.back() :
        //                                          nullptr;
        //skip_nodes = {prev_node};
    }
};

} // namespace Slic3r::sla
#endif // slic3r_SLA_SuppotstIslands_NodeDataWithResult_hpp_
