#pragma once

#include <wx/event.h>
#include <wx/timer.h>

#include <string>
#include <vector>

class wxWebView;
class wxWindow;
class wxSizer;
class wxString;

namespace Slic3r::App::WX::WebView {

class WebViewHost : public wxEvtHandler
{
public:
    WebViewHost();
    ~WebViewHost() override;

    WebViewHost(const WebViewHost&) = delete;
    WebViewHost& operator=(const WebViewHost&) = delete;

    /** Creates the web view and adds it to the sizer. On failure shows the unavailable message in its place and returns false. */
    bool create(
        wxWindow* parent,
        wxSizer* sizer,
        const wxString& url,
        const std::vector<std::string>& message_handlers
    );

    void notify_created();

    /** Returns true when the error means the web view never came up, in which case the caller must not continue. */
    bool notify_error();

    /** Returns the web view only while it may be driven. This is the availability check every caller makes. */
    wxWebView* get() const;

    explicit operator bool() const
    {
        return get() != nullptr;
    }

    /** Returns the web view in any state. Only for teardown, which has to reach a failed web view as well. */
    wxWebView* get_unguarded() const
    {
        return m_web_view;
    }

    bool is_pending() const
    {
        return m_state == State::Pending;
    }

private:
    enum class State
    {
        Pending,
        Creating,
        Ready,
        Failed
    };

    void on_watchdog(wxTimerEvent& evt);
    void fail();

    wxWebView* m_web_view{nullptr};
    wxWindow* m_parent{nullptr};
    wxSizer* m_sizer{nullptr};
    State m_state{State::Pending};

    wxTimer m_watchdog;
};

} // namespace Slic3r::App::WX::WebView
