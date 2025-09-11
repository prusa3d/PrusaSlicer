#ifndef slic3r_SLA_SuppotstIslands_VoronoiGraph_hpp_
#define slic3r_SLA_SuppotstIslands_VoronoiGraph_hpp_

#include <map>
#include <libslic3r/Geometry.hpp>
#include <libslic3r/Geometry/Voronoi.hpp>
#include <numeric>

namespace Slic3r::sla {

/// <summary>
/// DTO store skeleton With longest path
/// </summary>
struct VoronoiGraph
{
    using VD = Slic3r::Geometry::VoronoiDiagram;
    struct Node;
    using Nodes = std::vector<const Node *>;
    struct Path;
    struct ExPath;
    using Circle = Path;
    struct Position;
    std::map<const VD::vertex_type *, Node> data;
};

/// <summary>
/// Node data structure for Voronoi Graph.
/// Extend information about Voronoi vertex.
/// </summary>
struct VoronoiGraph::Node
{
    // reference to Voronoi diagram VertexCategory::Inside OR
    // VertexCategory::OnContour but NOT VertexCategory::Outside
    const VD::vertex_type *vertex;
    // longest distance to edge sum of line segment size (no euclid because of shape U)
    double longestDistance;

    // actual distance to edge
    double distance;

    struct Neighbor;
    std::vector<Neighbor> neighbors;

    // constructor
    Node(const VD::vertex_type *vertex, double distance)
        : vertex(vertex), longestDistance(0.), distance(distance), neighbors()
    {}
};

/// <summary>
/// Surrond GraphNode data type.
/// Extend information about voronoi edge.
/// TODO IMPROVE: extends neighbors for store more edges
/// (cumulate Nodes with 2 neighbors - No cross)
/// </summary>
struct VoronoiGraph::Node::Neighbor
{
    const VD::edge_type *edge;
    // pointer on graph node structure
    const Node *node;
        
    /// <summary>
    /// DTO represents size property of one Neighbor
    /// </summary>
    struct Size{
        // length edge between vertices
        double length;

        // widht is distance between outlines
        // maximal width
        coord_t min_width;
        // minimal widht
        coord_t max_width;

        Size(double length, coord_t min_width, coord_t max_width)
            : length(length), min_width(min_width), max_width(max_width)
        {}
    };    
    std::shared_ptr<Size> size;

public:
    Neighbor(const VD::edge_type * edge,
             const Node *          node,
             std::shared_ptr<Size> size)
        : edge(edge)
        , node(node)
        , size(std::move(size))
    {}
    // accessor to member
    double  length() const { return size->length; }
    coord_t min_width() const { return size->min_width; }
    coord_t max_width() const { return size->max_width; }
};

/// <summary>
/// DTO represents path over nodes of VoronoiGraph
/// store queue of Nodes
/// store length of path
/// </summary>
struct VoronoiGraph::Path
{
    // row of neighbor Nodes
    VoronoiGraph::Nodes nodes; 

    // length of path
    // when circle contain length from back to front;
    double length;

public:
    Path() : nodes(), length(0.) {}
    Path(const VoronoiGraph::Node *node) : nodes({node}), length(0.) {}
    Path(VoronoiGraph::Nodes nodes, double length)
        : nodes(std::move(nodes)), length(length)
    {}

    void append(const VoronoiGraph::Node *node, double length)
    {
        nodes.push_back(node);
        this->length += length;
    }

    Path extend(const VoronoiGraph::Node *node, double length) const {
        Path result(*this); // make copy
        result.append(node, length);
        return result;
    }
    
    struct OrderLengthFromShortest{
        bool operator()(const VoronoiGraph::Path &path1,
                        const VoronoiGraph::Path &path2){
            return path1.length > path2.length;
        }
    };

    struct OrderLengthFromLongest{
        bool operator()(const VoronoiGraph::Path &path1,
                        const VoronoiGraph::Path &path2){
            return path1.length < path2.length;
        }
    };
};


/// <summary>
/// DTO
/// extends path with side branches and circles(connection of circles)
/// </summary>
struct VoronoiGraph::ExPath : public VoronoiGraph::Path
{
    // not main path is stored in secondary paths
    // key is pointer to source node
    using SideBranches    = std::priority_queue<VoronoiGraph::Path,
                                             std::vector<VoronoiGraph::Path>,
                                             OrderLengthFromLongest>;
    using SideBranchesMap = std::map<const VoronoiGraph::Node *, SideBranches>;

    // All side branches in Graph under node
    // Map contains only node, which has side branche(s)
    // There is NOT empty SideBranches in map !!!
    SideBranchesMap side_branches;

    // All circles in Graph under node
    std::vector<VoronoiGraph::Circle> circles;

    // alone circle does'n have record in connected_circle
    // every connected circle have at least two records(firs to second &
    // second to first) EXAMPLE with 3 circles(index to circles stored in
    // this->circles are: c1, c2 and c3) connected together
    // connected_circle[c1] = {c2, c3}; connected_circle[c2] = {c1, c3};
    // connected_circle[c3] = {c1, c2};
    using ConnectedCircles = std::map<size_t, std::set<size_t>>;    
    ConnectedCircles connected_circle;

public:
    ExPath() = default;
};

/// <summary>
/// DTO 
/// Extend neighbor with ratio to edge
/// For point position on VoronoiGraph use VoronoiGraphUtils::get_edge_point
/// </summary>
struct VoronoiGraph::Position
{
    // neighbor is stored inside of voronoi diagram
    const VoronoiGraph::Node::Neighbor* neighbor; 

    // define position on neighbor edge 
    // Value should be in range from 0. to 1. (shrinked when used)
    // Value 0 means position of edge->vertex0
    // Value 0.5 is on half edge way between edge->vertex0 and edge->vertex1
    // Value 1 means position of edge->vertex1
    double ratio;

    Position(const VoronoiGraph::Node::Neighbor *neighbor, double ratio)
        : neighbor(neighbor), ratio(ratio)
    {}

    Position(): neighbor(nullptr), ratio(0.) {}

    coord_t calc_distance() const {
        return static_cast<coord_t>(neighbor->length() * ratio);
    }

    coord_t calc_rest_distance() const
    {
        return static_cast<coord_t>(neighbor->length() * (1. - ratio));
    }
};

} // namespace Slic3r::sla
#endif // slic3r_SLA_SuppotstIslands_VoronoiGraph_hpp_
