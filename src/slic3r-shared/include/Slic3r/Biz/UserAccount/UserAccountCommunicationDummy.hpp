#pragma once

#include "Slic3r/Biz/UserAccount/IUserAccountCommunication.hpp"
#include "Slic3r/Directories.hpp"

namespace Slic3r::Biz::UserAccount {

class UserAccountCommunicationDummy final : public IUserAccountCommunication {
public:
    void init() override {}
    bool is_active() const override { return false; }
    void set_refresh_time(int seconds) override {}
    void on_username_changed(const std::string& username, bool store) override {}
    void on_race_lost(const std::string& body) override {}
    void add_session_listener(IUserAccountSessionListener* listener) override {}
    void on_read_token_store_message() override {}
    void request_refresh() override {}
    bool validate_and_refresh() override { return false; }
    std::string username() const override { return {}; }
    void cancel_ongoing_session_action() override {}

    void do_log_out(bool notify_owner) override {}
    std::string on_log_in_request(const std::string& lang_code, bool generate_code_verifier, const std::string& service) override { return {}; }
    void on_log_in_code_response(const std::string& url_message) override {}
    bool is_logged_in() const override { return false; }
    std::string access_token() const override { return {}; }
    // Reason for having this path here: avatar is being read also after switching Communication backends for notifications.
    boost::filesystem::path avatar() const override {  return boost::filesystem::path(resources_dir()) / "icons" / "user.svg"; }
    void on_avatar_url(const std::string& data) override {}
    std::string email() const override { return {}; }
    void on_email(const std::string& data) override {}
    void on_avatar_success(std::string&& data) const override {}
    void request_printables_secret_token() override {}
    void enqueue_connect_printer_states_action() override {}
    void enqueue_connect_printers_data_action(std::function<void(const std::string&)> succ_fn) override {}
};

} // namespace Slic3r::Biz::UserAccount