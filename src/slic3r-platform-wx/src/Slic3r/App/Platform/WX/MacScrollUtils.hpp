#pragma once

namespace Slic3r::App::Platform::WX {

/// Returns true if the native event currently being dispatched is a scroll wheel event
/// carrying pixel-precise deltas (trackpad, Magic Mouse, smooth-scrolling mice).
/// wxMouseEvent does not expose this information.
bool mac_current_scroll_event_is_precise();

} // namespace Slic3r::App::Platform::WX
