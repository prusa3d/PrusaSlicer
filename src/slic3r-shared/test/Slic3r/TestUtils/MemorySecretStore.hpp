#pragma once

#include "Slic3r/Biz/Platform/ISecretStore.hpp"

#include <atomic>
#include <map>
#include <string>
#include <utility>

namespace Tests {

/**
 * @brief Secret store kept in memory, for Biz::Platform::PlatformServices::set_secret_store().
 *
 * A missing record loads as an empty one.
 */
class MemorySecretStore : public Slic3r::Biz::Platform::ISecretStore
{
public:
    bool
    save_secret(const std::string& opt, const std::string& usr, const std::string& psswd) override
    {
        secrets[opt] = {usr, psswd};
        return true;
    }

    bool load_secret(const std::string& opt, std::string& usr, std::string& psswd) override
    {
        if (fail_loading) {
            return false;
        }
        const auto it = secrets.find(opt);
        if (it == secrets.end()) {
            usr.clear();
            psswd.clear();
            return true;
        }
        usr   = it->second.first;
        psswd = it->second.second;
        return true;
    }

    /** @brief Service name to user and secret. */
    std::map<std::string, std::pair<std::string, std::string>> secrets;
    /** @brief Makes load_secret() fail, as an unreadable system store does. */
    std::atomic_bool fail_loading{false};
};

} // namespace Tests
