#pragma once

#include <string>

class IAppConfigChangedListener
{
public:
    virtual ~IAppConfigChangedListener() = default;

    virtual void on_app_config_changed(const std::string& key) {}
};
