#pragma once

#include <wx/webview.h>
#include <string>
#include <atomic>
namespace Slic3r::App::WX::WebView {
    void setup_webview_with_credentials(wxWebView* web_view, const std::string& username, const std::string& password);
    void remove_webview_credentials(wxWebView* web_view);
    void delete_cookies(wxWebView* web_view, const std::string& url);
    void delete_cookies_with_counter(wxWebView* web_view, const std::string& url, std::atomic<size_t>& counter);
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

