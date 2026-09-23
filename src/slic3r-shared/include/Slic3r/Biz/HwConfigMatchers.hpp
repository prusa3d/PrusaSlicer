#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include "Slic3r/Domain/Preset/HwConfig.hpp"

namespace Slic3r::Biz {

bool match_feeder(
    const Domain::Preset::Address& address_string,
    const nlohmann::json& json,
    const Domain::Preset::HwFeederConfigs& feeders);

const Domain::Preset::HwPrinterConfig*
find_matching_printer_config(const auto& printer_configs_view, const nlohmann::json& json)
{
    const std::string model{json.value("model", "")};
    const std::string base_model{json.value("base_model", "")};
    const uint8_t tool_count{json.value("tool_count", static_cast<uint8_t>(0))};
    if (model.empty() || base_model.empty() || tool_count == 0) {
        return nullptr;
    }
    const auto it{std::ranges::find_if(printer_configs_view, [&](const auto& item){
        const Domain::Preset::HwPrinterConfig& hw_config{item.first.get()};
        if (hw_config.model.model != model
            || hw_config.model.base_model != base_model
            || hw_config.tool_count != tool_count)
        {
            return false;
        }
        if (!json.contains("tools")) {
            // If no tools are recieved, we cannot check if a printer has MMU,
            // thus make sure to match non-MMU printer.
            if (!hw_config.feeders.empty()) {
                return false;
            }
        } else {
            if (!json["tools"].is_object()) {
                return false;
            }

            for (const auto& [address_string, tool] : json["tools"].items()) {
                const auto address{Domain::Preset::to_address(address_string)};
                if (!address.has_value()) {
                    return false;
                }

                if (tool.contains("feeder")) {
                    if (!match_feeder(address.value(), tool["feeder"], hw_config.feeders)){
                        return false;
                    }
                } else {
                    if (hw_config.feeders.contains(address.value())) {
                        return false;
                    }
                }
            }
        }
        return true;
    })};

    if (it == printer_configs_view.end()) {
        return nullptr;
    }

    return &((*it).first.get());
}

}
