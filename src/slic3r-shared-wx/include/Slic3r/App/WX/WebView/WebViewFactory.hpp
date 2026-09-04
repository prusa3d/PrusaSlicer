#pragma once

#include "Slic3r/App/WX/WebView/AbstractWebViewDialog.hpp"
#include "Slic3r/App/WX/WebView/AbstractWebViewPanel.hpp"
#include "Slic3r/App/Browser/AbstractBrowserLogic.hpp"
#include <memory>

namespace Slic3r::App::WX::WebView {

std::unique_ptr<AbstractWebViewDialog> new_web_view_dialog(std::unique_ptr<App::Browser::AbstractBrowserLogic>&& logic);
AbstractWebViewPanel* new_web_view_panel(wxWindow* parent, int id, std::unique_ptr<App::Browser::AbstractBrowserLogic>&& logic, bool do_create);
   

} // namespace Slic3r::App::WX::WebView