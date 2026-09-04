#include "Slic3r/App/WX/WebView/WebViewFactory.hpp"

namespace Slic3r::App::WX::WebView {

std::unique_ptr<AbstractWebViewDialog> new_web_view_dialog(std::unique_ptr<App::Browser::AbstractBrowserLogic>&& logic)
{
    return std::make_unique<AbstractWebViewDialog>(nullptr, wxID_ANY, wxString());
}

AbstractWebViewPanel* new_web_view_panel(wxWindow* parent, int id, std::unique_ptr<App::Browser::AbstractBrowserLogic>&& logic, bool do_create)
{
    return new AbstractWebViewPanel(parent, id);
}

} // namespace Slic3r::App::WX::WebView