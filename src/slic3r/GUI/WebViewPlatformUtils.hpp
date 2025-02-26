#pragma once

#include <wx/webview.h>
#include <string>
#include <atomic>
namespace Slic3r::GUI {
    void setup_webview_with_credentials(wxWebView* web_view, const std::string& username, const std::string& password);
    void remove_webview_credentials(wxWebView* web_view);
    void delete_cookies(wxWebView* web_view, const std::string& url);
    void delete_cookies_with_counter(wxWebView* web_view, const std::string& url, std::atomic<size_t>& counter);
    void add_request_authorization(wxWebView* web_view, const wxString& address, const std::string& token);
    void remove_request_authorization(wxWebView* web_view);
    void load_request(wxWebView* web_view, const std::string& address, const std::string& token);
}

