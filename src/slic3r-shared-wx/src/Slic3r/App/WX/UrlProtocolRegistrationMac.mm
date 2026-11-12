#include "Slic3r/App/WX/UrlProtocolRegistration.hpp"

#include "Slic3r/Log.hpp"

#import <AppKit/AppKit.h>
#import <CoreServices/CoreServices.h>
#import <Foundation/Foundation.h>

namespace Slic3r::App::WX {

void register_prusaslicer_url()
{
    NSBundle* bundle = [NSBundle mainBundle];
    NSString* bundle_id = [bundle bundleIdentifier];
    if (bundle_id == nil) {
        SPDLOG_INFO("prusaslicer:// URL registration skipped: not running from an application bundle.");
        return;
    }
    NSURL* bundle_url = [bundle bundleURL];

    OSStatus status = LSRegisterURL((CFURLRef) bundle_url, true);
    if (status != noErr) {
        SPDLOG_WARN("LSRegisterURL failed with status {}.", status);
    }

    if (@available(macOS 12.0, *)) {
        [[NSWorkspace sharedWorkspace] setDefaultApplicationAtURL:bundle_url
                                            toOpenURLsWithScheme:@"prusaslicer"
                                               completionHandler:^(NSError* error) {
            if (error != nil) {
                SPDLOG_ERROR(
                    "Setting the default handler of prusaslicer:// failed: {}",
                    [[error localizedDescription] UTF8String]
                );
            }
        }];
    } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        status = LSSetDefaultHandlerForURLScheme(CFSTR("prusaslicer"), (CFStringRef) bundle_id);
#pragma clang diagnostic pop
        if (status != noErr) {
            SPDLOG_ERROR("LSSetDefaultHandlerForURLScheme failed with status {}.", status);
        }
    }
}

} // namespace Slic3r::App::WX
