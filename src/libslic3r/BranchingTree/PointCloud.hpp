///|/ Copyright (c) Prusa Research 2022 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef POINTCLOUD_HPP
#define POINTCLOUD_HPP

#include <assert.h>
#include <stddef.h>
#include <boost/geometry.hpp>
#include <optional>
#include <Eigen/Geometry>
#include <algorithm>
#include <cmath>
#include <type_traits>
#include <utility>
#include <vector>
#include <cassert>
#include <cstdlib>

#include "BranchingTree.hpp"
//#include "libslic3r/Execution/Execution.hpp"
#include "libslic3r/MutablePriorityQueue.hpp"
#include "libslic3r/BoostAdapter.hpp"
#include "boost/geometry/index/rtree.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/libslic3r.h"

struct indexed_triangle_set;

namespace Slic3r { namespace branchingtree {

std::optional<Vec3f> find_merge_pt(const Vec3f &A,
                                   const Vec3f &B,
                                   float        max_slope);

void to_eigen_mesh(const indexed_triangle_set &its,
                   Eigen::MatrixXd            &V,
                   Eigen::MatrixXi            &F);

std::vector<Node> sample_mesh(const indexed_triangle_set &its, double radius);

std::vector<Node> sample_bed(const ExPolygons &bed,
                                 float             z,
                                 double            radius = 1.);

enum PtType { LEAF, MESH, BED, JUNCTION, NONE };

inline BoundingBox3Base<Vec3f> get_support_cone_bb(const Vec3f &p, const Properties &props)
{
    double gnd = props.ground_level() - EPSILON;
    double h   = p.z() - gnd;
    double phi = PI / 2 - props.max_slope();
    auto   r   = float(std::min(h * std::tan(phi), props.max_branch_length() * std::sin(phi)));

    Vec3f bb_min = {p.x() - r, p.y() - r, float(gnd)};
    Vec3f bb_max = {p.x() + r, p.y() + r, p.z()};

    return {bb_min, bb_max};
}

// A cloud of points including support points, mesh points, junction points
// and anchor points on the bed. Junction points can be added or removed, all
// the other point types are established on creation and remain unchangeable.
class PointCloud {
    std::vector<Node> m_leafs, m_junctions, m_meshpoints, m_bedpoints;

    const branchingtree::Properties &m_props;

    const double cos2bridge_slope;
    const size_t MESHPTS_BEGIN, LEAFS_BEGIN, JUNCTIONS_BEGIN;

private:

    // These vectors have the same size as there are indices for nodes to keep
    // access complexity constant. WARN: there might be cache non-locality costs
    std::vector<bool> m_searchable_indices; // searchable flag value of a node
    std::vector<size_t> m_queue_indices;    // queue id of a node if queued

    size_t m_reachable_cnt;

    struct CoordFn
    {
        const PointCloud *self;
        CoordFn(const PointCloud *s) : self{s} {}
        float operator()(size_t nodeid, size_t dim) const
        {
            return self->get(nodeid).pos(int(dim));
        }
    };

    using PointIndexEl = std::pair<Vec3f, unsigned>;

    boost::geometry::index::
        rtree<PointIndexEl, boost::geometry::index::rstar<16, 4> /* ? */>
            m_ktree;

    template<class PC>
    static auto *get_node(PC &&pc, size_t id)
    {
        auto *ret = decltype(pc.m_bedpoints.data())(nullptr);

        switch(pc.get_type(id)) {
        case BED: ret = &pc.m_bedpoints[id]; break;
        case MESH: ret = &pc.m_meshpoints[id - pc.MESHPTS_BEGIN]; break;
        case LEAF: ret = &pc.m_leafs [id - pc.LEAFS_BEGIN]; break;
        case JUNCTION: ret = &pc.m_junctions[id - pc.JUNCTIONS_BEGIN]; break;
        case NONE: assert(false);
        }

        return ret;
    }

public:

    bool is_outside_support_cone(const Vec3f &supp, const Vec3f &pt) const
    {
        Vec3d D = (pt - supp).cast<double>();
        double dot_sq = -D.z() * std::abs(-D.z());

        return dot_sq < D.squaredNorm() * cos2bridge_slope;
    }

    static constexpr auto Unqueued = size_t(-1);

    struct ZCompareFn
    {
        const PointCloud *self;
        ZCompareFn(const PointCloud *s) : self{s} {}
        bool operator()(size_t node_a, size_t node_b) const
        {
            return self->get(node_a).pos.z() > self->get(node_b).pos.z();
        }
    };

    PointCloud(const indexed_triangle_set &M,
               std::vector<Node>           support_leafs,
               const Properties           &props);

    PointCloud(std::vector<Node> meshpts,
               std::vector<Node> bedpts,
               std::vector<Node> support_leafs,
               const Properties &props);

    PtType get_type(size_t node_id) const
    {
        PtType ret = NONE;

        if (node_id < MESHPTS_BEGIN && !m_bedpoints.empty()) ret = BED;
        else if (node_id < LEAFS_BEGIN && !m_meshpoints.empty()) ret = MESH;
        else if (node_id < JUNCTIONS_BEGIN && !m_leafs.empty()) ret = LEAF;
        else if (node_id >= JUNCTIONS_BEGIN && !m_junctions.empty()) ret = JUNCTION;

        return ret;
    }

    const Node &get(size_t node_id) const
    {
        return *get_node(*this, node_id);
    }

    Node &get(size_t node_id)
    {
        return *get_node(*this, node_id);
    }

    const Node *find(size_t node_id) const { return get_node(*this, node_id); }
    Node *find(size_t node_id) { return get_node(*this, node_id); }

    // Return the original index of a leaf in the input array, if the given
    // node id is indeed of type SUPP
    int get_leaf_id(size_t node_id) const
    {
        return node_id >= LEAFS_BEGIN && node_id < JUNCTIONS_BEGIN ?
                   node_id - LEAFS_BEGIN :
                   Node::ID_NONE;
    }

    size_t get_queue_idx(size_t node_id) const { return m_queue_indices[node_id]; }

    float get_distance(const Vec3f &p, size_t node) const;

    size_t next_junction_id() const
    {
        return JUNCTIONS_BEGIN + m_junctions.size();
    }

    size_t insert_junction(const Node &p)
    {
        size_t new_id = next_junction_id();
        m_junctions.emplace_back(p);
        m_junctions.back().id = int(new_id);
        m_ktree.insert({m_junctions.back().pos, new_id});
        m_searchable_indices.emplace_back(true);
        m_queue_indices.emplace_back(Unqueued);
        ++m_reachable_cnt;

        return new_id;
    }

    const std::vector<Node> &get_junctions() const noexcept  { return m_junctions; }
    const std::vector<Node> &get_bedpoints() const noexcept  { return m_bedpoints; }
    const std::vector<Node> &get_meshpoints() const noexcept { return m_meshpoints; }
    const std::vector<Node> &get_leafs() const noexcept      { return m_leafs; }
    const Properties & properties() const noexcept { return m_props; }

    void mark_unreachable(size_t node_id)
    {
        assert(node_id < m_searchable_indices.size());

        m_searchable_indices[node_id] = false;
        m_queue_indices[node_id] = Unqueued;
        --m_reachable_cnt;
    }

    size_t reachable_count() const { return m_reachable_cnt; }

    template<class Fn>
    void foreach_reachable(const Vec3f &pos,
                           Fn         &&visitor,
                           size_t       k,
                           double       min_dist = 0.)
    {
        // Fake output iterator
        struct Output {
            const PointCloud *self;
            Vec3f p;
            Fn &visitorfn;

            Output& operator *() { return *this; }
            void operator=(const PointIndexEl &el) {
                visitorfn(el.second, self->get_distance(p, el.second),
                             (p - el.first).squaredNorm());
            }
            Output& operator++() { return *this; }
        };

        namespace bgi = boost::geometry::index;
        float brln = 2 * m_props.max_branch_length();
        BoundingBox3Base<Vec3f> bb{{pos.x() - brln, pos.y() - brln,
                                    float(m_props.ground_level() - EPSILON)},
                                   {pos.x() + brln, pos.y() + brln,
                                    m_ktree.bounds().max_corner().get<Z>()}};

        // Extend upwards to find mergable junctions and support points
        bb.max.z() = m_ktree.bounds().max_corner().get<Z>();

        auto filter = bgi::satisfies(
                          [this, &pos, min_dist](const PointIndexEl &e) {
                              double D_branching = get_distance(pos, e.second);
                              double D_euql      = (pos - e.first).squaredNorm() ;
                              return m_searchable_indices[e.second] &&
                                     !std::isinf(D_branching) && D_euql > min_dist;
                          });

        m_ktree.query(bgi::intersects(bb) && filter && bgi::nearest(pos, k),
                      Output{this, pos, visitor});
    }

    auto start_queue()
    {
        auto ptsqueue = make_mutable_priority_queue<size_t, true>(
            [this](size_t el, size_t idx) { m_queue_indices[el] = idx; },
            ZCompareFn{this});

        ptsqueue.reserve(m_leafs.size());
        size_t iend = LEAFS_BEGIN + m_leafs.size();
        for (size_t i = LEAFS_BEGIN; i < iend; ++i)
            ptsqueue.push(i);

        return ptsqueue;
    }
};

template<class Fn> constexpr bool IsTraverseFn = std::is_invocable_v<Fn, Node&>;

struct TraverseReturnT
{
    bool to_left : 1; // if true, continue traversing to the left
    bool to_right : 1; // if true, continue traversing to the right
};

template<class PC, class Fn> void traverse(PC &&pc, size_t root, Fn &&fn)
{
    if (auto nodeptr = pc.find(root); nodeptr != nullptr) {
        auto &nroot = *nodeptr;
        TraverseReturnT ret{true, true};

        if constexpr (std::is_same_v<std::invoke_result_t<Fn, decltype(nroot)>, void>) {
            // Our fn has no return value
            fn(nroot);
        } else {
            // Fn returns instructions about how to continue traversing
            ret = fn(nroot);
        }

        if (ret.to_left && nroot.left >= 0)
            traverse(pc, nroot.left, fn);
        if (ret.to_right && nroot.right >= 0)
            traverse(pc, nroot.right, fn);
    }
}

void build_tree(PointCloud &pcloud, Builder &builder);

inline void build_tree(PointCloud &&pc, Builder &builder)
{
    build_tree(pc, builder);
}

}} // namespace Slic3r::branchingtree

#endif // POINTCLOUD_HPP
