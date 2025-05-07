///|/ Copyright (c) Prusa Research 2020 - 2023 Tomáš Mészáros @tamasmeszaros, Vojtěch Bubník @bubnikv
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include <libslic3r/SLA/SupportTreeBuilder.hpp>
#include <libslic3r/SLA/SupportTreeMesher.hpp>

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/SLA/SupportTree.hpp"
#include "libslic3r/TriangleMesh.hpp"

namespace Slic3r {
namespace sla {

Head::Head(double       r_big_mm,
           double       r_small_mm,
           double       length_mm,
           double       penetration,
           const Vec3d &direction,
           const Vec3d &offset)
    : dir(direction)
    , pos(offset)
    , r_back_mm(r_big_mm)
    , r_pin_mm(r_small_mm)
    , width_mm(length_mm)
    , penetration_mm(penetration)
{
}

SupportTreeBuilder::SupportTreeBuilder(SupportTreeBuilder &&o)
    : m_heads(std::move(o.m_heads))
    , m_head_indices{std::move(o.m_head_indices)}
    , m_pillars{std::move(o.m_pillars)}
    , m_bridges{std::move(o.m_bridges)}
    , m_crossbridges{std::move(o.m_crossbridges)}
    , m_meshcache{std::move(o.m_meshcache)}
    , m_meshcache_valid{o.m_meshcache_valid}
    , m_model_height{o.m_model_height}
{}

SupportTreeBuilder::SupportTreeBuilder(const SupportTreeBuilder &o)
    : m_heads(o.m_heads)
    , m_head_indices{o.m_head_indices}
    , m_pillars{o.m_pillars}
    , m_bridges{o.m_bridges}
    , m_crossbridges{o.m_crossbridges}
    , m_meshcache{o.m_meshcache}
    , m_meshcache_valid{o.m_meshcache_valid}
    , m_model_height{o.m_model_height}
{}

SupportTreeBuilder &SupportTreeBuilder::operator=(SupportTreeBuilder &&o)
{
    m_heads = std::move(o.m_heads);
    m_head_indices = std::move(o.m_head_indices);
    m_pillars = std::move(o.m_pillars);
    m_bridges = std::move(o.m_bridges);
    m_crossbridges = std::move(o.m_crossbridges);
    m_meshcache = std::move(o.m_meshcache);
    m_meshcache_valid = o.m_meshcache_valid;
    m_model_height = o.m_model_height;
    return *this;
}

SupportTreeBuilder &SupportTreeBuilder::operator=(const SupportTreeBuilder &o)
{
    m_heads = o.m_heads;
    m_head_indices = o.m_head_indices;
    m_pillars = o.m_pillars;
    m_bridges = o.m_bridges;
    m_crossbridges = o.m_crossbridges;
    m_meshcache = o.m_meshcache;
    m_meshcache_valid = o.m_meshcache_valid;
    m_model_height = o.m_model_height;
    return *this;
}

void SupportTreeBuilder::add_pillar_base(long pid, double baseheight, double radius)
{
    std::lock_guard<Mutex> lk(m_mutex);
    assert(pid >= 0 && size_t(pid) < m_pillars.size());
    Pillar& pll = m_pillars[size_t(pid)];
    m_pedestals.emplace_back(pll.endpt, std::min(baseheight, pll.height),
                             std::max(radius, pll.r_start), pll.r_start);

    m_pedestals.back().id = m_pedestals.size() - 1;
    m_meshcache_valid = false;
}

const indexed_triangle_set &SupportTreeBuilder::merged_mesh(size_t steps) const
{
    if (m_meshcache_valid) return m_meshcache;
    
    indexed_triangle_set merged;
    
    for (auto &head : m_heads) {
        if (ctl().stopcondition()) break;
        if (head.is_valid()) its_merge(merged, get_mesh(head, steps));
    }
    
    for (auto &pill : m_pillars) {
        if (ctl().stopcondition()) break;
        its_merge(merged, get_mesh(pill, steps));
    }

    for (auto &pedest : m_pedestals) {
        if (ctl().stopcondition()) break;
        its_merge(merged, get_mesh(pedest, steps));
    }
    
    for (auto &j : m_junctions) {
        if (ctl().stopcondition()) break;
        its_merge(merged, get_mesh(j, steps));
    }

    for (auto &bs : m_bridges) {
        if (ctl().stopcondition()) break;
        its_merge(merged, get_mesh(bs, steps));
    }
    
    for (auto &bs : m_crossbridges) {
        if (ctl().stopcondition()) break;
        its_merge(merged, get_mesh(bs, steps));
    }

    for (auto &bs : m_diffbridges) {
        if (ctl().stopcondition()) break;
        its_merge(merged, get_mesh(bs, steps));
    }

    for (auto &anch : m_anchors) {
        if (ctl().stopcondition()) break;
        its_merge(merged, get_mesh(anch, steps));
    }

    if (ctl().stopcondition()) {
        // In case of failure we have to return an empty mesh
        m_meshcache = {};
        return m_meshcache;
    }
    
    m_meshcache = std::move(merged);
    
    // The mesh will be passed by const-pointer to TriangleMeshSlicer,
    // which will need this.
    its_merge_vertices(m_meshcache);
    
    BoundingBoxf3 bb = bounding_box(m_meshcache);
    m_model_height   = bb.max(Z) - bb.min(Z);

    m_meshcache_valid = true;
    return m_meshcache;
}

const indexed_triangle_set &SupportTreeBuilder::merge_and_cleanup()
{
    // in case the mesh is not generated, it should be...
    auto &ret = merged_mesh(); 
    
    // Doing clear() does not garantee to release the memory.
    clear_and_shrink(m_heads);
    clear_and_shrink(m_head_indices);
    clear_and_shrink(m_pillars);
    clear_and_shrink(m_junctions);
    clear_and_shrink(m_bridges);

    return ret;
}

const indexed_triangle_set &SupportTreeBuilder::retrieve_mesh(MeshType meshtype) const
{
    static const indexed_triangle_set EMPTY_MESH;

    switch(meshtype) {
    case MeshType::Support: return merged_mesh();
    case MeshType::Pad:     return EMPTY_MESH; //pad().tmesh;
    }
    
    return m_meshcache;
}

}} // namespace Slic3r::sla
