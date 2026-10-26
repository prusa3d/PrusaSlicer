#pragma once

#include "TestServer.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace Slic3r::App {
class Navigator;
} // namespace Slic3r::App

namespace Slic3r::App::Platform::WX {
class WXRenderCanvas;
} // namespace Slic3r::App::Platform::WX

namespace Slic3r::App::Desktop::Testing {

class TestDialogs;

class TestMode
{
public:
    TestMode(uint16_t port, std::string password, std::shared_ptr<TestDialogs> dialogs);
    ~TestMode();

    TestMode(const TestMode&)            = delete;
    TestMode& operator=(const TestMode&) = delete;

    bool start();

    /**
     * @brief Connects the UI once it exists. Until then the server only answers that the UI isn't
     * ready, and the dialog routes.
     */
    void attach(Platform::WX::WXRenderCanvas& canvas, Navigator& navigator);

private:
    struct State;

    using MainThreadTask = std::function<HttpResponse(Platform::WX::WXRenderCanvas&)>;

    // Frames an input request waits for after its event, unless the request asks for fewer or more.
    static constexpr int default_frames = 2;

    // A task still queued at the deadline never runs, so late input can't arrive after a 504.
    HttpResponse
    run_on_main(MainThreadTask task, std::chrono::steady_clock::time_point deadline);

    HttpResponse run_input(MainThreadTask task, int frames = default_frames);
    HttpResponse
    wait_for_frame(int target_frame, std::chrono::steady_clock::time_point deadline);

    HttpResponse handle_status(const HttpRequest& request);
    HttpResponse handle_tree(const HttpRequest& request);
    HttpResponse handle_mouse(const HttpRequest& request);
    HttpResponse handle_key(const HttpRequest& request);
    HttpResponse handle_text(const HttpRequest& request);
    HttpResponse handle_scroll(const HttpRequest& request);
    HttpResponse handle_dialogs(const HttpRequest& request);
    HttpResponse handle_dialog_answers(const HttpRequest& request);
    HttpResponse handle_clear_dialogs(const HttpRequest& request);

    std::shared_ptr<State> m_state;
    std::shared_ptr<TestDialogs> m_dialogs;
    TestServer m_server;
};

} // namespace Slic3r::App::Desktop::Testing
