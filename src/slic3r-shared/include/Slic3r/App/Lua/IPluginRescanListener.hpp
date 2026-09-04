#pragma once

namespace Slic3r::App::Lua {
class PluginRegistry;

class IPluginRescanListener
{
public:
    virtual ~IPluginRescanListener() = default;

    virtual void on_plugins_scanned(const PluginRegistry& registry) = 0;
};
} // namespace Slic3r::App::Lua
