#pragma once

#include "Slic3r/App/Render/Types.hpp"
#include "Slic3r/App/Render/Buffer.hpp"
#include "Slic3r/App/Render/VertexAttribDesc.hpp"
#include "Slic3r/App/Render/WithInternal.hpp"
#include "Slic3r/App/Render/DrawCommand.hpp"

namespace Slic3r::App::Render {

class Device;

class PullGeometry : public WithInternal
{
public:
    PullGeometry(Device& device);
    ~PullGeometry();
private:
    Device& m_device;
};

/**
 * @brief Static GPU geometry container.
 *
 * It wraps vertex and optional index buffer along with list of DrawCommand s. Use GeometryBuilder
 * to construct geometry in structured way, or you can use upload() method to pass raw buffers and
 * relevant format descriptors.
 *
 */
class Geometry : public WithInternal
{
public:
    explicit Geometry(Device& device) : Geometry(device, BufferUsage::StaticDraw) {}
    Geometry(Device& device, BufferUsage buffer_usage) : Geometry(device, buffer_usage, buffer_usage) {}
    Geometry(Device& device, BufferUsage vertex_usage, BufferUsage index_usage);
    ~Geometry() override;

    Geometry(Geometry&&) = default;

    Geometry(const Geometry&) = delete;
    Geometry& operator=(const Geometry&) = delete;

    void upload(
        const void* vertex_data,
        size_t vertex_count,
        const VertexAttribsDesc& vertex_format,
        const void* index_data = nullptr,
        size_t index_count = 0,
        IndexType index_format = IndexType::UInt
    );

    VertexBuffer* vertex_buffer() { return m_vb.get(); }
    const VertexBuffer* vertex_buffer() const { return m_vb.get(); }

    IndexBuffer* index_buffer() { return m_ib.get(); }
    const IndexBuffer* index_buffer() const { return m_ib.get(); }

    const VertexAttribsDesc& vertex_format() const { return m_vertex_format; }
    IndexType index_type() const { return m_index_type; }

    bool ready() const { return m_built; }

    size_t vertex_count() const { return m_vertex_count; }
    size_t index_count() const { return m_index_count; }

    const DrawCommands& draw_commands() const { return m_commands; }
    DrawCommands& draw_commands() { return m_commands; }

private:
    Device& m_device;
    std::unique_ptr<VertexBuffer> m_vb;
    std::unique_ptr<IndexBuffer> m_ib;

    VertexAttribsDesc m_vertex_format;
    IndexType m_index_type{IndexType::UInt};
    BufferUsage m_vertex_usage{BufferUsage::StaticDraw};
    BufferUsage m_index_usage{BufferUsage::StaticDraw};
    DrawCommands m_commands;
    size_t m_vertex_count{0};
    size_t m_index_count{0};

    bool m_built{false};
};

}
