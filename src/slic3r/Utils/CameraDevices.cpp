// PrusaSlicer is released under the terms of the AGPLv3 or higher.
#include "CameraDevices.hpp"
extern "C" {
#include <libavdevice/avdevice.h>
}
namespace Slic3r::GUI {
std::vector<CameraDevice> camera_devices()
{
    avdevice_register_all();
#ifdef _WIN32
    const AVInputFormat* format = av_find_input_format("dshow");
#else
    const AVInputFormat* format = av_find_input_format("video4linux2");
#endif
    std::vector<CameraDevice> result;
    AVDeviceInfoList* devices = nullptr;
    if (format && avdevice_list_input_sources(format, nullptr, nullptr, &devices) >= 0) {
        for (int index = 0; index < devices->nb_devices; ++index) {
            const AVDeviceInfo& device = *devices->devices[index];
            for (int media = 0; media < device.nb_media_types; ++media) {
                if (device.media_types[media] == AVMEDIA_TYPE_VIDEO) {
                    result.push_back({device.device_name, device.device_description});
                    break;
                }
            }
        }
    }
    avdevice_free_list_devices(&devices);
    return result;
}
}
