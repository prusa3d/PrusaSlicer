#include "Slic3r/App/Scene/MouseDragDetector.hpp"
#include "Slic3r/Log.hpp"

namespace {
bool is_over_span(const std::chrono::time_point<std::chrono::steady_clock>& start_time, int time_span)
{
    const std::chrono::duration<double> elapsed_seconds{std::chrono::steady_clock::now() - start_time};
    return static_cast<int>(elapsed_seconds.count() * 1'000'000) > time_span;
}

using Slic3r::App::Platform::MouseEvent;

bool is_over_offset(const MouseEvent& me1, const MouseEvent& me2, int offset)
{
    int max_distance = std::max(abs(me1.x() - me2.x()), abs(me1.y() - me2.y()));

    // TODO: To convert on micrometers need DPI of the monitor
    return static_cast<int>(max_distance * (1'000 * 96 / 2.54)) > offset;
}

void log_weird_state(const std::string& message)
{
    SPDLOG_INFO("Drag detector weird state {}", message);
}
} // namespace

namespace Slic3r::App::Scene {
MouseDragDetector::MouseDragDetector(int min_time_span, int min_offset) :
    m_min_time_span(min_time_span),
    m_min_offset(min_offset)
{}

void MouseDragDetector::add_listener(IGizmo* gizmo)
{
    if (gizmo == nullptr)
        return;
    IMouseDrag* drag = dynamic_cast<IMouseDrag*>(gizmo);
    if (drag == nullptr)
        return;

    auto pred = [](const Listener& l, IGizmo* gizmo) {
        return gizmo < l.first;
    };
    auto it = std::lower_bound(m_listeners.begin(), m_listeners.end(), gizmo, pred);
    if (it == m_listeners.end()) {
        m_listeners.emplace_back(gizmo, drag);
    } else if (it->first == gizmo) {
        // already registred
        return;
    } else {
        // sorted insert
        m_listeners.insert(it, Listener{gizmo, drag});
    }
}

void MouseDragDetector::rem_listener(IGizmo* gizmo)
{
    if (gizmo == nullptr)
        return;
    auto pred = [](const Listener& l, IGizmo* gizmo) {
        return gizmo < l.first;
    };
    auto it = std::lower_bound(m_listeners.begin(), m_listeners.end(), gizmo, pred);
    if (it != m_listeners.end() && it->first == gizmo) {
        IMouseDrag* drag = dynamic_cast<IMouseDrag*>(gizmo);
        if (drag != nullptr && m_dragging == drag) {
            cancel_drag_event();
            m_dragging = nullptr;
            m_dragging_gizmo = nullptr;
        }
        m_listeners.erase(it);
    }
}

bool MouseDragDetector::on_start(const std::vector<IGizmo*>& gizmos)
{
    auto pred             = [](const Listener& l, const IGizmo* gizmo) {
        return gizmo < l.first;
    };
    for (const IGizmo* gizmo : gizmos) {
        auto it = std::lower_bound(m_listeners.begin(), m_listeners.end(), gizmo, pred);
        if (it != m_listeners.end() && it->first == gizmo && it->second->on_drag_start(m_start->ctx)) {
            // gizmo consume drag
            m_dragging = it->second;
            m_dragging_gizmo = gizmo;
            return true;
        }
    }
    m_dragging = nullptr;
    m_dragging_gizmo = nullptr;
    return false;
}

bool MouseDragDetector::mouse_event(const GizmoEventContext& ctx, GetActiveGizmos get_gizmos)
{
    const Platform::MouseEvent& me = ctx.mouse_event();
    switch (me.type()) {
    case Platform::MouseEvent::Type::Move:
        switch (m_state) {
        case DragState::NoDrag:
            return false;
        case DragState::Dragging:
            if (m_dragging == nullptr)
                return false;
            if (!m_dragging->on_dragging(ctx)) {
                // stop dragging inside on_drag event
                m_dragging = nullptr;
                m_dragging_gizmo = nullptr;
                return false;
            }
            return true;
        case DragState::StartWeWillSee:
            if (!m_start.has_value()) { // For sure
                log_weird_state("Missing start data");
                m_state = DragState::NoDrag;
            } else if (is_over_span(m_start_time, m_min_time_span)
                       || is_over_offset(m_start->ctx.mouse_event(), me, m_min_offset))
            {
                m_state                     = DragState::Dragging;
                std::vector<IGizmo*> gizmos = get_gizmos();
                return on_start(gizmos);
            }
            return false;
        }
        return false;
    case Platform::MouseEvent::Type::ButtonDown:
        switch (me.button()) {
        case Platform::MouseButton::Left:
            if (!me.is_imgui_captured() && can_start_drag()) {
                m_state      = DragState::StartWeWillSee;
                m_start_time = std::chrono::steady_clock::now();
                m_start.emplace(ctx);
            }
            return false;
        case Platform::MouseButton::Right:
            if (m_right_down) {
                log_weird_state("Second Right Button down in row.");
            }
            m_right_down = true;
            cancel_drag_event();
            return false;
        case Platform::MouseButton::Middle:
            if (m_middle_down) {
                log_weird_state("Second Middle Button down in row.");
            }
            m_middle_down = true;
            cancel_drag_event();
            return false;
        case Platform::MouseButton::NoButton:
            log_weird_state("Mouse Button down without button.");
            cancel_drag_event();
            return false;
        default:
            log_weird_state("Unknown mouse button down.");
            cancel_drag_event();
            return false;
        }
    case Platform::MouseEvent::Type::ButtonUp:
        switch (me.button()) {
        case Platform::MouseButton::Left:
            switch (m_state) {
            case DragState::Dragging:
                m_state = DragState::NoDrag;
                if (m_dragging != nullptr) {
                    m_dragging->on_drag_finish();
                    return true;
                }
                return false;
            case DragState::StartWeWillSee:
                m_state = DragState::NoDrag;
            // case DragState::NoDrag: no action
            // it can appear after external cancel dragging(e.g. ESC)
            default: return false;
            }
        case Platform::MouseButton::Right:
            if (!m_right_down) {
                log_weird_state("Right mouse button up before down.");
            }
            m_right_down = false;
            cancel_drag_event();
            return false;
        case Platform::MouseButton::Middle:
            if (!m_middle_down) {
                log_weird_state("Middle mouse button up before down.");
            }
            m_middle_down = false;
            cancel_drag_event();
            return false;
        case Platform::MouseButton::NoButton:
            log_weird_state("Mouse Button up without button.");
            cancel_drag_event();
            return false;
        default:
            log_weird_state("Unknown mouse button down.");
            cancel_drag_event();
            return false;
        }
    case Platform::MouseEvent::Type::Enter:
        // clear internal state
        m_state = DragState::NoDrag;
        // TODO: search for current mouse state and fix it
        // NOTE: When true but in reality false it will be blocking next drag
        m_right_down  = false;
        m_middle_down = false;
        return false;
    case Platform::MouseEvent::Type::Wheel:
        [[fallthrough]];
    case Platform::MouseEvent::Type::Leave:
        [[fallthrough]];
    case Platform::MouseEvent::Type::DoubleClick:
        cancel_drag_event();
        return false;
    default:
        log_weird_state("Unknown Platform::MouseEvent::Type. " + std::to_string((int) me.type()));
    }
    return false;
}

void MouseDragDetector::cancel_drag_event()
{
    switch (m_state) {
    case DragState::NoDrag:
        return;
    case DragState::Dragging:
        if (m_dragging != nullptr)
            m_dragging->on_drag_cancel();
        m_state = DragState::NoDrag;
        return;
    case DragState::StartWeWillSee:
        m_state = DragState::NoDrag;
    }
}

bool MouseDragDetector::can_start_drag()
{
    switch (m_state) {
    case DragState::NoDrag:
        return !m_right_down && !m_middle_down;
    case DragState::Dragging:
        log_weird_state("Try to start drag during dragging. Second left mouse button in row");
        cancel_drag_event();
        return false;
    case DragState::StartWeWillSee:
        log_weird_state("Try to start drag during draging condition");
        m_state = DragState::NoDrag;
        return false;
    default:
        log_weird_state("Undefined DragState in can start funtion.");
        return false;
    }
}

std::string MouseDragDetector::to_string(DragState state)
{
    switch (state) {
    case DragState::NoDrag:
        return "no drag";
    case DragState::Dragging:
        return "dragging";
    case DragState::StartWeWillSee:
        return "start_we_will_see";
    default:
        return "undefined";
    }
}
} // namespace Slic3r::App::Scene
