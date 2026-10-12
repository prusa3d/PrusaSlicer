#pragma once

#include "Slic3r/Biz/Platform/IMainThreadDispatcher.hpp"
#include "Slic3r/Biz/Platform/WithListeners.hpp"

#include <string>
#include <vector>

namespace Slic3r::Biz::UserAccount {
class UserAccountInteractor;
} // namespace Slic3r::Biz::UserAccount

namespace Slic3r::Biz::ConnectPrinter {

class IConnectFavoriteMaterialsListener
{
public:
    virtual ~IConnectFavoriteMaterialsListener() = default;
    virtual void on_connect_favorite_materials_changed() = 0;
};

/// Owns the uuids of the user's favorite materials in Prusa Connect, fetched from the Connect GraphQL endpoint.
/// Owned by ConnectPrinterService, which drives it on log in / log out. Main thread only.
class ConnectFavoriteMaterialService final : public WithListeners<IConnectFavoriteMaterialsListener>
{
public:
    ConnectFavoriteMaterialService(
        Platform::IMainThreadDispatcher& dispatcher,
        UserAccount::UserAccountInteractor& user_account_interactor
    );

    ConnectFavoriteMaterialService(const ConnectFavoriteMaterialService&)            = delete;
    ConnectFavoriteMaterialService(ConnectFavoriteMaterialService&&)                 = delete;
    ConnectFavoriteMaterialService& operator=(const ConnectFavoriteMaterialService&) = delete;
    ConnectFavoriteMaterialService& operator=(ConnectFavoriteMaterialService&&)      = delete;

    /// Fetches the favorite materials again. Listeners are notified if the list changes.
    void request_refresh();

    const std::vector<std::string>& favorite_material_uuids() const;
    bool is_favorite(const std::string& material_uuid) const;

    /// Drops the current list and ignores responses of requests started before log out.
    void on_logged_out();

private:
    void fetch();
    void on_fetch_finished();
    void apply(std::vector<std::string> uuids);

    Platform::IMainThreadDispatcher& m_dispatcher;
    UserAccount::UserAccountInteractor& m_user_account_interactor;

    std::vector<std::string> m_uuids;
    size_t m_session_generation{0};
    bool m_refresh_in_flight{false};
    bool m_refresh_pending{false};
};

} // namespace Slic3r::Biz::ConnectPrinter
