#include "Slic3r/Biz/UserAccount/UserAccountInteractor.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/UserAccount/UserAccountCommunicationDummy.hpp"
#include "Slic3r/Log.hpp"

#include <nlohmann/json.hpp>

namespace Slic3r::Biz::UserAccount {
UserAccountInteractor::UserAccountInteractor(Platform::IMainThreadDispatcher& dispatcher) :
    m_dispatcher{dispatcher},
    m_communication{std::make_unique<UserAccountCommunicationDummy>()}
{
    m_communication->add_session_listener(this);
}

void UserAccountInteractor::init(bool app_config_enabled)
{
    bool was_enabled = is_enabled();
    if (app_config_enabled) {
        if (!is_enabled()) {
            m_communication = std::make_unique<UserAccountCommunication>(m_dispatcher);
            m_communication->add_session_listener(this);
            m_communication->init();
        }
    } else {
        if (is_enabled()) {
            bool was_logged = is_logged_in();
            do_log_out();
            m_communication = std::make_unique<UserAccountCommunicationDummy>();
            m_communication->add_session_listener(this);

            // This switch ends dispatching from UserAccountSessionDispatchBase before it can asynchrtonously dispatch loggout.
            // We do it manually here (possible double dispatch is ok).
            // Cut off of other messages is correct.
            if (was_logged) {
                invoke_listeners<IUserAccountListener>(
                    [](auto* listener) { listener->on_user_account_logged_out(); }
                );
            }
            
        }
    }
    
    if (bool enabled = is_enabled(); enabled != was_enabled) {
        invoke_listeners<IUserAccountListener>(
            [enabled](auto* listener) { listener->on_user_account_enabled_state_changed(enabled); }
        );
    }
}

bool UserAccountInteractor::is_enabled() const
{
   return m_communication->is_active();
}

UserAccountInteractor::~UserAccountInteractor()
{
    ASSERT(
        m_dispatcher.is_closed(),
        "There must be no queued events (not even in the future),"
        " because they may remember the address of this instance!"
    );
}

void UserAccountInteractor::do_log_out()
{
    m_communication->do_log_out(is_logged_in());
}

std::string
UserAccountInteractor::on_log_in_request(const std::string& lang_code, bool generate_code_verifier, const std::string& service /* = std::string()*/)
{
    return m_communication->on_log_in_request(lang_code, generate_code_verifier, service);
}

void UserAccountInteractor::on_log_in_code_response(const std::string& url_message)
{
    m_communication->on_log_in_code_response(url_message);
}

bool UserAccountInteractor::is_logged_in() const
{
    return m_communication->is_logged_in();
}

void UserAccountInteractor::on_read_token_store_message()
{
    m_communication->on_read_token_store_message();
}

std::string UserAccountInteractor::username() const
{
    return m_communication->username();
}

boost::filesystem::path UserAccountInteractor::avatar() const
{
    return m_communication->avatar();
}

std::string UserAccountInteractor::email() const
{
    return m_communication->email();
}

void UserAccountInteractor::request_refresh()
{
    m_communication->request_refresh();
}

bool UserAccountInteractor::validate_and_refresh()
{
    return m_communication->validate_and_refresh();
}

std::string UserAccountInteractor::access_token() const
{
    return m_communication->access_token();
}

void UserAccountInteractor::request_printables_secret_token()
{
    m_communication->request_printables_secret_token();
}

void UserAccountInteractor::on_action_retry(const Network::IHttp::Retry& retry)
{
    SPDLOG_INFO(
        "UserAccountInteractor: Retry attempt {}: {} ms to next attempt",
        retry.attempt,
        retry.ms_to_next_attempt
    );
    invoke_listeners<IUserAccountListener>(
        [this, retry](auto* listener)
        {
            listener
                ->on_user_account_action_retry(retry, [this]() { cancel_ongoing_session_action(); });
        }
    );
}

void UserAccountInteractor::on_action_success(ActionSuccessType success_type, std::string body)
{
    SPDLOG_INFO("UserAccountInteractor: Action success({})", static_cast<int>(success_type));
    notify_action_retry_finished();
    switch (success_type) {
    case Slic3r::Biz::UserAccount::ActionSuccessType::None:
        // Empty callback
        return;
    case Slic3r::Biz::UserAccount::ActionSuccessType::UserID:
    case Slic3r::Biz::UserAccount::ActionSuccessType::UserIDAfterToken:
        on_user_id(body);
        if (update_menu_callback) {
            update_menu_callback(false);
        }
        break;
    case Slic3r::Biz::UserAccount::ActionSuccessType::ConnectStatus:
        break;
    case Slic3r::Biz::UserAccount::ActionSuccessType::ConnectPrinterModels:
        break;
    case Slic3r::Biz::UserAccount::ActionSuccessType::Avatar:
        m_communication->on_avatar_success(std::move(body));
        if (update_menu_callback) {
            update_menu_callback(true);
        }
        invoke_listeners<IUserAccountListener>([](auto* l) { l->on_avatar_downloaded(); });
        break;
    case Slic3r::Biz::UserAccount::ActionSuccessType::PrinterData:
        break;
    default:
        ASSERT(false, "Unknown success type");
        break;
    }
}

void UserAccountInteractor::on_action_fail(ActionFailType fail_type, std::string body)
{
    SPDLOG_INFO("UserAccountInteractor: Action fail({})", static_cast<int>(fail_type));
    notify_action_retry_finished();
    switch (fail_type) {
    case Slic3r::Biz::UserAccount::ActionFailType::None:
        // Empty callback
        return;
    case Slic3r::Biz::UserAccount::ActionFailType::Fail:
        break;
    case Slic3r::Biz::UserAccount::ActionFailType::Reset:
        do_log_out();
        break;
    case Slic3r::Biz::UserAccount::ActionFailType::PrinterData:
        break;
    default:
        ASSERT(false, "Unknown fail type");
        break;
    }
}

void UserAccountInteractor::on_enqueued_refresh()
{
    // Here information about refresh being enqueued should be passed to other components, f.e. Printables WebView.
}

void UserAccountInteractor::on_new_refresh_time(long long exp)
{
    m_communication->set_refresh_time(exp);
}

void UserAccountInteractor::on_race_lost(const std::string& msg)
{
    // The session queue is already cancelled here - no retry of the refresh is in flight anymore.
    // Recovery from the store below is silent, so this is the only chance to report the end of it.
    notify_action_retry_finished();
    m_communication->on_race_lost(msg);
}

void UserAccountInteractor::on_logged_out(bool notify_owner, bool was_logged_in)
{
    notify_action_retry_finished();
    if (update_menu_callback) {
        update_menu_callback(true);
    }
    if (!was_logged_in) {
        return;
    }
    invoke_listeners<IUserAccountListener>(
        [](auto* listener) { listener->on_user_account_logged_out(); }
    );

    if (notify_owner) {
        invoke_listeners<IUserAccountListener>(
            [](auto* listener) { listener->on_user_account_logged_out_notify_instances(); }
        );
    }
}

void UserAccountInteractor::on_printables_secret_token(const std::string& body)
{
    invoke_listeners<IUserAccountListener>(
        [body](auto* listener) { listener->on_printables_secret_token(body); }
    );
}

void UserAccountInteractor::notify_action_retry_finished()
{
    invoke_listeners<IUserAccountListener>(
        [](auto* listener) { listener->on_user_account_action_retry_finished(); }
    );
}

void UserAccountInteractor::on_user_id(const std::string& body)
{
    bool was_logged = is_logged_in();
    SPDLOG_INFO("UserAccountInteractor: User ID message received ({} bytes).", body.size());
    std::string received_keys;
    try {
        nlohmann::json j = nlohmann::json::parse(body);

        m_account_user_data.clear();
        for (const auto& [key, value] : j.items()) {
            if (!received_keys.empty()) {
                received_keys += ", ";
            }
            received_keys += key;
            if (value.is_string()) {
                m_account_user_data[key] = value.get<std::string>();
            }
        }
    } catch (const nlohmann::json::exception&) {
        SPDLOG_INFO("UserIDUserAction Could not parse server response.");
        return;
    }

    if (m_account_user_data.find("public_username") == m_account_user_data.end()) {
        SPDLOG_ERROR("User ID message from PrusaAuth did not contain public_username. Login failed. Keys received: [{}]", received_keys);
        return;
    }
    std::string public_username = m_account_user_data["public_username"];
    m_communication->on_username_changed(public_username, true);

    if (m_account_user_data.find("email") != m_account_user_data.end()) {
        m_communication->on_email(m_account_user_data["email"]);
    } else {
        SPDLOG_ERROR("User ID message from PrusaAuth did not contain email info.");
    }

    // enqueue GET with avatar url

    if (m_account_user_data.find("avatar_small") != m_account_user_data.end()) {
        m_communication->on_avatar_url(m_account_user_data["avatar_small"]);
    } else {
        SPDLOG_ERROR("User ID message from PrusaAuth did not contain avatar info.");
    }
    // update printers list
    // enqueue_connect_printer_models_action();

    invoke_listeners<IUserAccountListener>(
        [was_logged, public_username](auto* listener)
        { listener->on_user_account_id_success(was_logged, public_username); }
    );
}

} // namespace Slic3r::Biz::UserAccount
