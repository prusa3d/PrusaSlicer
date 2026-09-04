#pragma once

#include <vector>
#include <memory>
#include <utility>

#include <Slic3r/App/Platform/CommandRegistry.hpp>
#include <Slic3r/Biz/Platform/ListenerScope.hpp>

#include "Slic3r/App/Scene/IGizmo.hpp"
#include "Slic3r/App/Scene/ISceneProvider.hpp"
#include "Slic3r/App/Render/ScreenInfo.hpp"
#include "Slic3r/App/Scene/GeometryDataFactory.hpp"
#include "Slic3r/App/Scene/MouseDragDetector.hpp"
#include "Slic3r/Biz/ProjectScoped.hpp"
#include "Slic3r/App/Scene/GizmoCommandRegistry.hpp"
#include "Slic3r/App/Scene/Clipper.hpp"
#include "Slic3r/App/Scene/IGizmoController.hpp"
#include "Slic3r/Domain/PrinterTechnology.hpp"


#ifndef DEBUG_GIZMO_MANAGER
#define DEBUG_GIZMO_MANAGER 0
#endif


namespace Slic3r::App::Scene {

class IGizmoActiveToolListener {
public:
    virtual ~IGizmoActiveToolListener() = default;

    virtual void active_tool_changed(IToolGizmo* active_tool) = 0;
};

class GizmoManager :
    public WithListeners<IGizmoActiveToolListener>,
    public Biz::ISelectedProjectChangedListener,
    public Biz::IProjectsChangedListener,
    public Biz::Scene::ISceneSelectionChangedListener,
    public IGizmoController
{
public:
    GizmoManager(
        Render::Device& device,
        ISceneProvider& scene_provider,
        Biz::ProjectInteractor& project_interactor,
        std::unique_ptr<MouseDragDetector> mouse_drag_detector
    );

    void on_scene_mouse_event(const Platform::MouseEvent& e, const Render::ScreenInfo& screen_info);
    bool on_scene_keyboard_event(const Platform::KeyboardEvent& e);
    void on_project_will_be_removed(Domain::SelectionId project_id) override;
    void on_scene_selection_changed(
        Domain::SelectionId project_id,
        const Biz::Scene::ObjectSelection& selection
    ) override;

    template<typename G, typename... ArgsT>
    G& add_base_gizmo(ArgsT&&... args)
    {
        m_base_gizmos.emplace_back(std::make_unique<G>(std::forward<ArgsT>(args)...));
        const auto& ptr = m_base_gizmos.back();
        m_mouse_drag_detector->add_listener(ptr.get());
        ptr->register_commands(m_command_registry);
        ptr->provide_clipper(m_clipper);
        return *static_cast<G*>(ptr.get());
    }

    template<typename G, typename... ArgsT>
    G& add_tool_gizmo(ArgsT&&... args)
    {
        m_tool_gizmos.emplace_back(std::make_unique<G>(std::forward<ArgsT>(args)...));
        const auto& ptr = m_tool_gizmos.back();
        m_command_registry.set_registering_tool_gizmo(*ptr);
        m_mouse_drag_detector->add_listener(ptr.get());
        ptr->register_commands(m_command_registry);
        ptr->provide_clipper(m_clipper);
        ptr->provide_gizmo_controller(*this);
        return *static_cast<G*>(ptr.get());
    }

    void render_scene(Render::CommandBuffer& cmd_buffer);
    void render_imgui();

    /**
     * @name Implementation of IGizmoController interface
     * @{
     */
    void deactivate_current_tool() override;
    void activate_tool(ToolType tool) override;

    ToolType current_tool_type() const override;

    Undo::ToolsState tools_state() const override;
    void set_tools_state(const Undo::ToolsState& tools_state) override;
    /**@}*/

    bool is_tool_active_in_current_project(const IToolGizmo& tool) const;

    GeometryDataFactory& data_factory() { return m_data_factory; }

    using IGizmoPtr = std::unique_ptr<IGizmo>;
    using GizmoList = std::vector<IGizmoPtr>;

    using IToolGizmoPtr = std::unique_ptr<IToolGizmo>;
    using ToolGizmoList = std::vector<IToolGizmoPtr>;

    const GizmoList& base_gizmos() const {
        return m_base_gizmos;
    }

    const ToolGizmoList& tool_gizmos() const {
        return m_tool_gizmos;
    }

    const GizmoCommandRegistry& command_registry() const
    {
        return m_command_registry;
    }

    const Platform::CommandRegistry::CommandsMap& commands() const
    {
        return m_command_registry.commands();
    }

    const Platform::ICommand& command(const char* name) const
    {
        return m_command_registry.command(name);
    }

    NodePickResults repick() const override;

    IToolGizmo* find_tool(ToolType tool, Domain::PrinterTechnology pt);

private:
    using PickResultWithRay = std::tuple<NodePickResults, Ray>;
    PickResultWithRay
    pick(const Platform::MouseEvent& e, const Render::ScreenInfo& screen_info) const;

    void on_selected_project_changed(size_t index) override;

    void prepare_cycle();

    struct ProjectContext;
    ProjectContext& current_context() { return m_projects.selected(); }
    const ProjectContext& current_context() const { return m_projects.selected(); }
#if DEBUG_GIZMO_MANAGER
    void update_gizmo_activation_debug_data(const IGizmo* g, GizmoActivationState state);
    void update_gizmo_activation_debug_frame_begin();
    void render_gizmo_activation_debug();
#endif

private:
    using MouseEventContext =
        std::tuple<Platform::MouseEvent, Render::ScreenInfo>;
    using LastMouseEventContext =
        std::optional<MouseEventContext>;

    Biz::ListenerScope<Biz::IProjectsChangedListener, Biz::ProjectInteractor, GizmoManager>
        m_project_changed_listener_scope;
    Biz::ListenerScope<Biz::ISelectedProjectChangedListener, Biz::ProjectInteractor, GizmoManager>
        m_selected_project_changed_listener_scope;
    Biz::ListenerScope<
        Biz::Scene::ISceneSelectionChangedListener,
        Biz::Scene::SceneInteractor,
        GizmoManager>
        m_scene_selection_changed_listener_scope;

    struct ProjectContext
    {
        IToolGizmo* active_tool{nullptr};

        bool in_cycle {false};
        bool object_selection_disabled{ false };
        LastMouseEventContext last_mouse_event;
        std::vector<IGizmo*> in_cycle_gizmos;
    };
    using ProjectContexts = Biz::ProjectScoped<ProjectContext>;

    ProjectContexts m_projects;
    ISceneProvider& m_scene_provider;
    Biz::ProjectInteractor& m_project_interactor;

    GeometryDataFactory m_data_factory;
    Domain::SelectionId m_last_project_id{Domain::INVALID_ID};

    std::unique_ptr<MouseDragDetector> m_mouse_drag_detector;

    GizmoList m_base_gizmos;
    ToolGizmoList m_tool_gizmos;
    GizmoCommandRegistry m_command_registry;

    Clipper m_clipper;

#if DEBUG_GIZMO_MANAGER
    constexpr static size_t NUM_DEBUG_ACTIVATION_LAST_STEPS = 63;
    using GizmoActivationDebugData = std::list<GizmoActivationState>;
    using GizmosActivationDebugData = std::unordered_map<const IGizmo*, GizmoActivationDebugData>;
    bool m_activation_debug_shown {true};
    GizmosActivationDebugData m_activation_debug;
#endif
};

} // namespace Slic3r::App::Scene

