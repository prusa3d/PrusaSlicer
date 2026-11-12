#pragma once

#include "Slic3r/Biz/Platform/ISecretStore.hpp"
#include <string>

namespace Slic3r::Biz::WX {
class SecretStore : public Platform::ISecretStore 
{
public:
	SecretStore() = default;

    /**
     * @ brief Stores secret (psswd and usr) under opt in system secret store. Uses wxSecretStore.
     */
    bool save_secret(const std::string& opt, const std::string& usr, const std::string& psswd) override;
    
    /**
     * @ brief Loads secret (psswd and usr) under opt from system secret store. Uses wxSecretStore.
     */
    bool load_secret(const std::string& opt, std::string& usr, std::string& psswd) override;
};
}