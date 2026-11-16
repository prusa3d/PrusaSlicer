#include "Slic3r/Biz/ConnectPrinter/ConnectPrinterService.hpp"

#include "Slic3r/Biz/Network/ServiceConfig.hpp"
#include "Slic3r/Biz/UserAccount/UserAccountInteractor.hpp"

#include "Slic3r/Log.hpp"

#include <algorithm>
#include <thread>
#include <utility>

namespace Slic3r::Biz::ConnectPrinter {

ConnectPrinterService::ConnectPrinterService(
    Platform::IMainThreadDispatcher& dispatcher,
    UserAccount::UserAccountInteractor& user_account_interactor
) :
    m_dispatcher(dispatcher),
    m_user_account_interactor(user_account_interactor),
    m_favorite_material_service(dispatcher, user_account_interactor)
{}

ConnectPrinterService::~ConnectPrinterService() = default;

const ConnectPrinter& ConnectPrinterService::at(size_t index) const
{
    return m_printers.at(index);
}

size_t ConnectPrinterService::size() const
{
    return m_printers.size();
}

const ConnectPrinter* ConnectPrinterService::find(const std::string& uuid) const
{
    std::optional<size_t> index = find_index(uuid);
    return index.has_value() ? &m_printers.at(index.value()) : nullptr;
}

ConnectFavoriteMaterialService& ConnectPrinterService::favorite_material_service()
{
    return m_favorite_material_service;
}

const ConnectFavoriteMaterialService& ConnectPrinterService::favorite_material_service() const
{
    return m_favorite_material_service;
}

std::optional<size_t> ConnectPrinterService::find_index(const std::string& uuid) const
{
    std::vector<ConnectPrinter>::const_iterator it = std::ranges::find_if(
        m_printers,
        [&](const ConnectPrinter& printer) { return printer.uuid == uuid; }
    );
    return it == m_printers.end() ? std::nullopt :
                                    std::optional<size_t>{std::distance(m_printers.cbegin(), it)};
}

void ConnectPrinterService::fetch_printer_list(
    std::function<void(const std::string&)> success_fn,
    std::function<void(const std::string&)> fail_fn,
    std::function<void(Network::IHttp::Retry, bool&)> retry_fn,
    std::function<void(Network::IHttp::Progress, bool&)> progress_fn
)
{
    std::thread(
        [this,
         generation   = m_session_generation,
         url          = Network::ServiceConfig::instance().connect_printer_list_url(),
         access_token = m_user_account_interactor.access_token(),
         success_fn   = std::move(success_fn),
         fail_fn      = std::move(fail_fn),
         retry_fn     = std::move(retry_fn),
         progress_fn  = std::move(progress_fn)]() mutable
        {
            std::unique_ptr<Network::IHttp> http =
                Network::IHttp::create(Network::IHttp::RequestMethod::Get, url, retry_fn);

            if (!access_token.empty()) {
                http->header("Authorization", "Bearer " + access_token);
            }

            http->on_complete(
                [this,
                 generation,
                 success_fn = std::move(success_fn)](std::string body, unsigned status)
                {
                    SPDLOG_INFO("Connect printer list: HTTP {}, {} bytes", status, body.size());
                    if (std::optional<std::vector<ConnectPrinter>> printers =
                            parse_connect_printer_list(body))
                    {
                        m_dispatcher.dispatch_on_main_thread(
                            [this, generation, printers = std::move(*printers)]() mutable
                            {
                                if (generation == m_session_generation) {
                                    apply_printer_list(std::move(printers));
                                }
                            }
                        );
                    }
                    if (success_fn) {
                        success_fn(body);
                    }
                }
            );

            http->on_error(
                [this,
                 generation,
                 access_token,
                 fail_fn = std::move(fail_fn)](std::string body, std::string error, unsigned status)
                {
                    SPDLOG_ERROR(
                        "Connect HTTP request failed. Status: {}, Error: {}",
                        status,
                        error
                    );
                    if (status == 401) {
                        m_dispatcher.dispatch_on_main_thread(
                            [this, generation, access_token]()
                            {
                                if (generation == m_session_generation) {
                                    m_list_refused = true;
                                    m_user_account_interactor.on_unauthorized(access_token);
                                }
                            }
                        );
                    }
                    if (fail_fn) {
                        fail_fn("Connect HTTP request failed: " + error);
                    }
                }
            );
            http->on_progress(
                [progress_fn =
                     std::move(progress_fn)](Network::IHttp::Progress progress, bool& cancel)
                {
                    if (progress_fn) {
                        progress_fn(progress, cancel);
                    }
                }
            );

            http->perform_sync(Network::HttpRetryOpt::default_retry());
        }
    ).detach();
}

void ConnectPrinterService::on_user_account_id_success(bool is_refresh, const std::string& username)
{
    const std::string access_token = m_user_account_interactor.access_token();
    if (!m_updates) {
        start_updates();
    } else if (access_token != m_updates_access_token) {
        m_updates->set_credentials(m_user_account_interactor.user_id(), access_token);
        m_updates_access_token = access_token;
    }

    if (!is_refresh) {
        m_list_refused = false;
        fetch_printer_list({}, {});
        m_favorite_material_service.request_refresh();
    } else if (std::exchange(m_list_refused, false)) {
        fetch_printer_list({}, {});
    }
}

void ConnectPrinterService::on_user_account_logged_out()
{
    ++m_session_generation;
    m_refresh_in_flight = false;
    m_refresh_pending   = false;
    m_list_refused      = false;
    stop_updates();
    m_favorite_material_service.on_logged_out();

    if (!m_printers.empty()) {
        invoke_listeners<IListObserver<ConnectPrinter>>([](IListObserver<ConnectPrinter>* l)
                                                        { l->on_will_be_reset(); });
        m_printers.clear();
        invoke_listeners<IListObserver<ConnectPrinter>>([](IListObserver<ConnectPrinter>* l)
                                                        { l->on_reset(); });
    }
}

std::unique_ptr<IConnectPrinterUpdates> ConnectPrinterService::create_updates()
{
    const size_t generation = m_session_generation;
    IConnectPrinterUpdates::Sink sink;
    sink.on_list_changed = [this, generation]()
    {
        m_dispatcher.dispatch_on_main_thread(
            [this, generation]()
            {
                if (generation == m_session_generation && m_user_account_interactor.is_logged_in()) {
                    request_printer_list_refresh();
                }
            }
        );
    };
    sink.on_state = [this, generation](const std::string& uuid, ConnectPrinterState state)
    {
        m_dispatcher.dispatch_on_main_thread(
            [this, generation, uuid, state]()
            {
                if (generation == m_session_generation) {
                    apply_state(uuid, state);
                }
            }
        );
    };

    const std::string mqtt_url = Network::ServiceConfig::instance().connect_mqtt_url();
    if (mqtt_url.empty()) {
        return std::make_unique<PollingConnectPrinterUpdates>(POLL_INTERVAL, std::move(sink));
    }
    return std::make_unique<MqttConnectPrinterUpdates>(
        mqtt_url,
        m_user_account_interactor.user_id(),
        m_updates_access_token,
        std::move(sink)
    );
}

void ConnectPrinterService::start_updates()
{
    m_updates_access_token = m_user_account_interactor.access_token();
    m_updates              = create_updates();
    for (ConnectPrinter& printer : m_printers) {
        m_updates->watch(printer.uuid);
    }
}

void ConnectPrinterService::stop_updates()
{
    m_updates.reset();
    m_updates_access_token.clear();
}

void ConnectPrinterService::request_printer_list_refresh()
{
    if (m_refresh_in_flight) {
        m_refresh_pending = true;
        return;
    }
    m_refresh_in_flight = true;

    auto done = [this, generation = m_session_generation](const std::string&)
    {
        m_dispatcher.dispatch_on_main_thread(
            [this, generation]()
            {
                if (generation != m_session_generation) {
                    return;
                }
                m_refresh_in_flight = false;
                if (m_refresh_pending) {
                    m_refresh_pending = false;
                    request_printer_list_refresh();
                }
            }
        );
    };
    fetch_printer_list(done, done);
}

void ConnectPrinterService::apply_printer_list(std::vector<ConnectPrinter> new_printers)
{
    const bool states_pushed = m_updates && m_updates->pushes_states();
    for (size_t i = m_printers.size(); i-- > 0;) {
        const std::string uuid = m_printers.at(i).uuid;
        if (std::ranges::none_of(
                new_printers,
                [&uuid](const ConnectPrinter& p) { return p.uuid == uuid; }
            ))
        {
            if (m_updates) {
                m_updates->unwatch(uuid);
            }
            IndexRange index{i};
            invoke_listeners<IListObserver<ConnectPrinter>>([&](IListObserver<ConnectPrinter>* l)
                                                            { l->on_will_be_removed(index); });
            m_printers.erase(m_printers.begin() + i);
            invoke_listeners<IListObserver<ConnectPrinter>>([&](IListObserver<ConnectPrinter>* l)
                                                            { l->on_removed(index); });
        }
    }

    for (ConnectPrinter& printer : new_printers) {
        if (std::optional<size_t> index = find_index(printer.uuid)) {
            ConnectPrinter& current = m_printers.at(*index);
            if (states_pushed) {
                printer.state = current.state;
            }
            if (printer != current) {
                current = std::move(printer);
                invoke_listeners<IListObserver<ConnectPrinter>>(
                    [&](IListObserver<ConnectPrinter>* l) { l->on_updated({index.value()}); }
                );
            }
        } else {
            const std::string uuid = printer.uuid;
            m_printers.insert(m_printers.end(), std::move(printer));
            if (m_updates) {
                m_updates->watch(uuid);
            }
            invoke_listeners<IListObserver<ConnectPrinter>>(
                [&](IListObserver<ConnectPrinter>* l)
                { l->on_inserted(m_printers.back(), m_printers.size() - 1); }
            );
        }
    }
}

void ConnectPrinterService::apply_state(const std::string& uuid, ConnectPrinterState state)
{
    if (std::optional<size_t> index = find_index(uuid)) {
        ConnectPrinter& current = m_printers.at(*index);
        current.state           = state;
        invoke_listeners<IListObserver<ConnectPrinter>>([&](IListObserver<ConnectPrinter>* l)
                                                        { l->on_updated({index.value()}); });
    }
}

} // namespace Slic3r::Biz::ConnectPrinter
