#include "Slic3r/App/Browser/ConnectPageAuth.hpp"

#include "Slic3r/App/Browser/BrowserCookie.hpp"
#include "Slic3r/Biz/Network/ServiceConfig.hpp"
#include "Slic3r/Biz/UserAccount/UserAccountInteractor.hpp"
#include "Slic3r/Log.hpp"

#include <nlohmann/json.hpp>

#include <utility>

namespace Slic3r::App::Browser {

void ConnectTokenAuth::prepare_page(Commands then, FallbackFn, bool, PushFn push)
{
    push(std::move(then));
}

void ConnectTokenAuth::
    add_credentials(nlohmann::json& init_options, const std::string& access_token) const
{
    init_options["accessToken"] = access_token;
}

std::string ConnectTokenAuth::page_js_condition() const
{
    return "location.protocol === 'https:'";
}

ConnectPageSessionAuth::
    ConnectPageSessionAuth(std::string server_url, RequestSessionFn request_session) :
    m_server_url(std::move(server_url)),
    m_request_session(std::move(request_session))
{}

void ConnectPageSessionAuth::
    prepare_page(Commands then, FallbackFn on_failure, bool fresh, PushFn push)
{
    m_request_session(
        [alive      = m_lifetime.token(),
         server     = m_server_url,
         then       = std::move(then),
         on_failure = std::move(on_failure),
         push       = std::move(push)](std::optional<std::string> reply) mutable
        {
            if (alive.expired()) {
                return;
            }
            const std::optional<BrowserCookie> cookie =
                reply ? parse_page_session(*reply, server) : std::nullopt;
            if (cookie) {
                then.insert(
                    then.begin(),
                    BrowserLogicCommand(
                        BrowserLogicCommandType::SetCookie,
                        cookie_set_command_data(*cookie)
                    )
                );
            } else if (reply) {
                SPDLOG_ERROR(
                    "Connect Local page session: unreadable reply ({} bytes).",
                    reply->size()
                );
            }
            if (cookie || !on_failure) {
                push(std::move(then));
            } else {
                push(on_failure());
            }
        },
        fresh
    );
}

void ConnectPageSessionAuth::add_credentials(nlohmann::json&, const std::string&) const {}

std::string ConnectPageSessionAuth::page_js_condition() const
{
    if (m_server_url.starts_with("http://")) {
        return "(location.protocol === 'https:' || location.protocol === 'http:')";
    }
    return "location.protocol === 'https:'";
}

std::unique_ptr<IConnectPageAuth>
make_connect_page_session_auth(Biz::UserAccount::UserAccountInteractor& user_account)
{
    return std::make_unique<ConnectPageSessionAuth>(
        Biz::Network::ServiceConfig::instance().connect_local_server_url(),
        [&user_account](ConnectPageSessionAuth::SessionReplyFn done, bool fresh)
        { user_account.request_page_session(std::move(done), fresh); }
    );
}

std::unique_ptr<IConnectPageAuth>
make_connect_page_auth(Biz::UserAccount::UserAccountInteractor& user_account)
{
    if (user_account.is_connect_local_account()) {
        return make_connect_page_session_auth(user_account);
    }
    return std::make_unique<ConnectTokenAuth>();
}

} // namespace Slic3r::App::Browser
