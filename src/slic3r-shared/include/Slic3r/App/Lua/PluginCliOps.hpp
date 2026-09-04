#pragma once

#include "Slic3r/App/Init.hpp"

namespace Slic3r::App::Lua {

void plugin_init(PluginInitActionParams& params);

void plugin_keygen(const PluginKeygenActionParams& params);

void plugin_sign(const PluginSignActionParams& params);

} // namespace Slic3r::App::Lua
