#include "Slic3r/App/TestRenderModule.hpp"
#include "imgui/imgui.h"
#include <iostream>
#include <chrono>
#include <fmt/chrono.h>

#include <Slic3r/Assert.hpp>

#include "Slic3r/App/Render/Device.hpp"
#include "Slic3r/App/Render/ShaderManager.hpp"
#include "Slic3r/App/Render/GeometryBuilder.hpp"
#include "Slic3r/App/Render/CommandBuffer.hpp"
#include "Slic3r/App/Render/MathUtils.hpp"

#include "Slic3r/App/Scene/Scene.hpp"
#include "Slic3r/App/Scene/MeshRenderNodeComponent.hpp"
#include "Slic3r/App/Scene/NodeBuilder.hpp"
#include "Slic3r/App/Scene/ScreenSpaceSizedTransformModifier.hpp"

#include "Slic3r/Domain/Color.hpp"

#include "Slic3r/Math.hpp"

using Slic3r::Domain::ColorRGBA;
using Slic3r::Domain::SquareMatrix4f;
using Slic3r::Domain::Transform3f;
using Slic3r::Domain::Vec3d;
using Slic3r::Domain::Vec3f;

#include "Slic3r/Domain/Types.hpp"

namespace Slic3r::App {

namespace TriMesh = Biz::Algorithms::TriangleMesh;

std::chrono::duration<double, std::milli> get_delta()
{
    static auto last = std::chrono::high_resolution_clock::now();
    auto now = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> delta = now - last;
    last = now;
    return delta;
}

TestRenderModule::TestRenderModule() :
    m_menu_manager(m_command_registry),
    m_command_binding_manager(m_command_registry)
{
    memset(m_text_buffer, 0, sizeof(m_text_buffer));

    // Test asserts (being compilable)
    std::cout << DEBUG_ASSERT_VAL(2, "extra message") << "\n";
    std::cout << DEBUG_ASSERT_VAL(5 != 3) << "\n";

    DEBUG_ASSERT(1);
    DEBUG_ASSERT(1, "extra message");

    std::cout << ASSERT_VAL(2, "extra message") << "\n";
    std::cout << ASSERT_VAL(5 != 3) << "\n";

    ASSERT(1);
    ASSERT(1, "extra message");

    if (false) {
        PANIC();
        PANIC("Extra message");
    }
}

const Platform::CommandRegistry::CommandsMap& TestRenderModule::gizmo_commands() const
{
    PANIC("Unexpected function call. Define gizmo manager to use it");
    static Platform::CommandRegistry::CommandsMap commands;
    return commands;
}

void TestRenderModule::on_init(
    Render::Device& device,
    Render::ImguiRender& imgui_render,
    Platform::AnimationManager& animation_manager
)
{
    AbstractRenderModule::on_init(device, imgui_render, animation_manager);
    init_render();
    init_scene();
}

void TestRenderModule::init_render()
{
    SPDLOG_TRACE("TestRenderModule() 1");
    Render::GeometryBuilder<Render::VertexP3> geometry;
    geometry.add_vertex({{1, 1, 0}})
        .add_vertex({{-1, 1, 0}})
        .add_vertex({{0, -1, 0}})
        .add_triangle_indices(0, 1, 2);
    m_geometry = geometry.build(*m_device);

    Render::GeometryBuilder<Render::VertexP3T2> geometry2;
    geometry2.add_vertex({{1, 1, 0}, {1, 1}})
        .add_vertex({{-1, 1, 0}, {0, 1}})
        .add_vertex({{1, -1, 0}, {1, 0}})
        .add_vertex({{-1, -1, 0}, {0, 0}})
        .add_triangle_indices(0, 1, 2)
        .add_triangle_indices(1, 3, 2);
    m_geometry2 = geometry2.build(*m_device);

    SPDLOG_TRACE("TestRenderModule() 2");
    auto& ctx = Render::Context::instance();
    m_shader = ctx.shader_manager().shader("flat");
    m_shader2 = ctx.shader_manager().shader("flat_texture");
    SPDLOG_TRACE("TestRenderModule() 3");
    Render::ImageLoadOptions opts;
    opts.gen_mipmaps = true;
    opts.force_power_of_two = true;
    opts.flip_y = true;
    m_tex = Render::Context::instance().texture_manager().get_or_create_image(
        //        "icons/PrusaSlicer-gcodeviewer-mac_128px.png",
        //         "icons/funnel.svg",
        "icons/PrusaSlicer-gcodeviewer.svg", opts
    );
    //    m_tex = Render::Context::instance().texture_manager()
    //                .create_empty("white", Render::PixelFormat::RGBA8, 16, 16);


}

void TestRenderModule::init_scene()
{
    auto& device  = *m_device;
    m_scene = std::make_unique<Scene::Scene>();
    m_scene->camera().set_viewport(Render::Rect::from(0, 0, m_screen_info));
    
    Scene::NodeBuilder node_builder{*m_scene};
    auto* cone_mesh = m_scene->triangle_mesh_manager().get_or_create("cone-2-5", [](){
        return std::make_unique<Scene::TriangleMesh>(TriMesh::its_make_cone(2, 5, M_PI / 8));
    });
    auto* cube_mesh = m_scene->triangle_mesh_manager().get_or_create("box-2-2-2", [](){
        return std::make_unique<Scene::TriangleMesh>(TriMesh::its_make_cube(2, 2, 2));
    });

    auto cone = m_scene->geometry_manager().get_or_create("cone-2-5", [&]() {
        return Render::geometry_from_triangle_mesh(device, cone_mesh->triangles());
    });

    auto cube = m_scene->geometry_manager().get_or_create("box-2-2-2", [&](){
       return Render::geometry_from_triangle_mesh(device, cube_mesh->triangles());
    });

    auto shader = device.context().shader_manager().shader("gouraud_light");
    constexpr float right_angle = std::numbers::pi_v<float> / 2.f;
    
    node_builder
//        .transform([](auto& xform){
//            xform
//                .rotate(Eigen::AngleAxisf{right_angle/2, Vec3f::UnitZ()})
//                .rotate(Eigen::AngleAxisf{right_angle/2, Vec3f::UnitX()});
//        })
        .child([&](auto& node_builder){
            node_builder
                .set_screen_space_sized_modifier(0.1f)
                .children(3, [&](auto& builder, size_t i){
                    ColorRGBA color{0, 0, 0, 1.f};
                    color.set(i, 1);
                    builder
                        .transform([&](auto& xform) {
                            Vec3d offset = Vec3d::Zero();
                            offset[i] = 10;
                            xform.translate(offset);
                        })
                        .child([&](auto& builder){
                            builder
                                .transform([&](auto& xform){
                                    // cone points in +Z direction,
                                    if (i == 0) {
                                        // make it pointing in +X direction
                                        xform.rotate(Eigen::AngleAxisd{right_angle, Vec3d::UnitY()});
                                    } else if (i == 1) {
                                        // make it pointing in +Y direction
                                        xform.rotate(Eigen::AngleAxisd{-right_angle, Vec3d::UnitX()});
                                    }
                                })
                                .set_mesh(
                                    cone,
                                    Render::Material{}
                                        .set_shader(shader)
                                        .set_uniform("uniform_color", color)
                                )
                                .set_aabb(&cone_mesh->aabb_mesh());

                            if (i == 0)
                                builder.set_imgui_func([this](const auto& n, const auto& screen_bb) {
                                    this->render_object_hud(n, screen_bb);
                                });

                        });
                });
        })
        .child([&](auto& builder){
            builder
                .transform([](auto& xform) {
                    xform.translate(Vec3d{-1, -1, -1});
                })
                .set_mesh(
                    cube,
                    Render::Material{}
                        .set_shader(shader)
                        .set_uniform("uniform_color", ColorRGBA{1.0f, 0.0f, 0.5f, 1.0f})
                )
                .set_aabb(&cube_mesh->aabb_mesh());
        });
    m_scene->add_child(node_builder.build().release());
    m_scene->camera_trackball().set_distance_to_target(30);
}

void TestRenderModule::render_scene(Render::CommandBuffer& cmd_buffer)
{
    m_device->load_state();

    if (m_render_low)
        render_scene_render();
    render_scene_scene();
}

void TestRenderModule::render_scene_render()
{
    auto command_buffer = m_device->create_command_buffer();
    command_buffer->set_clear_values({0.45f, 0.55f, 0.60f, 1.00f});
    command_buffer->clear_buffers(true, true);

    SPDLOG_TRACE("TestRenderModule::render_scene() 1");

    // glViewport(0, 0, m_screen_info.physical_width(), m_screen_info.physical_height());
    command_buffer->set_viewport(Render::Rect::from(0, 0, m_screen_info));

    //    SPDLOG_INFO(
    //        "Setting viewport to {}x{}", m_screen_info.physical_width(), m_screen_info.physical_height()
    //    );

    Transform3f view = Transform3f::Identity();
    view = view.translate(Vec3f(0, 0, -2));
    SPDLOG_TRACE("TestRenderModule::render_scene() 2");
    command_buffer->bind_shader(*m_shader);
    //m_shader->bind();
    SPDLOG_TRACE("TestRenderModule::render_scene() 3");

    command_buffer->bind_geometry(*m_geometry, *m_shader);
    SquareMatrix4f vm = view.matrix();
    SquareMatrix4f proj = Render::perspective(
        60,
        float(m_screen_info.physical_width()) / float(m_screen_info.physical_height()),
        0.01f, 10
    );

    command_buffer->set_blending({
        {Render::BlendFactor::SrcAlpha, Render::BlendFactor::OneMinusSrcAlpha},
        {Render::BlendFactor::One, Render::BlendFactor::OneMinusSrcAlpha}
    });
    command_buffer->set_blending_enabled(true);

    SPDLOG_TRACE("TestRenderModule::render_scene() 4");
    m_shader->set_uniform("view_model_matrix", vm);
    m_shader->set_uniform("projection_matrix", proj);
    m_shader->set_uniform("uniform_color", std::array<float, 4>{1, 0.5, 0, 1});
    SPDLOG_TRACE("TestRenderModule::render_scene() 5");

    command_buffer->draw(Render::PrimitiveType::Triangles, 0, 3);
    SPDLOG_TRACE("TestRenderModule::render_scene() 6");


    Transform3f model = Transform3f::Identity();
    model.translate(Vec3f(-0.5, -0.5, 0));
    model.scale(m_geom2_scale);
    vm = view.matrix() * model.matrix();

    command_buffer->bind_shader(*m_shader2);
    command_buffer->bind_geometry(*m_geometry2, *m_shader2);
    m_shader2->set_uniform("view_model_matrix", vm);
    m_shader2->set_uniform("projection_matrix", proj);
    m_shader2->set_uniform("uniform_texture", 0);

    command_buffer->bind_texture(0, *m_tex);
    command_buffer->draw(Render::PrimitiveType::Triangles, 0, 6);
    command_buffer->unbind_texture(0, *m_tex);
    SPDLOG_TRACE("TestRenderModule::render_scene() 8");
    command_buffer->set_blending_enabled(false);
    command_buffer->submit();

}

void TestRenderModule::render_scene_scene()
{
    auto cmd_buffer = m_device->create_command_buffer();
    if (!m_render_low) {
        cmd_buffer->set_clear_values({0.45f, 0.55f, 0.60f, 1.00f});
        cmd_buffer->clear_buffers(true, true);
        cmd_buffer->set_viewport(Render::Rect::from(0, 0, m_screen_info));
    }
    m_scene->render(*m_device, *cmd_buffer);
}


void TestRenderModule::reset_highlighted(const Scene::Node::NodeList& nodes_to_highlight, const Render::Material& material)
{
    remove_highlighted();
    for (auto* n : nodes_to_highlight)
        n->set_material_override(material);
    m_highlighted_nodes = nodes_to_highlight;
}

void TestRenderModule::remove_highlighted()
{
    for (auto* n : m_highlighted_nodes)
        n->remove_material_override();
}

void TestRenderModule::render_object_hud(const Scene::Node& n, const Eigen::AlignedBox<float, 2>& screen_bounding_box)
{
    ImGui::SetNextWindowPos({
        screen_bounding_box.max().x(),
        screen_bounding_box.min().y()
    });
    std::string win_name = "obj##" + std::to_string(reinterpret_cast<size_t>(&n));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5);
    if (ImGui::Begin(win_name.c_str(), nullptr, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDecoration)) {
        ImGui::InputText("Text", this->m_text_buffer, BUF_SIZE);
        ImGui::Button("(i)");
        ImGui::Text("Abc");
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void TestRenderModule::render_imgui(Render::CommandBuffer& cmd_buffer)
{
    if (ImGui::Begin("Slicer")) {
        ImGui::Checkbox("Low level rendering", &m_render_low);
        ImGui::Text("Hello there");
        ImGui::InputText("Text", m_text_buffer, BUF_SIZE);
        if (ImGui::Button("Press me")) {
            // pressed
        }
        if (ImGui::BeginPopupContextItem("MyWinPopup")) {
            ImGui::Text("Item 1");
            ImGui::EndPopup();
        }

        ImGui::Text("%s", fmt::format("Delta: {}", get_delta()).c_str());

        double a = 5005.5;
        std::stringstream ss;
        ss << a;
        ImGui::Text("%s", ss.str().c_str());
        char sss[50];
        sprintf(sss, "%g", a);
        ImGui::Text("%s", sss);

    }
    ImGui::End();

#if 0
    int windows_flag = ImGuiWindowFlags_NoTitleBar
        | ImGuiWindowFlags_NoCollapse
        //   | ImGuiWindowFlags_NoBackground
        | ImGuiWindowFlags_NoMove
        //   | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse;

    if (ImGui::Begin("My main Win", nullptr, ImGuiWindowFlags(windows_flag))) {
        ImGui::Text("First line");
        static int my_int = 5;
        ImGui::InputInt("Input number", &my_int);
        ImGui::Text("Second line ");
    }
    ImGui::End();
#endif

    m_scene->render_imgui(m_screen_info);
}

void TestRenderModule::on_scene_mouse_event(const Platform::MouseEvent &e)
{
    Scene::CameraTrackballController& trackball = m_scene->camera_trackball();

    if (e.type() == Platform::MouseEvent::Type::Enter) {
        m_last_mouse_x = e.x();
        m_last_mouse_y = e.y();
        //SPDLOG_INFO("[Mouse Enter] {} {}", e.x(), e.y());
    } else if (e.type() == Platform::MouseEvent::Type::Move) {
        {
            float dx = 2 * float(e.x()) / float(m_screen_info.logical_width()) - 1.0f;
            m_geom2_scale = std::pow(2.0f, dx * 5);
            //SPDLOG_INFO("Geom scale: {}  dx: {}", m_geom2_scale, dx);
        }

        float dx = float(m_last_mouse_x - e.x()) / (m_screen_info.logical_width() / 180.0f);
        float dy = float(m_last_mouse_y - e.y()) / (m_screen_info.logical_height() / 180.0f);
        //SPDLOG_INFO(
        //    "[Mouse Move] {} {}  ∆ {} {}",
        //    e.x(), e.y(),
        //    m_last_mouse_x - e.x(),
        // trackball()
        //);

        trackball.add_azimuth_and_zenith(-deg2rad(dx), -deg2rad(dy));

        m_last_mouse_x = e.x();
        m_last_mouse_y = e.y();

        Scene::NodePickResults pick_results;
        m_scene->pick_at(
            m_screen_info.mouse_to_screen(e.x()),
            m_screen_info.mouse_to_screen(e.y()),
            pick_results
        );

        if (!pick_results.empty()) {

            Scene::Node::NodeList to_highlight = {pick_results[0].node};

            reset_highlighted(
                to_highlight,
                Render::Material()
                    .set_uniform(
                        "uniform_color", ColorRGBA(0.9f, 0.9f, 0.9f, 1.f)
                    )
            );
        } else {
            remove_highlighted();
        }

        if (!pick_results.empty())
            SPDLOG_INFO("Cam pick found {} results", pick_results.size());
    } else if (e.type() == Platform::MouseEvent::Type::Wheel) {
        const float wheel_delta_y = e.wheel_delta_y();
        if (wheel_delta_y != 0) {
            trackball.update_zoom(wheel_delta_y / std::abs(wheel_delta_y));
        }

    }
}

void TestRenderModule::on_scene_keyboard_event(const Platform::KeyboardEvent &e)
{
    std::cout <<  "KeyboardEvent type: " << uint32_t(e.type()) << "\n";
    Platform::AbstractRenderModule::on_scene_keyboard_event(e);
}

void TestRenderModule::on_screen_resized()
{
    m_scene->camera().set_viewport(Render::Rect::from(0, 0, m_screen_info));
}

void TestRenderModule::register_commands()
{
    m_command_registry
        .register_command(
            std::make_unique<Platform::FuncCommand>(
                "zoom-in",
                [&]() { m_scene->camera_trackball().update_zoom(1.); },
                Platform::FuncCommandExtraOpts{
                    .keyboard_shortcuts =
                        Platform::KeyboardShortcuts{
                            Platform::KeyboardShortcut{0, Platform::KeyCode::I}
                        }
                }
            )
        )
        .register_command(
            std::make_unique<Platform::FuncCommand>(
                "switch-camera-type",
                [&]() { m_scene->camera_trackball().switch_projection_type(); },
                Platform::FuncCommandExtraOpts{
                    .keyboard_shortcuts =
                        Platform::KeyboardShortcuts{
                            Platform::KeyboardShortcut{0, Platform::KeyCode::K}
                        }
                }
            )
        )
        .register_command(
            std::make_unique<Platform::FuncCommand>(
                "zoom-out",
                [&]() { m_scene->camera_trackball().update_zoom(-1.); },
                Platform::FuncCommandExtraOpts{
                    .keyboard_shortcuts = Platform::KeyboardShortcuts{
                        Platform::KeyboardShortcut{0, Platform::KeyCode::O}
                    }
                }
            )
        );
}

}
