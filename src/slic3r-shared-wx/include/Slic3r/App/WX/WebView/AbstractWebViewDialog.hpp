#pragma once

#include "Slic3r/Biz/UserAccount/IUserAccountListener.hpp"

#include <wx/dialog.h>

namespace Slic3r::App::WX::WebView {

class AbstractWebViewDialog : public wxDialog, public Biz::UserAccount::IUserAccountListener
{
public:
    AbstractWebViewDialog(
        wxWindow* parent,
        wxWindowID id,
        const wxString& title,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style         = wxDEFAULT_DIALOG_STYLE
    ) :
        wxDialog(parent, id, title, pos, size, style)
    {}

    virtual ~AbstractWebViewDialog() = default;

    // IUserAccountListener
    void on_user_account_id_success(bool is_refresh, const std::string& username) override {}

    void on_user_account_logged_out() override {}

    void on_user_account_will_refresh() override {}

    void on_user_account_action_retry(
        const Biz::Network::IHttp::Retry& retry,
        std::function<void(void)> cancel_callback
    ) override
    {}
};
} // namespace Slic3r::App::WX::WebView
