#pragma once

#include <ctime>
#include <optional>
#include <string>

namespace Slic3r::Biz::UserAccount::TokenStore {

struct StoreData {
    std::string access_token;
    std::string refresh_token;
    std::string shared_session_key;
    std::string next_timeout;
    std::string master_pid;
    bool malformed{false};
};

/**
 * @brief Saves secret to store.
 * @return true if secret was saved successfully.
 */
bool save_tokens(const StoreData& secrets);

/**
 * @brief Loads secret from store.
 * @return true if the store could be read. A broken record still returns true, see StoreData::malformed.
 */
bool load_tokens(StoreData& result);


/**
 * @brief Saves empty secret to store. Used when App should load without using User Account by settings.
 */
void reset();

/**
 * @brief reset() and clear_connect_local_credential(): no account of either service mode is kept.
 */
void reset_all_modes();

/**
 * @brief The credential of a Connect Local app pairing (Connect Local). Kept in its own store as
 * token|credential_id|expires_at|server_url, apart from the Prusa Account tokens.
 */
struct ConnectLocalCredential
{
    std::string token;
    std::string credential_id;
    /** @brief Unix time, 0 when unknown. */
    std::time_t expires_at{0};
    /** @brief The server that issued it, normalized (ServiceConfig::connect_local_server_url()). */
    std::string server_url;
};

std::string serialize_connect_local_credential(const ConnectLocalCredential& credential);

/**
 * @brief nullopt for an empty or broken record.
 */
std::optional<ConnectLocalCredential> parse_connect_local_credential(const std::string& record);

bool save_connect_local_credential(const ConnectLocalCredential& credential);

enum class StoreReadStatus
{
    /** @brief A usable record was read. */
    Ok,
    /** @brief The store holds no record (or a broken one). */
    Empty,
    /** @brief The store could not be read: nothing is known about its content. */
    Error,
};

struct ConnectLocalCredentialRead
{
    StoreReadStatus status{StoreReadStatus::Empty};
    /** @brief Set exactly when status is Ok. */
    std::optional<ConnectLocalCredential> credential;
};

/**
 * @brief Reads the Connect Local store, telling an empty store from one that cannot be read.
 */
ConnectLocalCredentialRead read_connect_local_credential();

void clear_connect_local_credential();
}