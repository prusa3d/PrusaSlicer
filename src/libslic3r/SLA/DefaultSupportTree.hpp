///|/ Copyright (c) Prusa Research 2020 - 2023 Tomáš Mészáros @tamasmeszaros, Lukáš Hejl @hejllukas
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef LEGACYSUPPORTTREE_HPP
#define LEGACYSUPPORTTREE_HPP

#include <libslic3r/SLA/SpatIndex.hpp>
#include <libslic3r/Execution/ExecutionTBB.hpp>
#include <Eigen/Geometry>
#include <algorithm>
#include <map>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

#include "SupportTreeUtilsLegacy.hpp"
#include "libslic3r/AABBMesh.hpp"
#include "libslic3r/Execution/Execution.hpp"
#include "libslic3r/SLA/Pad.hpp"
#include "libslic3r/SLA/SupportTree.hpp"
#include "libslic3r/SLA/SupportTreeBuilder.hpp"
#include "libslic3r/Point.hpp"

namespace Slic3r { namespace sla {

inline constexpr const auto &suptree_ex_policy = ex_tbb;

class PillarIndex {
    PointIndex m_index;
    using Mutex = execution::BlockingMutex<decltype(suptree_ex_policy)>;
    mutable Mutex m_mutex;

public:

    template<class...Args> inline void guarded_insert(Args&&...args)
    {
        std::lock_guard<Mutex> lck(m_mutex);
        m_index.insert(std::forward<Args>(args)...);
    }

    template<class...Args>
    inline std::vector<PointIndexEl> guarded_query(Args&&...args) const
    {
        std::lock_guard<Mutex> lck(m_mutex);
        return m_index.query(std::forward<Args>(args)...);
    }

    template<class...Args> inline void insert(Args&&...args)
    {
        m_index.insert(std::forward<Args>(args)...);
    }

    template<class...Args>
    inline std::vector<PointIndexEl> query(Args&&...args) const
    {
        return m_index.query(std::forward<Args>(args)...);
    }

    template<class Fn> inline void foreach(Fn fn) { m_index.foreach(fn); }
    template<class Fn> inline void guarded_foreach(Fn fn)
    {
        std::lock_guard<Mutex> lck(m_mutex);
        m_index.foreach(fn);
    }

    PointIndex guarded_clone()
    {
        std::lock_guard<Mutex> lck(m_mutex);
        return m_index;
    }
};

class DefaultSupportTree {
    const SupportableMesh &m_sm;

    using PtIndices = std::vector<unsigned>;

    PtIndices m_iheads;            // support points with pinhead
    PtIndices m_iheads_onmodel;

    std::map<unsigned, AABBMesh::hit_result> m_head_to_ground_scans;

    // normals for support points from model faces.
    Eigen::MatrixXd  m_support_nmls;

    // Clusters of points which can reach the ground directly and can be
    // bridged to one central pillar
    std::vector<PtIndices> m_pillar_clusters;

    // This algorithm uses the SupportTreeBuilder class to fill gradually
    // the support elements (heads, pillars, bridges, ...)
    SupportTreeBuilder& m_builder;

    // support points in Eigen/IGL format
    Eigen::MatrixXd m_points;

    // throw if canceled: It will be called many times so a shorthand will
    // come in handy.
    ThrowOnCancel m_thr;

    // A spatial index to easily find strong pillars to connect to.
    PillarIndex m_pillar_index;

    // When bridging heads to pillars... TODO: find a cleaner solution
    execution::BlockingMutex<ExecutionTBB> m_bridge_mutex;

    inline AABBMesh::hit_result ray_mesh_intersect(const Vec3d& s,
                                                      const Vec3d& dir)
    {
        return m_sm.emesh.query_ray_hit(s, dir);
    }

    // This function will test if a future pinhead would not collide with the
    // model geometry. It does not take a 'Head' object because those are
    // created after this test. Parameters: s: The touching point on the model
    // surface. dir: This is the direction of the head from the pin to the back
    // r_pin, r_back: the radiuses of the pin and the back sphere width: This
    // is the full width from the pin center to the back center m: The object
    // mesh.
    // The return value is the hit result from the ray casting. If the starting
    // point was inside the model, an "invalid" hit_result will be returned
    // with a zero distance value instead of a NAN. This way the result can
    // be used safely for comparison with other distances.
    AABBMesh::hit_result pinhead_mesh_intersect(
        const Vec3d& s,
        const Vec3d& dir,
        double r_pin,
        double r_back,
        double width,
        double safety_d);

    AABBMesh::hit_result pinhead_mesh_intersect(const Vec3d &s,
                                                   const Vec3d &dir,
                                                   double       r_pin,
                                                   double       r_back,
                                                   double       width)
    {
        return pinhead_mesh_intersect(s, dir, r_pin, r_back, width,
                                      r_back * m_sm.cfg.safety_distance_mm /
                                          m_sm.cfg.head_back_radius_mm);
    }

    // Checking bridge (pillar and stick as well) intersection with the model.
    // If the function is used for headless sticks, the ins_check parameter
    // have to be true as the beginning of the stick might be inside the model
    // geometry.
    // The return value is the hit result from the ray casting. If the starting
    // point was inside the model, an "invalid" hit_result will be returned
    // with a zero distance value instead of a NAN. This way the result can
    // be used safely for comparison with other distances.
    AABBMesh::hit_result bridge_mesh_intersect(
        const Vec3d& s,
        const Vec3d& dir,
        double r,
        double safety_d);

    AABBMesh::hit_result bridge_mesh_intersect(
        const Vec3d& s,
        const Vec3d& dir,
        double r)
    {
        return bridge_mesh_intersect(s, dir, r,
                                     r * m_sm.cfg.safety_distance_mm /
                                         m_sm.cfg.head_back_radius_mm);
    }

    template<class...Args>
    inline double bridge_mesh_distance(Args&&...args) {
        return bridge_mesh_intersect(std::forward<Args>(args)...).distance();
    }

    // Helper function for interconnecting two pillars with zig-zag bridges.
    bool interconnect(const Pillar& pillar, const Pillar& nextpillar);

    // For connecting a head to a nearby pillar.
    bool connect_to_nearpillar(const Head& head, long nearpillar_id);

    // Find route for a head to the ground. Inserts additional bridge from the
    // head to the pillar if cannot create pillar directly.
    // The optional dir parameter is the direction of the bridge which is the
    // direction of the pinhead if omitted.
    inline bool connect_to_ground(Head& head);

    bool connect_to_model_body(Head &head);

    bool search_pillar_and_connect(const Head& source);

    // This is a proxy function for pillar creation which will mind the gap
    // between the pad and the model bottom in zero elevation mode.
    // jp is the starting junction point which needs to be routed down.
    // sourcedir is the allowed direction of an optional bridge between the
    // jp junction and the final pillar.
    bool create_ground_pillar(const Junction &jp,
                              const Vec3d &sourcedir,
                              long         head_id = SupportTreeNode::ID_UNSET);

    void add_pillar_base(long pid)
    {
        m_builder.add_pillar_base(pid, m_sm.cfg.base_height_mm, m_sm.cfg.base_radius_mm);
    }

    std::optional<DiffBridge> search_widening_path(const Vec3d &jp,
                                                   const Vec3d &dir,
                                                   double       radius,
                                                   double       new_radius)
    {
        return sla::search_widening_path(suptree_ex_policy, m_sm, jp, dir, radius, new_radius);
    }

public:
    DefaultSupportTree(SupportTreeBuilder & builder, const SupportableMesh &sm);

    // Now let's define the individual steps of the support generation algorithm

    // Filtering step: here we will discard inappropriate support points
    // and decide the future of the appropriate ones. We will check if a
    // pinhead is applicable and adjust its angle at each support point. We
    // will also merge the support points that are just too close and can
    // be considered as one.
    void add_pinheads();

    // Further classification of the support points with pinheads. If the
    // ground is directly reachable through a vertical line parallel to the
    // Z axis we consider a support point as pillar candidate. If touches
    // the model geometry, it will be marked as non-ground facing and
    // further steps will process it. Also, the pillars will be grouped
    // into clusters that can be interconnected with bridges. Elements of
    // these groups may or may not be interconnected. Here we only run the
    // clustering algorithm.
    void classify();

    // Step: Routing the ground connected pinheads, and interconnecting
    // them with additional (angled) bridges. Not all of these pinheads
    // will be a full pillar (ground connected). Some will connect to a
    // nearby pillar using a bridge. The max number of such side-heads for
    // a central pillar is limited to avoid bad weight distribution.
    void routing_to_ground();

    // Step: routing the pinheads that would connect to the model surface
    // along the Z axis downwards. For now these will actually be connected with
    // the model surface with a flipped pinhead. In the future here we could use
    // some smart algorithms to search for a safe path to the ground or to a
    // nearby pillar that can hold the supported weight.
    void routing_to_model();

    void interconnect_pillars();

    inline void merge_result() { m_builder.merged_mesh(); }

    static bool execute(SupportTreeBuilder & builder, const SupportableMesh &sm);
};

inline void create_default_tree(SupportTreeBuilder &builder, const SupportableMesh &sm)
{
    DefaultSupportTree::execute(builder, sm);
}

}} // namespace Slic3r::sla

#endif // LEGACYSUPPORTTREE_HPP
