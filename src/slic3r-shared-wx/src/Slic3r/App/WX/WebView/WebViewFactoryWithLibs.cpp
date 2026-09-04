#include "Slic3r/App/WX/WebView/WebViewFactory.hpp"
#include "Slic3r/App/WX/WebView/WebViewPanel.hpp"
#include "Slic3r/App/WX/WebView/WebViewDialog.hpp"

namespace Slic3r::App::WX::WebView {

std::unique_ptr<AbstractWebViewDialog> new_web_view_dialog(std::unique_ptr<App::Browser::AbstractBrowserLogic>&& logic)
{
    return std::make_unique<WebViewDialog>(std::move(logic));
}

AbstractWebViewPanel* new_web_view_panel(wxWindow* parent, int id, std::unique_ptr<App::Browser::AbstractBrowserLogic>&& logic, bool do_create)
{
    return new WebViewPanel(parent, id, std::move(logic), do_create);
}

} // namespace Slic3r::App::WX::WebView