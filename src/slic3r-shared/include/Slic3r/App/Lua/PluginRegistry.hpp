#pragma once

#include <map>
#include <string>

#include "Slic3r/App/Lua/Plugin.hpp"
#include "Slic3r/App/Lua/PluginBundle.hpp"
#include "Slic3r/App/Lua/PublicKeyRegistry.hpp"

namespace Slic3r::App::Lua {

enum class PublicKeyLookupStrategy
{
    Author,
    VersionedBundle
};


class PluginRegistry
{
public:
    using Plugins = std::map<std::string, Plugin>;
    using PluginBundles = std::vector<PluginBundle>;

    PluginRegistry();

    void clear();
    void scan(const std::string& path, bool is_protected);

    PluginInstallResult install(PluginBundle& bundle, PublicKeyLookupStrategy lookup_strategy);
    PluginInstallResult uninstall(const std::string& bundle_id, const std::string& version);;

    const Plugins& plugins() const { return m_plugins; }
    const PluginBundles& plugin_bundles() const { return m_bundles; }
    PublicKeyRegistry& public_key_registry() { return m_key_registry; }
    const PublicKeyRegistry& public_key_registry() const { return m_key_registry; }

private:
    Plugins m_plugins;
    PluginBundles m_bundles;
    PublicKeyRegistry m_key_registry;
};

}