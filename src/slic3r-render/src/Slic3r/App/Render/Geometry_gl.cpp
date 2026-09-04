#include "Slic3r/App/Render/Geometry.hpp"
#include "Slic3r/App/Render/Device.hpp"

#include "Slic3r/App/Render/GL/GLGeometryInternal.hpp"
#include "Slic3r/App/Render/GL/GLDeviceInternal.hpp"
#include "Slic3r/App/Render/GL/GLBufferInternal.hpp"
#include "Slic3r/App/Render/GL/commonGL.hpp"

namespace Slic3r::App::Render {

PullGeometry::PullGeometry(Device& device)
    : WithInternal(InternalType<GL::GLPullGeometryInternal>()),
    m_device{device}
{
    auto& self{get_internal_as<GL::GLPullGeometryInternal>()};
    glGenVertexArrays(1, &self.m_vao_id);
    glCheck();
    auto& internal_device{m_device.get_internal_as<GL::GLDeviceInternal>()};
    internal_device.bind_vao(0);
}

PullGeometry::~PullGeometry() {
    auto& self{get_internal_as<GL::GLPullGeometryInternal>()};
    if (self.m_vao_id != 0) {
        glDeleteVertexArrays(1, &self.m_vao_id);
    }
}

Geometry::Geometry(Device& device, BufferUsage vertex_usage, BufferUsage index_usage)
    : WithInternal(InternalType<GL::GLGeometryInternal>())
    , m_device(device)
    , m_vertex_usage(vertex_usage)
    , m_index_usage(index_usage)
{}

Geometry::~Geometry()
{
    auto& self = get_internal_as<GL::GLGeometryInternal>();
    if (self.m_vao_id) {
        glDeleteVertexArrays(1, &self.m_vao_id);
    }
}

void Geometry::upload(
    const void* vertex_data,
    size_t vertex_count,
    const VertexAttribsDesc& vertex_format,
    const void* index_data,
    size_t index_count,
    Render::IndexType index_format
)
{
    SPDLOG_TRACE("Buffer::upload() Part 1");

    auto& ctx = Context::instance();
    // TODO: handle ES with VAO
    bool use_vao = ctx.is_vao_available();

    auto& self = get_internal_as<GL::GLGeometryInternal>();
    auto& device = m_device.get_internal_as<GL::GLDeviceInternal>();
    self.m_has_indices = index_count > 0;
    if (use_vao) {
        if (self.m_vao_id == 0) {
            glGenVertexArrays(1, &self.m_vao_id);
            glCheck();
        }
        device.bind_vao(0);
    }
    SPDLOG_TRACE("Buffer::upload() Part 2");

    DEBUG_ASSERT_BOUND_VAO(0);

    if (!m_vb)
        m_vb = m_device.create_vertex_buffer();
    m_vb->set_data(vertex_data, vertex_attribs_stride(vertex_format) * vertex_count, m_vertex_usage);
    m_vertex_count = vertex_count;
    m_vertex_format = vertex_format;

    if (index_data && index_count > 0) {
        if (!m_ib)
            m_ib = m_device.create_index_buffer();
        m_ib->set_data(index_data, index_type_size(index_format) * index_count, m_index_usage);
        DEBUG_ASSERT_BOUND_IB(m_ib->get_internal_as<GL::GLBufferInternal>().m_id);
        m_index_count = index_count;
        m_index_type = index_format;
    }
//    if (use_vao)
//        device.bind_vao(0);
    device.bind_vertex_buffer(0);
    device.bind_index_buffer(0);

    m_built = true;
    self.m_shader_id = 0;
}


} // namespace Slic3r::App::Render
