///|/ Copyright (c) Prusa Research 2022 - 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "BranchingTreeSLA.hpp"

#include <boost/container/vector.hpp>
#include <boost/geometry.hpp>
#include <boost/log/trivial.hpp>
#include <map>
#include <algorithm>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>
#include <cstddef>

#include "libslic3r/Execution/ExecutionTBB.hpp"
#include "libslic3r/KDTreeIndirect.hpp"
#include "SupportTreeUtils.hpp"
#include "libslic3r/BranchingTree/PointCloud.hpp"
#include "Pad.hpp"
#include "libslic3r/BranchingTree/BranchingTree.hpp"
#include "libslic3r/Execution/Execution.hpp"
#include "libslic3r/Execution/ExecutionSeq.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/SLA/SupportTree.hpp"
#include "libslic3r/SLA/SupportTreeBuilder.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r { namespace sla {

inline constexpr const auto &beam_ex_policy = ex_tbb;

class BranchingTreeBuilder: public branchingtree::Builder {
    SupportTreeBuilder &m_builder;
    const SupportableMesh  &m_sm;
    const branchingtree::PointCloud &m_cloud;

    std::vector<branchingtree::Node> m_pillars; // to put an index over them

    // cache succesfull ground connections
    mutable std::map<int, GroundConnection> m_gnd_connections;
    mutable execution::SpinningMutex<ExecutionTBB>  m_gnd_connections_mtx;

    // Scaling of the input value 'widening_factor:<0, 1>' to produce resonable
    // widening behaviour
    static constexpr double WIDENING_SCALE = 0.05;

    double get_radius(const branchingtree::Node &j) const
    {
        double w = WIDENING_SCALE * m_sm.cfg.pillar_widening_factor * j.weight;

        return double(j.Rmin) + w;
    }

    std::vector<size_t>  m_unroutable_pinheads;

    void build_subtree(size_t root)
    {
        traverse(m_cloud, root, [this](const branchingtree::Node &node) {
            if (node.left >= 0 && node.right >= 0) {
                auto nparent = m_cloud.get(node.id);
                auto nleft = m_cloud.get(node.left);
                auto nright = m_cloud.get(node.right);
                Vec3d from1d = nleft.pos.cast<double>();
                Vec3d from2d = nright.pos.cast<double>();
                Vec3d tod    = nparent.pos.cast<double>();
                double mergeR = get_radius(nparent);
                double leftR  = get_radius(nleft);
                double rightR = get_radius(nright);

                m_builder.add_diffbridge(from1d, tod, leftR, mergeR);
                m_builder.add_diffbridge(from2d, tod, rightR, mergeR);
                m_builder.add_junction(tod, mergeR);
            } else if (int child = node.left + node.right + 1; child >= 0) {
                auto from = m_cloud.get(child);
                auto to   = m_cloud.get(node.id);
                auto tod  = to.pos.cast<double>();
                double toR = get_radius(to);
                m_builder.add_diffbridge(from.pos.cast<double>(),
                                         tod,
                                         get_radius(from),
                                         toR);
                m_builder.add_junction(tod, toR);
            }
        });
    }

    void discard_subtree(size_t root)
    {
        // Discard all the support points connecting to this branch.
        traverse(m_cloud, root, [this](const branchingtree::Node &node) {
            int suppid_parent = m_cloud.get_leaf_id(node.id);
            int suppid_left   = m_cloud.get_leaf_id(node.left);
            int suppid_right  = m_cloud.get_leaf_id(node.right);
            if (suppid_parent >= 0)
                m_unroutable_pinheads.emplace_back(suppid_parent);
            if (suppid_left >= 0)
                m_unroutable_pinheads.emplace_back(suppid_left);
            if (suppid_right >= 0)
                m_unroutable_pinheads.emplace_back(suppid_right);
        });
    }

    void discard_subtree_rescure(size_t root)
    {
        // Discard all the support points connecting to this branch.
        // As a last resort, try to route child nodes to ground and stop
        // traversing if any child branch succeeds.
        traverse(m_cloud, root, [this](const branchingtree::Node &node) {
            branchingtree::TraverseReturnT ret{true, true};

            int suppid_parent = m_cloud.get_leaf_id(node.id);
            int suppid_left   = branchingtree::Node::ID_NONE;
            int suppid_right  = branchingtree::Node::ID_NONE;

            double glvl = ground_level(m_sm);
            branchingtree::Node dst = node;
            dst.pos.z() = glvl;
            dst.weight += node.pos.z() - glvl;

            if (node.left >= 0 && add_ground_bridge(m_cloud.get(node.left), dst))
                ret.to_left = false;
            else
                suppid_left = m_cloud.get_leaf_id(node.left);

            if (node.right >= 0 && add_ground_bridge(m_cloud.get(node.right), dst))
                ret.to_right = false;
            else
                suppid_right = m_cloud.get_leaf_id(node.right);

            if (suppid_parent >= 0)
                m_unroutable_pinheads.emplace_back(suppid_parent);
            if (suppid_left >= 0)
                m_unroutable_pinheads.emplace_back(suppid_left);
            if (suppid_right >= 0)
                m_unroutable_pinheads.emplace_back(suppid_right);

            return ret;
        });
    }

public:
    BranchingTreeBuilder(SupportTreeBuilder          &builder,
                     const SupportableMesh       &sm,
                     const branchingtree::PointCloud &cloud)
        : m_builder{builder}, m_sm{sm}, m_cloud{cloud}
    {}

    bool add_bridge(const branchingtree::Node &from,
                    const branchingtree::Node &to) override;

    bool add_merger(const branchingtree::Node &node,
                    const branchingtree::Node &closest,
                    const branchingtree::Node &merge_node) override;

    bool add_ground_bridge(const branchingtree::Node &from,
                           const branchingtree::Node &/*to*/) override;

    bool add_mesh_bridge(const branchingtree::Node &from,
                         const branchingtree::Node &to) override;

    std::optional<Vec3f> suggest_avoidance(const branchingtree::Node &from,
                                           float max_bridge_len) const override;

    void report_unroutable(const branchingtree::Node &j) override
    {
        double glvl = ground_level(m_sm);
        branchingtree::Node dst = j;
        dst.pos.z() = glvl;
        dst.weight += j.pos.z() - glvl;
        if (add_ground_bridge(j, dst))
            return;

        BOOST_LOG_TRIVIAL(warning) << "Cannot route junction at " << j.pos.x()
                                   << " " << j.pos.y() << " " << j.pos.z();

        // Discard all the support points connecting to this branch.
        discard_subtree_rescure(j.id);
//        discard_subtree(j.id);
    }

    const std::vector<size_t>& unroutable_pinheads() const
    {
        return m_unroutable_pinheads;
    }

    bool is_valid() const override { return !m_builder.ctl().stopcondition(); }

    const std::vector<branchingtree::Node> & pillars() const { return m_pillars; }

    const GroundConnection *ground_conn(size_t pillar) const
    {
        const GroundConnection *ret = nullptr;

        auto it = m_gnd_connections.find(m_pillars[pillar].id);
        if (it != m_gnd_connections.end())
            ret = &it->second;

        return ret;
    }
};

bool BranchingTreeBuilder::add_bridge(const branchingtree::Node &from,
                                      const branchingtree::Node &to)
{
    Vec3d fromd = from.pos.cast<double>(), tod = to.pos.cast<double>();
    double fromR = get_radius(from), toR = get_radius(to);
    Beam beam{Ball{fromd, fromR}, Ball{tod, toR}};
    auto   hit = beam_mesh_hit(beam_ex_policy , m_sm.emesh, beam,
                               m_sm.cfg.safety_distance_mm);

    bool ret = hit.distance() > (tod - fromd).norm();

    return ret;
}

bool BranchingTreeBuilder::add_merger(const branchingtree::Node &node,
                                      const branchingtree::Node &closest,
                                      const branchingtree::Node &merge_node)
{
    Vec3d from1d = node.pos.cast<double>(),
          from2d = closest.pos.cast<double>(),
          tod    = merge_node.pos.cast<double>();

    double mergeR   = get_radius(merge_node);
    double nodeR    = get_radius(node);
    double closestR = get_radius(closest);
    Beam beam1{Ball{from1d, nodeR}, Ball{tod, mergeR}};
    Beam beam2{Ball{from2d, closestR}, Ball{tod, mergeR}};

    auto sd = m_sm.cfg.safety_distance_mm ;
    auto hit1 = beam_mesh_hit(beam_ex_policy , m_sm.emesh, beam1, sd);
    auto hit2 = beam_mesh_hit(beam_ex_policy , m_sm.emesh, beam2, sd);

    bool ret = hit1.distance() > (tod - from1d).norm() &&
               hit2.distance() > (tod - from2d).norm();

    return ret;
}

bool BranchingTreeBuilder::add_ground_bridge(const branchingtree::Node &from,
                                             const branchingtree::Node &to)
{
    bool ret = false;

    namespace bgi = boost::geometry::index;

    auto it = m_gnd_connections.find(from.id);
    const GroundConnection *connptr = nullptr;

    if (it == m_gnd_connections.end()) {
        sla::Junction j{from.pos.cast<double>(), get_radius(from)};
        Vec3d init_dir = (to.pos - from.pos).cast<double>().normalized();

        auto conn = deepsearch_ground_connection(beam_ex_policy , m_sm, j,
                                                 get_radius(to), init_dir);

        // Remember that this node was tested if can go to ground, don't
        // test it with any other destination ground point because
        // it is unlikely that search_ground_route would find a better solution
        connptr = &(m_gnd_connections[from.id] = conn);
    } else {
        connptr = &(it->second);
    }

    if (connptr && *connptr) {
        m_pillars.emplace_back(from);
        ret = true;
        build_subtree(from.id);
    }

    return ret;
}

bool BranchingTreeBuilder::add_mesh_bridge(const branchingtree::Node &from,
                                           const branchingtree::Node &to)
{
    if (from.weight > m_sm.cfg.max_weight_on_model_support)
        return false;

    sla::Junction fromj = {from.pos.cast<double>(), get_radius(from)};

    auto anchor = m_sm.cfg.ground_facing_only ?
                      std::optional<Anchor>{} : // If no mesh connections are allowed
                      calculate_anchor_placement(beam_ex_policy , m_sm, fromj,
                                                 to.pos.cast<double>());

    if (anchor) {
        sla::Junction toj = {anchor->junction_point(), anchor->r_back_mm};

        auto hit = beam_mesh_hit(beam_ex_policy , m_sm.emesh,
                                 Beam{{fromj.pos, fromj.r}, {toj.pos, toj.r}}, 0.);

        if (hit.distance() > distance(fromj.pos, toj.pos)) {
            m_builder.add_diffbridge(fromj.pos, toj.pos, fromj.r, toj.r);
            m_builder.add_anchor(*anchor);

            build_subtree(from.id);
        } else {
            anchor.reset();
        }
    }

    return bool(anchor);
}

static std::optional<Vec3f> get_avoidance(const GroundConnection &conn,
                                          float maxdist)
{
    std::optional<Vec3f> ret;

    if (conn) {
        if (conn.path.size() > 1) {
            ret = conn.path[1].pos.cast<float>();
        } else {
            Vec3f pbeg = conn.path[0].pos.cast<float>();
            Vec3f pend = conn.pillar_base->pos.cast<float>();
            pbeg.z()   = std::max(pbeg.z() - maxdist, pend.z());
            ret = pbeg;
        }
    }

    return ret;
}

std::optional<Vec3f> BranchingTreeBuilder::suggest_avoidance(
    const branchingtree::Node &from, float max_bridge_len) const
{
    std::optional<Vec3f> ret;

    double glvl = ground_level(m_sm);
    branchingtree::Node dst = from;
    dst.pos.z() = glvl;
    dst.weight += from.pos.z() - glvl;
    sla::Junction j{from.pos.cast<double>(), get_radius(from)};

    auto found_it = m_gnd_connections.end();
    {
        std::lock_guard lk{m_gnd_connections_mtx};
        found_it = m_gnd_connections.find(from.id);
    }

    if (found_it != m_gnd_connections.end()) {
        ret = get_avoidance(found_it->second, max_bridge_len);
    } else {
        auto conn = deepsearch_ground_connection(
            beam_ex_policy , m_sm, j, get_radius(dst), sla::DOWN);

        {
            std::lock_guard lk{m_gnd_connections_mtx};
            m_gnd_connections[from.id] = conn;
        }

        ret = get_avoidance(conn, max_bridge_len);
    }

    return ret;
}

inline void build_pillars(SupportTreeBuilder &builder,
                          BranchingTreeBuilder &vbuilder,
                          const SupportableMesh &sm)
{
    for (size_t pill_id = 0; pill_id < vbuilder.pillars().size(); ++pill_id) {
        auto * conn = vbuilder.ground_conn(pill_id);
        if (conn)
            build_ground_connection(builder, sm, *conn);
    }
}

void create_branching_tree(SupportTreeBuilder &builder, const SupportableMesh &sm)
{
    auto coordfn = [&sm](size_t id, size_t dim) { return sm.pts[id].pos(dim); };
    KDTreeIndirect<3, float, decltype (coordfn)> tree{coordfn, sm.pts.size()};

    auto nondup_idx = non_duplicate_suppt_indices(tree, sm.pts, 0.1);
    std::vector<std::optional<Head>> heads(nondup_idx.size());
    auto leafs = reserve_vector<branchingtree::Node>(nondup_idx.size());

    execution::for_each(
        ex_tbb, size_t(0), nondup_idx.size(),
        [&sm, &heads, &nondup_idx, &builder](size_t i) {
            if (!builder.ctl().stopcondition())
                heads[i] = calculate_pinhead_placement(ex_seq, sm, nondup_idx[i]);
        },
        execution::max_concurrency(ex_tbb)
    );

    if (builder.ctl().stopcondition())
        return;

    for (auto &h : heads)
        if (h && h->is_valid()) {
            leafs.emplace_back(h->junction_point().cast<float>(), h->r_back_mm);
            h->id = long(leafs.size() - 1);
            builder.add_head(h->id, *h);
        }

    auto &its = *sm.emesh.get_triangle_mesh();
    ExPolygons bedpolys = {branchingtree::make_bed_poly(its)};

    auto props = branchingtree::Properties{}
                     .bed_shape(bedpolys)
                     .ground_level(sla::ground_level(sm))
                     .max_slope(sm.cfg.bridge_slope)
                     .max_branch_length(sm.cfg.max_bridge_length_mm);

    auto meshpts = sm.cfg.ground_facing_only ?
                       std::vector<branchingtree::Node>{} :
                       branchingtree::sample_mesh(its,
                                                  props.sampling_radius());

    auto bedpts  = branchingtree::sample_bed(props.bed_shape(),
                                             float(props.ground_level()),
                                             props.sampling_radius());

    for (auto &bp : bedpts)
        bp.Rmin = sm.cfg.head_back_radius_mm;

    branchingtree::PointCloud nodes{std::move(meshpts), std::move(bedpts),
                                    std::move(leafs), props};

    BranchingTreeBuilder vbuilder{builder, sm, nodes};

    execution::for_each(ex_tbb,
                        size_t(0),
                        nodes.get_leafs().size(),
                        [&nodes, &vbuilder](size_t leaf_idx) {
                            vbuilder.suggest_avoidance(nodes.get_leafs()[leaf_idx],
                                                       nodes.properties().max_branch_length());
                        });

    branchingtree::build_tree(nodes, vbuilder);

    build_pillars(builder, vbuilder, sm);

    for (size_t id : vbuilder.unroutable_pinheads())
        builder.head(id).invalidate();

}

}} // namespace Slic3r::sla
