#include "Slic3r/Biz/UserAccount/UserAccountCommunication.hpp"
#include "Slic3r/Biz/UserAccount/UserAccountCodeChallengeGenerator.hpp"
#include "Slic3r/Biz/Network/ServiceConfig.hpp"
#include "Slic3r/Biz/Network/IHttp.hpp"
#include "Slic3r/Log.hpp"

#include "fmt/format.h"

#include <nlohmann/json.hpp>

namespace Slic3r::Biz::UserAccount {

namespace {

std::string get_code_from_message(const std::string& url_message)
{
    size_t pos = url_message.rfind("code=");
    std::string out;
    for (size_t i = pos + 5; i < url_message.size(); i++) {
        const char& c = url_message[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
            out += c;
        else
            break;
    }
    return out;
}
} // namespace

UserAccountCommunication::UserAccountCommunication(Platform::IMainThreadDispatcher& dispatcher)
    : UserAccountCommunicationTokenBase{dispatcher}
{}

void UserAccountCommunication::do_log_out(bool notify_owner)
{
    do_clear(notify_owner);
    m_email = "";
    m_login_pending = false;
}

std::string UserAccountCommunication::on_log_in_request(
    const std::string& lang_code,
    bool generate_code_verifier,
    const std::string& service
)
{
    std::string result_url;
    const std::string AUTH_HOST    = Network::ServiceConfig::instance().account_url();
    const std::string CLIENT_ID    = Network::ServiceConfig::instance().account_client_id();
    const std::string REDIRECT_URI = "prusaslicer://login";

    UserAccountCodeChallengeGenerator ccg;
    if (generate_code_verifier) {
        m_code_verifier = ccg.generate_verifier();
    }
    std::string code_challenge = ccg.generate_challenge(m_code_verifier);

    m_login_pending = true;

    std::string language = lang_code;
    ASSERT(!language.empty(), "Language code must not be empty.");
    if (language.size() > 2) {
        language = language.substr(0, 2);
    }

    std::string params = fmt::format(
        "embed=1&client_id={}&response_type=code&code_challenge={}&code_challenge_method=S256&scope=basic_info&redirect_uri={}&language={}",
        CLIENT_ID,
        code_challenge,
        REDIRECT_URI,
        language
    );
    if (service.empty()) {
        result_url = fmt::format("{}/o/authorize/?{}&choose_account=1", AUTH_HOST, params);
    } else {
        result_url = fmt::format("{}/login/{}?next=/o/authorize/?{}", AUTH_HOST, service, params);
    }

    return result_url;
}

void UserAccountCommunication::on_log_in_code_response(const std::string& url_message)
{
    const std::string code = get_code_from_message(url_message);
    if (!m_login_pending) {
        return;
    }
    m_login_pending = false;
    m_session.on_log_in_code_response(code, m_code_verifier);
    wakeup_session_thread();
}

void UserAccountCommunication::request_printables_secret_token()
{
    ASSERT(m_session.is_initialized());
    auto succ_fn = [this](const std::string& body)
    { 
        m_session.dispatch_printables_secret_token(body); 
    };
    auto fail_fn = [this](const std::string& body)
    {
        SPDLOG_ERROR("Failed to retrieve Printables secret token. Response: {} bytes.", body.size());
        m_session.dispatch_printables_secret_token({});
    };

    nlohmann::json j;
    j["accessToken"] = access_token();
    
    ActionQueueData action{
        UserAccountActionID::PrintablesSecretToken,
        succ_fn,
        fail_fn,
        j.dump(),
        {{"Content-Type", "application/json"},
         {"Origin", Network::ServiceConfig::instance().printables_url()}}
    };
    m_session.enqueue_action(std::move(action));
    wakeup_session_thread();
}

void UserAccountCommunication::enqueue_connect_printers_data_action(std::function<void(const std::string&)> succ_fn)
{
    ActionQueueData action {
        UserAccountActionID::ConnectPrinterModels,
        std::move(succ_fn)
    };
    m_session.enqueue_action(std::move(action));
    wakeup_session_thread();
}

} // namespace Slic3r::Biz::UserAccount
