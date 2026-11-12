#include "Slic3r/App/Render/GL/commonGL.hpp"
#include "Slic3r/App/Render/Context.hpp"
#include "Slic3r/App/Render/ShaderManager.hpp"

namespace Slic3r::App::Render {

const char* gl_error_desc(GLenum err)
{
    const char* sErr = 0;
    switch (err) {
    case GL_INVALID_ENUM:       sErr = "Invalid Enum";      break;
    case GL_INVALID_VALUE:      sErr = "Invalid Value";     break;
    // be aware that GL_INVALID_OPERATION is generated if glGetError is executed between the execution of glBegin and the corresponding execution of glEnd
    case GL_INVALID_OPERATION:  sErr = "Invalid Operation"; break;
    case GL_STACK_OVERFLOW:     sErr = "Stack Overflow";    break;
    case GL_STACK_UNDERFLOW:    sErr = "Stack Underflow";   break;
    case GL_OUT_OF_MEMORY:      sErr = "Out Of Memory";     break;
    default:                    sErr = "Unknown";           break;
    }
    return sErr;
}


}