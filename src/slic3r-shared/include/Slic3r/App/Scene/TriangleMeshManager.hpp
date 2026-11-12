#pragma once

#include "Slic3r/App/Render/ResourceManager.hpp"
#include "Slic3r/Domain/TriangleMesh.hpp"

#include "Slic3r/Biz/Algorithms/AABBMesh.hpp"

namespace Slic3r::App::Scene {

class TriangleMesh
{
public:

    TriangleMesh() = default;
    TriangleMesh(TriangleMesh&&) = default;

    explicit TriangleMesh(indexed_triangle_set&& triangles)
        : m_triangles(std::move(triangles)), m_aabb_mesh(std::make_unique<AABBMesh>(this->m_triangles))
    {}

    explicit TriangleMesh(std::shared_ptr<const Slic3r::Domain::TriangleMesh> triangles)
        : m_shared_triangles(std::move(triangles))
        , m_aabb_mesh(std::make_unique<AABBMesh>(this->m_shared_triangles->its))
    {}

    const AABBMesh& aabb_mesh() const { return *m_aabb_mesh; }
    const indexed_triangle_set& triangles() const
    {
        return m_shared_triangles ? m_shared_triangles->its : m_triangles;
    }

    std::size_t memsize() const
    {
        return triangles().memsize() + m_aabb_mesh->memsize();
    }

private:
    // Note: we may merge m_triangles and m_shared_triangles into single field,
    // but this will require using Slic3r::TriangleMesh for both cases
    // which wraps indexed_triangle_set with extra stats (min/max vertex, etc.)
    // These stats will be computed in ctor, but we likely not need it. That's the reason having
    // m_triangles and m_shared_triangles separated
    indexed_triangle_set m_triangles;
    std::shared_ptr<const Slic3r::Domain::TriangleMesh> m_shared_triangles;
    std::unique_ptr<AABBMesh> m_aabb_mesh;

};

template <typename K>
using TriangleMeshManager = Render::ResourceManager<K, TriangleMesh>;

}
