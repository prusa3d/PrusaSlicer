#include "Slic3r/App/WX/WebView/WebView.hpp"

#include "Slic3r/App/WX/StringConversions.hpp"
#include "Slic3r/Log.hpp"
#include "Slic3r/App/WX/format.hpp"
#include "Slic3r/Version.hpp"
#include "Slic3r/Platform.hpp"

#include <wx/webview.h>
#include <wx/window.h>
#include <wx/string.h>
#include <wx/uri.h>
#include <wx/app.h>

namespace Slic3r::App::WX::WebView {

wxWebView* web_view_new()
{
#if wxUSE_WEBVIEW_EDGE || wxUSE_WEBVIEW_EDGE_STATIC
    bool backend_available = wxWebView::IsBackendAvailable(from_u8(wxWebViewBackendEdge));
#else
    bool backend_available = wxWebView::IsBackendAvailable(from_u8(wxWebViewBackendWebKit));
#endif

    wxWebView* webView = nullptr;
    if (backend_available)
#if wxUSE_WEBVIEW_EDGE || wxUSE_WEBVIEW_EDGE_STATIC
        webView = wxWebView::New(from_u8(wxWebViewBackendEdge));
#else
        webView = wxWebView::New(from_u8(wxWebViewBackendWebKit));
#endif
    if (!webView)
        SPDLOG_ERROR("Failed to create wxWebView object.");
    return webView;
}

void web_view_create(
    wxWebView* webView,
    wxWindow* parent,
    const wxString& url,
    const std::vector<std::string>& message_handlers
)
{
    assert(webView);
    wxString correct_url = url.empty() ? wxString(L"") : wxURI(url).BuildURI();
    wxString user_agent  = format_wxstr("%1%/%2% (%3%)",SLIC3R_APP_NAME, SLIC3R_VERSION, platform_to_string(platform()));

#ifdef __WIN32__
    webView->SetUserAgent(user_agent);
    webView->Create(parent, wxID_ANY, correct_url, wxDefaultPosition, wxDefaultSize, wxNO_BORDER);
    // We register the wxfs:// protocol for testing purposes
    // webView->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewArchiveHandler("wxfs")));
    // And the memory: file system
    // webView->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewFSHandler("memory")));
#else
    // With WKWebView handlers need to be registered before creation
    // webView->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewArchiveHandler("wxfs")));
    // And the memory: file system
    // webView->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewFSHandler("memory")));
    webView->Create(parent, wxID_ANY, correct_url, wxDefaultPosition, wxDefaultSize);
    webView->SetUserAgent(user_agent);
#endif
#ifndef __WIN32__
    wxTheApp->CallAfter([message_handlers, webView] {
#endif
        for (const std::string& handler : message_handlers) {
            if (!webView->AddScriptMessageHandler(from_u8(handler))) {
                SPDLOG_ERROR("WebView could not add script message handler {}", handler);
            }
        }
#ifndef __WIN32__
    });
#endif
    webView->EnableContextMenu(false);
}

} // namespace Slic3r::App::WX::WebView
