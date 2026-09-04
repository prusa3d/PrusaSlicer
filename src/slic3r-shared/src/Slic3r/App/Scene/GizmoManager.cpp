#include "Slic3r/App/Scene/GizmoManager.hpp"

#include "Slic3r/App/Render/ScopedDebugGroup.hpp"
#include "Slic3r/App/Scene/BedNodeTag.hpp"
#include "Slic3r/App/Scene/SceneNodeTag.hpp"
#include "Slic3r/Domain/TemplateUtils.hpp"
// DEBUG ONLY: for MEASURE_GIZMO_DEBUG
#include "Slic3r/App/Plater/MeasureGizmo.hpp"

#include <tracy/Tracy.hpp>

#if DEBUG_GIZMO_MANAGER
#include "Slic3r/TypeInfo.hpp"
#include "Slic3r/Log.hpp"
#include <imgui/imgui.h>
#endif


namespace Slic3r::App::Scene {

#if DEBUG_GIZMO_MANAGER
const char* ACTIVATION_STATE_NAMES[] = {
    "Inactive",
    "Probing",
    "Active",
    "Done"
};

#endif

GizmoManager::GizmoManager(
    Render::Device& device,
    ISceneProvider& scene_provider,
    Biz::ProjectInteractor& project_interactor,
    std::unique_ptr<MouseDragDetector> mouse_drag_detector
) :
    m_project_changed_listener_scope(project_interactor, *this),
    m_selected_project_changed_listener_scope(project_interactor, *this),
    m_scene_selection_changed_listener_scope(project_interactor.scene_interactor(), *this),
    m_projects(project_interactor),
    m_scene_provider(scene_provider),
    m_project_interactor(project_interactor),
    m_data_factory(device, "gizmo_manager"),
    m_mouse_drag_detector(std::move(mouse_drag_detector)),
    m_command_registry(*this)
{
    GizmoManager::on_selected_project_changed(m_project_interactor.selected_project_id());
}

namespace {
std::vector<IGizmo*> get_active_gizmos(
    const std::vector<std::unique_ptr<IGizmo>>& base_gizmos,
    IGizmo* active_tool,
    bool object_selection_disabled
)
{
    std::vector<IGizmo*> gizmos;
    if (active_tool == nullptr) {
        gizmos.reserve(base_gizmos.size());
    } else {
        gizmos.reserve(base_gizmos.size() + 1);
        gizmos.push_back(active_tool);
    }
    for (const auto& g : base_gizmos) {
        if (g->handles_object_selection() && object_selection_disabled) {
            continue;
        }
        gizmos.push_back(g.get());
    }
    return gizmos;
}
}

NodePickResults GizmoManager::repick() const
{
    const auto& p = current_context();
    if (!p.last_mouse_event.has_value()) {
        return {};
    }
    const auto& [e, screen_info] = p.last_mouse_event.value();
    auto [results, ray] = pick(e, screen_info);
    return results;
}

GizmoManager::PickResultWithRay GizmoManager::pick(
    const Platform::MouseEvent& e,
    const Render::ScreenInfo& screen_info
) const
{
    Scene& scene = m_scene_provider.scene();

    NodePickResults pick_results;
    Ray pick_ray;
    scene.pick_at(
        screen_info.mouse_to_screen(e.x()),
        screen_info.mouse_to_screen(e.y()),
        pick_results, &pick_ray

    );

    // filters out hits on volumes below the bed if the camera is pointing downward
    if (!scene.camera().pointing_upward()) {
        auto it = std::find_if(pick_results.begin(), pick_results.end(),
            [](const NodePickResult& r) {
                return r.node->tag_of_type<BedNodeTag>() != nullptr;
            }
        );
        if (it != pick_results.end()) {
            pick_results.erase(std::remove_if(it, pick_results.end(),
                [](const NodePickResult& r) { return r.node->tag_of_type<SceneNodeTag>() != nullptr; }),
                pick_results.end());
        }
    }

    // When volumes overlap exactly (equal distance), prefer the most recently added volume (higher node ID).
    std::ranges::sort(
        pick_results,
        [](const NodePickResult& a, const NodePickResult& b)
        {
            return a.cast.distance < b.cast.distance
                || (a.cast.distance == b.cast.distance && a.node->id() > b.node->id());
        }
    );

    return std::make_tuple(pick_results, pick_ray);
}

void GizmoManager::on_scene_mouse_event(const Platform::MouseEvent& e, const Slic3r::App::Render::ScreenInfo& screen_info)
{
    ZoneScoped;
    auto& p = current_context();
    if (!p.in_cycle) {
        if (e.is_imgui_captured())
            return;
        prepare_cycle();
    }
    p.last_mouse_event = MouseEventContext{e, screen_info};

    auto [pick_results, pick_ray] = pick(e, screen_info);

#if DEBUG_GIZMO_MANAGER
    update_gizmo_activation_debug_frame_begin();
#endif

    GizmoEventContext ctx{m_scene_provider, e, pick_ray, pick_results, screen_info};
    if (m_mouse_drag_detector
        && m_mouse_drag_detector->mouse_event(
            ctx,
            [&]()
            {
                return get_active_gizmos(
                    m_base_gizmos,
                    current_context().active_tool,
                    p.object_selection_disabled
                );
            }
        )) {
#if DEBUG_GIZMO_MANAGER
        const auto* g = m_mouse_drag_detector->dragging_gizmo();
        update_gizmo_activation_debug_data(g, GizmoActivationState::Active);
#endif
        return;
    }
    {
        ZoneScopedN("dbl-click");
        // activation by double click
        if (e.type() == Platform::MouseEvent::Type::DoubleClick &&
            e.button() == Platform::MouseButton::Left)
        {
            auto it = std::find_if(m_tool_gizmos.begin(), m_tool_gizmos.end(),
                [&ctx](const IToolGizmoPtr& tool) { return tool->allows_activation_by_double_click(ctx); });
            if (it != m_tool_gizmos.end()) {
                ToolType tool_type = (*it)->type();
                if (tool_type != current_tool_type()) {
                    activate_tool(tool_type);
                }
                return;
            }
        }
    }

    const bool single_active = p.in_cycle_gizmos.size() == 1;

    {
        ZoneScopedN("on_mouse");

        auto it = p.in_cycle_gizmos.begin();
        while (it != p.in_cycle_gizmos.end()) {
            auto g = *it;

            auto ret = g->on_mouse(ctx, single_active);
#if DEBUG_GIZMO_MANAGER
        update_gizmo_activation_debug_data(g, ret);
#endif

            if (ret == GizmoActivationState::Inactive) {
                it = p.in_cycle_gizmos.erase(it);
                continue;
            } else if (ret == GizmoActivationState::Done) {
                p.in_cycle_gizmos.clear();
                break;
            } else if (ret == GizmoActivationState::Active) {
                p.in_cycle_gizmos.clear();
                p.in_cycle_gizmos.push_back(g);
                if (m_mouse_drag_detector)
                    m_mouse_drag_detector->cancel_drag_event();
                break;
            }
            ++it;
        }
    }

    if (p.in_cycle && p.in_cycle_gizmos.empty())
        p.in_cycle = false;

    // process transient events
    for (auto& g : m_base_gizmos)
        g->on_transient_mouse(ctx);
    for (auto& g : m_tool_gizmos)
        g->on_transient_mouse(ctx);
}

bool GizmoManager::on_scene_keyboard_event(const Platform::KeyboardEvent& e)
{
    if (m_mouse_drag_detector && e.code() == Platform::KeyCode::Escape)
        m_mouse_drag_detector->cancel_drag_event();

    GizmoKeyEventContext ctx{ e };
    for (auto& g : m_base_gizmos) {
        if (!(current_context().object_selection_disabled && g->handles_object_selection())) {
            g->on_keyboard(ctx);
        }
    }
    for (auto& g : m_tool_gizmos)
        g->on_keyboard(ctx);

    return m_command_registry.process_keyboard_event(e);
}

void GizmoManager::on_project_will_be_removed(Domain::SelectionId project_id)
{
    if (project_id != m_last_project_id) {
        return;
    }

    auto& last_p = m_projects.project(m_last_project_id);
    if (last_p.active_tool)
        last_p.active_tool->on_project_deactivated(m_last_project_id);

    m_last_project_id = Domain::INVALID_ID;
}

void GizmoManager::on_scene_selection_changed(
    Domain::SelectionId project_id,
    const Biz::Scene::ObjectSelection& selection
) {
    for (const IToolGizmoPtr& tool_gizmo : m_tool_gizmos) {
        if (current_tool_type() == tool_gizmo->type()
            && !tool_gizmo->enabled())
        {
            deactivate_current_tool();
        }
    }
}

void GizmoManager::prepare_cycle()
{
    auto& p = current_context();
    p.in_cycle = true;
    p.in_cycle_gizmos.reserve(m_base_gizmos.size() + (p.active_tool != nullptr ? 1 : 0));
    if (p.active_tool)
        p.in_cycle_gizmos.push_back(p.active_tool);
    for (const auto& g : m_base_gizmos) {
        if (p.object_selection_disabled && g->handles_object_selection()) {
            // ignore this gizmo (QuickSelectGizmo)
            continue;
        }
        g->on_cycle_prepare();
        p.in_cycle_gizmos.push_back(g.get());
    }
}

void GizmoManager::render_scene(Render::CommandBuffer& cmd_buffer)
{
    const auto& p = current_context();
    Render::ScopedDebugGroup event_gizmo_manager("Gizmo Manager", cmd_buffer);
    // Most gizmos will render on top of scene, so disable depth test here so gizmos shouldn't care
    cmd_buffer.set_depth_test_enabled(false);
    for (auto* g : p.in_cycle_gizmos) {
        if (g != p.active_tool)
            g->render_scene(cmd_buffer);
    }
    if (p.active_tool != nullptr)
        p.active_tool->render_scene(cmd_buffer);

    //m_scene_provider.scene().log_nodes();
}

void GizmoManager::render_imgui() {    
    if (IToolGizmo* active_tool = current_context().active_tool;
        active_tool != nullptr)        
        active_tool->render_imgui();
#if DEBUG_GIZMO_MANAGER
    render_gizmo_activation_debug();
#endif
}

void GizmoManager::deactivate_current_tool()
{
    auto& p = current_context();
    if (p.active_tool == nullptr)
        return;
    p.active_tool->on_deactivated();
    p.active_tool = nullptr;
    invoke_listeners<IGizmoActiveToolListener>([p](auto* l) { l->active_tool_changed(p.active_tool); });
    p.object_selection_disabled = false;

    m_project_interactor.undo_provider().take_snapshot(
        Biz::UndoSnapshotType::DeactivateGizmo
    );
}

void GizmoManager::activate_tool(ToolType tool)
{
    if (tool == ToolType::None) {
        deactivate_current_tool();
        return;
    }

    auto& p = m_projects.project(m_project_interactor.selected_project_id());
    IToolGizmo* next_tool = ASSERT_VAL(
        find_tool(tool, m_project_interactor.selected_config_container().print_technology())
    );

    IToolGizmo* original_tool{p.active_tool};
    if (next_tool != original_tool) {
        deactivate_current_tool();
        p.active_tool = next_tool;
        p.active_tool->on_activated();
        invoke_listeners<IGizmoActiveToolListener>([p](auto* l) { l->active_tool_changed(p.active_tool); });

        m_project_interactor.undo_provider().take_snapshot(
            Biz::UndoSnapshotType::ActivateGizmo
        );
    }

    p.object_selection_disabled = p.active_tool ? p.active_tool->disable_object_selection() : false;
}

ToolType GizmoManager::current_tool_type() const
{
    const auto& ctx = m_projects.project(m_project_interactor.selected_project_id());
    return (ctx.active_tool != nullptr) ? ctx.active_tool->type() : ToolType::None;
}

Undo::ToolsState GizmoManager::tools_state() const
{
    Undo::ToolsState tools_state;
    for (const IToolGizmoPtr& tool_gizmo : m_tool_gizmos) {
        std::optional<Undo::ToolState> tool_state = tool_gizmo->get_tool_state();
        if (tool_state.has_value()) {
            tools_state.push_back(std::move(*tool_state));
        }
    }

    return tools_state;
}

static ToolType tool_state_to_tool_type(const Undo::ToolState& tool_state)
{
    return std::visit(
        Domain::overloaded{
            [](const Undo::HeightRangeGizmoState& state) { return ToolType::HeightRangeGizmo; },
            [](const Undo::CutGizmoState&) { return ToolType::CutGizmo; },
        },
        tool_state
    );
}

void GizmoManager::set_tools_state(const Undo::ToolsState& tools_state)
{
    for (const Undo::ToolState& tool_state : tools_state) {
        IToolGizmo* tool_gizmo = find_tool(
            tool_state_to_tool_type(tool_state),
            m_project_interactor.selected_config_container().print_technology()
        );
        ASSERT(tool_gizmo != nullptr);
        tool_gizmo->set_tool_state(Undo::ToolState{tool_state});
    }
}

bool GizmoManager::is_tool_active_in_current_project(const IToolGizmo& tool) const
{
    const auto& ctx = current_context();
    return ctx.active_tool == &tool;
}


IToolGizmo* GizmoManager::find_tool(ToolType tool, Domain::PrinterTechnology pt)
{
    auto it =
        std::find_if(m_tool_gizmos.begin(), m_tool_gizmos.end(), [tool, pt](const IToolGizmoPtr& tg) {
            return tg->type() == tool && tg->supports_printer(pt);
        });
    return it == m_tool_gizmos.end() ? nullptr : it->get();
}


void GizmoManager::on_selected_project_changed(size_t index)
{
    if (m_last_project_id != Domain::INVALID_ID) {
        auto& last_p = m_projects.project(m_last_project_id);
        if (last_p.active_tool)
            last_p.active_tool->on_project_deactivated(m_last_project_id);
    }

    m_last_project_id = index;
    auto& p = current_context();
    if (p.active_tool)
        p.active_tool->on_project_activated(index);
    invoke_listeners<IGizmoActiveToolListener>(
        [active_tool = p.active_tool](IGizmoActiveToolListener* l) {
            l->active_tool_changed(active_tool);
        }
    );
}

#if DEBUG_GIZMO_MANAGER
void GizmoManager::update_gizmo_activation_debug_data(const IGizmo* g, GizmoActivationState state)
{
    if (auto it = m_activation_debug.find(g); it != m_activation_debug.end()) {
        it->second.back() = state;
    } else {
        m_activation_debug[g] = {state};
    }
}

void GizmoManager::update_gizmo_activation_debug_frame_begin()
{
    for (auto& [g, gad] : m_activation_debug) {
        gad.push_back(GizmoActivationState::Inactive);
        if (gad.size() > NUM_DEBUG_ACTIVATION_LAST_STEPS)
            gad.pop_front();
    }
}

void GizmoManager::render_gizmo_activation_debug()
{
    if (!m_activation_debug_shown)
        return;

    ImGui::SetNextWindowSize(ImVec2(0,240), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(0, ImGui::GetIO().DisplaySize.y), ImGuiCond_Appearing, ImVec2(0, 1));

    if (ImGui::Begin("Gizmo Activation", &m_activation_debug_shown, ImGuiWindowFlags_NoSavedSettings)) {
        // Begin a table with 2 + maxCellsPerRow columns
        if (ImGui::BeginTable("MapDataTable", 1 + NUM_DEBUG_ACTIVATION_LAST_STEPS, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            // Set fixed width for the first column
            ImGui::TableSetupColumn("Gizmo", ImGuiTableColumnFlags_WidthFixed, 250.0f); // Adjust 150.0f as needed
            for (int i = 0; i < NUM_DEBUG_ACTIVATION_LAST_STEPS; ++i) {
                ImGui::TableSetupColumn("");
            }
            ImGui::TableHeadersRow();

            // Loop through each key-value pair in the map
            for (const auto& [key, values] : m_activation_debug) {
                // Create a new row for the current key
                ImGui::TableNextRow();

                // Render the key in the first column
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(type_name(*key).c_str());

                // Prepare a buffer for rendering values with padding
                int remainingCells = NUM_DEBUG_ACTIVATION_LAST_STEPS - static_cast<int>(values.size());

                // Render blank cells for padding on the left
                for (int cellIndex = 0; cellIndex < remainingCells; ++cellIndex) {
                    ImGui::TableSetColumnIndex(1 + cellIndex);
                    ImGui::TextUnformatted("");
                }

                // Render up to maxCellsPerRow cells for the values
                auto it = values.begin();
                for (int cellIndex = remainingCells; cellIndex < NUM_DEBUG_ACTIVATION_LAST_STEPS; ++cellIndex) {
                    ImGui::TableSetColumnIndex(1 + cellIndex);

                    if (it != values.end()) {
                        // Determine the cell color based on the value
                        ImVec4 color;
                        const char* name = "?";
                        switch (*it) {
                            case GizmoActivationState::Inactive:
                                color = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
                            name = "I";
                            break;
                            case GizmoActivationState::Probing:
                                color = ImVec4(0.8f, 0.8f, 0.0f, 1.0f);
                            name = "P";
                            break;
                            case GizmoActivationState::Active:
                                color = ImVec4(0.0f, 1.0f, 0.2f, 1.0f);
                            name = "A";
                            break;
                            case GizmoActivationState::Done:
                                color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
                            name = "D";
                            break;
                        }

                        // Draw a colored cell
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::ColorConvertFloat4ToU32(color));
                        ImGui::TextUnformatted(name);

                        // Advance the iterator
                        ++it;
                    } else {
                        // If no more values, leave the cell blank
                        ImGui::TextUnformatted("");
                    }
                }
            }

            ImGui::EndTable();
        }
    }
    ImGui::End();

}

#endif

} // namespace Slic3r::App::Scene
