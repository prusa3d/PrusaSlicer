#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/AStar.hpp"
#include "libslic3r/Execution/ExecutionSeq.hpp"
#include "libslic3r/PointGrid.hpp"

using namespace Slic3r;
using namespace Catch;

TEST_CASE("Testing basic invariants of AStar", "[AStar]") {
    struct DummyTracer {
        using Node = int;

        int goal = 0;

        float distance(int a, int b) const { return a - b; }

        float goal_heuristic(int n) const { return n == goal ? -1.f : 0.f; }

        size_t unique_id(int n) const { return n; }

        void foreach_reachable(int, std::function<bool(int)>) const {}
    };

    std::vector<int> out;

    SECTION("Output is empty when source is also the destination") {
        bool found = astar::search_route(DummyTracer{}, 0, std::back_inserter(out));
        REQUIRE(out.empty());
        REQUIRE(found);
    }

    SECTION("Return false when there is no route to destination") {
        bool found = astar::search_route(DummyTracer{}, 1, std::back_inserter(out));
        REQUIRE(!found);
        REQUIRE(out.empty());
    }
}

struct PointGridTracer3D {
    using Node = size_t;
    const PointGrid<float> &grid;
    size_t final;

    PointGridTracer3D(const PointGrid<float> &g, size_t goal) :
        grid{g}, final{goal} {}

    template<class Fn>
    void foreach_reachable(size_t from, Fn &&fn) const
    {
        Vec3i from_crd = grid.get_coord(from);
        REQUIRE(grid.get_idx(from_crd) == from);

        if (size_t i = grid.get_idx(from_crd + Vec3i{ 1,  0,  0}); i < grid.point_count()) fn(i);
        if (size_t i = grid.get_idx(from_crd + Vec3i{ 0,  1,  0}); i < grid.point_count()) fn(i);
        if (size_t i = grid.get_idx(from_crd + Vec3i{ 0,  0,  1}); i < grid.point_count()) fn(i);
        if (size_t i = grid.get_idx(from_crd + Vec3i{ 1,  1,  0}); i < grid.point_count()) fn(i);
        if (size_t i = grid.get_idx(from_crd + Vec3i{ 0,  1,  1}); i < grid.point_count()) fn(i);
        if (size_t i = grid.get_idx(from_crd + Vec3i{ 1,  1,  1}); i < grid.point_count()) fn(i);
        if (size_t i = grid.get_idx(from_crd + Vec3i{-1,  0,  0}); from_crd.x() > 0 && i < grid.point_count()) fn(i);
        if (size_t i = grid.get_idx(from_crd + Vec3i{ 0, -1,  0}); from_crd.y() > 0 && i < grid.point_count()) fn(i);
        if (size_t i = grid.get_idx(from_crd + Vec3i{ 0,  0, -1}); from_crd.z() > 0 && i < grid.point_count()) fn(i);
        if (size_t i = grid.get_idx(from_crd + Vec3i{-1, -1,  0}); from_crd.x() > 0 && from_crd.y() > 0 && i < grid.point_count()) fn(i);
        if (size_t i = grid.get_idx(from_crd + Vec3i{ 0, -1, -1}); from_crd.y() > 0 && from_crd.z() && i < grid.point_count()) fn(i);
        if (size_t i = grid.get_idx(from_crd + Vec3i{-1, -1, -1}); from_crd.x() > 0 && from_crd.y() > 0 && from_crd.z() && i < grid.point_count()) fn(i);

    }

    float distance(size_t a, size_t b) const
    {
        return (grid.get(a) - grid.get(b)).squaredNorm();
    }

    float goal_heuristic(size_t n) const
    {
        return n == final ? -1.f : (grid.get(n) - grid.get(final)).squaredNorm();
    }

    size_t unique_id(size_t n) const { return n; }
};

template<class Node, class Cmp = std::less<Node>>
bool has_duplicates(const std::vector<Node> &res, Cmp cmp = {})
{
    auto cpy = res;
    std::sort(cpy.begin(), cpy.end(), cmp);
    auto it = std::unique(cpy.begin(), cpy.end());
    return it != cpy.end();
}

TEST_CASE("astar algorithm test over 3D point grid", "[AStar]") {
    auto vol = BoundingBox3Base<Vec3f>{{0.f, 0.f, 0.f}, {1.f, 1.f, 1.f}};

    auto pgrid = point_grid(ex_seq, vol, {0.1f, 0.1f, 0.1f});

    size_t target = pgrid.point_count() - 1;

    PointGridTracer3D pgt{pgrid, target};
    std::vector<size_t> out;
    bool found = astar::search_route(pgt, 0, std::back_inserter(out));

    REQUIRE(found);
    REQUIRE(!out.empty());
    REQUIRE(out.front() == target);

#ifndef NDEBUG
    std::cout << "Route taken: ";
    for (auto it = out.rbegin(); it != out.rend(); ++it) {
        std::cout << "(" << pgrid.get_coord(*it).transpose() << ") ";
    }
    std::cout << std::endl;
#endif

    REQUIRE(!has_duplicates(out)); // No duplicates in output
}

enum CellValue {ON, OFF};

struct CellGridTracer2D_AllDirs {
    using Node = Vec2i;

    static constexpr auto Cols = size_t(5);
    static constexpr auto Rows = size_t(8);
    static constexpr size_t GridSize = Cols * Rows;

    const std::array<std::array<CellValue, Cols>, Rows> &grid;
    Vec2i goal;

    CellGridTracer2D_AllDirs(const std::array<std::array<CellValue, Cols>, Rows> &g,
                     const Vec2i &goal_)
        : grid{g}, goal{goal_}
    {}

    template<class Fn>
    void foreach_reachable(const Vec2i &src, Fn &&fn) const
    {
        auto is_inside = [](const Vec2i& v) { return v.x() >= 0 && v.x() < int(Cols) && v.y() >= 0 && v.y() < int(Rows); };
        if (Vec2i crd = src + Vec2i{0, 1}; is_inside(crd) && grid[crd.y()] [crd.x()] == ON) fn(crd);
        if (Vec2i crd = src + Vec2i{1, 0}; is_inside(crd) && grid[crd.y()] [crd.x()] == ON) fn(crd);
        if (Vec2i crd = src + Vec2i{1, 1}; is_inside(crd) && grid[crd.y()] [crd.x()] == ON) fn(crd);
        if (Vec2i crd = src + Vec2i{0, -1}; is_inside(crd) && grid[crd.y()] [crd.x()] == ON) fn(crd);
        if (Vec2i crd = src + Vec2i{-1, 0}; is_inside(crd) && grid[crd.y()] [crd.x()] == ON) fn(crd);
        if (Vec2i crd = src + Vec2i{-1, -1}; is_inside(crd) && grid[crd.y()] [crd.x()] == ON) fn(crd);
        if (Vec2i crd = src + Vec2i{1, -1}; is_inside(crd) && grid[crd.y()] [crd.x()] == ON) fn(crd);
        if (Vec2i crd = src + Vec2i{-1, 1}; is_inside(crd) && grid[crd.y()] [crd.x()] == ON) fn(crd);
    }

    float distance(const Vec2i & a, const Vec2i & b) const { return (a - b).squaredNorm(); }

    float goal_heuristic(const Vec2i & n) const { return n == goal ? -1.f : (n - goal).squaredNorm(); }

    size_t unique_id(const Vec2i & n) const { return n.y() * Cols + n.x(); }
};

struct CellGridTracer2D_Axis {
    using Node = Vec2i;

    static constexpr auto Cols = size_t(5);
    static constexpr auto Rows = size_t(8);
    static constexpr size_t GridSize = Cols * Rows;

    const std::array<std::array<CellValue, Cols>, Rows> &grid;
    Vec2i goal;

    CellGridTracer2D_Axis(
        const std::array<std::array<CellValue, Cols>, Rows> &g,
        const Vec2i                                         &goal_)
        : grid{g}, goal{goal_}
    {}

    template<class Fn>
    void foreach_reachable(const Vec2i &src, Fn &&fn) const
    {
        auto is_inside = [](const Vec2i& v) { return v.x() >= 0 && v.x() < int(Cols) && v.y() >= 0 && v.y() < int(Rows); };
        if (Vec2i crd = src + Vec2i{0, 1}; is_inside(crd) && grid[crd.y()] [crd.x()] == ON) fn(crd);
        if (Vec2i crd = src + Vec2i{0, -1}; is_inside(crd) && grid[crd.y()] [crd.x()] == ON) fn(crd);
        if (Vec2i crd = src + Vec2i{1, 0}; is_inside(crd) && grid[crd.y()] [crd.x()] == ON) fn(crd);
        if (Vec2i crd = src + Vec2i{-1, 0}; is_inside(crd) && grid[crd.y()] [crd.x()] == ON) fn(crd);
    }

    float distance(const Vec2i & a, const Vec2i & b) const { return (a - b).squaredNorm(); }

    float goal_heuristic(const Vec2i &n) const
    {
        int manhattan_dst = std::abs(n.x() - goal.x()) +
                            std::abs(n.y() - goal.y());

        return n == goal ? -1.f : manhattan_dst;
    }

    size_t unique_id(const Vec2i & n) const { return n.y() * Cols + n.x(); }
};

using TestClasses = std::tuple< CellGridTracer2D_AllDirs, CellGridTracer2D_Axis >;

TEMPLATE_LIST_TEST_CASE("Astar should avoid simple barrier", "[AStar]", TestClasses) {

    std::array<std::array<CellValue, 5>, 8> grid = {{
        {ON , ON , ON , ON , ON},
        {ON , ON , ON , ON , ON},
        {ON , ON , ON , ON , ON},
        {ON , ON , ON , ON , ON},
        {ON , ON , ON , ON , ON},
        {ON , OFF, OFF, OFF, ON},
        {ON , ON , ON , ON , ON},
        {ON , ON , ON , ON , ON}
    }};

    Vec2i dst = {2, 0};
    TestType cgt{grid, dst};

    std::vector<Vec2i> out;
    bool found = astar::search_route(cgt, {2, 7}, std::back_inserter(out));

    REQUIRE(found);
    REQUIRE(!out.empty());
    REQUIRE(out.front() == dst);
    REQUIRE(!has_duplicates(out, [](const Vec2i &a, const Vec2i &b) {
        return a.x() == b.x() ? a.y() < b.y() : a.x() < b.x();
    }));

#ifndef NDEBUG
    std::cout << "Route taken: ";
    for (auto it = out.rbegin(); it != out.rend(); ++it) {
        std::cout << "(" << it->transpose() << ") ";
    }
    std::cout << std::endl;
#endif
}

TEMPLATE_LIST_TEST_CASE("Astar should manage to avoid arbitrary barriers", "[AStar]", TestClasses) {

    std::array<std::array<CellValue, 5>, 8> grid = {{
        {ON , ON , ON , ON , ON},
        {ON , ON , ON , OFF, ON},
        {OFF, OFF, ON , OFF, ON},
        {ON , ON , ON , OFF, ON},
        {ON , OFF, ON , OFF, ON},
        {ON , OFF, ON , ON , ON},
        {ON , OFF, ON , OFF, ON},
        {ON , ON , ON , ON , ON}
    }};

    Vec2i dst = {0, 0};
    TestType cgt{grid, dst};

    std::vector<Vec2i> out;
    bool found = astar::search_route(cgt, {0, 7}, std::back_inserter(out));

    REQUIRE(found);
    REQUIRE(!out.empty());
    REQUIRE(out.front() == dst);
    REQUIRE(!has_duplicates(out, [](const Vec2i &a, const Vec2i &b) {
        return a.x() == b.x() ? a.y() < b.y() : a.x() < b.x();
    }));

#ifndef NDEBUG
    std::cout << "Route taken: ";
    for (auto it = out.rbegin(); it != out.rend(); ++it) {
        std::cout << "(" << it->transpose() << ") ";
    }
    std::cout << std::endl;
#endif
}

TEMPLATE_LIST_TEST_CASE("Astar should find the way out of a labyrinth", "[AStar]", TestClasses) {

    std::array<std::array<CellValue, 5>, 8> grid = {{
        {ON , ON , ON , ON , ON },
        {ON , OFF, OFF, OFF, OFF},
        {ON , ON , ON , ON , ON },
        {OFF, OFF, OFF, OFF, ON },
        {ON , ON , ON , ON , ON },
        {ON , OFF, OFF, OFF, OFF},
        {ON , ON , ON , ON , ON },
        {OFF, OFF, OFF, OFF, ON }
    }};

    Vec2i dst = {4, 0};
    TestType cgt{grid, dst};

    std::vector<Vec2i> out;
    bool found = astar::search_route(cgt, {4, 7}, std::back_inserter(out));

    REQUIRE(found);
    REQUIRE(!out.empty());
    REQUIRE(out.front() == dst);
    REQUIRE(!has_duplicates(out, [](const Vec2i &a, const Vec2i &b) {
        return a.x() == b.x() ? a.y() < b.y() : a.x() < b.x();
    }));

#ifndef NDEBUG
    std::cout << "Route taken: ";
    for (auto it = out.rbegin(); it != out.rend(); ++it) {
        std::cout << "(" << it->transpose() << ") ";
    }
    std::cout << std::endl;
#endif
}

TEST_CASE("Zero heuristic function should result in dijsktra's algo", "[AStar]")
{
    struct GraphTracer {
        using Node  = size_t;
        using QNode = astar::QNode<GraphTracer>;

        struct Edge
        {
            size_t to_id = size_t(-1);
            float  cost  = 0.f;
            bool operator <(const Edge &e) const { return to_id < e.to_id; }
        };

        struct ENode: public QNode {
            std::vector<Edge> edges;

            ENode(size_t node_id, std::initializer_list<Edge> edgelist)
                : QNode{node_id}, edges(edgelist)
            {}

            ENode &operator=(const QNode &q)
            {
                assert(node == q.node);
                g = q.g;
                h = q.h;
                parent = q.parent;
                queue_id = q.queue_id;

                return *this;
            }
        };

        // Example graph from
        // https://www.geeksforgeeks.org/dijkstras-shortest-path-algorithm-greedy-algo-7/?ref=lbp
        std::vector<ENode> nodes = {
            {0, {{1, 4.f}, {7, 8.f}}},
            {1, {{0, 4.f}, {2, 8.f}, {7, 11.f}}},
            {2, {{1, 8.f}, {3, 7.f}, {5, 4.f}, {8, 2.f}}},
            {3, {{2, 7.f}, {4, 9.f}, {5, 14.f}}},
            {4, {{3, 9.f}, {5, 10.f}}},
            {5, {{2, 4.f}, {3, 14.f}, {4, 10.f}, {6, 2.f}}},
            {6, {{5, 2.f}, {7, 1.f},  {8, 6.f}}},
            {7, {{0, 8.f}, {1, 11.f}, {6, 1.f}, {8, 7.f}}},
            {8, {{2, 2.f}, {6, 6.f},  {7, 7.f}}}
        };

        float distance(size_t a, size_t b) const {
            float ret = std::numeric_limits<float>::infinity();
            if (a < nodes.size()) {
                auto it = std::lower_bound(nodes[a].edges.begin(),
                                           nodes[a].edges.end(),
                                           Edge{b, 0.f});

                if (it != nodes[a].edges.end()) {
                    ret = it->cost;
                }
            }

            return ret;
        }

        float goal_heuristic(size_t) const { return 0.f; }

        size_t unique_id(size_t n) const { return n; }

        void foreach_reachable(size_t n, std::function<bool(int)> fn) const
        {
            if (n < nodes.size()) {
                for (const Edge &e : nodes[n].edges)
                    fn(e.to_id);
            }
        }
    } graph;

    std::vector<size_t> out;

    // 'graph.nodes' is able to be a node cache (it simulates an associative container)
    bool found = astar::search_route(graph, size_t(0), std::back_inserter(out), graph.nodes);

    // But should not crash or loop infinitely.
    REQUIRE(!found);

    // Without a destination, there is no output. But the algorithm should halt.
    REQUIRE(out.empty());

    // Source node should have it's parent unset
    REQUIRE(graph.nodes[0].parent == astar::Unassigned);

    // All other nodes should have their parents set
    for (size_t i = 1; i < graph.nodes.size(); ++i)
        REQUIRE(graph.nodes[i].parent != astar::Unassigned);

    std::array<float, 9> ref_distances = {0.f,  4.f, 12.f, 19.f, 21.f,
                                          11.f, 9.f, 8.f,  14.f};

    // Try to trace each node back to the source node. Each of them should
    // arrive to the source within less hops than the full number of nodes.
    for (size_t i = 0, k = 0; i < graph.nodes.size(); ++i, k = 0) {
        GraphTracer::QNode *q = &graph.nodes[i];
        REQUIRE(q->g == Approx(ref_distances[i]));
        while (k++ < graph.nodes.size() && q->parent != astar::Unassigned)
            q = &graph.nodes[q->parent];

        REQUIRE(q->parent == astar::Unassigned);
    }
}
