#pragma once

#include <iostream>
#include <Slic3r/Assert.hpp>
#include <GL/glew.h>

#ifdef NDEBUG
#define glCheck()
#define DEBUG_ASSERT_BOUND_IB(ib) (void)ib
#define DEBUG_ASSERT_BOUND_VAO(vao)  (void)vao
#else
#define glCheck() { \
  GLenum err = glGetError(); \
  if (err != GL_NO_ERROR) {  \
    PANIC(std::string("GL Error: ") + ::Slic3r::App::Render::gl_error_desc(err)); \
  } \
}

#define DEBUG_ASSERT_BOUND_IB(ib) \
    { \
        GLint bound_ib; \
        glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &bound_ib); \
        DEBUG_ASSERT(ib == bound_ib); \
    }

#define DEBUG_ASSERT_BOUND_VAO(vao)                         \
    {                                                       \
        GLint bound_vao;                                    \
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &bound_vao); \
        DEBUG_ASSERT(vao == bound_vao);                     \
    }

#endif

namespace Slic3r::App::Render {

const char* gl_error_desc(GLenum err);

}
