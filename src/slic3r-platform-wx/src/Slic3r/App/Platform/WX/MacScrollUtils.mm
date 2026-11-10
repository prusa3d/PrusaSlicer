#include "MacScrollUtils.hpp"

#import <AppKit/AppKit.h>

namespace Slic3r::App::Platform::WX {

bool mac_current_scroll_event_is_precise()
{
    NSEvent* event = [NSApp currentEvent];
    return event != nil && [event type] == NSEventTypeScrollWheel
        && [event hasPreciseScrollingDeltas];
}

} // namespace Slic3r::App::Platform::WX
