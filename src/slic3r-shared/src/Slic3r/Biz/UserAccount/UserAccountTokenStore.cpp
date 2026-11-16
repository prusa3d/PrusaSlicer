#include "Slic3r/Biz/UserAccount/UserAccountTokenStore.hpp"

#include "Slic3r/Biz/UserAccount/UserAccountTokenLog.hpp"
#include "Slic3r/Biz/Platform/ISecretStore.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Log.hpp"
#include "Slic3r/Assert.hpp"

#include <boost/algorithm/string.hpp>

#include <exception>
#include <mutex>

namespace Slic3r::Biz::UserAccount::TokenStore {

namespace {
std::mutex g_store_mutex;

/**
 * @brief Secret store service of the Connect Local credential, apart from the Prusa Account tokens.
 */
const std::string CONNECT_LOCAL_SERVICE = "PrusaAccountOffline";
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

void reset_all_modes()
{
    reset();
    clear_connect_local_credential();
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

std::string serialize_connect_local_credential(const ConnectLocalCredential& credential)
{
    return credential.token
        + "|"
        + credential.credential_id
        + "|"
        + std::to_string(credential.expires_at)
        + "|"
        + credential.server_url;
}

std::optional<ConnectLocalCredential> parse_connect_local_credential(const std::string& record)
{
    const size_t first  = record.find('|');
    const size_t second = first == std::string::npos ? first : record.find('|', first + 1);
    const size_t third  = second == std::string::npos ? second : record.find('|', second + 1);
    if (third == std::string::npos) {
        return std::nullopt;
    }
    ConnectLocalCredential credential;
    credential.token             = record.substr(0, first);
    credential.credential_id     = record.substr(first + 1, second - first - 1);
    const std::string expires_at = record.substr(second + 1, third - second - 1);
    credential.server_url        = record.substr(third + 1);
    if (credential.token.empty()
        || credential.credential_id.empty()
        || credential.server_url.empty())
    {
        return std::nullopt;
    }
    try {
        credential.expires_at = expires_at.empty() ? 0 : std::stoll(expires_at);
    } catch (const std::exception&) {
        return std::nullopt;
    }
    return credential;
}

bool save_connect_local_credential(const ConnectLocalCredential& credential)
{
#ifdef SLIC3R_HAS_WEBKIT
    std::lock_guard<std::mutex> lock(g_store_mutex);
    SPDLOG_INFO("{} token: {}", __FUNCTION__, token_log_fingerprint(credential.token));
    const std::string record =
        credential.token.empty() ? std::string() : serialize_connect_local_credential(credential);
    if (!Platform::PlatformServices::instance()
             .secret_store()
             .save_secret(CONNECT_LOCAL_SERVICE, "tokens", record))
    {
        SPDLOG_ERROR("Failed to write the Connect Local credential to the secret store.");
        return false;
    }
    return true;
#else
    SPDLOG_WARN("Saving the Connect Local credential is not allowed by SLIC3R_ENABLE_WEBKIT.");
    return false;
#endif // SLIC3R_HAS_WEBKIT
}

ConnectLocalCredentialRead read_connect_local_credential()
{
#ifdef SLIC3R_HAS_WEBKIT
    std::lock_guard<std::mutex> lock(g_store_mutex);
    std::string key;
    std::string record;
    if (!Platform::PlatformServices::instance()
             .secret_store()
             .load_secret(CONNECT_LOCAL_SERVICE, key, record))
    {
        SPDLOG_INFO("No Connect Local credential could be read from the secret store.");
        return {StoreReadStatus::Error, std::nullopt};
    }
    if (record.empty()) {
        return {StoreReadStatus::Empty, std::nullopt};
    }
    std::optional<ConnectLocalCredential> credential = parse_connect_local_credential(record);
    if (!credential) {
        SPDLOG_WARN(
            "The Connect Local credential in the secret store is unreadable ({} bytes).",
            record.size()
        );
        return {StoreReadStatus::Empty, std::nullopt};
    }
    return {StoreReadStatus::Ok, std::move(credential)};
#else
    SPDLOG_WARN("Loading the Connect Local credential is not allowed by SLIC3R_ENABLE_WEBKIT.");
    return {StoreReadStatus::Error, std::nullopt};
#endif // SLIC3R_HAS_WEBKIT
}

void clear_connect_local_credential()
{
    save_connect_local_credential({});
}
}
