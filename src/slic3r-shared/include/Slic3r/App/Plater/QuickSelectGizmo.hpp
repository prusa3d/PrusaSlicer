#pragma once

#include <chrono>

#include "Slic3r/App/Render/Geometry.hpp"
#include "Slic3r/App/Render/Material.hpp"
#include "Slic3r/App/Scene/IGizmo.hpp"
#include "Slic3r/App/Scene/ISceneProvider.hpp"
#include "Slic3r/App/Plater/SelectionHandler.hpp"
#include "Slic3r/App/Plater/RectangleSelectionPicker.hpp"

#include "Slic3r/Biz/Platform/WithListeners.hpp"
#include "Slic3r/Biz/Platform/TimerQueue.hpp"
#include "Slic3r/Domain/SelectionId.hpp"

namespace Slic3r::Biz::Scene {
class SceneInteractor;
} // namespace Slic3r::Biz::Scene

namespace Slic3r::App::Plater {

using MousePosition = std::array<int, 2>;

class RectangleSelection
{
public:
    enum class Type : uint8_t
    {
        Undefined,
        Replace,
        Add,
        Remove
    };

    RectangleSelection(const Render::ScreenInfo& screen_info, Render::Device& device, Scene::ISceneProvider& scene_provider,
       Biz::Scene::SceneInteractor& scene_interactor);

    void activate(Type type, const MousePosition& initial_mouse_pos);
    void deactivate() {
        m_active = false;
        m_defined = false;
        m_already_processed = false;
    }

    [[nodiscard]] bool is_active() const { return m_active; }
    Type type() const { return m_type; }

    bool is_already_processed() const { return m_already_processed; }
    bool is_defined() const { return m_defined; }

    void update(const MousePosition& curr_mouse_pos);
    bool update_selection(SelectionHandler& selection_handler);

    const Scene::Node::NodeList& contained_nodes() const { return m_contained_nodes; }
    void set_contained_nodes(const Scene::Node::NodeList& nodes) { m_contained_nodes = nodes; }

    void render(Render::CommandBuffer& cmd_buffer);

private:
    const Render::ScreenInfo& m_screen_info;
    Render::Device& m_device;
    Scene::ISceneProvider& m_scene_provider;
    Biz::Scene::SceneInteractor& m_scene_interactor;

    Type m_type{ Type::Undefined };
    bool m_active{ false };
    bool m_defined{ false };
    bool m_already_processed{ false };
    MousePosition m_initial_mouse_pos;
    Render::Geometry m_geometry;
    Render::Material m_material;
    RectangleSelectionPicker m_picker;
    Scene::Node::NodeList m_contained_nodes;
};

enum class HoverType : uint8_t
{
    None,
    Select,
    Unselect
};

struct HoverData
{
    HoverType type{ HoverType::None };
    Scene::Node::NodeList nodes;

    bool operator==(const HoverData& other) const
    {
        if (type != other.type) return false;
        if (nodes != other.nodes) return false;
        return true;
    }

    bool operator!=(const HoverData& other) const
    {
        return !operator==(other);
    }
};

class IHoverChangedListener
{
public:
    virtual ~IHoverChangedListener() = default;
    virtual void on_hover_changed(const HoverData& hover_data) = 0;
};

class QuickSelectGizmo :
    public Scene::IGizmo,
    public Scene::ICameraUpdateListener,
    public WithListeners<IHoverChangedListener>
{
public:
    QuickSelectGizmo(
        const Scene::IGizmoController& gizmo_controller,
        Biz::Scene::SceneInteractor& scene_interactor,
        Render::Device& device,
        Scene::ISceneProvider& scene_provider,
        const Render::ScreenInfo& screen_info
    ) :
        m_gizmo_controller(gizmo_controller),
        m_scene_interactor(scene_interactor),
        m_scene_provider(scene_provider),
        m_selection_handler(scene_interactor),
        m_rectangle_selection(screen_info, device, scene_provider, scene_interactor)
    {}

    ~QuickSelectGizmo();

    Scene::GizmoActivationState on_mouse(Scene::GizmoEventContext& ctx, bool only_active) override;
    void on_keyboard(Scene::GizmoKeyEventContext& ctx) override;

    void on_cycle_prepare() override;

    bool handles_object_selection() const override { return true; }

    void render_scene(Render::CommandBuffer& cmd_buffer) override;

    void camera_updated(const Scene::Camera& cam) override;

private:
    void invoke_hover_changed(const HoverData& hover_data);

    void on_mouse_up(std::optional<size_t> node_id, bool modifier_pressed);
    void on_double_click(Scene::Node* node, bool modifier_pressed);
    void clear_timers();

private:
    using Clock = std::chrono::steady_clock;
    using TimePoint = std::chrono::time_point<Clock>;

    const Scene::IGizmoController& m_gizmo_controller;
    Biz::Scene::SceneInteractor& m_scene_interactor;
    Scene::ISceneProvider& m_scene_provider;
    SelectionHandler m_selection_handler;
    TimePoint m_click_start;

    bool m_processing{false};

    std::vector<Biz::Platform::TimerQueue::TimerID> m_up_timer_ids;
    bool m_pending_single_click{ false };
    std::size_t m_pending_click_node{ Domain::INVALID_ID };  // instance-level node to select on timeout

    RectangleSelection m_rectangle_selection;
    HoverData m_hover_data;
};

} // namespace Slic3r::App::Plater
