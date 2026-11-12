#include "Slic3r/App/Render/ShaderManager.hpp"

#include <string_view>

#include "Slic3r/App/Render/GL/commonGL.hpp"
#include "Slic3r/App/Render/Context.hpp"

#include "Slic3r/Assert.hpp"
#include "Slic3r/Domain/TriangleSelector.hpp"
#include "Slic3r/Log.hpp"
#include "Slic3r/PlatformInfo.hpp"

using Slic3r::Domain::TriangleSelector::TRIANGLE_STATE_TYPE_COUNT;
using Slic3r::Domain::TriangleSelector::TriangleStateType;

namespace Slic3r::App::Render {

std::pair<bool, std::string> ShaderManager::init()
{
    using namespace std::literals;
    std::string error;

    auto append_shader = [this, &error](const std::string& name, const Shader::ShaderFilenames& filenames, 
        const std::initializer_list<std::string_view> &defines = {}) {
        m_shaders.push_back(std::make_unique<Shader>(m_context.device()));
        SPDLOG_INFO("Loading shader {}", name);
        if (!m_shaders.back()->init_from_files(name, filenames, defines)) {
            error += name + "\n";
            // if any error happens while initializating the shader, we remove it from the list
            m_shaders.pop_back();
            SPDLOG_ERROR("Loading shader {} failed with error: {}", name, error);
            return false;
        }
        return true;
    };

    DEBUG_ASSERT(m_shaders.empty());

    bool valid = true;

    const std::string mm_palette_size =
        "MM_PALETTE_SIZE " + std::to_string(TRIANGLE_STATE_TYPE_COUNT);

#if SLIC3R_OPENGL_ES
    const std::string prefix = "ES/";
    // used to render wireframed triangles
    valid &= append_shader("wireframe", { prefix + "wireframe.vs", prefix + "wireframe.fs" });
#else
    const std::string prefix = m_context.gl_version() >= Semver(3, 1, 0) ? "140/" : "110/";
#endif // SLIC3R_OPENGL_ES
    // imgui shader
    valid &= append_shader("imgui", { prefix + "imgui.vs", prefix + "imgui.fs" });
    // basic shader, used to render all what was previously rendered using the immediate mode
    valid &= append_shader("flat", { prefix + "flat.vs", prefix + "flat.fs" });
    // basic shader with plane clipping, used to render volumes in picking pass
    valid &= append_shader("flat_clip", { prefix + "flat_clip.vs", prefix + "flat_clip.fs" });
    // basic shader for textures, used to render textures
    valid &= append_shader("flat_texture", { prefix + "flat_texture.vs", prefix + "flat_texture.fs" });
    // used to render 3D scene background
    valid &= append_shader("background", { prefix + "background.vs", prefix + "background.fs" });
#if SLIC3R_OPENGL_ES
    // used to render dashed lines
    valid &= append_shader("dashed_lines", { prefix + "dashed_lines.vs", prefix + "dashed_lines.fs" });
#else
    if (m_context.is_core_profile())
        // used to render thick and/or dashed lines
        valid &= append_shader("dashed_thick_lines", { prefix + "dashed_thick_lines.vs", prefix + "dashed_thick_lines.fs", prefix + "dashed_thick_lines.gs" });
#endif // SLIC3R_OPENGL_ES
    // used to render bed axes and model, selection hints, gcode sequential view marker model, preview shells, options in gcode preview
    valid &= append_shader("gouraud_light", { prefix + "gouraud_light.vs", prefix + "gouraud_light.fs" });
    // extend "gouraud_light" by per-vertex colors, used to render MM-painted volumes in the plater
    valid &= append_shader(
        "mm_gouraud_light",
        {prefix + "mm_gouraud_light.vs", prefix + "mm_gouraud_light.fs"},
        {mm_palette_size}
    );
    // extend "gouraud_light" by adding two clipping planes at different world z, used in sla preview
    valid &= append_shader("gouraud_light_double_z_clip", { prefix + "gouraud_light_double_z_clip.vs", prefix + "gouraud_light_double_z_clip.fs" });
    // extend "gouraud_light" by adding clipping, used in sla gizmos
    valid &= append_shader("gouraud_light_clip", { prefix + "gouraud_light_clip.vs", prefix + "gouraud_light_clip.fs" });
    // used to render printbed
    valid &= append_shader("printbed", { prefix + "printbed.vs", prefix + "printbed.fs" });
    // used to render objects in 3d editor
    valid &= append_shader("gouraud", { prefix + "gouraud.vs", prefix + "gouraud.fs" }
#if ENABLE_ENVIRONMENT_MAP
        , { "ENABLE_ENVIRONMENT_MAP"sv }
#endif // ENABLE_ENVIRONMENT_MAP
        );
    // used to render variable layers heights in 3d editor
    valid &= append_shader("variable_layer_height", { prefix + "variable_layer_height.vs", prefix + "variable_layer_height.fs" });
    // used to render highlight contour around selected triangles inside the multi-material gizmo
    valid &= append_shader("mm_contour", {prefix + "mm_contour.vs", prefix + "mm_contour.fs"});
    auto platform_info = PlatformInfo::instance();
    // Used to render painted triangles inside the multi-material gizmo. Triangle normals are
    // computed inside fragment shader.
    valid &= append_shader("mm_gouraud", { prefix + "mm_gouraud.vs", prefix + "mm_gouraud.fs" });
    // used to render gcode toolpaths
    valid &= append_shader("segments", { prefix + "segments.vs", prefix + "segments.fs" });
    // used to render gcode options
    valid &= append_shader("options", { prefix + "options.vs", prefix + "options.fs" });
    // used to render gcode toolpaths center of gravity marker
    valid &= append_shader("cog_marker", { prefix + "cog_marker.vs", prefix + "cog_marker.fs" });
    // used to render gcode toolpaths tool marker
    valid &= append_shader("tool_marker", { prefix + "tool_marker.vs", prefix + "tool_marker.fs" });
    // used to render models' shadowsmap
    valid &= append_shader("shadowsmap", { prefix + "shadowsmap.vs", prefix + "shadowsmap.fs" });
    // extend "shadowsmap" by adding two clipping planes at different world z, used in sla preview
    valid &= append_shader("shadowsmap_double_z_clip", { prefix + "shadowsmap_double_z_clip.vs", prefix + "shadowsmap_double_z_clip.fs" });
   // used to render gcode options' shadowsmap
    valid &= append_shader("options_shadowsmap", { prefix + "options_shadowsmap.vs", prefix + "shadowsmap.fs" });
    // used to render gcode segments' shadowsmap
    valid &= append_shader("segments_shadowsmap", { prefix + "segments_shadowsmap.vs", prefix + "shadowsmap.fs" });
    // used to render ao texture
    valid &= append_shader("ao_texture", { prefix + "ao_texture.vs", prefix + "ao_texture.fs" });
    // used to blur the ao texture
    valid &= append_shader("ao_hblur", { prefix + "ao_blur.vs", prefix + "ao_hblur.fs" });
    valid &= append_shader("ao_vblur", { prefix + "ao_blur.vs", prefix + "ao_vblur.fs" });
    // used to render ao lighting
    valid &= append_shader("ao_lighting", { prefix + "ao_lighting.vs", prefix + "ao_lighting.fs" });
    // used to render ao g-buffer for models
    valid &= append_shader("gbuffer_ao", { prefix + "gbuffer_ao.vs", prefix + "gbuffer_ao.fs" });
    // extend "gbuffer_ao" by per-vertex colors, used to render MM-painted volumes in the plater
    valid &= append_shader(
        "mm_gbuffer_ao",
        {prefix + "mm_gbuffer_ao.vs", prefix + "mm_gbuffer_ao.fs"},
        {mm_palette_size}
    );
    // extend "gbuffer_ao" by adding two clipping planes at different world z, used in sla preview
    valid &= append_shader("gbuffer_ao_double_z_clip", { prefix + "gbuffer_ao_double_z_clip.vs", prefix + "gbuffer_ao_double_z_clip.fs" });
    // used to render ao g-buffer for printbed
    valid &= append_shader("printbed_ao", { prefix + "printbed_ao.vs", prefix + "printbed_ao.fs" });
    // used to render ao g-buffer for gcode options
    valid &= append_shader("options_ao", { prefix + "options_ao.vs", prefix + "options_ao.fs" });
    // used to render ao g-buffer for gcode segments
    valid &= append_shader("segments_ao", { prefix + "segments_ao.vs", prefix + "segments_ao.fs" });
    // used to render shadowed models with phong shading
    valid &= append_shader("phong_shadows", { prefix + "phong_shadows.vs", prefix + "phong_shadows.fs" });
    // extend "phong_shadows" by per-vertex colors, used to render MM-painted volumes in the plater
    valid &= append_shader(
        "mm_phong_shadows",
        {prefix + "mm_phong_shadows.vs", prefix + "mm_phong_shadows.fs"},
        {mm_palette_size}
    );
    // extend "phong_shadows" by adding two clipping planes at different world z, used in sla preview
    valid &= append_shader("phong_shadows_double_z_clip", { prefix + "phong_shadows_double_z_clip.vs", prefix + "phong_shadows_double_z_clip.fs" });
    // used to render shadowed printbed with phong shading
    valid &= append_shader("printbed_phong_shadows", { prefix + "printbed_phong_shadows.vs", prefix + "printbed_phong_shadows.fs" });
    // used to render shadowed gcode options with phong shading
    valid &= append_shader("options_phong_shadows", { prefix + "options_phong_shadows.vs", prefix + "options_phong_shadows.fs" });
    // used to render shadowed gcode segments with phong shading
    valid &= append_shader("segments_phong_shadows", { prefix + "segments_phong_shadows.vs", prefix + "segments_phong_shadows.fs" });
    // similar to gouraud_light with added wireframe rendering
    valid &= append_shader("gouraud_light_wireframe", { prefix + "gouraud_light_wireframe.vs", prefix + "gouraud_light_wireframe.fs", prefix + "gouraud_light_wireframe.gs" });

    return { valid, error };
}

void ShaderManager::shutdown()
{
    m_shaders.clear();
}

Shader* ShaderManager::shader(const std::string& shader_name)
{
    auto it = std::find_if(m_shaders.begin(), m_shaders.end(), [&shader_name](std::unique_ptr<Shader>& p) { return p->get_name() == shader_name; });
    return (it != m_shaders.end()) ? it->get() : nullptr;
}

std::string ShaderManager::shader_name(const Shader* shader) const
{
    auto it = std::find_if(m_shaders.begin(), m_shaders.end(),
        [&shader](auto& p) { return p.get() == shader; });
    return (it != m_shaders.end()) ? (*it)->get_name() : std::string();
}


}
