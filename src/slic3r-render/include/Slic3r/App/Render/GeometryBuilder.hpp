#pragma once

#include "Slic3r/Domain/Types.hpp"

#include "Geometry.hpp"

struct indexed_triangle_set;
namespace Slic3r::Domain { class TriangleMesh; }

namespace Slic3r::App::Render {

class Device;

struct VertexP3
{
    Domain::Vec3f position;

    static const VertexAttribsDesc& format();
};

struct VertexP3N3
{
    Domain::Vec3f position;
    Domain::Vec3f normal;

    static const VertexAttribsDesc& format();
};

struct VertexP3T2
{
    Domain::Vec3f position;
    Domain::Vec2f tex_coord;

    static const VertexAttribsDesc& format();
};

struct VertexP3N3T2
{
    Domain::Vec3f position;
    Domain::Vec3f normal;
    Domain::Vec2f tex_coord;

    static const VertexAttribsDesc& format();
};

struct VertexP3N3I1
{
    Domain::Vec3f position;
    Domain::Vec3f normal;
    // Index into the material's color palette texture; resolved to a color in the vertex shader.
    float palette_index;

    static const VertexAttribsDesc& format();
};

struct VertexP2T2
{
    Domain::Vec2f position;
    Domain::Vec2f tex_coord;

    static const VertexAttribsDesc& format();
};

static_assert(sizeof(VertexP3) == 3 * 4);
static_assert(sizeof(VertexP3T2) == (3 + 2) * 4);
static_assert(sizeof(VertexP3N3) == (3 + 3) * 4);
static_assert(sizeof(VertexP3N3T2) == (3 + 3 + 2) * 4);
static_assert(sizeof(VertexP3N3I1) == (3 + 3 + 1) * 4);
static_assert(sizeof(VertexP2T2) == (2 + 2) * 4);


template <typename T>
struct CommonIndexTypeTraits {
    constexpr static bool allowed = false;
};

template <>
struct CommonIndexTypeTraits<uint8_t> { constexpr static bool allowed = true; };
template <>
struct CommonIndexTypeTraits<uint16_t> { constexpr static bool allowed = true; };
template <>
struct CommonIndexTypeTraits<uint32_t> { constexpr static bool allowed = true; };

template <typename V, typename I=uint32_t>
class GeometryBuilder
{
    static_assert(
        CommonIndexTypeTraits<I>::allowed,
        "I parameters must be one of unsigned int, unsigned short, unsigned char"
    );

public:
    GeometryBuilder() = default;
    GeometryBuilder(GeometryBuilder&&) = default;
    GeometryBuilder(const GeometryBuilder&) = delete;

    using VertexType = V;
    //using IndexType = I;

    GeometryBuilder& reserve(size_t num_vertices, size_t num_indices)
    {
        m_vertices.reserve(num_vertices);
        m_indices.reserve(num_indices);
        return *this;
    }

    GeometryBuilder& add_vertex(const V& v)
    {
        m_vertices.push_back(v);
        return *this;
    }

    GeometryBuilder& add_vertex(const V& v, I& out_idx)
    {
        out_idx = static_cast<I>(m_vertices.size());
        m_vertices.push_back(v);
        return *this;
    }

    GeometryBuilder& add_vertices(std::initializer_list<V> v)
    {
        m_vertices.insert(m_vertices.end(), v.begin(), v.end());
        return *this;
    }

    GeometryBuilder& add_index(I i)
    {
        m_indices.push_back(i);
        return *this;
    }

    GeometryBuilder& add_indices(std::initializer_list<I> indices)
    {
        m_indices.insert(m_indices.end(), indices.begin(), indices.end());
        return *this;
    }

    GeometryBuilder& add_indices(std::vector<I>&& indices)
    {
        std::move(
            std::make_move_iterator(indices.begin()),
            std::make_move_iterator(indices.end()),
            std::back_inserter(m_indices)
        );
        indices.clear();

        return *this;
    }

    GeometryBuilder& add_triangle_indices(I i0, I i1, I i2)
    {
        m_indices.push_back(i0);
        m_indices.push_back(i1);
        m_indices.push_back(i2);
        return *this;
    }

    size_t& current_offset() const
    {
        return m_indices.empty() ? m_vertices.size() : m_indices.size();
    }

    GeometryBuilder& current_offset(size_t& vertex_offset) const
    {
        vertex_offset = current_offset();
        return *this;
    }

    GeometryBuilder& add_draw_command(const DrawCommand& rc)
    {
        m_commands.push_back(rc);
        return *this;
    }

    void update(Geometry& geometry, bool clear_after = true)
    {
        void* index_data = nullptr;
        size_t index_count = 0;
        IndexType index_type = IndexType::UInt;
        std::unique_ptr<char[]> repacked_index_data;

        if (!m_indices.empty()) {
            index_count = m_indices.size();
            if (repack_index<unsigned char>(m_indices, repacked_index_data, index_type) ||
                repack_index<unsigned short>(m_indices, repacked_index_data, index_type))
                index_data = repacked_index_data.get();
            else {
                index_type = IndexTypeTraits<I>::index_type;
                index_data = m_indices.data();
            }
        }

        geometry.upload(
            m_vertices.data(), m_vertices.size(), VertexType::format(),
            index_data, index_count,index_type
        );
        geometry.draw_commands() = m_commands;
        if (clear_after) {
            m_vertices.clear();
            m_indices.clear();
            m_commands.clear();
        }
    }

    std::unique_ptr<Geometry> build(Device& device)
    {
        std::unique_ptr<Geometry> geometry = std::make_unique<Geometry>(device);
        update(*geometry);
        return geometry;
    }

    size_t vertex_count() const { return m_vertices.size(); }
    size_t index_count() const { return m_indices.size(); }

private:
    template <typename DestI>
    static bool repack_index(const std::vector<I>& src, std::unique_ptr<char[]>& dest, IndexType& destType)
    {
        if (IndexTypeTraits<I>::index_type == IndexTypeTraits<DestI>::index_type)
            return false;
        const auto size = src.size();
        if (size > std::numeric_limits<DestI>::max())
            return false;

        destType = IndexTypeTraits<DestI>::index_type;
        dest = std::make_unique<char[]>(size * sizeof(DestI));
        for (size_t i = 0; i < size; i++) {
            auto* dest_el = reinterpret_cast<DestI*>(&dest[sizeof(DestI) * i]);
            *dest_el = static_cast<DestI>(src[i]);
        }
        return true;
    }

private:
    std::vector<V> m_vertices;
    std::vector<I> m_indices;
    DrawCommands m_commands;
};

std::unique_ptr<Geometry> geometry_from_triangle_mesh(
    Device& device, const Domain::TriangleMesh& triangle_mesh, const Material& material = {}
);
std::unique_ptr<Geometry> geometry_from_triangle_mesh(
    Device& device, const indexed_triangle_set& triangle_mesh, const Material& material = {}
);
std::unique_ptr<Geometry> geometry_from_triangles(
    Device& device, const std::vector<Domain::Vec3f>& triangles, const Material& material = {}
);
std::unique_ptr<Geometry> geometry_from_triangles(
    Device& device, const std::vector<std::pair<Domain::Vec3f, Domain::Vec2f>>& triangles, const Material& material = {}
);
std::unique_ptr<Geometry> geometry_from_lines(
    Device& device, const std::vector<Domain::Vec3f>& lines, const Material& material = {}
);

} // namespace Slic3r::App::Render
