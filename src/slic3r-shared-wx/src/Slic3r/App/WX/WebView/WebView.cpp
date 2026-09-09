#include "Slic3r/App/WX/WebView/WebView.hpp"

#include "Slic3r/App/WX/StringConversions.hpp"
#include "Slic3r/App/WX/I18N.hpp"
#include "Slic3r/App/WX/WidgetsConfig.hpp"
#include "Slic3r/Directories.hpp"
#include "Slic3r/Log.hpp"
#include "Slic3r/App/WX/format.hpp"
#include "Slic3r/Version.hpp"
#include "Slic3r/Platform.hpp"

#include <wx/webview.h>
#include <wx/window.h>
#include <wx/string.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/uri.h>
#include <wx/app.h>

namespace Slic3r::App::WX::WebView {

wxWebView* web_view_new()
{
#if wxUSE_WEBVIEW_EDGE || wxUSE_WEBVIEW_EDGE_STATIC
    const wxString backend = from_u8(wxWebViewBackendEdge);
#else
    const wxString backend = from_u8(wxWebViewBackendWebKit);
#endif

    if (!wxWebView::IsBackendAvailable(backend)) {
        SPDLOG_ERROR("Failed to create wxWebView object. Backend is not available.");
        return nullptr;
    }

    wxWebView* webView = nullptr;
#ifdef __WIN32__
    wxWebViewConfiguration configuration = wxWebView::NewConfiguration(backend);
    const std::string webview_dir = webview_data_dir();
    if (!webview_dir.empty()) {
        SPDLOG_INFO("Web view data directory: {}", webview_dir);
        configuration.SetDataPath(from_u8(webview_dir));
    }
    webView = wxWebView::New(configuration);
#else
    webView = wxWebView::New(backend);
#endif

    if (!webView) {
        SPDLOG_ERROR("Failed to create wxWebView object.");
    }
    return webView;
}

void web_view_show_unavailable_message(wxWindow* parent, wxSizer* sizer, wxWebView* webView)
{
    assert(parent);
    assert(sizer);

    if (webView) {
        webView->Hide();
    }

    // TRN %1% is the application name, e.g. PrusaSlicer.
    wxString message = format_wxstr(_L("%1% could not start the embedded web browser."), SLIC3R_APP_NAME);
    message += L"\n\n";
    // TRN Both %1% are the application name, e.g. PrusaSlicer.
    message += format_wxstr(_L("This usually happens when another version of %1% is running. Close it and restart %1%."), SLIC3R_APP_NAME);

    const int em_unit = w_config()->em_unit();

    wxStaticText* text = new wxStaticText(parent, wxID_ANY, message);
    text->Wrap(30 * em_unit);
    sizer->Add(text, wxSizerFlags().Border(wxALL, em_unit).Center());
    parent->Layout();
}

bool web_view_create(
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
    if (!webView->Create(parent, wxID_ANY, correct_url, wxDefaultPosition, wxDefaultSize, wxNO_BORDER)) {
        SPDLOG_ERROR("Failed to create the web view control.");
        return false;
    }
    // We register the wxfs:// protocol for testing purposes
    // webView->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewArchiveHandler("wxfs")));
    // And the memory: file system
    // webView->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewFSHandler("memory")));
#else
    // With WKWebView handlers need to be registered before creation
    // webView->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewArchiveHandler("wxfs")));
    // And the memory: file system
    // webView->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewFSHandler("memory")));
    if (!webView->Create(parent, wxID_ANY, correct_url, wxDefaultPosition, wxDefaultSize)) {
        SPDLOG_ERROR("Failed to create the web view control.");
        return false;
    }
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
    return true;
}

} // namespace Slic3r::App::WX::WebView
