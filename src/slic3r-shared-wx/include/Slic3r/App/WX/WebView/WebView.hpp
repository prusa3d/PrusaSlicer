#pragma once

#include <vector>
#include <string>

class wxWebView;
class wxWindow;
class wxString;

namespace Slic3r::App::WX::WebView {

wxWebView* web_view_new();
void web_view_create(
    wxWebView* webview,
    wxWindow* parent,
    const wxString& url,
    const std::vector<std::string>& message_handlers
);

} // namespace Slic3r::App::WX::WebView
