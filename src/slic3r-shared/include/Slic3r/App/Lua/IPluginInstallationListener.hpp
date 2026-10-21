#pragma once

#include <string>

#include "Slic3r/App/Lua/PluginBundle.hpp"

namespace Slic3r::App::Lua {

class IPluginInstallationListener {
public:
    virtual ~IPluginInstallationListener() = default;

    virtual void on_plugin_installation_error(const std::string& error_message) = 0;
    virtual void on_plugin_installation_succeeded(const PluginBundleMeta& plugin_bundle_meta) = 0;
    virtual void on_plugin_uninstallation_error(const std::string& error_message) = 0;
    virtual void on_plugin_uninstallation_succeeded(const std::string& bundle_id, const std::string& version) = 0;
};

}
