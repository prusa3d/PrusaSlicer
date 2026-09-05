#include "NativeScroll.hpp"

#import <AppKit/AppKit.h>

namespace Slic3r::App::Platform::WX {

bool query_native_scroll(NativeScrollInfo& info)
{
    NSEvent* event = [NSApp currentEvent];
    if (event == nil || [event type] != NSEventTypeScrollWheel)
        return false;

    // A notched wheel reports scrollingDelta in lines rather than points, which
    // is no better than what wxMouseEvent already gave us. Only claim the event
    // when the device actually has something finer to say.
    if (![event hasPreciseScrollingDeltas])
        return false;

    info.precise  = true;
    info.momentum = [event momentumPhase] != NSEventPhaseNone;

    // scrollingDeltaX/Y carry the same sign convention as deltaX/Y, so the
    // "natural scrolling" preference the OS has already applied is preserved
    // and the rest of the pipeline keeps the direction it had before.
    info.delta_x = static_cast<float>([event scrollingDeltaX]);
    info.delta_y = static_cast<float>([event scrollingDeltaY]);

    return true;
}

} // namespace Slic3r::App::Platform::WX
