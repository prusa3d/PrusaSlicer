#include "TestMode.hpp"
#include "TestDialogs.hpp"

#include <Slic3r/App/Navigator.hpp>
#include <Slic3r/App/Platform/AbstractRenderCanvas.hpp>
#include <Slic3r/App/Platform/WX/DpiScale.hpp>
#include <Slic3r/App/Platform/WX/WXRenderCanvas.hpp>
#include <Slic3r/App/Yoga/RootItem.hpp>
#include <Slic3r/App/Yoga/ScrollArea.hpp>
#include <Slic3r/App/Yoga/TreeDump.hpp>
#include <Slic3r/Biz/Platform/PlatformServices.hpp>
#include <Slic3r/Log.hpp>

#include <nlohmann/json.hpp>

#include <wx/display.h>
#include <wx/event.h>
#include <wx/image.h>
#include <wx/mstream.h>
#include <wx/weakref.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <future>
#include <map>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace Slic3r::App::Desktop::Testing {


namespace {

constexpr int api_version         = 1;
constexpr auto main_timeout       = std::chrono::seconds(5);
constexpr auto status_timeout     = std::chrono::seconds(1);
constexpr auto wait_poll_interval = std::chrono::milliseconds(5);

Yoga::RootItem* rendered_root(Navigator* navigator)
{
    Yoga::RootItem* root = navigator != nullptr ? navigator->current_root_item() : nullptr;
    return root != nullptr && root->last_rendered_frame() > 0 ? root : nullptr;
}

std::string dump_json(const nlohmann::json& json)
{
    return json.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

struct InputError : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

struct Modifiers
{
    bool ctrl{false};
    bool shift{false};
    bool alt{false};
    bool raw_ctrl{false};
};

struct KeySpec
{
    int key_code{WXK_NONE};
    wxChar uni_char{WXK_NONE};
};

const std::map<std::string_view, int> named_keys{
    {"Enter", WXK_RETURN},   {"Escape", WXK_ESCAPE}, {"Tab", WXK_TAB},
    {"Backspace", WXK_BACK}, {"Delete", WXK_DELETE}, {"Insert", WXK_INSERT},
    {"Space", WXK_SPACE},    {"Left", WXK_LEFT},     {"Right", WXK_RIGHT},
    {"Up", WXK_UP},          {"Down", WXK_DOWN},     {"Home", WXK_HOME},
    {"End", WXK_END},        {"PageUp", WXK_PAGEUP}, {"PageDown", WXK_PAGEDOWN},
    {"F1", WXK_F1},          {"F2", WXK_F2},         {"F3", WXK_F3},
    {"F4", WXK_F4},          {"F5", WXK_F5},         {"F6", WXK_F6},
    {"F7", WXK_F7},          {"F8", WXK_F8},         {"F9", WXK_F9},
    {"F10", WXK_F10},        {"F11", WXK_F11},       {"F12", WXK_F12},
};

nlohmann::json parse_body(const HttpRequest& request)
{
    nlohmann::json body = nlohmann::json::parse(request.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
        throw InputError("Request body must be a JSON object");
    }
    return body;
}

double number_field(const nlohmann::json& body, const std::string& name)
{
    if (!body.contains(name) || !body[name].is_number()) {
        throw InputError("Field '" + name + "' must be a number");
    }
    return body[name].get<double>();
}

double number_field(const nlohmann::json& body, const std::string& name, double default_value)
{
    return body.contains(name) ? number_field(body, name) : default_value;
}

std::string string_field(const nlohmann::json& body, const std::string& name)
{
    if (!body.contains(name) || !body[name].is_string()) {
        throw InputError("Field '" + name + "' must be a string");
    }
    return body[name].get<std::string>();
}

std::string
string_field(const nlohmann::json& body, const std::string& name, const std::string& default_value)
{
    return body.contains(name) ? string_field(body, name) : default_value;
}

int frames_field(const nlohmann::json& body, int default_value)
{
    if (!body.contains("frames")) {
        return default_value;
    }
    if (!body["frames"].is_number_integer() || body["frames"].get<int>() < 1
        || body["frames"].get<int>() > 10)
    {
        throw InputError("Field 'frames' must be an integer from 1 to 10");
    }
    return body["frames"].get<int>();
}

Modifiers parse_modifiers(const nlohmann::json& body)
{
    Modifiers modifiers;
    if (!body.contains("mods")) {
        return modifiers;
    }
    if (!body["mods"].is_array()) {
        throw InputError("Field 'mods' must be an array");
    }
    for (const nlohmann::json& mod : body["mods"]) {
        if (!mod.is_string()) {
            throw InputError("Field 'mods' must contain strings");
        }
        const std::string& name = mod.get_ref<const std::string&>();
        if (name == "ctrl") {
            modifiers.ctrl = true;
        } else if (name == "shift") {
            modifiers.shift = true;
        } else if (name == "alt") {
            modifiers.alt = true;
        } else if (name == "rawctrl") {
            modifiers.raw_ctrl = true;
        } else {
            throw InputError("Unknown modifier '" + name + "'");
        }
    }
    return modifiers;
}

void apply_modifiers(wxKeyboardState& state, const Modifiers& modifiers)
{
    state.SetControlDown(modifiers.ctrl);
    state.SetShiftDown(modifiers.shift);
    state.SetAltDown(modifiers.alt);
    if (modifiers.raw_ctrl) {
        state.SetRawControlDown(true);
    }
}

std::vector<char32_t> decode_utf8(std::string_view text)
{
    std::vector<char32_t> result;
    for (size_t i = 0; i < text.size();) {
        const auto lead  = static_cast<unsigned char>(text[i]);
        const int length = lead < 0x80 ?
            1 :
            (lead >> 5) == 0x6 ?
            2 :
            (lead >> 4) == 0xE ?
            3 :
            (lead >> 3) == 0x1E ?
            4 :
            0;
        if (length == 0 || i + length > text.size()) {
            throw InputError("Invalid UTF-8");
        }
        char32_t code_point = length == 1 ? lead : lead & (0x7F >> length);
        for (int k = 1; k < length; ++k) {
            const auto continuation = static_cast<unsigned char>(text[i + k]);
            if ((continuation & 0xC0) != 0x80) {
                throw InputError("Invalid UTF-8");
            }
            code_point = (code_point << 6) | (continuation & 0x3F);
        }
        result.push_back(code_point);
        i += length;
    }
    return result;
}

KeySpec resolve_key(const std::string& key)
{
    if (const auto it = named_keys.find(key); it != named_keys.end()) {
        return KeySpec{it->second, static_cast<wxChar>(it->second < 128 ? it->second : WXK_NONE)};
    }

    const std::vector<char32_t> code_points = decode_utf8(key);
    if (code_points.size() != 1 || code_points.front() < 0x20 || code_points.front() == 0x7F) {
        throw InputError("Unknown key '" + key + "'");
    }

    char32_t code_point = code_points.front();
    if (code_point >= 'a' && code_point <= 'z') {
        code_point = code_point - 'a' + 'A';
    }
    if (code_point < 128) {
        return KeySpec{static_cast<int>(code_point), static_cast<wxChar>(code_point)};
    }
    return KeySpec{WXK_NONE, static_cast<wxChar>(code_point)};
}

int to_physical(Platform::WX::WXRenderCanvas& canvas, double dip)
{
#if defined(__WXGTK__)
    return static_cast<int>(std::lround(dip * Platform::WX::get_dpi_scale(&canvas).dpi_scale));
#else
    return canvas.FromDIP(static_cast<int>(std::lround(dip)));
#endif
}

Yoga::Object* find_object(Yoga::Object& parent, const std::string& id)
{
    for (Yoga::Object* child : parent.objects()) {
        if (child->object_name() == id) {
            return child;
        }
        if (Yoga::Object* found = find_object(*child, id)) {
            return found;
        }
    }
    return nullptr;
}

HttpResponse frame_response(int frame)
{
    return HttpResponse{200, dump_json({{"frame", frame}})};
}

} // namespace

struct TestMode::State : Platform::IFrameRenderedListener
{
    void on_frame_rendered(int rendered_frame) override
    {
        frame.store(rendered_frame);
    }

    wxWeakRef<Platform::WX::WXRenderCanvas> canvas;
    Navigator* navigator{nullptr};
    std::atomic<int> frame{0};
    std::array<bool, 3> buttons{};
};

TestMode::TestMode(
    uint16_t port,
    std::string password,
    std::shared_ptr<TestDialogs> dialogs
) :
    m_state(std::make_shared<State>()),
    m_dialogs(std::move(dialogs)),
    m_server(port, std::move(password))
{}

void TestMode::attach(Platform::WX::WXRenderCanvas& canvas, Navigator& navigator)
{
    m_state->canvas    = &canvas;
    m_state->navigator = &navigator;
    canvas.add_listener<Platform::IFrameRenderedListener>(m_state.get());
}

TestMode::~TestMode()
{
    m_server.stop();
    if (m_state->canvas) {
        m_state->canvas->remove_listener<Platform::IFrameRenderedListener>(m_state.get());
    }
    m_state->canvas    = nullptr;
    m_state->navigator = nullptr;
}

bool TestMode::start()
{
    m_server.add_route(
        "GET",
        "/status",
        [this](const HttpRequest& request) { return handle_status(request); }
    );
    m_server.add_route(
        "GET",
        "/tree",
        [this](const HttpRequest& request) { return handle_tree(request); }
    );
    m_server.add_route(
        "GET",
        "/screenshot",
        [this](const HttpRequest& request) { return handle_screenshot(request); }
    );
    m_server.add_route(
        "POST",
        "/mouse",
        [this](const HttpRequest& request) { return handle_mouse(request); }
    );
    m_server.add_route(
        "POST",
        "/key",
        [this](const HttpRequest& request) { return handle_key(request); }
    );
    m_server.add_route(
        "POST",
        "/text",
        [this](const HttpRequest& request) { return handle_text(request); }
    );
    m_server.add_route(
        "POST",
        "/scroll",
        [this](const HttpRequest& request) { return handle_scroll(request); }
    );
    m_server.add_route(
        "GET",
        "/dialogs",
        [this](const HttpRequest& request) { return handle_dialogs(request); }
    );
    m_server.add_route(
        "POST",
        "/dialogs/answers",
        [this](const HttpRequest& request) { return handle_dialog_answers(request); }
    );
    m_server.add_route(
        "DELETE",
        "/dialogs",
        [this](const HttpRequest& request) { return handle_clear_dialogs(request); }
    );

    if (!m_server.start()) {
        SPDLOG_ERROR("TESTING MODE: cannot listen on 127.0.0.1:{}", m_server.port());
        return false;
    }
    SPDLOG_INFO("TESTING MODE: listening on 127.0.0.1:{}", m_server.port());
    return true;
}

HttpResponse
TestMode::run_on_main(MainThreadTask task, std::chrono::steady_clock::time_point deadline)
{
    auto promise                     = std::make_shared<std::promise<HttpResponse>>();
    std::future<HttpResponse> future = promise->get_future();
    // Whoever sets it first decides: the main thread runs the task, or the server gave up on it.
    auto claimed = std::make_shared<std::atomic<bool>>(false);

    const bool dispatched =
        Biz::Platform::PlatformServices::instance()
            .main_thread_dispatcher()
            .dispatch_on_main_thread(
                [promise, claimed, state = m_state, task = std::move(task)]()
                {
                    if (claimed->exchange(true)) {
                        return;
                    }
                    try {
                        if (!state->canvas) {
                            promise->set_value(error_response(503, "Render canvas is gone"));
                            return;
                        }
                        promise->set_value(task(*state->canvas));
                    } catch (const std::exception& e) {
                        promise->set_value(error_response(500, e.what()));
                    } catch (...) {
                        promise->set_value(error_response(500, "Unknown error"));
                    }
                }
            );
    if (!dispatched) {
        return error_response(503, "Main thread dispatcher is closed");
    }

    while (future.wait_for(wait_poll_interval) != std::future_status::ready) {
        if (m_server.stopping()) {
            return error_response(503, "Testing mode is shutting down");
        }
        if (std::chrono::steady_clock::now() >= deadline && !claimed->exchange(true)) {
            return error_response(504, "Main thread did not answer in time");
        }
    }
    return future.get();
}

HttpResponse TestMode::run_input(MainThreadTask task, int frames)
{
    const auto deadline         = std::chrono::steady_clock::now() + main_timeout;
    const HttpResponse response = run_on_main(
        [task = std::move(task), state = m_state](Platform::WX::WXRenderCanvas& canvas)
        {
            if (rendered_root(state->navigator) == nullptr) {
                return error_response(503, "UI is not ready");
            }
            const int frame             = state->frame.load();
            const HttpResponse injected = task(canvas);
            if (injected.status != 200) {
                return injected;
            }
            canvas.request_render();
            return frame_response(frame);
        },
        deadline
    );
    if (response.status != 200) {
        return response;
    }
    return wait_for_frame(
        nlohmann::json::parse(response.body)["frame"].get<int>() + frames,
        deadline
    );
}

HttpResponse
TestMode::wait_for_frame(int target_frame, std::chrono::steady_clock::time_point deadline)
{
    int requested_at = -1;
    while (true) {
        const int frame = m_state->frame.load();
        if (frame >= target_frame) {
            return frame_response(frame);
        }
        if (m_server.stopping()) {
            return error_response(503, "Testing mode is shutting down");
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return error_response(504, "Frames did not advance in time");
        }
        if (frame != requested_at) {
            requested_at = frame;
            Biz::Platform::PlatformServices::instance()
                .main_thread_dispatcher()
                .dispatch_on_main_thread(
                    [state = m_state]()
                    {
                        if (state->canvas) {
                            state->canvas->request_render();
                        }
                    }
                );
        }
        std::this_thread::sleep_for(wait_poll_interval);
    }
}

HttpResponse TestMode::handle_status(const HttpRequest&)
{
    const HttpResponse response = run_on_main(
        [state = m_state](Platform::WX::WXRenderCanvas& canvas)
        {
            nlohmann::json main{
                {"ready", rendered_root(state->navigator) != nullptr},
                {"screen", nullptr},
                {"window", nullptr},
            };
            if (wxWindow* window = wxGetTopLevelParent(&canvas)) {
                const int display_index = wxDisplay::GetFromWindow(window);
                const wxSize screen =
                    wxDisplay(display_index == wxNOT_FOUND ? 0u : unsigned(display_index))
                        .GetGeometry()
                        .GetSize();
                const wxSize size = window->GetSize();
                main["screen"]    = {screen.GetWidth(), screen.GetHeight()};
                main["window"]    = {size.GetWidth(), size.GetHeight()};
            }
            return HttpResponse{200, dump_json(main)};
        },
        std::chrono::steady_clock::now() + status_timeout
    );

    nlohmann::json status{
        {"version", api_version},
        {"ready", false},
        {"frame", m_state->frame.load()},
        {"screen", nullptr},
        {"window", nullptr},
    };
    if (response.status == 200) {
        status.update(nlohmann::json::parse(response.body));
    }
    return HttpResponse{200, dump_json(status)};
}

HttpResponse TestMode::handle_tree(const HttpRequest& request)
{
    const auto it = request.query.find("include_hidden");
    const bool include_hidden =
        it != request.query.end() && (it->second == "1" || it->second == "true");

    auto tree                   = std::make_shared<nlohmann::json>();
    const HttpResponse response = run_on_main(
        [include_hidden, tree, state = m_state](Platform::WX::WXRenderCanvas&)
        {
            const Yoga::RootItem* root = rendered_root(state->navigator);
            if (root == nullptr) {
                return error_response(503, "UI is not ready");
            }
            *tree = Yoga::dump_tree(*root, include_hidden);
            return HttpResponse{};
        },
        std::chrono::steady_clock::now() + main_timeout
    );
    if (response.status != 200) {
        return response;
    }
    return HttpResponse{200, dump_json(*tree)};
}

HttpResponse TestMode::handle_screenshot(const HttpRequest&)
{
    auto shot                   = std::make_shared<std::optional<Domain::Image>>();
    const HttpResponse response = run_on_main(
        [shot](Platform::WX::WXRenderCanvas& canvas)
        {
            *shot = canvas.screenshot();
            return shot->has_value() ? HttpResponse{} :
                                       error_response(503, "No frame was rendered");
        },
        std::chrono::steady_clock::now() + main_timeout
    );
    if (response.status != 200) {
        return response;
    }

    // wx wants RGB with the top row first; the frame is RGBA with the bottom row first.
    const Domain::Image& frame = **shot;
    wxImage image(frame.width(), frame.height(), false);
    unsigned char* out = image.GetData();
    for (int y = frame.height() - 1; y >= 0; --y) {
        const uint8_t* in = frame.pixels.data() + size_t(y) * frame.width() * 4;
        for (int x = 0; x < frame.width(); ++x, in += 4, out += 3) {
            std::copy_n(in, 3, out);
        }
    }
    image.SetOption(wxIMAGE_OPTION_PNG_COMPRESSION_LEVEL, 1);
    wxMemoryOutputStream stream;
    if (!image.SaveFile(stream, wxBITMAP_TYPE_PNG)) {
        return error_response(500, "Cannot encode the screenshot");
    }
    std::string png(stream.GetSize(), '\0');
    stream.CopyTo(png.data(), png.size());
    return HttpResponse{200, std::move(png), "image/png"};
}

HttpResponse TestMode::handle_mouse(const HttpRequest& request)
{
    wxEventType event_type{wxEVT_NULL};
    int button_index{0};
    double x{0};
    double y{0};
    double delta{0};
    bool horizontal{false};
    Modifiers modifiers;
    std::string action;
    int frames{default_frames};

    try {
        const nlohmann::json body = parse_body(request);
        action                    = string_field(body, "action");
        x                         = number_field(body, "x");
        y                         = number_field(body, "y");
        modifiers                 = parse_modifiers(body);
        frames                    = frames_field(body, default_frames);

        const std::string button = string_field(body, "button", "left");
        if (button == "left") {
            button_index = 0;
        } else if (button == "right") {
            button_index = 1;
        } else if (button == "middle") {
            button_index = 2;
        } else {
            throw InputError("Unknown button '" + button + "'");
        }

        const std::array<wxEventType, 3>
            down_events{wxEVT_LEFT_DOWN, wxEVT_RIGHT_DOWN, wxEVT_MIDDLE_DOWN};
        const std::array<wxEventType, 3> up_events{wxEVT_LEFT_UP, wxEVT_RIGHT_UP, wxEVT_MIDDLE_UP};
        const std::array<wxEventType, 3>
            double_click_events{wxEVT_LEFT_DCLICK, wxEVT_RIGHT_DCLICK, wxEVT_MIDDLE_DCLICK};

        if (action == "move") {
            event_type = wxEVT_MOTION;
        } else if (action == "down") {
            event_type = down_events[button_index];
        } else if (action == "up") {
            event_type = up_events[button_index];
        } else if (action == "double_click") {
            event_type = double_click_events[button_index];
        } else if (action == "wheel") {
            event_type             = wxEVT_MOUSEWHEEL;
            delta                  = number_field(body, "delta", 1.0);
            const std::string axis = string_field(body, "axis", "vertical");
            if (axis == "horizontal") {
                horizontal = true;
            } else if (axis != "vertical") {
                throw InputError("Unknown axis '" + axis + "'");
            }
        } else {
            throw InputError("Unknown mouse action '" + action + "'");
        }
    } catch (const InputError& e) {
        return error_response(400, e.what());
    }

    return run_input(
        [state = m_state, event_type, action, button_index, x, y, delta, horizontal, modifiers](
            Platform::WX::WXRenderCanvas& canvas
        )
        {
            if (action == "down" || action == "double_click") {
                state->buttons[button_index] = true;
            } else if (action == "up") {
                state->buttons[button_index] = false;
            }

            wxMouseEvent event(event_type);
            event.SetEventObject(&canvas);
            event.SetId(canvas.GetId());
            event.SetPosition(wxPoint(to_physical(canvas, x), to_physical(canvas, y)));
            event.SetLeftDown(state->buttons[0]);
            event.SetRightDown(state->buttons[1]);
            event.SetMiddleDown(state->buttons[2]);
            apply_modifiers(event, modifiers);
            if (action == "wheel") {
                event.m_wheelRotation = static_cast<int>(std::lround(delta * 120));
                event.m_wheelDelta    = 120;
                event.m_wheelAxis = horizontal ? wxMOUSE_WHEEL_HORIZONTAL : wxMOUSE_WHEEL_VERTICAL;
            } else if (action == "double_click") {
                event.m_clickCount = 2;
            } else if (action != "move") {
                event.m_clickCount = 1;
            }

            canvas.GetEventHandler()->ProcessEvent(event);
            return HttpResponse{};
        },
        frames
    );
}

HttpResponse TestMode::handle_key(const HttpRequest& request)
{
    wxEventType event_type{wxEVT_NULL};
    KeySpec key;
    Modifiers modifiers;
    int frames{default_frames};

    try {
        const nlohmann::json body = parse_body(request);
        const std::string action  = string_field(body, "action");
        if (action == "down") {
            event_type = wxEVT_KEY_DOWN;
        } else if (action == "up") {
            event_type = wxEVT_KEY_UP;
        } else {
            throw InputError("Unknown key action '" + action + "'");
        }
        key       = resolve_key(string_field(body, "key"));
        modifiers = parse_modifiers(body);
        frames    = frames_field(body, default_frames);
    } catch (const InputError& e) {
        return error_response(400, e.what());
    }

    return run_input(
        [event_type, key, modifiers](Platform::WX::WXRenderCanvas& canvas)
        {
            wxKeyEvent event(event_type);
            event.SetEventObject(&canvas);
            event.SetId(canvas.GetId());
            event.m_keyCode = key.key_code;
            event.m_uniChar = key.uni_char;
            apply_modifiers(event, modifiers);
            canvas.GetEventHandler()->ProcessEvent(event);
            return HttpResponse{};
        },
        frames
    );
}

HttpResponse TestMode::handle_text(const HttpRequest& request)
{
    std::vector<char32_t> code_points;
    int frames{default_frames};

    try {
        const nlohmann::json body = parse_body(request);
        code_points               = decode_utf8(string_field(body, "text"));
        frames                    = frames_field(body, default_frames);
    } catch (const InputError& e) {
        return error_response(400, e.what());
    }

    return run_input(
        [code_points](Platform::WX::WXRenderCanvas& canvas)
        {
            for (const char32_t code_point : code_points) {
                wxKeyEvent event(wxEVT_CHAR);
                event.SetEventObject(&canvas);
                event.SetId(canvas.GetId());
                event.m_uniChar = static_cast<wxChar>(code_point);
                event.m_keyCode = code_point < 128 ? static_cast<int>(code_point) : WXK_NONE;
                canvas.GetEventHandler()->ProcessEvent(event);
            }
            return HttpResponse{};
        },
        frames
    );
}

HttpResponse TestMode::handle_scroll(const HttpRequest& request)
{
    std::string area_id;
    std::string item_id;

    try {
        const nlohmann::json body = parse_body(request);
        area_id                   = string_field(body, "area");
        item_id                   = string_field(body, "item");
    } catch (const InputError& e) {
        return error_response(400, e.what());
    }

    return run_input(
        [state = m_state, area_id, item_id](Platform::WX::WXRenderCanvas&)
        {
            Yoga::RootItem& root = *rendered_root(state->navigator);
            auto* area = dynamic_cast<Yoga::ScrollArea*>(find_object(root, area_id));
            if (area == nullptr) {
                return error_response(400, "No ScrollArea with id '" + area_id + "'");
            }
            Yoga::Item* item = nullptr;
            for (Yoga::Object* child : area->objects()) {
                if (child->object_name() == item_id) {
                    item = dynamic_cast<Yoga::Item*>(child);
                }
            }
            if (item == nullptr) {
                return error_response(
                    400, "'" + item_id + "' is not a direct child Item of '" + area_id + "'"
                );
            }
            area->scroll_at_item(item);
            return HttpResponse{};
        }
    );
}

// The dialog routes don't go through the main thread: TestDialogs is locked on its own, so they
// also work while the main thread is busy.
HttpResponse TestMode::handle_dialogs(const HttpRequest&)
{
    return HttpResponse{200, dump_json(m_dialogs->to_json())};
}

HttpResponse TestMode::handle_dialog_answers(const HttpRequest& request)
{
    try {
        const nlohmann::json body = parse_body(request);
        if (!body.contains("answers")) {
            throw InputError("Field 'answers' is missing");
        }
        m_dialogs->add_answers(body["answers"]);
    } catch (const InputError& e) {
        return error_response(400, e.what());
    } catch (const std::invalid_argument& e) {
        return error_response(400, e.what());
    }
    return HttpResponse{200, dump_json(m_dialogs->to_json())};
}

HttpResponse TestMode::handle_clear_dialogs(const HttpRequest&)
{
    m_dialogs->clear();
    return HttpResponse{200, dump_json(m_dialogs->to_json())};
}

} // namespace Slic3r::App::Desktop::Testing
