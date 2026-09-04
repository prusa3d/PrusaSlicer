#pragma once

#include "Slic3r/Biz/Platform/ISecretStore.hpp"

namespace Slic3r::Biz {

class SecretStoreDummy : public Platform::ISecretStore {
public:
    SecretStoreDummy() = default;
    bool save_secret(const std::string& opt, const std::string& usr, const std::string& psswd) override { return false; }
    bool load_secret(const std::string& opt, std::string& usr, std::string& psswd) override { return false; }
};

}