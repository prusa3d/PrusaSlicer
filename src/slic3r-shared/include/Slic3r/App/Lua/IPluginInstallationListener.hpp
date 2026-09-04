#pragma once

#include <string>

#include "Slic3r/App/Lua/PluginBundle.hpp"

namespace Slic3r::App::Lua {

class IPluginInstallationListener {
public:
    virtual ~IPluginInstallationListener() = default;

    virtual void on_plugin_installation_error(const std::string& error_message) = 0;
    virtual void on_plugin_installation_succeeded(const PluginBundleMeta& plugin_bundle_meta) = 0;
};

}
