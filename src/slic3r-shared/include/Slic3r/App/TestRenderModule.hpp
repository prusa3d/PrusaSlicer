#pragma once
#include <cstddef>
#include <memory>
#include "Slic3r/App/Platform/AbstractRenderModule.hpp"
#include "Slic3r/App/Render/Geometry.hpp"
#include "Slic3r/App/Render/Shader.hpp"
#include "Slic3r/App/Render/TextureManager.hpp"
#include "Slic3r/App/Scene/Scene.hpp"
#include "Slic3r/App/MenuManager.hpp"
#include "Slic3r/App/CommandBindingManager.hpp"

namespace Slic3r::App {

class TestRenderModule : public Platform::AbstractRenderModule
{
public:
    TestRenderModule();

    void render_scene(Render::CommandBuffer& cmd_buffer) override;
    void render_imgui(Render::CommandBuffer& cmd_buffer) override;
    void on_scene_mouse_event(const Platform::MouseEvent &e) override;
    void on_scene_keyboard_event(const Platform::KeyboardEvent &e) override;
    void set_navigator(Navigator* n) override {};

    MenuManager& menu_manager() override
    {
        return m_menu_manager;
    }

    CommandBindingManager& command_binding_manager() override
    {
        return m_command_binding_manager;
    }

    const Platform::CommandRegistry::CommandsMap & gizmo_commands() const override;

protected:
    void on_init(
        Render::Device& device,
        Render::ImguiRender& imgui_render,
        Platform::AnimationManager& animation_manager
    ) override;
    void on_screen_resized() override;

    virtual void register_commands() override;

    void init_render();
    void init_scene();

    void render_scene_render();
    void render_scene_scene();

    void render_object_hud(const Scene::Node& n, const Eigen::AlignedBox<float, 2>& screen_bounding_box);

    void remove_highlighted();
    void reset_highlighted(const Scene::Node::NodeList& nodes_to_highlight, const Render::Material& material);

    const std::optional<Platform::CameraSynchData>& camera_synch_data() const override {
        static const std::optional<Platform::CameraSynchData> dummy;
        return dummy;
    }
    void set_camera_synch_data(const Platform::CameraSynchData& data) override {}

private:
    std::unique_ptr<Render::Geometry> m_geometry;
    std::unique_ptr<Render::Geometry> m_geometry2;
    Render::Shader* m_shader{nullptr};
    Render::Shader* m_shader2{nullptr};
    std::shared_ptr<Render::Texture> m_tex;

    static constexpr size_t BUF_SIZE = 256;
    char m_text_buffer[BUF_SIZE];
    bool m_main_window_opened{true};
    float m_geom2_scale{1};
    bool m_render_low{false};

    std::unique_ptr<Scene::Scene> m_scene;
    Scene::Node::NodeList m_highlighted_nodes;
    MenuManager m_menu_manager;
    CommandBindingManager m_command_binding_manager;

    int m_last_mouse_x{0};
    int m_last_mouse_y{0};
};

} // namespace Slic3r::App::SDLTest
