#include "NativeScroll.hpp"

namespace Slic3r::App::Platform::WX {

// Windows and GTK deliver high-resolution wheel deltas through
// wxMouseEvent::GetWheelRotation() itself (a fraction of GetWheelDelta()), so
// there is nothing to recover here beyond what the caller already has.
bool query_native_scroll(NativeScrollInfo&)
{
    return false;
}

} // namespace Slic3r::App::Platform::WX
