///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
// Bake a grayscale texture into a mesh as vertex displacement, in three passes:
//   1. Adaptive subdivision of edges longer than edge_length_mm, tracking the
//      original triangle each sub-triangle descends from.
//   2. Displace painted vertices along their smooth normals by the sampled
//      grayscale value.
//   3. QEM decimate down to target_triangle_count.

#include "MeshDisplace.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <queue>

#include "libslic3r/NormalUtils.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/QuadricEdgeCollapse.hpp"

namespace Slic3r::Feature::TextureSkin {

// Soft cap on the intermediate subdivided triangle count, matching
// stlTexturizer's safety limit.
static constexpr size_t MAX_SUBDIVIDED_TRIANGLES = 5u * 1000u * 1000u;

std::vector<bool> extract_painted_face_mask(
    size_t                                          num_original_facets,
    const TriangleSelector::TriangleSplittingData  &data)
{
    std::vector<bool> painted(num_original_facets, false);
    // Nothing painted: used_states index for TEXTURE_SKIN (== ENFORCER) is false.
    const size_t state_idx = static_cast<size_t>(TriangleStateType::TEXTURE_SKIN);
    if (state_idx >= data.used_states.size() || !data.used_states[state_idx])
        return painted;
    for (const auto &m : data.triangles_to_split) {
        if (m.triangle_idx >= 0 && size_t(m.triangle_idx) < num_original_facets)
            painted[m.triangle_idx] = true;
    }
    return painted;
}

// ---------------------------------------------------------------------------
// Subdivision with per-output-triangle origin tracking.
// ---------------------------------------------------------------------------

namespace {

struct SubdivResult {
    indexed_triangle_set    its;
    std::vector<uint32_t>   origin;   // origin[i] = original triangle index that output triangle i came from
    bool                    truncated = false;
};

// Midpoint-subdivision with an edge-midpoint cache. For each triangle we split
// its longest edge recursively until all edges are ≤ max_length. We only
// track *original* triangle identity — it's fine for a deeply subdivided
// triangle to share edges with its sibling chain.
static SubdivResult subdivide_with_origins(
    const indexed_triangle_set &in,
    float                       max_length,
    std::function<void()>       throw_on_cancel)
{
    SubdivResult r;
    r.its.vertices = in.vertices;           // will be appended to as we split
    r.its.indices.reserve(in.indices.size() * 2);
    r.origin.reserve(in.indices.size() * 2);

    // Edge key → new midpoint vertex index, so neighbouring triangles share
    // the same new vertex and the mesh stays watertight. Key is the unordered
    // pair of vertex indices of that edge.
    std::map<std::pair<int,int>, int> edge_midpoint_cache;
    auto edge_key = [](int a, int b) {
        return a < b ? std::pair<int,int>(a,b) : std::pair<int,int>(b,a);
    };
    auto get_midpoint = [&](int a, int b) {
        auto key = edge_key(a, b);
        auto it = edge_midpoint_cache.find(key);
        if (it != edge_midpoint_cache.end()) return it->second;
        const Vec3f &va = r.its.vertices[a];
        const Vec3f &vb = r.its.vertices[b];
        const int new_idx = int(r.its.vertices.size());
        r.its.vertices.push_back((va + vb) * 0.5f);
        edge_midpoint_cache.emplace(key, new_idx);
        return new_idx;
    };

    const float max_sq = max_length * max_length;
    auto sq_len = [&](int a, int b) {
        const Vec3f d = r.its.vertices[a] - r.its.vertices[b];
        return d.squaredNorm();
    };

    struct Work { int v0, v1, v2; uint32_t origin; };
    std::queue<Work> q;
    size_t cancel_counter = 0;

    for (uint32_t face_idx = 0; face_idx < in.indices.size(); ++face_idx) {
        const Vec3i &t = in.indices[face_idx];
        q.push(Work{t[0], t[1], t[2], face_idx});
        while (!q.empty()) {
            if (throw_on_cancel && (++cancel_counter & 0xFFFFu) == 0) throw_on_cancel();
            if (r.its.indices.size() + q.size() > MAX_SUBDIVIDED_TRIANGLES) {
                r.truncated = true;
                // Drain queue into output without further splitting — preserves validity.
                while (!q.empty()) {
                    const Work w = q.front(); q.pop();
                    r.its.indices.emplace_back(w.v0, w.v1, w.v2);
                    r.origin.push_back(w.origin);
                }
                break;
            }
            Work w = q.front(); q.pop();

            // Find the longest edge.
            const float l0 = sq_len(w.v0, w.v1);
            const float l1 = sq_len(w.v1, w.v2);
            const float l2 = sq_len(w.v2, w.v0);
            float lmax = l0; int which = 0;
            if (l1 > lmax) { lmax = l1; which = 1; }
            if (l2 > lmax) { lmax = l2; which = 2; }
            if (lmax <= max_sq) {
                r.its.indices.emplace_back(w.v0, w.v1, w.v2);
                r.origin.push_back(w.origin);
                continue;
            }

            // Split the longest edge at its midpoint.
            int m;
            if (which == 0) {
                m = get_midpoint(w.v0, w.v1);
                q.push(Work{w.v0, m, w.v2, w.origin});
                q.push(Work{m, w.v1, w.v2, w.origin});
            } else if (which == 1) {
                m = get_midpoint(w.v1, w.v2);
                q.push(Work{w.v0, w.v1, m, w.origin});
                q.push(Work{w.v0, m, w.v2, w.origin});
            } else {
                m = get_midpoint(w.v2, w.v0);
                q.push(Work{w.v0, w.v1, m, w.origin});
                q.push(Work{m, w.v1, w.v2, w.origin});
            }
        }
        if (r.truncated) break;
    }
    r.its.indices.shrink_to_fit();
    r.origin.shrink_to_fit();
    return r;
}

static double sample_displacement_for_vertex(
    const Vec3f          &pos_f,
    const Vec3f          &normal_f,
    const MeshDisplaceParams &p)
{
    const Vec3d pos(pos_f.x(), pos_f.y(), pos_f.z());
    const Vec3d nrm(normal_f.x(), normal_f.y(), normal_f.z());
    const UVResult uvs = compute_uv(pos, nrm, p.uv_mode, p.uv_settings, p.bounds);
    double grey = 0.0, wsum = 0.0;
    for (int i = 0; i < uvs.count; ++i) {
        const UVSample &s = uvs.samples[i];
        grey += s.w * sample_bilinear(*p.image, s.u, s.v);
        wsum += s.w;
    }
    if (wsum > 0.0) grey /= wsum;
    return (grey - 0.5) * 2.0 * double(p.amplitude_mm);
}

} // anon namespace

indexed_triangle_set mesh_displace(
    const indexed_triangle_set                 &original,
    const MeshDisplaceParams                   &params,
    std::function<void()>                       throw_on_cancel,
    std::function<void(int)>                    statusfn)
{
    if (!params.image || params.image->empty() || original.indices.empty())
        return {};

    auto tick = [&](int percent) { if (statusfn) statusfn(percent); };
    auto cancel = [&]() { if (throw_on_cancel) throw_on_cancel(); };

    // --- Pass 1: subdivide ------------------------------------------------
    tick(0);
    SubdivResult sub = subdivide_with_origins(original, params.edge_length_mm, throw_on_cancel);
    cancel();
    tick(35);

    // --- Build per-output-vertex painted mask -----------------------------
    std::vector<bool> vertex_painted;
    const bool region_masking = params.painted_region_its && !params.painted_region_its->indices.empty();
    const bool face_masking   = !region_masking && params.painted_face_mask && !params.painted_face_mask->empty();
    const bool masking        = region_masking || face_masking;

    if (region_masking) {
        // 3D barycentric + plane-distance test against each painted sub-tri.
        // Sub-tris live on their parent original triangle's plane; an output
        // vertex also lives on some original triangle's plane, so the test is
        // effectively planar when the planes match.
        vertex_painted.assign(sub.its.vertices.size(), false);
        const indexed_triangle_set &pr = *params.painted_region_its;
        constexpr float EDGE_EPS  = 1e-4f;
        constexpr float PLANE_EPS = 1e-3f;
        for (size_t pi = 0; pi < pr.indices.size(); ++pi) {
            if ((pi & 0x3Fu) == 0) cancel();
            const Vec3f &P0 = pr.vertices[pr.indices[pi][0]];
            const Vec3f &P1 = pr.vertices[pr.indices[pi][1]];
            const Vec3f &P2 = pr.vertices[pr.indices[pi][2]];
            const Vec3f e1 = P1 - P0;
            const Vec3f e2 = P2 - P0;
            const Vec3f n  = e1.cross(e2);
            const float n_len_sq = n.squaredNorm();
            if (n_len_sq < 1e-12f) continue;
            const float n_len = std::sqrt(n_len_sq);
            const Vec3f nn = n / n_len;
            const float d00 = e1.dot(e1);
            const float d01 = e1.dot(e2);
            const float d11 = e2.dot(e2);
            const float denom = d00 * d11 - d01 * d01;
            if (std::abs(denom) < 1e-12f) continue;
            const float inv_denom = 1.f / denom;

            for (size_t vi = 0; vi < sub.its.vertices.size(); ++vi) {
                if (vertex_painted[vi]) continue;
                const Vec3f w = sub.its.vertices[vi] - P0;
                if (std::abs(w.dot(nn)) > PLANE_EPS) continue;
                const float d20 = w.dot(e1);
                const float d21 = w.dot(e2);
                const float v   = (d11 * d20 - d01 * d21) * inv_denom;
                const float u2  = (d00 * d21 - d01 * d20) * inv_denom;
                const float u   = 1.f - v - u2;
                if (u >= -EDGE_EPS && v >= -EDGE_EPS && u2 >= -EDGE_EPS)
                    vertex_painted[vi] = true;
            }
        }
    } else if (face_masking) {
        vertex_painted.assign(sub.its.vertices.size(), false);
        const auto &pm = *params.painted_face_mask;
        for (size_t i = 0; i < sub.its.indices.size(); ++i) {
            const uint32_t orig = sub.origin[i];
            if (orig < pm.size() && pm[orig]) {
                const Vec3i &t = sub.its.indices[i];
                vertex_painted[t[0]] = true;
                vertex_painted[t[1]] = true;
                vertex_painted[t[2]] = true;
            }
        }
    }
    cancel();

    // --- Pass 2: displace painted vertices --------------------------------
    const std::vector<Vec3f> normals = NormalUtils::create_normals(sub.its, NormalUtils::VertexNormalType::NelsonMaxWeighted);
    assert(normals.size() == sub.its.vertices.size());

    for (size_t v = 0; v < sub.its.vertices.size(); ++v) {
        if (masking && !vertex_painted[v]) continue;
        if (params.skip_bottom_face && normals[v].z() < params.bottom_threshold) continue;
        if ((v & 0xFFFFu) == 0) cancel();
        const double disp = sample_displacement_for_vertex(sub.its.vertices[v], normals[v], params);
        sub.its.vertices[v] += normals[v] * float(disp);
    }
    tick(45);
    cancel();

    // --- Pass 3: decimate --------------------------------------------------
    float max_err = params.max_error;
    auto collapse_status = [&](int percent) {
        // Map 0..100 → 45..100 so the overall progress keeps moving.
        tick(45 + percent * 55 / 100);
    };
    its_quadric_edge_collapse(sub.its, params.target_triangle_count, &max_err, throw_on_cancel, collapse_status);
    tick(100);
    return std::move(sub.its);
}

} // namespace Slic3r::Feature::TextureSkin
