#pragma once

#include <wx/webview.h>
#include <string>

namespace Slic3r::GUI {
    void setup_webview_with_credentials(wxWebView* web_view, const std::string& username, const std::string& password);
    void remove_webview_credentials(wxWebView* web_view);
    void delete_cookies(wxWebView* web_view, const std::string& url);
}

