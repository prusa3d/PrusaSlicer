#include "Slic3r/App/WX/WebView/WebViewHost.hpp"

#include "WebViewPlatformUtils.hpp"

#include "Slic3r/App/WX/WebView/WebView.hpp"
#include "Slic3r/Biz/Network/ServiceConfig.hpp"

#include "Slic3r/Assert.hpp"
#include "Slic3r/Log.hpp"

#include <wx/webview.h>
#include <wx/sizer.h>
#include <wx/window.h>

namespace Slic3r::App::WX::WebView {

WebViewHost::WebViewHost()
{
    m_watchdog.SetOwner(this);
    Bind(wxEVT_TIMER, &WebViewHost::on_watchdog, this);
}

WebViewHost::~WebViewHost()
{
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
    web_view_show_unavailable_message(m_parent, m_sizer, m_web_view);
}

} // namespace Slic3r::App::WX::WebView
