#pragma once
#include <chrono>
#include <optional>
#include <string>
#include <functional>
#include <Slic3r/App/Platform/MouseEvent.hpp>
#include <Slic3r/App/Scene/Scene.hpp> // NodePickResults
#include <Slic3r/App/Scene/Ray.hpp>
#include <Slic3r/App/Scene/GizmoEventContext.hpp>
#include "Slic3r/App/Scene/IGizmo.hpp"

/**
 *  @brief Way to unify drag conditions(time or offset).
 *  On detection of start dragging (MouseEvent::Move)
 *  is selected first gizmo from active list 
 *  to process dragging operation(dragging + finish(cancel) evenet)
 */
namespace Slic3r::App::Scene {

/**
 *  @brief   Interface is used together with IGizmo.
 *           allowe process mouse drag events
 */
class IMouseDrag
{
public:
    virtual ~IMouseDrag()                                = default;
    virtual bool on_drag_start(const GizmoEventContext&) = 0;
    virtual bool on_dragging(const GizmoEventContext&)   = 0;
    virtual void on_drag_finish()                        = 0;
    virtual void on_drag_cancel()                        = 0;
};

/**
 *  @brief   Only one in the application(centralized place for dragging detection),
 *           Sniff mouse events from OS provide Drag object
 *  @note Consumers(listeners) are registred by add_tool_gizmo(add_base_gizmo) into GizmoManager
 */
class MouseDragDetector
{
public:
    MouseDragDetector(int min_time_span, int min_offset);

    /**
     *  @brief When gizmo implement IMouseDrag interface
     *         add into internal sorted listener list
     *  @param gizmo - listener when implement IMouseDrag interface
     */
    void add_listener(IGizmo* gizmo);
    void rem_listener(IGizmo* gizmo);
    
    /**
     *  @brief  Change state by mouse events to detect draging by mouse events
     *  @param  ctx        - Mouse event + casted ray
     *  @param  get_gizmos - call back to getter on the Current active gizmos
     *  @retval            - True when consume event otherwise False
     * @note MouseEvent::ButtonDown and event til the drag is confirm return False
     */
    using GetActiveGizmos = std::function<std::vector<IGizmo*>()>;
    bool mouse_event(const GizmoEventContext& ctx, GetActiveGizmos get_gizmos);

    /**
     *  @brief Way to cancel draging e.g. on ESC key
     */
    void cancel_drag_event();

    const IGizmo* dragging_gizmo() const
    {
        return m_dragging_gizmo;
    }

private:
    bool can_start_drag();
    bool on_start(const std::vector<IGizmo*>& gizmos);

    // minimal time from button down to validate it as drag in miliseconds
    int m_min_time_span; // [in ms]
    // minimal mouse offset from button down to validate it is a drag
    // unit is micrometers of real world display size
    int m_min_offset; // [in um]

    std::chrono::time_point<std::chrono::steady_clock> m_start_time;

    bool m_right_down  = false;
    bool m_middle_down = false;

    /**
     *  @brief Keep information about place of drag start
     */
    struct DragStart
    {
        GizmoEventContext ctx;

        DragStart(const GizmoEventContext& ctx) : ctx(ctx) {}
    };

    /**
    *  @brief Current state of dragging for button
    */
    enum class DragState
    {
        NoDrag,
        StartWeWillSee, // mouse click, record mouse event
        Dragging // mouse move with button down
    };
    // keep state of left mouse button drag
    DragState m_state = DragState::NoDrag;
    static std::string to_string(DragState state);
    // keep data from drag start
    std::optional<DragStart> m_start = std::nullopt;

    // set on_start, discard on finish OR cancel
    IMouseDrag* m_dragging = nullptr;
    const IGizmo* m_dragging_gizmo = nullptr;

    using Listener  = std::pair<IGizmo*, IMouseDrag*>;
    using Listeners = std::vector<Listener>;
    // sorted by IGizmo pointer
    Listeners m_listeners;
};
} // namespace Slic3r::App::Scene
