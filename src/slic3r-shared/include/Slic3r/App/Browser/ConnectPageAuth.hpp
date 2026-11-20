#pragma once

#include "Slic3r/Biz/Lifetime.hpp"
#include "Slic3r/App/Browser/BrowserLogicCommand.hpp"

#include <nlohmann/json_fwd.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::Biz::UserAccount {
class UserAccountInteractor;
} // namespace Slic3r::Biz::UserAccount

namespace Slic3r::App::Browser {

/**
 * @brief How the web pages of a Connect service act as the signed-in person: Prusa Connect pages
 * are handed the access token, Prusa Connect Local pages run on a page session cookie and never
 * see the credential.
 */
class IConnectPageAuth
{
public:
    using Commands   = std::vector<BrowserLogicCommand>;
    using PushFn     = std::function<void(Commands)>;
    using FallbackFn = std::function<Commands()>;

    virtual ~IConnectPageAuth() = default;

    virtual bool pages_get_token() const = 0;

    /**
     * @brief Makes the web view able to show a page as the signed-in person, then pushes `then`.
     * @param on_failure Pushed in place of `then` when that cannot be done. Without it `then` is
     * pushed anyway.
     * @param fresh Do not reuse what an earlier call prepared, the page reported it refused.
     */
    virtual void
    prepare_page(Commands then, FallbackFn on_failure, bool fresh, PushFn push) = 0;

    virtual void
    add_credentials(nlohmann::json& init_options, const std::string& access_token) const = 0;

    /**
     * @brief JavaScript condition that is true on a page of the service and false on the app's
     * own resource pages.
     */
    virtual std::string page_js_condition() const = 0;
};

class ConnectTokenAuth final : public IConnectPageAuth
{
public:
    bool pages_get_token() const override
    {
        return true;
    }

    void prepare_page(Commands then, FallbackFn on_failure, bool fresh, PushFn push) override;
    void
    add_credentials(nlohmann::json& init_options, const std::string& access_token) const override;
    std::string page_js_condition() const override;
};

class ConnectPageSessionAuth final : public IConnectPageAuth
{
public:
    using SessionReplyFn   = std::function<void(std::optional<std::string>)>;
    using RequestSessionFn = std::function<void(SessionReplyFn done, bool fresh)>;

    /**
     * @param request_session Asks the server for a page session; done gets its reply on the main
     * thread, nullopt when there is none.
     */
    ConnectPageSessionAuth(std::string server_url, RequestSessionFn request_session);

    bool pages_get_token() const override
    {
        return false;
    }

    void prepare_page(Commands then, FallbackFn on_failure, bool fresh, PushFn push) override;
    void
    add_credentials(nlohmann::json& init_options, const std::string& access_token) const override;
    std::string page_js_condition() const override;

private:
    std::string m_server_url;
    RequestSessionFn m_request_session;
    Biz::Lifetime m_lifetime;
};

std::unique_ptr<IConnectPageAuth>
make_connect_page_auth(Biz::UserAccount::UserAccountInteractor& user_account);

std::unique_ptr<IConnectPageAuth>
make_connect_page_session_auth(Biz::UserAccount::UserAccountInteractor& user_account);

} // namespace Slic3r::App::Browser
