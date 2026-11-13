#pragma once

#include "Slic3r/App/Browser/BrowserCookie.hpp"

#include <wx/webview.h>
#include <string>
#include <atomic>
#include <functional>
namespace Slic3r::App::WX::WebView {
    void setup_webview_with_credentials(wxWebView* web_view, const std::string& username, const std::string& password);
    void remove_webview_credentials(wxWebView* web_view);
    void delete_cookies(wxWebView* web_view, const std::string& url);
    void delete_cookies_with_counter(wxWebView* web_view, const std::string& url, std::atomic<size_t>& counter);
    /**
     * @brief Deletes the cookie `name` that the server at `url` set, no other cookie. Works on the
     * cookie store shared by the app's web views and keeps working after web_view is destroyed
     * (macOS, Linux). On Windows the request is sent to WebView2 before returning.
     */
    void delete_cookie(wxWebView* web_view, const std::string& url, const std::string& name);
    /**
     * @brief Puts the cookie into the cookie store shared by the app's web views, as if
     * cookie.url had set it (host-only session cookie with its path, HttpOnly, Secure and
     * SameSite). Calls done on the main thread once the store has it, or once it is clear it
     * cannot be set (then maybe right away). Needs the created web view
     * (WebViewHost::run_when_ready).
     */
    void set_cookie(
        wxWebView* web_view,
        const App::Browser::BrowserCookie& cookie,
        std::function<void()> done
    );
    void load_request(wxWebView* web_view, const std::string& address, const std::string& token);
    /**
     * @brief Run JavaScript through the native engine, bypassing wxWebView::RunScript.
     *
     * wx wraps every script in eval("..."), which pages with a Content-Security-Policy
     * lacking 'unsafe-eval' refuse (EvalError). This runs the script verbatim.
     * Fire-and-forget: no result is returned; script exceptions are logged.
     */
    void run_script(wxWebView* web_view, const std::string& javascript);
    void setup_webview_devtools(wxWebView* web_view);
}

