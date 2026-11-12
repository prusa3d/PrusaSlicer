#pragma once

#include "Slic3r/Biz/UserAccount/IUserAccountListener.hpp"
#include "Slic3r/App/LeftBarTabs.hpp"
#include "Slic3r/App/Browser/AbstractBrowserLogic.hpp"

#include <wx/panel.h>
#include <functional>

namespace Slic3r::App::WX::WebView {

class AbstractWebViewPanel : public wxPanel, public Biz::UserAccount::IUserAccountListener
{
public:
    AbstractWebViewPanel(wxWindow* parent, int id) :
        wxPanel(parent, id, wxDefaultPosition, wxDefaultSize)
    {}

    virtual ~AbstractWebViewPanel() = default;

    // IUserAccountListener;
    void on_user_account_id_success(bool is_refresh, const std::string& username) override {}

    void on_user_account_logged_out() override {}

    void on_user_account_will_refresh() override {}

    void on_user_account_action_retry(
        const Biz::Network::IHttp::Retry& retry,
        std::function<void(void)> cancel_callback
    ) override
    {}

    void on_printables_secret_token(const std::string& body) override {}

    virtual void set_next_show_url(const std::string url) {}

    virtual App::Browser::AbstractBrowserLogic* browser_logic() { return nullptr; }

    void set_switch_left_tab_fn(std::function<void(LeftBarTabs, const std::string&)> switch_left_tab_fn)
    {
        m_switch_left_tab_fn = switch_left_tab_fn;
    }

protected:
    std::function<void(LeftBarTabs, const std::string&)> m_switch_left_tab_fn;

};

} // namespace Slic3r::App::WX::WebView
