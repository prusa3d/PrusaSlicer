#include "Slic3r/App/WX/WebView/WebViewHost.hpp"

#include "WebViewPlatformUtils.hpp"

#include "Slic3r/App/WX/WebView/WebView.hpp"
#include "Slic3r/App/Browser/BrowserCookie.hpp"
#include "Slic3r/App/Browser/BrowserLogicCommand.hpp"
#include "Slic3r/App/ConnectLocalTrust.hpp"
#include "Slic3r/Biz/Network/ServiceConfig.hpp"

#include "Slic3r/Assert.hpp"
#include "Slic3r/Log.hpp"

#include <wx/webview.h>
#include <wx/sizer.h>
#include <wx/window.h>

#include <mutex>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Slic3r::App::WX::WebView {

namespace {
/** @brief The hosts with a web view, main thread only. */
std::unordered_set<WebViewHost*> g_hosts;

/** @brief Tells every web view that the Connect Local trust changed. */
void install_server_trust_listener()
{
    static std::once_flag installed;
    std::call_once(
        installed,
        []()
        {
            ConnectLocalTrust::set_apply_listener(
                []()
                {
                    for (WebViewHost* host : std::vector(g_hosts.begin(), g_hosts.end())) {
                        if (wxWebView* web_view = host->get()) {
                            webview_server_trust_changed(web_view);
                        }
                    }
                }
            );
        }
    );
}
} // namespace

WebViewHost::WebViewHost()
{
    m_watchdog.SetOwner(this);
    Bind(wxEVT_TIMER, &WebViewHost::on_watchdog, this);
}

WebViewHost::~WebViewHost()
{
    g_hosts.erase(this);
    m_watchdog.Stop();
}

bool WebViewHost::create(
    wxWindow* parent,
    wxSizer* sizer,
    const wxString& url,
    const std::vector<std::string>& message_handlers
)
{
    DEBUG_ASSERT(parent);
    DEBUG_ASSERT(sizer);
    DEBUG_ASSERT(m_state == State::Pending);

    m_parent = parent;
    m_sizer  = sizer;

    m_web_view = web_view_new();
    if (!m_web_view) {
        fail();
        return false;
    }

    if (!web_view_create(m_web_view, parent, url, message_handlers)) {
        m_web_view->Destroy();
        m_web_view = nullptr;
        fail();
        return false;
    }

    m_state = State::Creating;
    setup_webview_server_trust(m_web_view);
    install_server_trust_listener();
    g_hosts.insert(this);

    if (Biz::Network::ServiceConfig::instance().webdev_enabled()) {
        setup_webview_devtools(m_web_view);
    }
    sizer->Add(m_web_view, wxSizerFlags().Expand().Proportion(1));

    m_watchdog.StartOnce(WEBVIEW_CREATE_TIMEOUT_MS);
    return true;
}

void WebViewHost::notify_created()
{
    m_watchdog.Stop();
    if (m_state != State::Creating) {
        return;
    }
    m_state = State::Ready;
    // WebView2 has its native web view only now.
    setup_webview_server_trust(m_web_view);
    for (const std::function<void()>& fn : std::exchange(m_when_ready, {})) {
        fn();
    }
}

void WebViewHost::run_when_ready(std::function<void()> fn)
{
    if (m_state == State::Ready) {
        fn();
    } else if (m_state != State::Failed) {
        m_when_ready.push_back(std::move(fn));
    }
}

void WebViewHost::set_cookie(const std::string& command_data, std::function<void()> done)
{
    run_when_ready(
        [this, command_data, done = std::move(done)]()
        {
            const std::optional<App::Browser::BrowserCookie> cookie =
                App::Browser::parse_cookie_set_command_data(command_data);
            if (!cookie) {
                SPDLOG_ERROR("Invalid SetCookie command.");
                done();
                return;
            }
            WebView::set_cookie(
                m_web_view,
                *cookie,
                [alive = std::weak_ptr<char>(m_alive), done]()
                {
                    if (alive.lock()) {
                        done();
                    }
                }
            );
        }
    );
}

void WebViewHost::delete_cookie(const std::string& command_data)
{
    run_when_ready(
        [this, command_data]()
        {
            const auto [name, url] = App::Browser::parse_cookie_command_data(command_data);
            if (!name.empty() && !url.empty()) {
                WebView::delete_cookie(m_web_view, url, name);
            }
        }
    );
}

bool WebViewHost::notify_error()
{
    if (m_state != State::Creating || m_web_view->GetNativeBackend() != nullptr) {
        return false;
    }
    fail();
    return true;
}

wxWebView* WebViewHost::get() const
{
    return m_state == State::Creating || m_state == State::Ready ? m_web_view : nullptr;
}

void WebViewHost::on_watchdog(wxTimerEvent& evt)
{
    if (m_state != State::Creating) {
        return;
    }
    SPDLOG_ERROR("Web view was not created within {} ms.", WEBVIEW_CREATE_TIMEOUT_MS);
    fail();
}

void WebViewHost::fail()
{
    if (m_state == State::Failed) {
        return;
    }
    m_state = State::Failed;
    m_watchdog.Stop();
    m_when_ready.clear();
    web_view_show_unavailable_message(m_parent, m_sizer, m_web_view);
}

} // namespace Slic3r::App::WX::WebView
