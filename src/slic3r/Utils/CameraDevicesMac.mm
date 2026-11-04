// PrusaSlicer is released under the terms of the AGPLv3 or higher.
#include "CameraDevices.hpp"
#import <AVFoundation/AVFoundation.h>

namespace Slic3r::GUI {
std::vector<CameraDevice> camera_devices()
{
    std::vector<CameraDevice> result;
    // Match the video-then-muxed ordering used by the pinned AVFoundation input.
    @autoreleasepool {
        for (AVCaptureDevice* device in [AVCaptureDevice devicesWithMediaType:AVMediaTypeVideo])
            result.push_back({[[device uniqueID] UTF8String], [[device localizedName] UTF8String]});
        for (AVCaptureDevice* device in [AVCaptureDevice devicesWithMediaType:AVMediaTypeMuxed])
            result.push_back({[[device uniqueID] UTF8String], [[device localizedName] UTF8String]});
    }
    return result;
}
std::string camera_device_input(const std::string& id)
{
    const std::vector<CameraDevice> devices = camera_devices();
    for (size_t index = 0; index < devices.size(); ++index) {
        if (devices[index].id == id)
            return std::to_string(index) + ":none";
    }
    return {};
}
}
