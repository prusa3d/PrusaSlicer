#pragma once

#include <string>

#include <imgui.h>

namespace Slic3r::Domain {
class ConfigItem;
struct ConfigValue;
} // namespace Slic3r::Domain

namespace Slic3r::App {

namespace ConfigItemUtils {

std::string config_item_to_string(const Domain::ConfigItem& config_item);
std::string
config_item_to_string(const Domain::ConfigItem& config_item, const Domain::ConfigValue& value);
std::string config_item_tooltip(const Domain::ConfigItem& config_item);

} // namespace ConfigItemUtils

} // namespace Slic3r::App
