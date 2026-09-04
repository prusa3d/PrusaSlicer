#pragma once

#include <map>
#include <string>

#include "Slic3r/App/Lua/Plugin.hpp"
#include "Slic3r/App/Lua/PluginBundle.hpp"
#include "Slic3r/App/Lua/AuthorRegistry.hpp"

namespace Slic3r::App::Lua {

class PluginRegistry
{
public:
    using Plugins = std::map<std::string, Plugin>;
    using PluginBundles = std::vector<PluginBundle>;

    PluginRegistry();

    void clear();
    void scan(const std::string& path);

    PluginInstallResult install(PluginBundle& bundle);

    const Plugins& plugins() const { return m_plugins; }
    const PluginBundles& plugin_bundles() const { return m_bundles; }
    AuthorRegistry& author_registry() { return m_author_registry; }
    const AuthorRegistry& author_registry() const { return m_author_registry; }

private:
    Plugins m_plugins;
    PluginBundles m_bundles;
    AuthorRegistry m_author_registry;
};

}