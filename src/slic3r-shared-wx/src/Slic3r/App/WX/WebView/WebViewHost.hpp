#pragma once

#include <wx/event.h>
#include <wx/timer.h>

#include <functional>
#include <memory>
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

    /**
     * @brief Runs fn once the web view is created (WebView2 creates its native web view
     * asynchronously), right away when it is. Dropped if the web view never comes up.
     */
    void run_when_ready(std::function<void()> fn);

    /**
     * @brief Sets the cookie of a SetCookie command once the web view is created, then calls
     * done, also when the cookie cannot be set. done is dropped when this host is gone.
     */
    void set_cookie(const std::string& command_data, std::function<void()> done);

    /** @brief Deletes the cookie of a DeleteCookie command once the web view is created. */
    void delete_cookie(const std::string& command_data);

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
    std::vector<std::function<void()>> m_when_ready;
    /** @brief Expires with this host, late completions of cookie operations check it. */
    std::shared_ptr<char> m_alive{std::make_shared<char>(0)};
};

} // namespace Slic3r::App::WX::WebView
