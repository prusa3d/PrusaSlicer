#pragma once

#include "CommandBuffer.hpp"

#include <vector>

#include "Slic3r/App/Render/Geometry.hpp"
#include "Slic3r/App/Render/GeometryBuilder.hpp"
#include "Slic3r/Domain/Types.hpp"

namespace Slic3r::App::Render {

namespace Internal {

#define DEFINE_HAS_ATTR_OF_TYPE_TRAIT(trait, attr_type, attr) \
    template <typename T, typename = void> \
    struct trait : std::false_type {}; \
    template <typename T> \
    struct trait<T, std::void_t<decltype(std::declval<T>().attr)>> \
        : std::is_same<decltype(std::declval<T>().attr), attr_type> {};

DEFINE_HAS_ATTR_OF_TYPE_TRAIT(HasPosition3, Domain::Vec3f, position)
DEFINE_HAS_ATTR_OF_TYPE_TRAIT(HasPosition2, Domain::Vec2f, position)
DEFINE_HAS_ATTR_OF_TYPE_TRAIT(HasNormal, Domain::Vec3f, normal)
DEFINE_HAS_ATTR_OF_TYPE_TRAIT(HasTexCoord2, Domain::Vec2f, color)

template <typename V, typename DerivedT>
class VertexAttributeBuilder
{
protected:
    VertexAttributeBuilder() = default;
    explicit VertexAttributeBuilder(const V& vertex) : m_vertex(vertex) {}
public:
    DerivedT& attr(std::function<void(V&)> modifier)
    {
        modifier(m_vertex);
        return *this;
    }

    template <typename U = V, typename = std::enable_if_t<Internal::HasNormal<U>::value>>
    DerivedT& normal(const Domain::Vec3f& n)
    {
        m_vertex.normal = n;
        return *this;
    }

    template <typename U = V, typename = std::enable_if_t<Internal::HasTexCoord2<U>::value>>
    DerivedT& tex_coord(const Domain::Vec2f& tc)
    {
        m_vertex.tex_coord = tc;
        return *this;
    }
protected:
    template <typename U = V, typename = std::enable_if_t<Internal::HasPosition3<U>::value>>
    void set_position(const Domain::Vec3f& v)
    {
        m_vertex.position = v;
    }

    template <typename U = V, typename = std::enable_if_t<Internal::HasPosition2<U>::value>>
    void set_position(const Domain::Vec2f& v)
    {
        m_vertex.position = v;
    }


protected:
    V m_vertex{};
};

}


template <typename V>
class DynamicGeometry : public Internal::VertexAttributeBuilder<V, DynamicGeometry<V>>
{
public:
    using Vertices = std::vector<V>;

    class PrimitiveBuilder : public Internal::VertexAttributeBuilder<V, DynamicGeometry<V>::PrimitiveBuilder>
    {
    public:
        ~PrimitiveBuilder()
        {
            if (m_vertices.empty())
                return;

            const size_t offset = m_parent.m_vertices.size();
            const size_t count = m_vertices.size();

            m_parent.m_vertices.insert(
                m_parent.m_vertices.end(),
                std::make_move_iterator(m_vertices.begin()),
                std::make_move_iterator(m_vertices.end())
            );
            m_parent.m_draw_commands.emplace_back(DrawCommand{m_primitive, offset, count, m_material});
            m_parent.m_uploaded = false;
        }

        template <typename T>
        PrimitiveBuilder& vertex(const T& v)
        {
            set_position(v);
            m_vertices.push_back(m_vertex);
            return *this;
        }
    private:
        friend class DynamicGeometry<V>;
        PrimitiveBuilder(DynamicGeometry& parent, PrimitiveType primitive, Material& material)
            : Internal::VertexAttributeBuilder<V, PrimitiveBuilder>(parent.m_vertex), m_parent(parent), m_primitive(primitive), m_material(material)
        {
            m_vertex = m_parent.m_vertex;
        }

        using Internal::VertexAttributeBuilder<V, PrimitiveBuilder>::m_vertex;
        using Internal::VertexAttributeBuilder<V, PrimitiveBuilder>::set_position;

    private:
        DynamicGeometry& m_parent;
        PrimitiveType m_primitive;
        Material m_material;
        Vertices m_vertices;
    };

    explicit DynamicGeometry(Device& device, BufferUsage usage = BufferUsage::DynamicDraw)
        : m_device(device), m_usage(usage), m_geometry(device, m_usage)
    {}

    bool empty() const { return m_draw_commands.empty(); }

    DynamicGeometry& set_material(const Material& material)
    {
        m_material = material;
        return *this;
    }

    void upload()
    {
        if (m_uploaded)
            return;

        m_geometry.upload(&m_vertices[0], m_vertices.size(), V::format());
        m_geometry.draw_commands() = m_draw_commands;
        m_uploaded = true;

        clear();
    }

    void clear()
    {
        const size_t data_size = m_vertices.size() * sizeof(V);
        m_vertices.clear();
        m_draw_commands.clear();
        if (data_size > MAX_TRANSIENT_DATA_SIZE) {
            m_vertices.shrink_to_fit();
            m_draw_commands.shrink_to_fit();
        }
    }

    void draw(CommandBuffer& cmd_buffer, const Material& material = {})
    {
        if (!m_uploaded)
            upload();
        cmd_buffer.bind_and_draw(m_geometry, material);
    }

    PrimitiveBuilder build_primitive(PrimitiveType primitive)
    { return PrimitiveBuilder(*this, primitive, m_material); }

    PrimitiveBuilder build_primitive(PrimitiveType primitive, const Material& material)
    { return PrimitiveBuilder(*this, primitive, material); }

private:
    friend class PrimitiveBuilder;

    Device& m_device;
    BufferUsage m_usage;
    Vertices m_vertices;
    DrawCommands m_draw_commands;
    Geometry m_geometry;

    bool m_uploaded{false};

    // template for building elements
    Material m_material;

    // Max size of vertex data to keep after upload
    static constexpr size_t MAX_TRANSIENT_DATA_SIZE = 655536;
};

}
