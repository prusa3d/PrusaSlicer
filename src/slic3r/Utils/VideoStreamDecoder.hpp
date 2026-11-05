// PrusaSlicer is released under the terms of the AGPLv3 or higher.
#ifndef slic3r_VideoStreamDecoder_hpp_
#define slic3r_VideoStreamDecoder_hpp_

#include <memory>
#include <string>
#include <vector>

namespace Slic3r::GUI {

// Built-in FFmpeg RTSP transport and video decoding.
// Public methods belong to the UI thread; the decoder worker never touches wxWidgets.
class VideoStreamDecoder
{
public:
    static constexpr unsigned width = 640;
    static constexpr unsigned height = 360;
    enum class State { Stopped, Connecting, Playing, Ended, Error };

    VideoStreamDecoder();
    ~VideoStreamDecoder();
    VideoStreamDecoder(const VideoStreamDecoder&) = delete;
    VideoStreamDecoder& operator=(const VideoStreamDecoder&) = delete;

    // Opens asynchronously. State::Error and last_error() report connection failures.
    void start(const std::string& uri, const std::string& type = "rtsp");
    void stop();
    State state() const;
    std::string last_error() const;
    bool take_frame(std::vector<unsigned char>& rgb);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Slic3r::GUI
#endif
