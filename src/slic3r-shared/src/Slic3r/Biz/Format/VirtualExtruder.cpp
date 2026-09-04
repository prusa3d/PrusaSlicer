#include "Slic3r/Biz/Format/VirtualExtruder.hpp"

#include "Slic3r/Biz/Algorithms/VirtualExtruder.hpp"
#include "Slic3r/Domain/Constants.hpp"
#include "Slic3r/Domain/TriangleSelector.hpp"
#include "Slic3r/Domain/VirtualExtruder.hpp"
#include "Slic3r/Log.hpp"

#include <nlohmann/json.hpp>

#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace Slic3r::Biz::Format::VirtualExtruder {

using Slic3r::Biz::VirtualExtrudersConfig;
using Slic3r::Domain::VirtualExtruder;
using Slic3r::Domain::VirtualExtruderComponent;
using Slic3r::Domain::VirtualExtruderComponents;
using Slic3r::Domain::VirtualExtruderGradient;
using Slic3r::Domain::VirtualExtruderGradientStop;
using Slic3r::Domain::VirtualExtruderGradientStops;
using Slic3r::Domain::VirtualExtruders;
using Slic3r::Domain::TriangleSelector::TRIANGLE_STATE_TYPE_COUNT;

constexpr int VIRTUAL_EXTRUDER_CONFIG_VERSION = 1;

namespace {

std::string serialize_gradient_color_string(
    const VirtualExtruderGradientStops& stops,
    const std::vector<std::string>& physical_colors
)
{
    if (stops.size() < 2) {
        return {};
    }

    auto color_for_stop = [&](const VirtualExtruderGradientStop& stop) -> std::string
    {
        if (stop.extruder_id >= 1 && stop.extruder_id <= physical_colors.size()) {
            const std::string& c = physical_colors[stop.extruder_id - 1];
            if (c.size() == 7 && c.front() == '#') {
                return c;
            }
        }

        return "#808080";
    };

    const bool elide_positions = stops.size() == 2
        && std::abs(stops.front().position - 0.) < Domain::EPSILON
        && std::abs(stops.back().position - 1.) < Domain::EPSILON;

    std::ostringstream out;
    out << "g:";
    for (size_t i = 0; i < stops.size(); ++i) {
        if (i > 0) {
            out << ':';
        }

        out << color_for_stop(stops[i]);
        if (!elide_positions) {
            out << ',' << stops[i].position;
        }
    }

    return out.str();
}

nlohmann::json serialize_virtual_extruder_entries(
    const VirtualExtruders& virtual_extruders,
    const std::vector<std::string>& physical_extruders_colors
)
{
    nlohmann::json virtual_extruders_array = nlohmann::json::array();
    for (const VirtualExtruder& virtual_extruder : virtual_extruders) {
        nlohmann::json entry;
        entry["id"] = static_cast<int>(virtual_extruder.id);

        if (virtual_extruder.gradient.has_value()) {
            entry["kind"]                    = "gradient";
            const std::string gradient_color = serialize_gradient_color_string(
                virtual_extruder.gradient->stops,
                physical_extruders_colors
            );
            if (!gradient_color.empty()) {
                entry["color"] = gradient_color;
            }

            if (virtual_extruder.gradient->z_min.has_value()) {
                entry["minz"] = *virtual_extruder.gradient->z_min;
            }

            if (virtual_extruder.gradient->z_max.has_value()) {
                entry["maxz"] = *virtual_extruder.gradient->z_max;
            }

            nlohmann::json components_array = nlohmann::json::array();
            for (const VirtualExtruderGradientStop& stop : virtual_extruder.gradient->stops) {
                nlohmann::json component_entry;
                component_entry["extruder"] = static_cast<int>(stop.extruder_id);
                component_entry["position"] = stop.position;
                components_array.push_back(component_entry);
            }

            entry["components"] = components_array;
        } else {
            entry["kind"]           = "fullspectrum";
            const std::string color = Algorithms::VirtualExtruder::effective_color(
                virtual_extruder,
                physical_extruders_colors
            );
            if (!color.empty()) {
                entry["color"] = color;
            }

            nlohmann::json components_array = nlohmann::json::array();
            for (const VirtualExtruderComponent& component : virtual_extruder.components) {
                nlohmann::json component_entry;
                component_entry["extruder"] = static_cast<int>(component.extruder_id);
                component_entry["ratio"]    = component.ratio;
                components_array.push_back(component_entry);
            }

            entry["components"] = components_array;
        }

        virtual_extruders_array.push_back(entry);
    }

    return virtual_extruders_array;
}

VirtualExtruders deserialize_virtual_extruder_entries(const nlohmann::json& virtual_extruders_json)
{
    constexpr unsigned int max_extruder_id =
        static_cast<unsigned int>(TRIANGLE_STATE_TYPE_COUNT) - 1;

    VirtualExtruders result;

    for (const nlohmann::json& entry : virtual_extruders_json) {
        try {
            const int id = entry.value("id", -1);
            if (id <= 0) {
                SPDLOG_ERROR("Virtual extruders JSON: entry missing or non-positive id, skipping");
                continue;
            }

            const std::string kind_raw = entry.value("kind", std::string("fullspectrum"));
            const bool has_components =
                entry.contains("components") && entry["components"].is_array();
            bool is_gradient = (kind_raw == "gradient");
            if (!is_gradient && has_components) {
                for (const nlohmann::json& component : entry["components"]) {
                    if (component.contains("position")) {
                        SPDLOG_WARN(
                            "Virtual extruders JSON: "
                            "entry id={} has 'position' field without kind=\"gradient\"; "
                            "treating as gradient",
                            id
                        );
                        is_gradient = true;
                        break;
                    }
                }
            }

            VirtualExtruder virtual_extruder{static_cast<unsigned int>(id), std::nullopt};

            if (is_gradient) {
                std::optional<double> z_min;
                std::optional<double> z_max;
                if (entry.contains("minz") && entry["minz"].is_number()) {
                    z_min = entry["minz"].get<double>();
                }

                if (entry.contains("maxz") && entry["maxz"].is_number()) {
                    z_max = entry["maxz"].get<double>();
                }

                if (z_min.has_value() && z_max.has_value()) {
                    if (!std::isfinite(*z_min) || !std::isfinite(*z_max) || !(*z_max > *z_min)) {
                        SPDLOG_ERROR(
                            "Virtual extruders JSON: "
                            "entry id={} gradient has invalid minz={} maxz={} (maxz must be "
                            "strictly greater than minz), skipping entry",
                            id,
                            *z_min,
                            *z_max
                        );
                        continue;
                    }
                }

                if (!has_components) {
                    SPDLOG_ERROR(
                        "Virtual extruders JSON: entry id={} gradient missing components, skipping",
                        id
                    );
                    continue;
                }

                const nlohmann::json& components_array = entry["components"];
                VirtualExtruderGradientStops stops;
                bool entry_is_valid = true;
                for (const nlohmann::json& component_entry : components_array) {
                    if (component_entry.contains("ratio")) {
                        SPDLOG_ERROR(
                            "Virtual extruders JSON: "
                            "entry id={} gradient component has 'ratio' (expected 'position'), "
                            "skipping entry",
                            id
                        );
                        entry_is_valid = false;
                        break;
                    }

                    const int extruder = component_entry.value("extruder", -1);
                    const double position =
                        component_entry.value("position", std::numeric_limits<double>::quiet_NaN());
                    if (extruder < 1 || extruder > static_cast<int>(max_extruder_id)) {
                        SPDLOG_ERROR(
                            "Virtual extruders JSON: "
                            "entry id={} gradient stop has invalid extruder {}, skipping entry",
                            id,
                            extruder
                        );
                        entry_is_valid = false;
                        break;
                    }

                    if (!std::isfinite(position) || position < 0.0 || position > 1.0) {
                        SPDLOG_ERROR(
                            "Virtual extruders JSON: "
                            "entry id={} gradient stop has invalid position {} (expected in "
                            "[0, 1]), skipping entry",
                            id,
                            position
                        );
                        entry_is_valid = false;
                        break;
                    }

                    stops.push_back({static_cast<unsigned int>(extruder), position});
                }

                if (!entry_is_valid) {
                    continue;
                }

                if (stops.size() < 2) {
                    SPDLOG_ERROR(
                        "Virtual extruders JSON: "
                        "entry id={} gradient needs >= 2 stops, got {}, skipping",
                        id,
                        stops.size()
                    );
                    continue;
                }

                virtual_extruder.gradient = VirtualExtruderGradient{z_min, z_max, std::move(stops)};
                result.push_back(std::move(virtual_extruder));
                continue;
            }

            if (!has_components) {
                SPDLOG_ERROR(
                    "Virtual extruders JSON: entry id={} missing components, skipping",
                    id
                );
                continue;
            }

            if (entry.contains("color") && entry["color"].is_string()) {
                const std::string color_raw = entry["color"].get<std::string>();
                if (color_raw.size() >= 2 && color_raw.compare(0, 2, "g:") == 0) {
                    SPDLOG_ERROR(
                        "Virtual extruders JSON: "
                        "entry id={} kind=fullspectrum has gradient color string, ignoring color",
                        id
                    );
                } else {
                    virtual_extruder.color = color_raw;
                }
            }

            const nlohmann::json& components_array = entry["components"];
            bool entry_is_valid                    = true;
            for (const nlohmann::json& component_entry : components_array) {
                if (component_entry.contains("position")) {
                    SPDLOG_ERROR(
                        "Virtual extruders JSON: "
                        "entry id={} kind=fullspectrum component has 'position' (expected "
                        "'ratio'), skipping entry",
                        id
                    );
                    entry_is_valid = false;
                    break;
                }

                const int extruder = component_entry.value("extruder", -1);
                const double ratio = component_entry.value("ratio", 0.);
                if (extruder < 1 || extruder > static_cast<int>(max_extruder_id)) {
                    SPDLOG_ERROR(
                        "Virtual extruders JSON: "
                        "entry id={} has component with invalid extruder {}, skipping entry",
                        id,
                        extruder
                    );
                    entry_is_valid = false;
                    break;
                }

                virtual_extruder.components.push_back({static_cast<unsigned int>(extruder), ratio});
            }

            if (!entry_is_valid) {
                continue;
            }

            if (virtual_extruder.components.empty()) {
                SPDLOG_ERROR(
                    "Virtual extruders JSON: entry id={} has no valid components, skipping",
                    id
                );
                continue;
            }

            result.push_back(std::move(virtual_extruder));
        } catch (const nlohmann::json::exception& json_error) {
            SPDLOG_ERROR(
                "Virtual extruders JSON: failed to read virtual extruder entry: {}",
                json_error.what()
            );
        }
    }

    return result;
}

} // namespace

std::string serialize_virtual_extruders_to_json(
    const std::vector<std::string>& physical_extruders_colors,
    const VirtualExtruders& virtual_extruders
)
{
    nlohmann::json root;
    root["version"] = VIRTUAL_EXTRUDER_CONFIG_VERSION;

    nlohmann::json physical_extruders_array = nlohmann::json::array();
    for (size_t physical_extruder_idx = 0; physical_extruder_idx < physical_extruders_colors.size();
         ++physical_extruder_idx)
    {
        nlohmann::json entry;
        entry["id"]    = static_cast<int>(physical_extruder_idx + 1);
        entry["color"] = physical_extruders_colors[physical_extruder_idx];
        physical_extruders_array.push_back(entry);
    }

    root["physical_extruders"] = physical_extruders_array;
    root["virtual_extruders"] =
        serialize_virtual_extruder_entries(virtual_extruders, physical_extruders_colors);

    return root.dump(4);
}

VirtualExtrudersConfig deserialize_virtual_extruders_from_json(const std::string& json_content)
{
    VirtualExtrudersConfig result;

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(json_content);
    } catch (const nlohmann::json::parse_error& parse_error) {
        SPDLOG_ERROR("Prusa_Slicer_full_spectrum.json: JSON parse error: {}", parse_error.what());
        return result;
    }

    const int version = root.value("version", 0);
    if (version == 0) {
        SPDLOG_ERROR("Prusa_Slicer_full_spectrum.json: missing version field");
        return result;
    }

    if (version > VIRTUAL_EXTRUDER_CONFIG_VERSION) {
        SPDLOG_ERROR(
            "Prusa_Slicer_full_spectrum.json: unsupported version {} (max supported: {})",
            version,
            VIRTUAL_EXTRUDER_CONFIG_VERSION
        );
        return result;
    }

    if (root.contains("physical_extruders") && root["physical_extruders"].is_array()) {
        result.source_physical_count = static_cast<unsigned int>(root["physical_extruders"].size());
    }

    if (!root.contains("virtual_extruders") || !root["virtual_extruders"].is_array()) {
        return result;
    }

    result.virtual_extruders = deserialize_virtual_extruder_entries(root["virtual_extruders"]);

    return result;
}

nlohmann::json serialize_virtual_extruders_to_project_json(
    const std::vector<std::string>& physical_extruders_colors,
    const VirtualExtruders& virtual_extruders
)
{
    return serialize_virtual_extruder_entries(virtual_extruders, physical_extruders_colors);
}

VirtualExtruders deserialize_virtual_extruders_from_project_json(
    const nlohmann::json& virtual_extruders_json
)
{
    if (!virtual_extruders_json.is_array()) {
        SPDLOG_ERROR("PrusaSlicer3_project.json: virtual_extruders is not an array, ignoring");
        return {};
    }

    return deserialize_virtual_extruder_entries(virtual_extruders_json);
}

} // namespace Slic3r::Biz::Format::VirtualExtruder
