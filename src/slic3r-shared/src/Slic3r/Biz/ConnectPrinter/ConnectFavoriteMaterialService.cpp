#include "Slic3r/Biz/ConnectPrinter/ConnectFavoriteMaterialService.hpp"

#include "Slic3r/Biz/ConnectPrinter/ConnectFavoriteMaterials.hpp"
#include "Slic3r/Biz/Network/IHttp.hpp"
#include "Slic3r/Biz/Network/ServiceConfig.hpp"
#include "Slic3r/Biz/UserAccount/UserAccountInteractor.hpp"

#include "Slic3r/Log.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <thread>

namespace Slic3r::Biz::ConnectPrinter {

ConnectFavoriteMaterialService::ConnectFavoriteMaterialService(
    Platform::IMainThreadDispatcher& dispatcher,
    UserAccount::UserAccountInteractor& user_account_interactor
) :
    m_dispatcher(dispatcher),
    m_user_account_interactor(user_account_interactor)
{}

const std::vector<std::string>& ConnectFavoriteMaterialService::favorite_material_uuids() const
{
    return m_uuids;
}

bool ConnectFavoriteMaterialService::is_favorite(const std::string& material_uuid) const
{
    return std::ranges::find(m_uuids, material_uuid) != m_uuids.end();
}

void ConnectFavoriteMaterialService::on_logged_out()
{
    ++m_session_generation;
    m_refresh_in_flight = false;
    m_refresh_pending   = false;
    apply({});
}

void ConnectFavoriteMaterialService::request_refresh()
{
    if (m_user_account_interactor.access_token().empty()) {
        return;
    }
    if (m_refresh_in_flight) {
        m_refresh_pending = true;
        return;
    }
    m_refresh_in_flight = true;
    fetch();
}

void ConnectFavoriteMaterialService::fetch()
{
    std::thread(
        [this,
         generation   = m_session_generation,
         url          = Network::ServiceConfig::instance().connect_graphql_url(),
         access_token = m_user_account_interactor.access_token()]()
        {
            std::unique_ptr<Network::IHttp> http = Network::IHttp::create(
                Network::IHttp::RequestMethod::Post,
                url,
                [](Network::IHttp::Retry retry, bool& cancel)
                {
                    SPDLOG_INFO(
                        "Retry attempt {}: {} ms to next attempt",
                        retry.attempt,
                        retry.ms_to_next_attempt
                    );
                }
            );

            http->header("Authorization", "Bearer " + access_token)
                .header("Content-Type", "application/json")
                .set_post_body(favorite_materials_request_body());

            http->on_complete(
                [this, generation](std::string body, unsigned status)
                {
                    std::optional<std::vector<std::string>> uuids = parse_favorite_materials(body);
                    m_dispatcher.dispatch_on_main_thread(
                        [this, generation, uuids = std::move(uuids)]() mutable
                        {
                            if (generation != m_session_generation) {
                                return;
                            }
                            if (uuids) {
                                apply(std::move(*uuids));
                            }
                            on_fetch_finished();
                        }
                    );
                }
            );

            http->on_error(
                [this, generation](std::string body, std::string error, unsigned status)
                {
                    SPDLOG_ERROR(
                        "Connect favorite materials request failed. Status: {}, Error: {}",
                        status,
                        error
                    );
                    m_dispatcher.dispatch_on_main_thread(
                        [this, generation]()
                        {
                            if (generation == m_session_generation) {
                                on_fetch_finished();
                            }
                        }
                    );
                }
            );

            http->perform_sync(Network::HttpRetryOpt::default_retry());
        }
    ).detach();
}

void ConnectFavoriteMaterialService::on_fetch_finished()
{
    m_refresh_in_flight = false;
    if (m_refresh_pending) {
        m_refresh_pending = false;
        request_refresh();
    }
}

void ConnectFavoriteMaterialService::apply(std::vector<std::string> uuids)
{
    if (uuids == m_uuids) {
        return;
    }
    m_uuids = std::move(uuids);
    invoke_listeners<IConnectFavoriteMaterialsListener>(
        [](IConnectFavoriteMaterialsListener* l) { l->on_connect_favorite_materials_changed(); }
    );
}

} // namespace Slic3r::Biz::ConnectPrinter
