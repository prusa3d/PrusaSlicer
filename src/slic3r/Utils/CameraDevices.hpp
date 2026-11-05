// PrusaSlicer is released under the terms of the AGPLv3 or higher.
#ifndef slic3r_CameraDevices_hpp_
#define slic3r_CameraDevices_hpp_
#include <string>
#include <vector>
namespace Slic3r::GUI {
struct CameraDevice
{
    std::string id;
    std::string name;
};
// Lists local video inputs only; microphones and screen capture are excluded.
std::vector<CameraDevice> camera_devices();
#ifdef __APPLE__
std::string camera_device_input(const std::string& id);
#endif
}
#endif
