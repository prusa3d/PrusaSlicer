// Standalone integration test for the embedded FFmpeg decoder.
// Supply a changing video URI as argv[1]; no external player is required.
#include "slic3r/Utils/VideoStreamDecoder.hpp"
extern "C" {
#include <libavdevice/avdevice.h>
}
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <algorithm>
#include <filesystem>

using Slic3r::GUI::VideoStreamDecoder;

static void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

int main(int argc, char** argv)
{
    try {
        const std::string type = argc > 2 ? argv[2] : "rtsp";
        require(argc >= 2, "Usage: decoder_smoke <video URI>");
        avdevice_register_all();
#ifdef __APPLE__
        const char* local_backend = "avfoundation";
#elif defined(_WIN32)
        const char* local_backend = "dshow";
#else
        const char* local_backend = "video4linux2";
#endif
        require(av_find_input_format(local_backend) != nullptr,
            "The platform camera capture backend must be included in the build");
        VideoStreamDecoder decoder;
        std::vector<unsigned char> rgb;
        require(decoder.state() == VideoStreamDecoder::State::Stopped, "Initial state");
        require(!decoder.take_frame(rgb), "No frame before start");
        bool rejected = false;
        try { decoder.start(""); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "An empty URI must report an error");
        require(decoder.state() == VideoStreamDecoder::State::Stopped, "Failed start must clean up");
        const auto missing = std::filesystem::temp_directory_path() /
            ("prusa-camera-missing-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        decoder.start(missing.string());
        const auto failed_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (decoder.state() == VideoStreamDecoder::State::Connecting &&
            std::chrono::steady_clock::now() < failed_deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        require(decoder.state() == VideoStreamDecoder::State::Error, "Unavailable input reports an error");
        require(!decoder.last_error().empty(), "Failed input supplies an error message");
        decoder.stop();
        decoder.start("__prusa_camera_missing_device__", "usb");
        const auto device_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (decoder.state() == VideoStreamDecoder::State::Connecting &&
            std::chrono::steady_clock::now() < device_deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        require(decoder.state() == VideoStreamDecoder::State::Error,
            "A missing USB camera must report an error without blocking");
        decoder.stop();
        const auto cancel_started = std::chrono::steady_clock::now();
        decoder.start(argv[1], type);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        decoder.stop();
        require(std::chrono::steady_clock::now() - cancel_started < std::chrono::seconds(3),
            "Cancelling a connection must complete promptly");
        for (int run = 0; run < 3; ++run) {
            decoder.start(argv[1], type);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
            std::vector<unsigned char> first;
            bool changed = false;
            int count = 0;
            while (std::chrono::steady_clock::now() < deadline && (count < 3 || !changed)) {
                if (decoder.take_frame(rgb)) {
                    require(rgb.size() == VideoStreamDecoder::width * VideoStreamDecoder::height * 3, "RGB frame size");
                    require(std::any_of(rgb.begin(), rgb.end(), [](unsigned char c) { return c != 0; }), "Non-black fixture");
                    if (first.empty()) first = rgb;
                    else if (rgb != first) changed = true;
                    ++count;
                }
                if (decoder.state() == VideoStreamDecoder::State::Error)
                    throw std::runtime_error(decoder.last_error());
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            if (count < 3 || !changed)
                throw std::runtime_error("Expected changing video frames; received " + std::to_string(count) +
                    ", changed=" + std::to_string(changed));
            decoder.stop();
            decoder.stop();
            require(decoder.state() == VideoStreamDecoder::State::Stopped, "Stopped state");
            require(!decoder.take_frame(rgb), "No stale frames after stop");
            std::cout << "Playback/reconnect cycle " << run + 1 << " passed (" << count << " frames)\n";
        }
        // Exercise destruction while the decoder worker is active.
        { VideoStreamDecoder live; live.start(argv[1], type);
          std::this_thread::sleep_for(std::chrono::milliseconds(300)); }
        std::cout << "Decoder smoke test passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
