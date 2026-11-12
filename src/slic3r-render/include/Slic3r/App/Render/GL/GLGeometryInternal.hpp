#pragma once

#include "Slic3r/App/Render/Geometry.hpp"
#include "commonGL.hpp"

namespace Slic3r::App::Render::GL {

struct GLPullGeometryInternal : public Geometry::Internal {
    GLuint m_vao_id{0};
};

struct GLGeometryInternal : public Geometry::Internal {
    GLuint m_vao_id{0};
    bool m_has_indices{false};
    // Cached
    mutable GLuint m_shader_id{0};
};

}
