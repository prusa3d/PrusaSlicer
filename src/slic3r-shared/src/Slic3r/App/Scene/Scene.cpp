#include "Slic3r/App/Scene/Scene.hpp"
#include "Slic3r/App/Render/Device.hpp"
#include "Slic3r/App/Render/Material.hpp"
#include "Slic3r/App/Render/ScopedDebugGroup.hpp"
#include "Slic3r/App/Render/MathUtils.hpp"
#include "Slic3r/App/Render/Framebuffer.hpp"
#include "Slic3r/App/Render/FramebufferManager.hpp"
#include "Slic3r/App/Render/GeometryBuilder.hpp"
#include "Slic3r/App/Render/TextureManager.hpp"
#include "Slic3r/App/Scene/CameraHelper.hpp"
#include "Slic3r/App/Scene/LightingHelper.hpp"
#include "Slic3r/App/Scene/MeshRenderNodeComponent.hpp"
#include "Slic3r/App/Scene/NodeVisitor.hpp"
#include "Slic3r/App/Scene/TextureUnits.hpp"
#include "Slic3r/App/LightSetting.hpp"

#include "Slic3r/Domain/Color.hpp"
#include "Slic3r/Domain/Types.hpp"

#include <imgui/imgui.h>
#include <tracy/Tracy.hpp>

#include <list>
#include <random>

using Slic3r::Domain::ColorRGBA;
using Slic3r::Domain::SquareMatrix4f;
using Slic3r::Domain::Transform3d;
using Slic3r::Domain::Vec3d;
using Slic3r::Domain::Vec2f;
using Slic3r::Domain::Vec3f;

namespace Slic3r::App::Scene {

enum class ShadingPass
{
    Shadowsmap,
    ShadowsReceivers,
    AOGBuffer,
};

std::vector<std::pair<std::string, std::string>> SHADOWSMAP_PASS_DICTIONARY = {
    {"gouraud_light", "shadowsmap"},
    {"gouraud_light_double_z_clip", "shadowsmap_double_z_clip"},
    {"mm_gouraud_light", "shadowsmap"},
    {"printbed", "shadowsmap"},
    {"options", "options_shadowsmap"},
    {"segments", "segments_shadowsmap"},
};

std::vector<std::pair<std::string, std::string>> SHADOWS_RECEIVERS_PASS_DICTIONARY = {
    {"gouraud_light", "phong_shadows"},
    {"gouraud_light_double_z_clip", "phong_shadows_double_z_clip"},
    {"mm_gouraud_light", "mm_phong_shadows"},
    {"printbed", "printbed_phong_shadows"},
    {"options", "options_phong_shadows"},
    {"segments", "segments_phong_shadows"},
};

std::vector<std::pair<std::string, std::string>> AO_G_BUFFER_PASS_DICTIONARY = {
    {"gouraud_light", "gbuffer_ao"},
    {"gouraud_light_double_z_clip", "gbuffer_ao_double_z_clip"},
    {"mm_gouraud_light", "mm_gbuffer_ao"},
    {"printbed", "printbed_ao"},
    {"options", "options_ao"},
    {"segments", "segments_ao"},
};

static std::string shader_name_by_shading_pass(const std::string& shader_name, ShadingPass pass)
{
    switch (pass)
    {
    case ShadingPass::Shadowsmap:
    {
        auto it = std::find_if(SHADOWSMAP_PASS_DICTIONARY.begin(), SHADOWSMAP_PASS_DICTIONARY.end(),
          [&shader_name](const auto& pair) { return pair.first == shader_name; });
        return (it != SHADOWSMAP_PASS_DICTIONARY.end()) ? it->second : shader_name;
    }
    case ShadingPass::ShadowsReceivers:
    {
        auto it = std::find_if(SHADOWS_RECEIVERS_PASS_DICTIONARY.begin(), SHADOWS_RECEIVERS_PASS_DICTIONARY.end(),
          [&shader_name](const auto& pair) { return pair.first == shader_name; });
        return (it != SHADOWS_RECEIVERS_PASS_DICTIONARY.end()) ? it->second : shader_name;
    }
    case ShadingPass::AOGBuffer:
    {
        auto it = std::find_if(AO_G_BUFFER_PASS_DICTIONARY.begin(), AO_G_BUFFER_PASS_DICTIONARY.end(),
          [&shader_name](const auto& pair) { return pair.first == shader_name; });
        return (it != AO_G_BUFFER_PASS_DICTIONARY.end()) ? it->second : shader_name;
    }
    default:
    {
        // unsupported target
        PANIC("Unsupported shading pass");
    }
    }
}

void MinimalSceneRenderCustomizer::on_render_begin(Render::CommandBuffer& cmd_buf)
{
    //cmd_buf.clear_buffers(true, true);
}

void MinimalSceneRenderCustomizer::on_opaque_pass_begin(
    Render::CommandBuffer& cmd_buf, RenderLayerId layer_index
)
{
    cmd_buf.set_blending_enabled(false);
    cmd_buf.set_depth_write_enabled(true);
    cmd_buf.set_depth_test_enabled(true);
    cmd_buf.set_cull_face_enabled(true);
}


void MinimalSceneRenderCustomizer::on_transparent_pass_begin(
    Render::CommandBuffer& cmd_buf, RenderLayerId layer_index
)
{
    cmd_buf.set_depth_test_enabled(true);
    cmd_buf.set_blending_enabled(true);
    Render::Blending blending { {Render::BlendFactor::SrcAlpha, Render::BlendFactor::OneMinusSrcAlpha}};
    cmd_buf.set_blending(blending);
    cmd_buf.set_depth_write_enabled(false);
}

void MinimalSceneRenderCustomizer::on_transparent_pass_end(
    Render::CommandBuffer& cmd_buf, RenderLayerId layer_index
)
{
    cmd_buf.set_blending_enabled(false);
    cmd_buf.set_depth_write_enabled(true);
}


GraphicsSettings Scene::s_graphics_settings;

SceneColors Scene::s_scene_colors;

MinimalSceneRenderCustomizer Scene::ms_default_customizer;


Scene::Scene() : m_camera_trackball(m_camera)
{
    m_nodes_by_id[m_root.id()] = &m_root;
    m_root.set_debug_name("root");

    s_graphics_settings.add_listener<IGraphicsSettingsChangedListener>(this);
    validate_lights(m_lighting.lights);
}

Scene::~Scene()
{
    s_graphics_settings.remove_listener<IGraphicsSettingsChangedListener>(this);

    // Hacky way to clear the noise texture, while TextureManager::m_dynamic_textures
    // is still alive.
    // The 0 noise size should force re-generation.
    s_graphics_settings.m_ao.noise_tex.reset();
    s_graphics_settings.m_ao.pending_noise_size = 0;
    s_graphics_settings.m_ao.noise_size         = 0;
}

void Scene::on_shading_type_changed(ShadingType shading_type)
{
    validate_lights(m_lighting.lights);
}

void Scene::on_node_changed(Node* node)
{
    invoke_listeners<ISceneChangedListener>([node](auto* listener) {
        listener->on_node_changed(node);
    });
}

void Scene::add_child(Node* node, Node* parent)
{
    invoke_listeners<ISceneChangedListener>([node](auto* listener) {
        listener->on_node_added(node);
    });
    node->set_node_changed_listener(this);
    register_node(node);
    if (parent == nullptr)
        parent = &m_root;
    parent->add_child(node);
}

bool Scene::remove_child(Node* node)
{
    auto* parent = node->parent();
    if (parent == nullptr)
        return false;

    // remove all children of the node before removing the node itself
    node->set_node_changed_listener(nullptr);
    remove_children([](const Node* child) { return true; }, node);
    return remove_children([node](const Node* child) {
        return child == node;
    }, parent);
}

bool Scene::remove_children(const Node::NodePredicate& predicate, Node* parent)
{
    if (parent == nullptr)
        parent = &m_root;

    return parent->remove_children([this, predicate](Node* n) {
        if (predicate(n)) {
            remove_children([](const Node*){return true;}, n);

            invoke_listeners<ISceneChangedListener>([n](auto* listener) {
                listener->on_node_removed(n);
            });
            unregister_node(n);

            return true;
        }
        return false;
    });
}

Node::NodeOwningList Scene::detach_children(const Node::NodePredicate & predicate, Node* parent)
{
    if (parent == nullptr)
        parent = &m_root;

    return parent->detach_children([&](auto n){
        if (predicate(n)) {
            unregister_node(n);
            return true;
        }
        return false;
    });
}

void Scene::register_node(Node* node)
{
    visit(*node, [this](Node& n) {
        m_nodes_by_id[n.id()] = &n;

        auto* modifier = n.transform_modifier();
        if (modifier == nullptr)
            return;
        auto* cam_listener = dynamic_cast<ICameraUpdateListener*>(modifier);
        if (cam_listener)
            m_camera.add_listener<ICameraUpdateListener>(cam_listener);
    });
}

void Scene::unregister_node(Node* node)
{
    visit(*node, [this](Node& n) {
        m_nodes_by_id.erase(n.id());
        auto* modifier = n.transform_modifier();
        if (modifier == nullptr)
            return;
        auto* cam_listener = dynamic_cast<ICameraUpdateListener*>(modifier);
        if (cam_listener)
            m_camera.remove_listener<ICameraUpdateListener>(cam_listener);
    });
}


Render::Material resolve_material(const Node& n)
{
    std::list<const Node*> path = {&n};
    const Node* p = n.parent();
    while (p) {
        path.push_front(p);
        p = p->parent();
    }

    Render::Material mat;

    const IRenderNodeComponent* render_component = n.render_component();
    if (render_component != nullptr)
        mat = render_component->material();
    for (const Node* component : path) {
        if (const Render::Material* override = component->material_override())
            mat.update(*override);
    }




    return mat;
}

void Scene::init_screen_quad(Render::Device& device) const
{
    m_screen_quad = const_cast<Scene*>(this)->geometry_manager().get_or_create("screen_quad", [&device]() {
        std::vector<std::pair<Vec2f, Vec2f>> sq_vertices = {
            {{-1.0f,  1.0f}, {0.0f, 1.0f}},
            {{-1.0f, -1.0f}, {0.0f, 0.0f}},
            {{ 1.0f,  1.0f}, {1.0f, 1.0f}},
            {{ 1.0f, -1.0f}, {1.0f, 0.0f}}
        };

        Render::GeometryBuilder<Render::VertexP2T2> builder;
        builder.reserve(sq_vertices.size(), 0);
        for (const auto& v : sq_vertices) {
            builder.add_vertex({ v.first, v.second });
        }
        builder.add_draw_command({ Render::PrimitiveType::TriangleStrip, 0, sq_vertices.size(), Render::Material() });
        return builder.build(device);
    });
}

void Scene::generate_ao_kernel(Render::Device& device) const
{
    AmbientOcclusion& ao = s_graphics_settings.m_ao;

    if (ao.kernel.empty())
        ao.pending_kernel_size = ao.DEFAULT_KERNEL_SIZE;

    if (ao.pending_kernel_size.has_value() && *ao.pending_kernel_size != ao.kernel.size()) {
        std::uniform_real_distribution<float> random_floats(0.0f, 1.0f); // generates random floats between 0.0 and 1.0
        std::default_random_engine generator;

        ao.kernel.clear();
        ao.kernel.reserve(*ao.pending_kernel_size);
        for (size_t i = 0; i < *ao.pending_kernel_size; ++i) {
            Vec3f sample(random_floats(generator) * 2.0f - 1.0f,
                         random_floats(generator) * 2.0f - 1.0f,
                         random_floats(generator));
            sample.normalize();
            sample *= random_floats(generator);
            float scale = float(i) / float(*ao.pending_kernel_size);
            // scale samples s.t. they're more aligned to center of kernel
            scale = 0.1f + scale * scale * 0.9f;
            sample *= scale;
            ao.kernel.emplace_back(sample);
        }

        ao.pending_kernel_size.reset();
    }
}

void Scene::generate_ao_noise(Render::Device& device) const
{
    AmbientOcclusion& ao = s_graphics_settings.m_ao;

    if (ao.noise_size == 0)
        ao.pending_noise_size = ao.DEFAULT_NOISE_SIZE;

    if (ao.pending_noise_size.has_value() && *ao.pending_noise_size != ao.noise_size) {
        ao.noise_size = *ao.pending_noise_size;

        std::uniform_real_distribution<float> random_floats(0.0f, 1.0f); // generates random floats between 0.0 and 1.0
        std::default_random_engine generator;

        std::vector<Vec3f> noise_data;
        noise_data.reserve(ao.noise_size * ao.noise_size);
        for (size_t i = 0; i < ao.noise_size * ao.noise_size; ++i) {
            Vec3f noise(random_floats(generator) * 2.0f - 1.0f,
                        random_floats(generator) * 2.0f - 1.0f,
                        0.0f); // rotate around z-axis (in tangent space)
            noise_data.emplace_back(noise);
        }

        ao.noise_tex = device.context().texture_manager().get_or_create_dynamic("ao_noise", Domain::PixelFormat::RGB32F, ao.noise_size, ao.noise_size);
        ao.noise_tex->set_data(Domain::PixelFormat::RGB32F, 0, ao.noise_size, ao.noise_size, (void*)noise_data.data(), noise_data.size() * sizeof(Vec3f));

        ao.pending_noise_size.reset();
    }
}

Scene::NodeMaterials Scene::collect_nodes_with_material(const Node::NodePredicate& predicate) const
{
    Scene::NodeMaterials ret;
    Node::ConstNodeList nodes;
    m_root.query(predicate, nodes);
    if (!nodes.empty()) {
        ret.reserve(nodes.size());
        for (const Node* node : nodes)
            ret.emplace_back(node, resolve_material(*node));
    }
    return ret;
}

void Scene::render_background(Render::Device& device, Render::CommandBuffer& cmd_buffer) const
{
    ZoneScoped;

    ColorRGBA top_color    = m_use_background_error_color ? s_scene_colors.bg_top_color_error :
                                                            s_scene_colors.bg_top_color_default;
    ColorRGBA bottom_color = m_use_background_error_color ? s_scene_colors.bg_bottom_color_error :
                                                            s_scene_colors.bg_bottom_color_default;

    Render::Material material;
    material.set_shader(device.context().shader_manager().shader("background"))
        .set_uniform("top_color", top_color)
        .set_uniform("bottom_color", bottom_color);

    cmd_buffer.bind_and_draw(*m_screen_quad, material);
    cmd_buffer.clear_buffers(false, true);
}

void Scene::render_shadowsmap_pass(Render::Device& device, const Camera& camera, ISceneRenderCustomizer* customizer) const
{
    ZoneScoped;

    Shadows& shadows = s_graphics_settings.m_shadows;
    if (shadows.light_cam.cam_projection().type() == CameraProjectionType::Perspective)
        shadows.light_cam.switch_projection_type();

    if (shadows.framebuffer == nullptr)
        shadows.pending_framebuffer_size = shadows.DEFAULT_FRAMEBUFFER_SIZE;

    if (shadows.pending_framebuffer_size.has_value() && *shadows.pending_framebuffer_size != shadows.framebuffer_size) {
        if (shadows.framebuffer != nullptr)
            device.context().framebuffer_manager().destroy(shadows.framebuffer);
        Render::FramebufferCreationData data;
        data.width = *shadows.pending_framebuffer_size;
        data.height = *shadows.pending_framebuffer_size;
        shadows.framebuffer = device.context().framebuffer_manager().create(data);
        shadows.framebuffer_size = *shadows.pending_framebuffer_size;
        shadows.pending_framebuffer_size.reset();
    }

    auto cmd_buffer = device.create_command_buffer();
    // set light viewport
    cmd_buffer->bind_framebuffer(*shadows.framebuffer);
    cmd_buffer->set_viewport({ 0, 0, shadows.framebuffer_size, shadows.framebuffer_size });
    cmd_buffer->set_depth_test_enabled(true);
    cmd_buffer->set_cull_face_enabled(true);
    cmd_buffer->clear_buffers(false, true);

    NodeMaterials nodes = collect_nodes_with_material([](auto n) {
        return n->has_render_component() && n->render_component()->cast_shadows() && !resolve_material(*n).transparent();
    });

    if (!nodes.empty()) {
        Eigen::AlignedBox3d world_aabb;
        // extend the bounding box to contain all shadow casters
        for (const auto& [node, material] : nodes) {
            if (node->has_raycast_component())
                world_aabb.extend(node->raycast_component()->world_bounding_box(node->world_transform().matrix()).cast<double>());
        }

        auto it = std::find_if(m_lighting.lights.begin(), m_lighting.lights.end(),
            [](const Light& l) { return l.shadows; }
        );
        DEBUG_ASSERT(it != m_lighting.lights.end());

        Vec3d world_light_dir = (it->system == LightReferenceSystem::Camera) ?
            Vec3d(camera.model().matrix().block<3, 3>(0, 0) * it->direction.cast<double>()) :
            it->direction.cast<double>();
        Vec3d center = world_aabb.center();
        Vec3d world_light_pos = center - 1000.0f * world_light_dir;
        shadows.light_cam.look_at(world_light_pos, center, Vec3d::UnitZ());

        Eigen::AlignedBox3d light_aabb = world_aabb.transformed(Transform3d(shadows.light_cam.view()));

        double eye_max_x = -DBL_MAX;
        double eye_max_y = -DBL_MAX;
        double eye_min_z = DBL_MAX;
        double eye_max_z = -DBL_MAX;
        for (int i = 0; i < 8; ++i) {
            Vec3d eye_c = light_aabb.corner(Eigen::AlignedBox3d::CornerType(i));
            eye_max_x = std::max(eye_max_x, eye_c.x());
            eye_max_y = std::max(eye_max_y, eye_c.y());
            eye_min_z = std::min(eye_min_z, -eye_c.z());
            eye_max_z = std::max(eye_max_z, -eye_c.z());
        }

        // ensure the bbox is in front of the light camera
        if (eye_min_z < 100.0) {
            double delta_z = 100.0 - eye_min_z;
            eye_min_z += delta_z;
            eye_max_z += delta_z;
            world_light_pos -= delta_z * world_light_dir;
            shadows.light_cam.look_at(world_light_pos, center, Vec3d::UnitZ());
        }

        static constexpr double Z_MARGIN = 10.0;
        eye_min_z -= Z_MARGIN;
        eye_max_z += Z_MARGIN;

        shadows.light_cam.set_projection(Render::ortho(-eye_max_x, eye_max_x, -eye_max_y, eye_max_y, eye_min_z, eye_max_z));

        if (customizer)
            customizer->on_render_begin(*cmd_buffer);

        constexpr int INITIAL_LAYER = std::numeric_limits<int>::min();
        int current_layer = INITIAL_LAYER;
        for (auto& [n, mat]  : nodes) {
            // did we start next layer
            if (auto layer = n->render_component()->layer_index(); layer != current_layer) {
                if (customizer) {
                    if (current_layer != INITIAL_LAYER) {
                        customizer->on_opaque_pass_end(*cmd_buffer, current_layer);
                        customizer->on_layer_end(*cmd_buffer, current_layer);
                    }
                    customizer->on_layer_begin(*cmd_buffer, layer);
                    customizer->on_opaque_pass_begin(*cmd_buffer, layer);
                }
                current_layer = layer;
            }

            std::string shader_name = device.context().shader_manager().shader_name(mat.shader());
            shader_name = shader_name_by_shading_pass(shader_name, ShadingPass::Shadowsmap);
            mat
              .set_shader(device.context().shader_manager().shader(shader_name))
              .set_uniform("light_position", (Vec3f)world_light_pos.cast<float>());
            n->render_component()->render(*n, shadows.light_cam, mat, *cmd_buffer);
        }

        if (customizer) {
            customizer->on_opaque_pass_end(*cmd_buffer, current_layer);
            customizer->on_layer_end(*cmd_buffer, current_layer);
            customizer->on_render_end(*cmd_buffer);
        }
    }

    // restore camera viewport
    cmd_buffer->set_viewport(camera.viewport());
    cmd_buffer->unbind_framebuffer(*shadows.framebuffer);
}

void Scene::render_shadows_receivers_pass(Render::Device& device, const Camera& camera, Render::CommandBuffer& cmd_buffer,
    ISceneRenderCustomizer* customizer) const
{
    ZoneScoped;

    NodeMaterials nodes = collect_nodes_with_material([](auto n) {
        return n->has_render_component() && n->render_component()->receive_shadows() && !resolve_material(*n).transparent();
    });

    if (nodes.empty())
        return;

    const Shadows& shadows = s_graphics_settings.m_shadows;

    Domain::SquareMatrix4d light_cam_proj_view_matrix = shadows.light_cam.projection() * shadows.light_cam.view().matrix();
    for (auto& [node, material] : nodes) {
        SquareMatrix4f light_cam_matrix = (light_cam_proj_view_matrix * node->world_transform().matrix()).cast<float>();
        std::string shader_name = device.context().shader_manager().shader_name(material.shader());
        shader_name = shader_name_by_shading_pass(shader_name, ShadingPass::ShadowsReceivers);
        material
            .set_shader(device.context().shader_manager().shader(shader_name))
            .set_uniform("light_matrix", light_cam_matrix)
            .set_uniform("shadows_intensity", shadows.intensity)
            .set_texture(TextureUnits::SHADOW_MAP, shadows.framebuffer->depth())
            .set_uniform("shadowsmap", TextureUnits::SHADOW_MAP);

        set_uniforms(m_lighting, material);
    }

    cmd_buffer.set_depth_test_enabled(true);
    cmd_buffer.set_cull_face_enabled(true);

    if (customizer)
        customizer->on_render_begin(cmd_buffer);

    constexpr int INITIAL_LAYER = std::numeric_limits<int>::min();
    int current_layer = INITIAL_LAYER;
    for (auto& [n, mat]  : nodes) {
        // did we start next layer
        if (auto layer = n->render_component()->layer_index(); layer != current_layer) {
            if (customizer) {
                if (current_layer != INITIAL_LAYER) {
                    customizer->on_opaque_pass_end(cmd_buffer, current_layer);
                    customizer->on_layer_end(cmd_buffer, current_layer);
                }
                customizer->on_layer_begin(cmd_buffer, layer);
                customizer->on_opaque_pass_begin(cmd_buffer, layer);
            }
            current_layer = layer;
        }

        set_uniforms(m_lighting, mat);
        n->render_component()->render(*n, camera, mat, cmd_buffer);
    }

    if (customizer) {
        customizer->on_opaque_pass_end(cmd_buffer, current_layer);
        customizer->on_layer_end(cmd_buffer, current_layer);
        customizer->on_render_end(cmd_buffer);
    }
}

void Scene::render_no_shadows_pass(const Camera& camera, Render::CommandBuffer& cmd_buffer, ISceneRenderCustomizer* customizer) const
{
    ZoneScoped;

    NodeMaterials nodes = collect_nodes_with_material([this](auto n) {
        return n->has_render_component() && (!s_graphics_settings.shadows_enabled() || !n->render_component()->receive_shadows());
    });

    if (nodes.empty())
        return;

    std::stable_partition(nodes.begin(), nodes.end(), [](const auto& p) {
        return !p.second.transparent();
    });
    // Currently the only textured transparent object is the bed plate when the camera points upward or the bed is disabled.
    // Sorts transparent objects so that the bed plate is rendered last to avoid its update of the depth buffer
    // which would result in hiding the other transparent objects rendered after it
    auto first_transparent_it = std::find_if(nodes.begin(), nodes.end(), [](const auto& p) {
        return p.second.transparent();
    });
    if (first_transparent_it != nodes.end()) {
        std::stable_sort(first_transparent_it, nodes.end(), [](const auto& a, const auto& b) {
            return a.second.textures().size() < b.second.textures().size();
        });
    }
    // ensure that gizmo nodes are rendered last, as they require a cleanup of the depth buffer
    std::stable_sort(nodes.begin(), nodes.end(), [](const auto& a, const auto& b) {
        return a.first->render_component()->layer_index() < b.first->render_component()->layer_index();
    });    

    cmd_buffer.set_depth_test_enabled(true);
    cmd_buffer.set_cull_face_enabled(true);

    if (customizer)
        customizer->on_render_begin(cmd_buffer);

    constexpr int INITIAL_LAYER = std::numeric_limits<int>::min();
    int current_layer = INITIAL_LAYER;
    // set was_opaque as the opposite of the first element in list
    bool was_opaque = nodes.front().second.transparent();
    for (auto& [n, mat]  : nodes) {
        const bool first_iteration = current_layer == INITIAL_LAYER;

        // did we start next layer
        if (auto layer = n->render_component()->layer_index(); layer != current_layer) {
            //cmd_buffer.clear_buffers(false, true);
            if (customizer) {
                if (current_layer != INITIAL_LAYER)
                    customizer->on_layer_end(cmd_buffer, current_layer);
                customizer->on_layer_begin(cmd_buffer, layer);
            }
            current_layer = layer;
        }

        // did we switch between opaque/transparent passes
        bool is_opaque = !mat.transparent();
        if (customizer) {
            bool pass_switch = is_opaque != was_opaque;

            // end last pass
            if (!first_iteration && pass_switch) {
                if (was_opaque)
                    customizer->on_opaque_pass_end(cmd_buffer, current_layer);
                else
                    customizer->on_transparent_pass_end(cmd_buffer, current_layer);
            }

            // begin new pass
            if (pass_switch) {
                if (is_opaque)
                    customizer->on_opaque_pass_begin(cmd_buffer, current_layer);
                else
                    customizer->on_transparent_pass_begin(cmd_buffer, current_layer);
            }
        }

        set_uniforms(m_lighting, mat);
        n->render_component()->render(*n, camera, mat, cmd_buffer);
        was_opaque = is_opaque;
    }

    if (customizer) {
        if (was_opaque)
            customizer->on_opaque_pass_end(cmd_buffer, current_layer);
        else
            customizer->on_transparent_pass_end(cmd_buffer, current_layer);
        customizer->on_layer_end(cmd_buffer, current_layer);
        customizer->on_render_end(cmd_buffer);
    }
}

void Scene::render_ao_gbuffer_pass(Render::Device& device, const Camera& camera, ISceneRenderCustomizer* customizer,
    const Render::Rect& viewport, PBRParamsList& pbr_params_list) const
{
    ZoneScoped;

    Domain::Index2 viewport_size = { viewport.width, viewport.height };

    AmbientOcclusion& ao = s_graphics_settings.m_ao;
    if (ao.gbuffer_fb == nullptr || ao.framebuffer_size != viewport_size) {
        if (ao.gbuffer_fb != nullptr)
            device.context().framebuffer_manager().destroy(ao.gbuffer_fb);
        Render::FramebufferCreationData data;
        data.width = viewport_size[0];
        data.height = viewport_size[1];
        data.color_attachments.resize(2);
        // eye norm attachement contains the normal compressed into two floats using octahedral normal encoding 
        data.color_attachments[AmbientOcclusion::EYE_NORM_CLR_ATTR].format = Domain::PixelFormat::RG16F;
        // color attachement contains the rgb color in its red, green, blue channels 
        // and the material id (normalized to belong to the range [0.0..1.0]) in its alpha channel
        data.color_attachments[AmbientOcclusion::COLOR_CLR_ATTR].format = Domain::PixelFormat::RGBA8;
        ao.gbuffer_fb = device.context().framebuffer_manager().create(data);
    }

    auto cmd_buffer = device.create_command_buffer();
    cmd_buffer->bind_framebuffer(*ao.gbuffer_fb);
    cmd_buffer->set_depth_test_enabled(true);
    cmd_buffer->set_cull_face_enabled(true);
    cmd_buffer->set_viewport(viewport);
    cmd_buffer->set_clear_values({ 0.0f, 0.0f, 0.0f, 0.0f });
    cmd_buffer->clear_buffers(true, true);

    NodeMaterials nodes = collect_nodes_with_material([](auto n) {
        return n->has_render_component() && n->render_component()->receive_shadows() && !resolve_material(*n).transparent();
    });

    const Shadows& shadows = s_graphics_settings.m_shadows;

    if (!nodes.empty()) {
        if (customizer)
            customizer->on_render_begin(*cmd_buffer);

        constexpr int INITIAL_LAYER = std::numeric_limits<int>::min();
        int current_layer = INITIAL_LAYER;
        for (auto& [n, mat]  : nodes) {
            // did we start next layer
            if (auto layer = n->render_component()->layer_index(); layer != current_layer) {
                if (customizer) {
                    if (current_layer != INITIAL_LAYER) {
                        customizer->on_opaque_pass_end(*cmd_buffer, current_layer);
                        customizer->on_layer_end(*cmd_buffer, current_layer);
                    }
                    customizer->on_layer_begin(*cmd_buffer, layer);
                    customizer->on_opaque_pass_begin(*cmd_buffer, layer);
                }
                current_layer = layer;
            }

            std::string shader_name = device.context().shader_manager().shader_name(mat.shader());
            shader_name = shader_name_by_shading_pass(shader_name, ShadingPass::AOGBuffer);
            mat.set_shader(device.context().shader_manager().shader(shader_name));

            if (s_graphics_settings.pbr_enabled() && n->render_component()->has_pbr()) {
                float mat_id = -1.0f;
                if (pbr_params_list.size() == MAX_NUM_PBR_MATERIALS)
                    mat_id = float(pbr_params_list.size() - 1.0f) / float(MAX_NUM_PBR_MATERIALS);
                else {
                    const PBRParams& params = *n->render_component()->pbr();
                    auto it = std::find(pbr_params_list.begin(), pbr_params_list.end(), params);
                    if (it == pbr_params_list.end()) {
                        pbr_params_list.push_back(params);
                        it = std::prev(pbr_params_list.end());
                    }
                    mat_id = float(std::distance(pbr_params_list.begin(), it)) / float(MAX_NUM_PBR_MATERIALS);
                }
                // Materials id are stored in the alpha channel of the color g-buffer which is of RGBA8 format.
                // OpenGL clamps the values in the range [0.0, 1.0] so we need to provide a normalized value
                // which is then converted back to integer in the ambient occlusion lighting shader
                mat.set_uniform("material_id", mat_id);
            }

            set_uniforms(m_lighting, mat);
            n->render_component()->render(*n, camera, mat, *cmd_buffer);
        }

        if (customizer) {
            customizer->on_opaque_pass_end(*cmd_buffer, current_layer);
            customizer->on_layer_end(*cmd_buffer, current_layer);
            customizer->on_render_end(*cmd_buffer);
        }
    }

    cmd_buffer->unbind_framebuffer(*ao.gbuffer_fb);
}

void Scene::render_ao_texture_pass(Render::Device& device, const Camera& camera, const Render::Rect& viewport) const
{
    ZoneScoped;

    Domain::Index2 viewport_size = { viewport.width / 2, viewport.height / 2 };

    AmbientOcclusion& ao = s_graphics_settings.m_ao;
    if (ao.ao_tex_fb == nullptr || ao.tex_fb_size != viewport_size) {
        if (ao.ao_tex_fb != nullptr)
            device.context().framebuffer_manager().destroy(ao.ao_tex_fb);
        Render::FramebufferCreationData data;
        data.width = viewport_size[0];
        data.height = viewport_size[1];
        data.depth = false;
        Render::FramebufferColorAttachment color;
        color.format = Domain::PixelFormat::R16F;
        data.color_attachments.push_back(color);
        ao.ao_tex_fb = device.context().framebuffer_manager().create(data);
        ao.tex_fb_size = viewport_size;
    }

    auto cmd_buffer = device.create_command_buffer();
    cmd_buffer->bind_framebuffer(*ao.ao_tex_fb);
    cmd_buffer->set_viewport({ viewport.x, viewport.y, viewport_size[0], viewport_size[1] });
    cmd_buffer->set_clear_values({ 0.0f, 0.0f, 0.0f, 0.0f });
    cmd_buffer->clear_buffers(true, false);

    Vec2f v_size = { float(viewport_size[0]), float(viewport_size[1]) };
    SquareMatrix4f projection = camera.projection().cast<float>();
    SquareMatrix4f inv_projection = projection.inverse();

    Render::Material material;
    material
        .set_shader(device.context().shader_manager().shader("ao_texture"))
        .set_uniform("intensity", ao.intensity)
        .set_uniform("kernel_size", int(ao.kernel.size()))
        .set_uniform("radius", ao.radius)
        .set_uniform("bias", ao.bias)
        .set_uniform("z_threshold", ao.z_threshold)
        .set_uniform("viewport_size", v_size)
        .set_uniform("projection_matrix", projection)
        .set_uniform("inverse_projection_matrix", inv_projection)
        .set_uniform("g_depth", TextureUnits::AO_DEPTH)
        .set_uniform("g_eye_normal", TextureUnits::AO_EYE_NORMAL)
        .set_uniform("tex_noise", TextureUnits::AO_NOISE)
        .set_texture(TextureUnits::AO_DEPTH, ao.gbuffer_fb->depth())
        .set_texture(TextureUnits::AO_EYE_NORMAL, ao.gbuffer_fb->color_attachment(AmbientOcclusion::EYE_NORM_CLR_ATTR))
        .set_texture(TextureUnits::AO_NOISE, ao.noise_tex);


    for (size_t i = 0; i < ao.kernel.size(); ++i) {
        std::string name = "kernel[" + std::to_string(i) + "]";
        material.set_uniform(name, ao.kernel[i]);
    }

    cmd_buffer->bind_and_draw(*m_screen_quad, material);

    cmd_buffer->unbind_framebuffer(*ao.ao_tex_fb);
}

void Scene::render_ao_texture_hblur_pass(Render::Device& device, const Camera& camera, const Render::Rect& viewport) const
{
    ZoneScoped;

    Domain::Index2 viewport_size = { viewport.width / 2, viewport.height / 2 };

    AmbientOcclusion& ao = s_graphics_settings.m_ao;
    if (ao.hblur_fb == nullptr || ao.hblur_fb_size != viewport_size) {
        if (ao.hblur_fb != nullptr)
            device.context().framebuffer_manager().destroy(ao.hblur_fb);
        Render::FramebufferCreationData data;
        data.width = viewport_size[0];
        data.height = viewport_size[1];
        data.depth = false;
        Render::FramebufferColorAttachment color;
        color.format = Domain::PixelFormat::R16F;
        data.color_attachments.push_back(color);
        ao.hblur_fb = device.context().framebuffer_manager().create(data);
        ao.hblur_fb_size = viewport_size;
    }

    auto cmd_buffer = device.create_command_buffer();
    cmd_buffer->bind_framebuffer(*ao.hblur_fb);
    cmd_buffer->set_viewport({ viewport.x, viewport.y, viewport_size[0], viewport_size[1] });
    cmd_buffer->set_clear_values({ 0.0f, 0.0f, 0.0f, 0.0f });
    cmd_buffer->clear_buffers(true, false);

    Render::Material material;
    material
        .set_shader(device.context().shader_manager().shader("ao_hblur"))
        .set_uniform("in_tex", TextureUnits::AO_RESULT)
        .set_uniform("filter_size", int(ao.blur_filter_size))
        .set_texture(TextureUnits::AO_RESULT, ao.ao_tex_fb->color_attachment(0));

    cmd_buffer->bind_and_draw(*m_screen_quad, material);

    cmd_buffer->unbind_framebuffer(*ao.hblur_fb);
}

void Scene::render_ao_texture_vblur_pass(Render::Device& device, const Camera& camera, const Render::Rect& viewport) const
{
    ZoneScoped;

    Domain::Index2 viewport_size = { viewport.width / 2, viewport.height / 2 };

    AmbientOcclusion& ao = s_graphics_settings.m_ao;
    if (ao.vblur_fb == nullptr || ao.vblur_fb_size != viewport_size) {
        if (ao.vblur_fb != nullptr)
            device.context().framebuffer_manager().destroy(ao.vblur_fb);
        Render::FramebufferCreationData data;
        data.width = viewport_size[0];
        data.height = viewport_size[1];
        data.depth = false;
        Render::FramebufferColorAttachment color;
        color.format = Domain::PixelFormat::R16F;
        data.color_attachments.push_back(color);
        ao.vblur_fb = device.context().framebuffer_manager().create(data);
        ao.vblur_fb_size = viewport_size;
    }

    auto cmd_buffer = device.create_command_buffer();
    cmd_buffer->bind_framebuffer(*ao.vblur_fb);
    cmd_buffer->set_viewport({ viewport.x, viewport.y, viewport_size[0], viewport_size[1] });
    cmd_buffer->set_clear_values({ 0.0f, 0.0f, 0.0f, 0.0f });
    cmd_buffer->clear_buffers(true, false);

    Render::Material material;
    material
        .set_shader(device.context().shader_manager().shader("ao_vblur"))
        .set_uniform("in_tex", TextureUnits::AO_RESULT)
        .set_uniform("filter_size", int(ao.blur_filter_size))
        .set_texture(TextureUnits::AO_RESULT, ao.hblur_fb->color_attachment(0));

    cmd_buffer->bind_and_draw(*m_screen_quad, material);

    cmd_buffer->unbind_framebuffer(*ao.vblur_fb);
}

void Scene::render_ao_lighting_pass(Render::Device& device, const Camera& camera, Render::CommandBuffer& cmd_buffer,
    const Render::Rect& viewport, const PBRParamsList& pbr_params_list) const
{
    ZoneScoped;

    cmd_buffer.set_viewport(viewport);
    cmd_buffer.set_depth_test_enabled(false);
    cmd_buffer.set_depth_write_enabled(false);
    cmd_buffer.set_blending_enabled(true);
    Render::Blending blending{ {Render::BlendFactor::SrcAlpha, Render::BlendFactor::OneMinusSrcAlpha} };
    cmd_buffer.set_blending(blending);

    Vec2f v_size = { float(viewport.width), float(viewport.height) };
    SquareMatrix4f view = camera.view().matrix().cast<float>();
    SquareMatrix4f projection = camera.projection().matrix().cast<float>();
    SquareMatrix4f inv_projection = projection.inverse();
    SquareMatrix4f inv_projection_view = (projection * view).inverse();

    const AmbientOcclusion& ao = s_graphics_settings.m_ao;
    const Shadows& shadows = s_graphics_settings.m_shadows;
    const PBR& pbr = s_graphics_settings.m_pbr;

    Domain::SquareMatrix4f light_cam_proj_view_matrix = (shadows.light_cam.projection() * shadows.light_cam.view().matrix()).cast<float>();

    Render::Material material;
    material
        .set_shader(device.context().shader_manager().shader("ao_lighting"))
        .set_uniform("apply_pbr", s_graphics_settings.pbr_enabled())
        .set_uniform("pbr_intensity", s_graphics_settings.pbr_enabled() ? pbr.intensity : 1.0f)
        .set_uniform("apply_shadows", s_graphics_settings.shadows_enabled())
        .set_uniform("shadows_intensity", s_graphics_settings.shadows_enabled() ? shadows.intensity : 0.0f)
        .set_uniform("viewport_size", v_size)
        .set_uniform("view_matrix", view)
        .set_uniform("inverse_projection_matrix", inv_projection)
        .set_uniform("inverse_projection_view_matrix", inv_projection_view)
        .set_uniform("light_matrix", light_cam_proj_view_matrix)
        .set_uniform("g_depth", TextureUnits::AO_DEPTH)
        .set_uniform("g_eye_normal", TextureUnits::AO_EYE_NORMAL)
        .set_uniform("g_color", TextureUnits::AO_COLOR)
        .set_uniform("ssao", TextureUnits::AO_RESULT)
        .set_uniform("shadowsmap", TextureUnits::SHADOW_MAP)
        .set_texture(TextureUnits::AO_DEPTH, ao.gbuffer_fb->depth())
        .set_texture(TextureUnits::AO_EYE_NORMAL, ao.gbuffer_fb->color_attachment(AmbientOcclusion::EYE_NORM_CLR_ATTR))
        .set_texture(TextureUnits::AO_COLOR, ao.gbuffer_fb->color_attachment(AmbientOcclusion::COLOR_CLR_ATTR))
        .set_texture(TextureUnits::AO_RESULT, ao.vblur_fb->color_attachment(0))
        .set_texture(TextureUnits::SHADOW_MAP, shadows.framebuffer->depth());
 
    set_uniforms(m_lighting, material);
    set_uniforms(pbr_params_list, material);

    cmd_buffer.bind_and_draw(*m_screen_quad, material);

    cmd_buffer.set_blending_enabled(false);
    cmd_buffer.set_depth_write_enabled(true);
    cmd_buffer.set_depth_test_enabled(true);
}

void Scene::render(Render::Device& device, Render::CommandBuffer& cmd_buffer, ISceneRenderCustomizer* customizer,
    Camera* override_camera) const
{
    ZoneScoped;

    Render::ScopedDebugGroup event_scene_render("Scene", cmd_buffer);

    const Camera& camera = (override_camera != nullptr) ? *override_camera : m_camera;

    if (m_screen_quad == nullptr)
        init_screen_quad(device);

    if (m_background_enabled)
        render_background(device, cmd_buffer);

    if (s_graphics_settings.shadows_enabled())
        render_shadowsmap_pass(device, camera, customizer);

    if (s_graphics_settings.ao_enabled()) {
        ZoneScopedN("render AO");

        const Render::Rect& viewport = camera.viewport();
        PBRParamsList pbr_params_list;
        render_ao_gbuffer_pass(device, camera, customizer, viewport, pbr_params_list);
        generate_ao_kernel(device);
        generate_ao_noise(device);
        render_ao_texture_pass(device, camera, viewport);
        render_ao_texture_hblur_pass(device, camera, viewport);
        render_ao_texture_vblur_pass(device, camera, viewport);
        render_ao_lighting_pass(device, camera, cmd_buffer, viewport, pbr_params_list);
        AmbientOcclusion& ao = s_graphics_settings.m_ao;
        cmd_buffer.blit_to_draw_framebuffer(*ao.gbuffer_fb, viewport.width, viewport.height,
            Render::BlitFramebufferMask::DepthBufferBit, Render::BlitFramebufferFilter::Nearest);
        ao.framebuffer_size = { viewport.width, viewport.height };
    }
    else if (s_graphics_settings.shadows_enabled())
        render_shadows_receivers_pass(device, camera, cmd_buffer, customizer);

    render_no_shadows_pass(camera, cmd_buffer, customizer);
}

Eigen::AlignedBox<float, 2> resolve_bounding_box(const Node& node, const Camera& cam)
{
    const Node* n = &node;
    while (n) {
        if (n->has_raycast_component()) {
            const auto& raycast = *n->raycast_component();
            const auto& m = n->world_transform().matrix();
            const auto v = cam.view().matrix();
            const auto& p = cam.projection();

            const auto vp = p * v;

            return projected_bounding_box(raycast, m, vp.cast<float>(), cam.viewport());
        }
        n = n->parent();
    }

    return {};
}

Eigen::AlignedBox<float, 2> to_imgui_coords(const Eigen::AlignedBox<float, 2>& bb, const Render::ScreenInfo& screen_info)
{
    Eigen::AlignedBox<float, 2> ret;
    ret
        .extend(Vec2f{
            screen_info.physical_to_imgui_x(bb.min().x()),
            screen_info.physical_to_imgui_y(bb.min().y()),
        })
        .extend(Vec2f{
            screen_info.physical_to_imgui_x(bb.max().x()),
            screen_info.physical_to_imgui_y(bb.max().y()),
        });
    return ret;
}


void Scene::render_imgui(const Render::ScreenInfo& screen_info) const
{
    ZoneScoped;

    Node::ConstNodeList nodes;
    m_root.query([](auto n){ return n->has_imgui_render_component();}, nodes);
    for (const auto* n : nodes) {
        auto bb = resolve_bounding_box(*n, m_camera);
        if (!bb.isEmpty())
            n->imgui_render_component()->render_imgui(*n, to_imgui_coords(bb, screen_info));
    }
}

void Scene::notify_thumbnail_render_begin()
{
    invoke_listeners<IThumbnailRenderListener>([](IThumbnailRenderListener* l)
                                               { l->on_thumbnail_render_begin(); });
}

void Scene::notify_thumbnail_render_end()
{
    invoke_listeners<IThumbnailRenderListener>([](IThumbnailRenderListener* l)
                                               { l->on_thumbnail_render_end(); });
}

bool Scene::pick_at(float mouse_x, float mouse_y, ConstNodePickResults& results, Ray* out_ray) const
{
    Node::ConstNodeList query_result;
    auto ray = m_camera.ray_at(mouse_x, mouse_y);
    if (out_ray != nullptr)
        *out_ray = ray;
    const Node& n = m_root;
    auto ret = visit_conditional_transform<RaycastResult>(n, [&ray](const Node& n, RaycastResult& res) {
        if (!n.has_raycast_component())
            return false;
        return n.raycast_component()->raycast(n.world_transform().matrix(), ray, res);
    });
    results.reserve(results.size() + ret.size());
    std::transform(
        ret.begin(),
        ret.end(),
        std::back_inserter(results),
        [](const auto& p) -> ConstNodePickResult { return {p.first, std::move(p.second)}; }
    );
    std::sort(ret.begin(), ret.end(), [](auto a, auto b) {
        return a.second.distance < b.second.distance;
    });
    return !ret.empty();
}

bool Scene::pick_at(float mouse_x, float mouse_y, NodePickResults& results, Ray* out_ray)
{
    Node::NodeList query_result;
    auto ray = m_camera.ray_at(mouse_x, mouse_y);
    if (out_ray != nullptr)
        *out_ray = ray;
    Node& n  = m_root;
    auto ret = visit_conditional_transform<RaycastResult>(n, [&ray](Node& n, RaycastResult& t) {
        if (!n.has_raycast_component())
            return false;
        return n.raycast_component()->raycast(n.world_transform().matrix(), ray, t);
    });
    std::sort(ret.begin(), ret.end(), [](const auto& a, const auto& b) {
        return a.second.distance < b.second.distance;
    });
    results.reserve(results.size() + ret.size());
    std::transform(
        ret.begin(),
        ret.end(),
        std::back_inserter(results),
        [](const auto& p) -> NodePickResult { return {p.first, std::move(p.second)}; }
    );

    return !ret.empty();
}

void Scene::validate_lights(Lights& lights)
{
    if (s_graphics_settings.shadows_enabled()) {
        // ensure one light is set to cast shadows
        int count = std::count_if(lights.begin(), lights.end(),
            [](const Light& l) {
                return l.shadows;
            }
        );
        if (count == 0)
            lights.front().shadows = true;
        else if (count > 1) {
            // keeps only the first light as casting shadows
            bool found = false;
            for (auto& l : lights) {
                if (l.shadows) {
                    found = true;
                    continue;
                }
                if (found)
                    l.shadows = false;
            }
        }
    }
    else
        // ensure no light is set to cast shadows
        std::for_each(lights.begin(), lights.end(), [](Light& l) { l.shadows = false; });

    // avoid shininess == 0.0, see: https://registry.khronos.org/OpenGL-Refpages/gl4/html/pow.xhtml
    std::for_each(lights.begin(), lights.end(),
        [](Light& l) { if (l.shininess == 0.0f) l.shininess = 0.001f; });
}

void Scene::log_nodes() const
{
    std::string indent;
    indent.reserve(32);

    visit(m_root, [&](const Node& n) {
        indent.clear();
        const size_t level = n.level();
        for (size_t i = 0; i < level; i++)
            indent.append(" ");
        SPDLOG_INFO("{}- {}", indent, n.debug_name());
        indent.append("  ");
        SPDLOG_INFO("{}world transform:", indent);
        const auto& wt = n.world_transform();
        for (size_t i = 0; i < 4; i++) {
            SPDLOG_INFO("{}  ({:5} {:5} {:5} {:5})", indent, wt(i, 0), wt(i, 1), wt(i, 2), wt(i, 3));
        }
    });
}

} // namespace Slic3r::App::Scene
