///|/ Copyright (c) Prusa Research 2019 - 2022 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef SLA_SUPPORTTREEBUILDER_HPP
#define SLA_SUPPORTTREEBUILDER_HPP

#include <libslic3r/Execution/ExecutionTBB.hpp>
#include <libslic3r/SLA/SupportTree.hpp>
#include <libslic3r/SLA/SupportTreeTypes.hpp>
#include <libslic3r/TriangleMesh.hpp>
#include <libslic3r/SLA/Pad.hpp>
#include <libslic3r/MTUtils.hpp>
#include <assert.h>
#include <oneapi/tbb/spin_mutex.h>
#include <stddef.h>
#include <algorithm>
#include <mutex>
#include <utility>
#include <vector>
#include <cassert>
#include <cstddef>

#include "admesh/stl.h"
#include "libslic3r/Point.hpp"
#include "libslic3r/SLA/JobController.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r {
namespace sla {

/**
 * Terminology:
 *
 * Support point:
 * The point on the model surface that needs support.
 *
 * Pillar:
 * A thick column that spans from a support point to the ground and has
 * a thick cone shaped base where it touches the ground.
 *
 * Ground facing support point:
 * A support point that can be directly connected with the ground with a pillar
 * that does not collide or cut through the model.
 *
 * Non ground facing support point:
 * A support point that cannot be directly connected with the ground (only with
 * the model surface).
 *
 * Head:
 * The pinhead that connects to the model surface with the sharp end end
 * to a pillar or bridge stick with the dull end.
 *
 * Headless support point:
 * A support point on the model surface for which there is not enough place for
 * the head. It is either in a hole or there is some barrier that would collide
 * with the head geometry. The headless support point can be ground facing and
 * non ground facing as well.
 *
 * Bridge:
 * A stick that connects two pillars or a head with a pillar.
 *
 * Junction:
 * A small ball in the intersection of two or more sticks (pillar, bridge, ...)
 *
 * CompactBridge:
 * A bridge that connects a headless support point with the model surface or a
 * nearby pillar.
 */

template<class T, int I> T distance(const Vec<I, T>& p) {
    return p.norm();
}

template<class T, int I>
T distance(const Vec<I, T>& pp1, const Vec<I, T>& pp2) {
    return (pp1 - pp2).norm();
}


// This class will hold the support tree parts (not meshes, but logical parts)
// with some additional bookkeeping as well. Various parts of the support
// geometry are stored separately and are merged when the caller queries the
// merged mesh (for every part, there is a meshing routine, see
// SupportTreeMesher.hpp). The merged result is cached for fast subsequent
// delivery of the merged mesh which can be quite complex. The support tree
// creation algorithm can use an instance of this class as a somewhat higher
// level tool for crafting the 3D support mesh. Parts can be added with the
// appropriate methods such as add_head or add_pillar which forwards the
// constructor arguments and fills the IDs of these substructures. The IDs are
// basically indices into the arrays of the appropriate type (heads, pillars,
// etc...). One can later query e.g. a pillar for a specific head...
class SupportTreeBuilder {
    // For heads it is beneficial to use the same IDs as for the support points.
    std::vector<Head>       m_heads;
    std::vector<size_t>     m_head_indices;
    std::vector<Pillar>     m_pillars;
    std::vector<Junction>   m_junctions;
    std::vector<Bridge>     m_bridges;
    std::vector<Bridge>     m_crossbridges;
    std::vector<DiffBridge> m_diffbridges;
    std::vector<Pedestal>   m_pedestals;
    std::vector<Anchor>     m_anchors;

    JobController m_ctl;
    
    using Mutex = tbb::spin_mutex;
    
    mutable indexed_triangle_set m_meshcache;
    mutable Mutex m_mutex;
    mutable bool m_meshcache_valid = false;
    mutable double m_model_height = 0; // the full height of the model
    
    template<class BridgeT, class...Args>
    const BridgeT& _add_bridge(std::vector<BridgeT> &br, Args&&... args)
    {
        std::lock_guard<Mutex> lk(m_mutex);
        br.emplace_back(std::forward<Args>(args)...);
        br.back().id = long(br.size() - 1);
        m_meshcache_valid = false;
        return br.back();
    }
    
public:
    
    explicit SupportTreeBuilder(const JobController &ctl = {}) : m_ctl{ctl} {}
    SupportTreeBuilder(SupportTreeBuilder &&o);
    SupportTreeBuilder(const SupportTreeBuilder &o);
    SupportTreeBuilder& operator=(SupportTreeBuilder &&o);
    SupportTreeBuilder& operator=(const SupportTreeBuilder &o);

    const JobController &ctl() const { return m_ctl; }

    template<class...Args> Head& add_head(unsigned id, Args&&... args)
    {
        std::lock_guard<Mutex> lk(m_mutex);
        m_heads.emplace_back(std::forward<Args>(args)...);
        m_heads.back().id = id;
        
        if (id >= m_head_indices.size()) m_head_indices.resize(id + 1);
        m_head_indices[id] = m_heads.size() - 1;
        
        m_meshcache_valid = false;
        return m_heads.back();
    }
    
    long add_pillar(long headid, double length)
    {
        std::lock_guard<Mutex> lk(m_mutex);
        if (m_pillars.capacity() < m_heads.size())
            m_pillars.reserve(m_heads.size() * 10);
        
        assert(headid >= 0 && size_t(headid) < m_head_indices.size());
        Head &head = m_heads[m_head_indices[size_t(headid)]];
        
        Vec3d hjp = head.junction_point() - Vec3d{0, 0, length};
        m_pillars.emplace_back(hjp, length, head.r_back_mm);

        Pillar& pillar = m_pillars.back();
        pillar.id = long(m_pillars.size() - 1);
        head.pillar_id = pillar.id;
        pillar.start_junction_id = head.id;
        pillar.starts_from_head = true;
        
        m_meshcache_valid = false;
        return pillar.id;
    }
    
    void add_pillar_base(long pid, double baseheight = 3, double radius = 2);

    template<class...Args> const Anchor& add_anchor(Args&&...args)
    {
        std::lock_guard<Mutex> lk(m_mutex);
        m_anchors.emplace_back(std::forward<Args>(args)...);
        m_anchors.back().id = long(m_junctions.size() - 1);
        m_meshcache_valid = false;
        return m_anchors.back();
    }
    
    void increment_bridges(const Pillar& pillar)
    {
        std::lock_guard<Mutex> lk(m_mutex);
        assert(pillar.id >= 0 && size_t(pillar.id) < m_pillars.size());
        
        if(pillar.id >= 0 && size_t(pillar.id) < m_pillars.size())
            m_pillars[size_t(pillar.id)].bridges++;
    }
    
    void increment_links(const Pillar& pillar)
    {
        std::lock_guard<Mutex> lk(m_mutex);
        assert(pillar.id >= 0 && size_t(pillar.id) < m_pillars.size());
        
        if(pillar.id >= 0 && size_t(pillar.id) < m_pillars.size()) 
            m_pillars[size_t(pillar.id)].links++;
    }
    
    unsigned bridgecount(const Pillar &pillar) const {
        std::lock_guard<Mutex> lk(m_mutex);
        assert(pillar.id >= 0 && size_t(pillar.id) < m_pillars.size());
        return pillar.bridges;
    }
    
    template<class...Args> long add_pillar(Args&&...args)
    {
        std::lock_guard<Mutex> lk(m_mutex);
        if (m_pillars.capacity() < m_heads.size())
            m_pillars.reserve(m_heads.size() * 10);
        
        m_pillars.emplace_back(std::forward<Args>(args)...);
        Pillar& pillar = m_pillars.back();
        pillar.id = long(m_pillars.size() - 1);
        pillar.starts_from_head = false;
        m_meshcache_valid = false;
        return pillar.id;
    }
    
    template<class...Args> const Junction& add_junction(Args&&... args)
    {
        std::lock_guard<Mutex> lk(m_mutex);
        m_junctions.emplace_back(std::forward<Args>(args)...);
        m_junctions.back().id = long(m_junctions.size() - 1);
        m_meshcache_valid = false;
        return m_junctions.back();
    }
    
    const Bridge& add_bridge(const Vec3d &s, const Vec3d &e, double r)
    {
        return _add_bridge(m_bridges, s, e, r);
    }
    
    const Bridge& add_bridge(long headid, const Vec3d &endp)
    {
        std::lock_guard<Mutex> lk(m_mutex);
        assert(headid >= 0 && size_t(headid) < m_head_indices.size());
        
        Head &h = m_heads[m_head_indices[size_t(headid)]];
        m_bridges.emplace_back(h.junction_point(), endp, h.r_back_mm);
        m_bridges.back().id = long(m_bridges.size() - 1);
        
        h.bridge_id = m_bridges.back().id;
        m_meshcache_valid = false;
        return m_bridges.back();
    }
    
    template<class...Args> const Bridge& add_crossbridge(Args&&... args)
    {
        return _add_bridge(m_crossbridges, std::forward<Args>(args)...);
    }

    template<class...Args> const DiffBridge& add_diffbridge(Args&&... args)
    {
        return _add_bridge(m_diffbridges, std::forward<Args>(args)...);
    }
    
    Head &head(unsigned id)
    {
        std::lock_guard<Mutex> lk(m_mutex);
        assert(id < m_head_indices.size());
        
        m_meshcache_valid = false;
        return m_heads[m_head_indices[id]];
    }
    
    inline size_t pillarcount() const {
        std::lock_guard<Mutex> lk(m_mutex);
        return m_pillars.size();
    }
    
    inline const std::vector<Pillar> &pillars() const { return m_pillars; }
    inline const std::vector<Head>   &heads() const { return m_heads; }
    inline const std::vector<Bridge> &bridges() const { return m_bridges; }
    inline const std::vector<Bridge> &crossbridges() const { return m_crossbridges; }
    
    template<class T> inline IntegerOnly<T, const Pillar&> pillar(T id) const
    {
        std::lock_guard<Mutex> lk(m_mutex);
        assert(id >= 0 && size_t(id) < m_pillars.size() &&
               size_t(id) < std::numeric_limits<size_t>::max());
        
        return m_pillars[size_t(id)];
    }
    
    template<class T> inline IntegerOnly<T, Pillar&> pillar(T id) 
    {
        std::lock_guard<Mutex> lk(m_mutex);
        assert(id >= 0 && size_t(id) < m_pillars.size() &&
               size_t(id) < std::numeric_limits<size_t>::max());
        
        return m_pillars[size_t(id)];
    }

    // WITHOUT THE PAD!!!
    const indexed_triangle_set &merged_mesh(size_t steps = 45) const;
    
    const indexed_triangle_set& retrieve_mesh(
        MeshType meshtype = MeshType::Support) const;

    SupportTreeOutput retrieve_output();

    void retrieve_full_mesh(indexed_triangle_set &outmesh) const
    {
        its_merge(outmesh, retrieve_mesh(MeshType::Support));
        its_merge(outmesh, retrieve_mesh(MeshType::Pad));
    }
};

}} // namespace Slic3r::sla

#endif // SUPPORTTREEBUILDER_HPP
