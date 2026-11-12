#include "Slic3r/App/Render/Init.hpp"
#include "Slic3r/App/Render/Context.hpp"

namespace Slic3r::App::Render {

void initialize_render() {
    Context& context = Context::instance();
    context.log_gl_info();
    context.shader_manager().init();
}
void shutdown_render() { Context::instance().release_resources(); }

}

