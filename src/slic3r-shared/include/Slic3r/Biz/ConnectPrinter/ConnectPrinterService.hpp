#pragma once

#include "Slic3r/Biz/ConnectPrinter/ConnectFavoriteMaterialService.hpp"
#include "Slic3r/Biz/ConnectPrinter/ConnectPrinter.hpp"
#include "Slic3r/Biz/Platform/IMainThreadDispatcher.hpp"
#include "Slic3r/Biz/UserAccount/IUserAccountListener.hpp"
#include "Slic3r/Biz/Network/IHttp.hpp"
#include "Slic3r/Biz/IObservableList.hpp"
#include "Slic3r/Log.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::Biz::Network::Mqtt {
class Subscriber;
} // namespace Slic3r::Biz::Network::Mqtt

namespace Slic3r::Biz::UserAccount {
class UserAccountInteractor;
} // namespace Slic3r::Biz::UserAccount

namespace Slic3r::Biz::ConnectPrinter {

/// Owns the current Prusa Connect printers, kept up to date from the printer list endpoint and MQTT. Main thread only.
class ConnectPrinterService final :
    public UserAccount::IUserAccountListener,
    public IObservableList<ConnectPrinter>
{
public:
    ConnectPrinterService(
        Platform::IMainThreadDispatcher& dispatcher,
        UserAccount::UserAccountInteractor& user_account_interactor
    );
    ~ConnectPrinterService();

    const ConnectPrinter& at(size_t index) const override;
    size_t size() const override;

    ConnectPrinterService(const ConnectPrinterService&)            = delete;
    ConnectPrinterService(ConnectPrinterService&&)                 = delete;
    ConnectPrinterService& operator=(const ConnectPrinterService&) = delete;
    ConnectPrinterService& operator=(ConnectPrinterService&&)      = delete;

    const ConnectPrinter* find(const std::string& uuid) const;

    ConnectFavoriteMaterialService& favorite_material_service();
    const ConnectFavoriteMaterialService& favorite_material_service() const;

    /// Fetches the printer list and applies it to the service. All callbacks run on a worker thread.
    void fetch_printer_list(
        std::function<void(const std::string&)> success_fn,
        std::function<void(const std::string&)> fail_fn,
        std::function<void(Network::IHttp::Retry, bool&)> retry_fn =
            [](Network::IHttp::Retry retry, bool& cancel)
        {
            SPDLOG_INFO(
                "Retry attempt {}: {} ms to next attempt",
                retry.attempt,
                retry.ms_to_next_attempt
            );
        },
        std::function<void(Network::IHttp::Progress, bool&)> progress_fn =
            [](Network::IHttp::Progress, bool&) {}
    );

    void on_user_account_id_success(bool is_refresh, const std::string& username) override;
    void on_user_account_logged_out() override;

private:
    void start_mqtt();
    void update_mqtt_access_token();
    void subscribe(const std::string& uuid);
    void unsubscribe(const std::string& uuid);
    void on_mqtt_message(size_t generation, const std::string& topic, const std::string& payload);
    void request_printer_list_refresh();

    void apply_printer_list(std::vector<ConnectPrinter> new_printers);
    void apply_state(const std::string& uuid, ConnectPrinterState state);

    std::optional<size_t> find_index(const std::string& uuid) const;

    Platform::IMainThreadDispatcher& m_dispatcher;
    UserAccount::UserAccountInteractor& m_user_account_interactor;

    std::vector<ConnectPrinter> m_printers;
    ConnectFavoriteMaterialService m_favorite_material_service;
    std::unique_ptr<Network::Mqtt::Subscriber> m_mqtt_subscriber;
    std::string m_mqtt_access_token;
    size_t m_session_generation{0};
    bool m_refresh_in_flight{false};
    bool m_refresh_pending{false};
};

} // namespace Slic3r::Biz::ConnectPrinter
