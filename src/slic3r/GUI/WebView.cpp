#include "WebView.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI.hpp"

#include <wx/uri.h>
#include <wx/webview.h>

#include <boost/log/trivial.hpp>

wxWebView* WebView::CreateWebView(wxWindow * parent, const wxString& url, const std::vector<std::string>& message_handlers)
{
#if wxUSE_WEBVIEW_EDGE
    bool backend_available = wxWebView::IsBackendAvailable(wxWebViewBackendEdge);
#else
    bool backend_available = wxWebView::IsBackendAvailable(wxWebViewBackendWebKit);
#endif

    wxWebView* webView = nullptr;
    if (backend_available)
        webView = wxWebView::New();
    
    if (webView) {
        wxString correct_url = url.empty() ? wxString("") : wxURI(url).BuildURI();

#ifdef __WIN32__
        webView->SetUserAgent(SLIC3R_APP_FULL_NAME);
        webView->Create(parent, wxID_ANY, correct_url, wxDefaultPosition, wxDefaultSize);
        //We register the wxfs:// protocol for testing purposes
        //webView->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewArchiveHandler("wxfs")));
        //And the memory: file system
        //webView->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewFSHandler("memory")));
#else
        // With WKWebView handlers need to be registered before creation
        //webView->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewArchiveHandler("wxfs")));
        // And the memory: file system
        //webView->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewFSHandler("memory")));
        webView->Create(parent, wxID_ANY, correct_url, wxDefaultPosition, wxDefaultSize);
        webView->SetUserAgent(wxString::FromUTF8(SLIC3R_APP_FULL_NAME));
#endif
#ifndef __WIN32__
        Slic3r::GUI::wxGetApp().CallAfter([message_handlers, webView] {
#endif
        for (const std::string& handler : message_handlers) {
            if (!webView->AddScriptMessageHandler(Slic3r::GUI::into_u8(handler))) {
                // TODO: dialog to user !!!
                //wxLogError("Could not add script message handler");
                BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << "Could not add script message handler " << handler;
            }
        }
#ifndef __WIN32__
        });
#endif
        webView->EnableContextMenu(false);
    } else {
        // TODO: dialog to user !!!
        BOOST_LOG_TRIVIAL(error) << "Failed to create wxWebView object.";
    }
    return webView;
}


