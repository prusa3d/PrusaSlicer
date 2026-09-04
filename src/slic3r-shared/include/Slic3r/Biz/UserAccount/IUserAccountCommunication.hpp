#pragma once

#include <string>
#include <functional>
#include <boost/filesystem/path.hpp>
#include "Slic3r/Biz/UserAccount/IUserAccountActionCallbacks.hpp" 
#include "Slic3r/Biz/UserAccount/IUserAccountSessionListener.hpp"

namespace Slic3r::Biz::UserAccount {

class IUserAccountCommunication {
public:
    virtual ~IUserAccountCommunication() = default;

    // From UserAccountCommunicationTokenBase
    virtual void init() = 0;
    virtual bool is_active() const = 0;
    virtual void set_refresh_time(int seconds) = 0;
    virtual void on_username_changed(const std::string& username, bool store) = 0;
    virtual void on_race_lost(const std::string& body) = 0;
    virtual void add_session_listener(IUserAccountSessionListener* listener) = 0;
    virtual void on_read_token_store_message() = 0;
    virtual void request_refresh() = 0;
    virtual bool validate_and_refresh() = 0;
    virtual std::string username() const = 0;
    virtual void cancel_ongoing_session_action() = 0;

    // From UserAccountCommunication
    virtual void do_log_out(bool notify_owner) = 0;
    virtual std::string on_log_in_request(const std::string& lang_code, bool generate_code_verifier, const std::string& service) = 0;
    virtual void on_log_in_code_response(const std::string& url_message) = 0;
    virtual bool is_logged_in() const = 0;
    virtual std::string access_token() const = 0;
    virtual boost::filesystem::path avatar() const = 0;
    virtual void on_avatar_url(const std::string& data) = 0;
    virtual void on_avatar_success(std::string&& data) const = 0;
    virtual std::string email() const = 0;
    virtual void on_email(const std::string& data) = 0;
    virtual void request_printables_secret_token() = 0;
    virtual void enqueue_connect_printer_states_action() = 0;
    virtual void enqueue_connect_printers_data_action(std::function<void(const std::string&)> succ_fn) = 0;
};

} // namespace Slic3r::Biz::UserAccount