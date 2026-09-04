#pragma once

#include <string>

namespace Slic3r::Biz::Platform {

class ISecretStore {
public:
	virtual ~ISecretStore() = default;

    virtual bool save_secret(const std::string& opt, const std::string& usr, const std::string& psswd) = 0;
    virtual bool load_secret(const std::string& opt, std::string& usr, std::string& psswd) = 0;
};

}