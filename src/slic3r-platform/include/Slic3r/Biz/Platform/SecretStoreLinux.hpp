#pragma once

#include "Slic3r/Biz/Platform/ISecretStore.hpp"

#include <string>

namespace Slic3r::Biz::Platform {

class SecretStoreLinux : public ISecretStore
{
public:
	SecretStoreLinux() = default;

    bool save_secret(const std::string& opt, const std::string& usr, const std::string& psswd) override;
    bool load_secret(const std::string& opt, std::string& usr, std::string& psswd) override;
};

}