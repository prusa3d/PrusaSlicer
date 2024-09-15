#include "ServiceConfig.hpp"

#include <cstdlib>
#include <cstring>

namespace Slic3r::Utils {

void update_from_env(std::string& dest, const char* env_name, bool remove_trailing_slash=false)
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

ServiceConfig::ServiceConfig()
    : m_connect_url("https://connect.prusa3d.com")
    , m_account_url("https://account.prusa3d.com")
    , m_account_client_id("oamhmhZez7opFosnwzElIgE2oGgI2iJORSkw587O")
    , m_media_url("https://media.printables.com")
    , m_preset_repo_url("https://preset-repo-api.prusa3d.com") {
#ifdef SLIC3R_REPO_URL
    m_preset_repo_url = SLIC3R_REPO_URL;
#endif

    update_from_env(m_connect_url, "PRUSA_CONNECT_URL", true);
    update_from_env(m_account_url, "PRUSA_ACCOUNT_URL", true);
    update_from_env(m_account_client_id, "PRUSA_ACCOUNT_CLIENT_ID");
    update_from_env(m_media_url, "PRUSA_MEDIA_URL", true);
    update_from_env(m_preset_repo_url, "PRUSA_PRESET_REPO_URL", true);
}

ServiceConfig& ServiceConfig::instance()
{
     static ServiceConfig inst;
     return inst;
}

}
