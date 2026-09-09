#pragma once

#include <vector>
#include <string>

class wxWebView;
class wxWindow;
class wxSizer;
class wxString;

namespace Slic3r::App::WX::WebView {

/** Time given to the web view backend to report a successfully created web view before it is considered unusable. */
inline constexpr int WEBVIEW_CREATE_TIMEOUT_MS = 20000;

wxWebView* web_view_new();

/** Returns false when the web view could not be created and must not be used. */
bool web_view_create(
    wxWebView* webview,
    wxWindow* parent,
    const wxString& url,
    const std::vector<std::string>& message_handlers
);

/** Hides an unusable web view and shows a message explaining that the embedded web browser is not available. */
void web_view_show_unavailable_message(wxWindow* parent, wxSizer* sizer, wxWebView* webview);

} // namespace Slic3r::App::WX::WebView
