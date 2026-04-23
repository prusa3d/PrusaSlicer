#include "ServiceConfig.hpp"

#include "Http.hpp"

#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <boost/log/trivial.hpp>

namespace Slic3r::Utils {

void update_from_env(std::string& dest, const char* env_name, bool remove_trailing_slash)
{
    const char* env_val = std::getenv(env_name);
    if (env_val == nullptr || std::strlen(env_val) == 0)
        return;

    dest = env_val;
    if (remove_trailing_slash) {
        auto idx = dest.find_last_not_of('/');
        if (idx != std::string::npos && idx + 1 < dest.length())
            dest.erase(idx + 1, std::string::npos);
    }
}

bool is_whitelisted_url(const std::string& url, const std::vector<std::string>& allowed_domains) 
{
    const std::string host = Http::get_apex_domain(url);
    return std::find(allowed_domains.begin(), allowed_domains.end(), host) != allowed_domains.end();
}

void update_url_from_env(std::string& dest, const char* env_name, const std::vector<std::string>& allowed_domains)
{
    std::string candidate = dest;
    update_from_env(candidate, env_name, true);
    
    if (is_whitelisted_url(candidate, allowed_domains)) {
        dest = std::move(candidate);
    } else {
        BOOST_LOG_TRIVIAL(error) << "Url was not set from env variable: " << candidate << ". New adress is not whitelisted." ;
    }
}

ServiceConfig::ServiceConfig()
    : m_connect_url("https://connect.prusa3d.com")
    , m_account_url("https://account.prusa3d.com")
    , m_account_client_id("oamhmhZez7opFosnwzElIgE2oGgI2iJORSkw587O")
    , m_media_url("https://media.printables.com")
    , m_preset_repo_url("https://preset-repo-api.prusa3d.com") 
    , m_printables_url("https://www.printables.com")
{
#ifdef SLIC3R_REPO_URL
    m_preset_repo_url = SLIC3R_REPO_URL;
#endif

    std::vector<std::string> all_domains = {"prusa.com", "prusa3d.com", "prusa.cz", "prusa3d.cz", "printables.com", "testprusaverse.com", "localhost"};
    update_url_from_env(m_connect_url, "PRUSA_CONNECT_URL", all_domains);
    update_url_from_env(m_account_url, "PRUSA_ACCOUNT_URL", all_domains);
    update_url_from_env(m_media_url, "PRUSA_MEDIA_URL", all_domains);
    update_url_from_env(m_preset_repo_url, "PRUSA_PRESET_REPO_URL", all_domains);
    update_url_from_env(m_printables_url, "PRUSA_PRINTABLES_URL", all_domains);

    update_from_env(m_account_client_id, "PRUSA_ACCOUNT_CLIENT_ID", false);
}

ServiceConfig& ServiceConfig::instance()
{
     static ServiceConfig inst;
     return inst;
}

}
