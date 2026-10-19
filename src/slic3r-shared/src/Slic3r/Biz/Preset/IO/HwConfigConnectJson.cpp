#include "Slic3r/Biz/Preset/IO/HwConfigConnectJson.hpp"
#include "Slic3r/Biz/Config/HwConfigJson.hpp"
#include "Slic3r/Biz/JsonValueJson.hpp"
#include <nlohmann/json.hpp>
#include <boost/algorithm/string/predicate.hpp>

namespace Slic3r::Biz::Preset::IO {

namespace {

tl::expected<Domain::PrinterTechnology, std::string>
estimate_technology(const nlohmann::ordered_json& node)
{
    if (!node.contains("tools")) {
        return Domain::PrinterTechnology::SLA;
    }
    const auto& tools_node = node.at("tools");
    if (!tools_node.is_object()) {
        return tl::unexpected{"'tools' node of is not of object type"};
    }

    if (tools_node.empty()) {
        return Domain::PrinterTechnology::SLA;
    }

    for (const auto& [k, v] : tools_node.items()) {
        if (v.contains("class")) {
            const auto& class_node = v.at("class");
            if (!class_node.is_string()) {
                return tl::unexpected{fmt::format("'class' node of tools '{}' is not string", k)};
            }
            auto class_type = class_node.get<std::string>();
            if (boost::iequals(class_type, "fff")) {
                return Domain::PrinterTechnology::FFF;
            }
            else if (boost::iequals(class_type, "sla")) {
                return Domain::PrinterTechnology::SLA;
            }
        }
    }

    return tl::unexpected{"Unable to estimate technology"};
}

tl::expected<Domain::Preset::HwToolConfig, std::string>
load_external_tool(const nlohmann::ordered_json& node)
{
    Domain::Preset::HwToolConfig ret;
    node.at("features").get_to(ret.features);
    return ret;
}

tl::expected<Domain::Preset::HwFeederConfig, std::string>
load_external_feeder(const nlohmann::ordered_json& node)
{
    Domain::Preset::HwFeederConfig ret{.type = Domain::Preset::FeederType::MMU};
    if (node.contains("features")) {
        node.at("features").get_to(ret.features);
    }
    if (auto model = Config::load_hw_model(node); model.has_value()) {
        ret.model = model.value();
    } else {
        return tl::unexpected{fmt::format("Parsing model failed: {}", model.error())};
    }

    return ret;
}
} // namespace

tl::expected<Domain::Preset::HwPrinterConfig, std::string>
load_external_hw_config(std::string_view json)
{
    try {
        auto node = nlohmann::ordered_json::parse(json);

        auto tech_result = estimate_technology(node);
        if (!tech_result.has_value()) {
            return tl::unexpected{tech_result.error()};
        }

        Domain::Preset::HwPrinterConfig ret{.technology = tech_result.value()};

        if (node.contains("vendor_id")) {
            node.at("vendor_id").get_to(ret.vendor_id);
        }
        if (node.contains("repo_id")) {
            node.at("repo_id").get_to(ret.repo_id);
        }

        if (auto model = Config::load_hw_model(node); model.has_value()) {
            ret.model = model.value();
        } else {
            return tl::unexpected{fmt::format("Parsing model failed: {}", model.error())};
        }

        if (!node.contains("tools") || !node.at("tools").is_object()) {
            return tl::unexpected{"Expected 'tools' key, but not found or is not of type object"};
        }

        const auto& tools_node = node.at("tools");

        if (node.contains("features")) {
            const auto& features_node = node.at("features");
            if (!features_node.is_object()) {
                return tl::unexpected{"Value of 'features' is not object"};
            }
            features_node.get_to(ret.features);
        }

        if (node.contains("tool_count")) {
            node.at("tool_count").get_to(ret.tool_count);
        } else {
            uint8_t max_tool = 0;
            for (const auto& [k, v] : tools_node.items()) {
                if (auto addr_result = Domain::Preset::to_address(k); !addr_result.has_value()) {
                    return tl::unexpected{fmt::format(
                        "Parsing address '{}' failed with error: {}",
                        k,
                        addr_result.error()
                    )};
                } else {
                    max_tool = std::max(addr_result.value().at(0), max_tool);
                }
            }
            ret.tool_count = max_tool + 1;
        }

        ASSERT(ret.tool_count > 0);
        ret.tools.resize(ret.tool_count);

        for (const auto& [k, v] : tools_node.items()) {
            if (auto addr_result = Domain::Preset::to_address(k); !addr_result.has_value()) {
                return tl::unexpected{fmt::format(
                    "Parsing address '{}' failed with error: {}",
                    k,
                    addr_result.error()
                )};
            } else {
                auto addr = addr_result.value();
                if (addr.size() == 1) {
                    if (auto tool_result = load_external_tool(v); tool_result.has_value()) {
                        ret.tools[addr.at(0)] = tool_result.value();
                    } else {
                        return tl::unexpected{fmt::format(
                            "Parsing tool at address '{}' failed with error: {}",
                            k,
                            tool_result.error()
                        )};
                    }
                }
                if (v.contains("feeder")) {
                    if (auto feeder_result = load_external_feeder(v.at("feeder"));
                        feeder_result.has_value())
                    {
                        ret.feeders[addr] = feeder_result.value();
                    } else {
                        return tl::unexpected{fmt::format(
                            "Parsing feeder at address '{}' failed with error: {}",
                            k,
                            feeder_result.error()
                        )};

                    }
                }
            }
        }

        return ret;
    } catch (nlohmann::ordered_json::exception const& e) {
        return tl::unexpected{e.what()};
    }
}
} // namespace Slic3r::Biz::Preset::IO
