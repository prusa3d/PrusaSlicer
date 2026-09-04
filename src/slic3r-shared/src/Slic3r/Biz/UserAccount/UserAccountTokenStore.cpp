#include "Slic3r/Biz/UserAccount/UserAccountTokenStore.hpp"

#include "Slic3r/Biz/UserAccount/UserAccountTokenLog.hpp"
#include "Slic3r/Biz/Platform/ISecretStore.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Log.hpp"
#include "Slic3r/Assert.hpp"

#include <boost/algorithm/string.hpp>

#include <mutex>

namespace Slic3r::Biz::UserAccount::TokenStore {

namespace {
std::mutex g_store_mutex;
} // namespace

bool save_tokens(const StoreData& secrets)
{
#ifdef SLIC3R_HAS_WEBKIT
    std::lock_guard<std::mutex> lock(g_store_mutex);
    SPDLOG_INFO("{} access_token: {}", __FUNCTION__, token_log_fingerprint(secrets.access_token));

    std::string tokens;
    tokens = secrets.access_token
        + "|" + secrets.refresh_token
        + "|" + secrets.shared_session_key
        + "|" + secrets.next_timeout
        + "|" + secrets.master_pid;

   	if (!Platform::PlatformServices::instance().secret_store().save_secret("PrusaAccount", "tokens", tokens))
    {
         SPDLOG_ERROR("Failed to write tokens to the secret store.");
         return false;
    }
    return true;
#else
    SPDLOG_WARN("Saving User Account tokens is not allowed by SLIC3R_ENABLE_WEBKIT.");
    return false;
#endif // SLIC3R_HAS_WEBKIT

}

void reset()
{
    save_tokens({});
}

bool load_tokens(StoreData& result)
{
#ifdef SLIC3R_HAS_WEBKIT
    std::lock_guard<std::mutex> lock(g_store_mutex);
    std::string key0, tokens;
    if (!Platform::PlatformServices::instance().secret_store().load_secret("PrusaAccount", key0, tokens))
    {
        SPDLOG_ERROR("Failed to read tokens from the secret store.");
        return false;
    }
    if (key0.empty() || tokens.empty())
    {
        return false;
    }
    ASSERT(key0 == "tokens");
    // read data
    std::vector<std::string> token_list;
    boost::split(token_list, tokens, boost::is_any_of("|"), boost::token_compress_off);
    //assert(token_list.empty() || token_list.size() == 5);
    if (token_list.size() < 5) {
        SPDLOG_ERROR("Size of read secrets is only: {} (expected 5). Data length: {}", token_list.size(), tokens.size());
        result.malformed = true;
    }
    result.access_token =       token_list.size() > 0 ? token_list[0] : std::string();
    result.refresh_token =      token_list.size() > 1 ? token_list[1] : std::string();
    result.shared_session_key = token_list.size() > 2 ? token_list[2] : std::string();
    result.next_timeout =       token_list.size() > 3 ? token_list[3] : std::string();
    result.master_pid =         token_list.size() > 4 ? token_list[4] : std::string();

    return true;
#else
    SPDLOG_WARN("Loading User Account tokens is not allowed by SLIC3R_ENABLE_WEBKIT.");
    return false;
#endif // SLIC3R_HAS_WEBKIT

}

}
