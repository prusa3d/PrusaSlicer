#pragma once

namespace Slic3r::App::Platform::WX {

/**
 * @brief Scroll detail the OS knows but wxWidgets flattens away.
 *
 * wxMouseEvent only carries a quantised wheel rotation, which is all a notched
 * wheel needs but throws away everything that makes trackpad scrolling feel
 * right: sub-detent resolution and the inertial phase the OS generates after
 * the fingers lift.
 */
struct NativeScrollInfo
{
    bool precise{false};  ///< Device reports sub-detent deltas (trackpad, Magic Mouse).
    bool momentum{false}; ///< Event belongs to the OS-generated inertial phase.
    float delta_x{0};     ///< Horizontal travel in logical pixels. Only valid when @c precise.
    float delta_y{0};     ///< Vertical travel in logical pixels. Only valid when @c precise.
};

/**
 * @brief Inspect the scroll event the OS is currently dispatching.
 *
 * Must be called from within the handler for the corresponding wxMouseEvent,
 * while the native event is still current.
 *
 * @return false when the platform has nothing to add over wxMouseEvent, in
 *         which case the caller falls back to the wheel rotation. Signs match
 *         wxMouseEvent::GetWheelRotation(), so the user's "natural scrolling"
 *         preference stays applied exactly once, by the OS.
 */
bool query_native_scroll(NativeScrollInfo& info);

} // namespace Slic3r::App::Platform::WX
