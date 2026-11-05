// PrusaSlicer is released under the terms of the AGPLv3 or higher.
#include "VideoStreamDecoder.hpp"
#ifdef __APPLE__
#include "CameraDevices.hpp"
#endif

extern "C" {
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/log.h>
#include <libswscale/swscale.h>
}
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace Slic3r::GUI {
namespace {
using Clock = std::chrono::steady_clock;
int64_t now_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
}
std::string describe_error(const char* action, int code)
{
    char detail[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, detail, sizeof(detail));
    return std::string(action) + ": " + detail;
}
struct DecodeResources
{
    AVFormatContext* input = nullptr;
    AVCodecContext* codec = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    SwsContext* scaler = nullptr;
    ~DecodeResources()
    {
        sws_freeContext(scaler);
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&codec);
        avformat_close_input(&input);
    }
};
} // namespace

struct VideoStreamDecoder::Impl
{
    std::atomic<bool> cancelled{false};
    std::atomic<State> playback{State::Connecting};
    std::atomic<int64_t> deadline{0};
    std::mutex mutex;
    std::condition_variable wake;
    std::vector<unsigned char> latest;
    std::string error;
    bool fresh = false;
    std::thread worker;

    ~Impl()
    {
        cancelled = true;
        wake.notify_all();
        // The interrupt callback breaks blocked FFmpeg network reads during stop.
        if (worker.joinable())
            worker.join();
    }
    static int interrupt(void* opaque)
    {
        auto& self = *static_cast<Impl*>(opaque);
        return self.cancelled || now_ms() > self.deadline.load();
    }
    void check(int code, const char* action)
    {
        if (code < 0) {
            if (code == AVERROR_EXIT && !cancelled && now_ms() > deadline.load())
                throw std::runtime_error("The camera connection timed out.");
            throw std::runtime_error(describe_error(action, code));
        }
    }

    void run(const std::string& uri, const std::string& type)
    {
        try {
            avdevice_register_all();
            std::string input = uri;
            const AVInputFormat* format = nullptr;
            if (type == "usb") {
#ifdef __APPLE__
                format = av_find_input_format("avfoundation");
                input = camera_device_input(uri);
                if (input.empty())
                    throw std::runtime_error("The selected local camera is unavailable. Reconnect it or choose another camera.");
#elif defined(_WIN32)
                format = av_find_input_format("dshow");
                input = "video=" + uri;
#else
                format = av_find_input_format("video4linux2");
#endif
                if (!format)
                    throw std::runtime_error("Local camera capture is not available in this build.");
            }
            DecodeResources data;
            data.input = avformat_alloc_context();
            if (!data.input)
                throw std::runtime_error("Cannot allocate camera input.");
            data.input->interrupt_callback = {&Impl::interrupt, this};
            AVDictionary* options = nullptr;
            if (type == "rtsp") {
                av_dict_set(&options, "rtsp_transport", "tcp", 0);
                av_dict_set(&options, "allowed_media_types", "video", 0);
            }
            if (type == "usb")
                av_dict_set(&options, "framerate", "30", 0);
            av_dict_set(&options, "timeout", "5000000", 0);
            av_dict_set(&options, "rw_timeout", "5000000", 0);
            av_dict_set(&options, "probesize", "1048576", 0);
            av_dict_set(&options, "analyzeduration", "3000000", 0);
            deadline = now_ms() + 10000;
            const int opened = avformat_open_input(&data.input, input.c_str(), format, &options);
            av_dict_free(&options);
            check(opened, "Cannot open the camera stream");
            deadline = now_ms() + 10000;
            check(avformat_find_stream_info(data.input, nullptr), "Cannot read camera stream information");
            const AVCodec* decoder = nullptr;
            const int video = av_find_best_stream(data.input, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
            check(video, "No supported video stream was found");
            data.codec = avcodec_alloc_context3(decoder);
            if (!data.codec)
                throw std::runtime_error("Cannot allocate the camera decoder.");
            check(avcodec_parameters_to_context(data.codec, data.input->streams[video]->codecpar),
                "Cannot configure the camera decoder");
            data.codec->thread_count = 2;
            data.codec->thread_type = FF_THREAD_SLICE;
            data.codec->flags |= AV_CODEC_FLAG_LOW_DELAY;
            data.codec->max_pixels = 4096LL * 2160;
            check(avcodec_open2(data.codec, decoder, nullptr), "Cannot start the camera decoder");
            data.packet = av_packet_alloc();
            data.frame = av_frame_alloc();
            if (!data.packet || !data.frame)
                throw std::runtime_error("Cannot allocate camera frame buffers.");

            // File inputs are used by the integration test; live RTSP is paced
            // by the camera. Avoid decoding an entire local clip into one frame.
            const bool file_input = type != "usb" && (uri.compare(0, 5, "file:") == 0 || uri.find("://") == std::string::npos);
            AVRational rate = av_guess_frame_rate(data.input, data.input->streams[video], nullptr);
            const double seconds = rate.num > 0 && rate.den > 0 ? double(rate.den) / rate.num : 0.04;
            const auto period = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(seconds));
            auto next_frame = Clock::now();
            std::vector<unsigned char> rgb(width * height * 3);
            auto receive_frames = [&]() {
                int result = AVERROR(EAGAIN);
                while (!cancelled && (result = avcodec_receive_frame(data.codec, data.frame)) >= 0) {
                    if (data.frame->width <= 0 || data.frame->height <= 0)
                        throw std::runtime_error("The camera returned an invalid frame.");
                    data.scaler = sws_getCachedContext(data.scaler, data.frame->width, data.frame->height,
                        AVPixelFormat(data.frame->format), width, height, AV_PIX_FMT_RGB24,
                        SWS_BILINEAR, nullptr, nullptr, nullptr);
                    if (!data.scaler)
                        throw std::runtime_error("Cannot convert the camera video frame.");
                    const int space = data.frame->colorspace == AVCOL_SPC_BT709 ? SWS_CS_ITU709 : SWS_CS_DEFAULT;
                    const int* coefficients = sws_getCoefficients(space);
                    sws_setColorspaceDetails(data.scaler, coefficients, data.frame->color_range == AVCOL_RANGE_JPEG,
                        coefficients, 1, 0, 1 << 16, 1 << 16);
                    uint8_t* planes[] = {rgb.data(), nullptr, nullptr, nullptr};
                    int strides[] = {int(width * 3), 0, 0, 0};
                    check(sws_scale(data.scaler, data.frame->data, data.frame->linesize, 0,
                        data.frame->height, planes, strides), "Cannot scale the camera frame");
                    av_frame_unref(data.frame);
                    if (file_input) {
                        std::unique_lock<std::mutex> guard(mutex);
                        wake.wait_until(guard, next_frame, [this] { return cancelled.load(); });
                        next_frame += period;
                    }
                    if (cancelled)
                        return;
                    {
                        std::lock_guard<std::mutex> guard(mutex);
                        latest = rgb;
                        fresh = true;
                    }
                    playback = State::Playing;
                }
                if (!cancelled && result != AVERROR(EAGAIN) && result != AVERROR_EOF)
                    check(result, "Cannot decode the camera video");
            };
            while (!cancelled) {
                deadline = now_ms() + 8000;
                const int read = av_read_frame(data.input, data.packet);
                if (read == AVERROR(EAGAIN)) {
                    std::unique_lock<std::mutex> guard(mutex);
                    wake.wait_for(guard, std::chrono::milliseconds(5), [this] { return cancelled.load(); });
                    continue;
                }
                if (read == AVERROR_EOF) {
                    avcodec_send_packet(data.codec, nullptr);
                    receive_frames();
                    if (!cancelled)
                        playback = State::Ended;
                    return;
                }
                check(read, "Camera connection interrupted");
                if (data.packet->stream_index == video) {
                    int sent = avcodec_send_packet(data.codec, data.packet);
                    if (sent == AVERROR(EAGAIN)) {
                        receive_frames();
                        sent = avcodec_send_packet(data.codec, data.packet);
                    }
                    check(sent, "Cannot read the camera video packet");
                    receive_frames();
                }
                av_packet_unref(data.packet);
            }
        } catch (const std::exception& failure) {
            if (!cancelled) {
                {
                    std::lock_guard<std::mutex> guard(mutex);
                    error = failure.what();
                }
                playback = State::Error;
            }
        }
    }
};

VideoStreamDecoder::VideoStreamDecoder() = default;
VideoStreamDecoder::~VideoStreamDecoder() = default;

void VideoStreamDecoder::start(const std::string& uri, const std::string& type)
{
    if (uri.empty())
        throw std::invalid_argument("Enter a camera stream address.");
    stop();
    static std::once_flag initialized;
    std::call_once(initialized, [] {
        // Keep URLs and possible credentials out of application console logs.
        av_log_set_level(AV_LOG_QUIET);
        if (avformat_network_init() < 0)
            throw std::runtime_error("Cannot initialize camera networking.");
    });
    auto impl = std::make_unique<Impl>();
    auto* worker = impl.get();
    impl->worker = std::thread([worker, uri, type] { worker->run(uri, type); });
    m_impl = std::move(impl);
}
void VideoStreamDecoder::stop() { m_impl.reset(); }
VideoStreamDecoder::State VideoStreamDecoder::state() const
{
    return m_impl ? m_impl->playback.load() : State::Stopped;
}
std::string VideoStreamDecoder::last_error() const
{
    if (!m_impl)
        return {};
    std::lock_guard<std::mutex> guard(m_impl->mutex);
    return m_impl->error;
}
bool VideoStreamDecoder::take_frame(std::vector<unsigned char>& rgb)
{
    if (!m_impl)
        return false;
    std::lock_guard<std::mutex> guard(m_impl->mutex);
    if (!m_impl->fresh)
        return false;
    rgb = m_impl->latest;
    m_impl->fresh = false;
    return true;
}
} // namespace Slic3r::GUI
