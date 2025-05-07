#ifndef slic3r_GUI_WebView_hpp_
#define slic3r_GUI_WebView_hpp_

#include <vector>
#include <string>

class wxWebView;
class wxWindow;
class wxString;

namespace WebView
{
    wxWebView* webview_new();
    void       webview_create(wxWebView* webview, wxWindow *parent, const wxString& url, const std::vector<std::string>& message_handlers);     
};

#endif // !slic3r_GUI_WebView_hpp_
