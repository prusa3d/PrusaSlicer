#include "Slic3r/Biz/Config/HwConfigJson.hpp"
#include <fmt/ranges.h>
#include <tl/expected.hpp>
#include <variant>
#include "Slic3r/Biz/JsonValueJson.hpp"
#include "Slic3r/Biz/Config/FeatureStructurizer.hpp"
#include "Slic3r/Biz/Config/ConfigJson.hpp"
#include "Slic3r/Biz/Algorithms/StringUtils.hpp"
#include "Slic3r/Domain/Preset/HwConfig.hpp"
#include "Slic3r/Domain/Preset/Types.hpp"
#include "fmt/format.h"
#include "nlohmann/json.hpp"
#include "magic_enum/magic_enum.hpp"

using nlohmann::ordered_json;
using Slic3r::Domain::Preset::HwFeederConfig;
using Slic3r::Domain::Preset::HwFeederConfigs;
using Slic3r::Domain::Preset::HwMaterialConfigs;
using Slic3r::Domain::Preset::HwToolConfig;
using Slic3r::Domain::Preset::HwToolConfigs;
using Slic3r::Domain::Preset::MaterialConfig;

namespace Slic3r::Domain::Preset {
void to_json(ordered_json& j, const HwToolConfig& v);
void from_json(const ordered_json& j, HwToolConfig& v);
void to_json(ordered_json& j, const HwFeederConfig& v);
void from_json(const ordered_json& j, HwFeederConfig& v);
void to_json(ordered_json& j, const MaterialConfig& v);
void from_json(const ordered_json& j, MaterialConfig& v);
} // namespace Slic3r::Domain::Preset

namespace {
using PartiallyParsedToolConfigs     = std::map<std::string, HwToolConfig>;
using PartiallyParsedFeederConfigs   = std::map<std::string, HwFeederConfig>;
using PartiallyParsedMaterialConfigs = std::map<std::string, MaterialConfig>;

using Slic3r::Domain::Preset::Address;
using Slic3r::Domain::Preset::to_address;

uint8_t address_from_legacy_public(uint8_t address)
{
    ASSERT(address > 0);
    return address - 1;
}

Address address_from_legacy_public(const Address& address)
{
    Address ret;
    for (const auto component : address)
        ret.push_back(address_from_legacy_public(component));
    return ret;
}

std::string to_string(const Address& v)
{
    return fmt::to_string(
        fmt::join(v | std::views::transform([](auto slot) { return int(slot); }), ".")
    );
}
} // namespace

NLOHMANN_JSON_NAMESPACE_BEGIN

using Slic3r::Domain::Preset::HwFeederConfig;
using Slic3r::Domain::Preset::HwFeederConfigs;
using Slic3r::Domain::Preset::HwMaterialConfigs;
using Slic3r::Domain::Preset::MaterialConfig;

template <>
struct adl_serializer<PartiallyParsedFeederConfigs>
{
    static void from_json(const ordered_json& j, PartiallyParsedFeederConfigs& v)
    {
        v.clear();
        for (const auto& [key, value] : j.items()) {
            if (value.contains("feeder"))
                v.insert({key, value.get<HwFeederConfig>()});
        }
    }
};

template <>
struct adl_serializer<PartiallyParsedToolConfigs>
{
    static void from_json(const ordered_json& j, PartiallyParsedToolConfigs& v)
    {
        v.clear();
        for (const auto& [key, value] : j.items()) {
            if (!value.contains("type"))
                continue;
            v.insert({key, value.get<HwToolConfig>()});
        }
    }
};

NLOHMANN_JSON_NAMESPACE_END

namespace {

const std::string PRINTER_TOOL_TYPE = "print_head";

struct KeyDesc
{
    std::string key;
    std::optional<std::string> json_key;
};

// This is a list of keys stored as features in material
// which should be moved level up from features into material
// when serializing into json
const KeyDesc MATERIAL_KEYS_TO_EXTRACT[] = {
    {"material_uuid"},
    {"material_color"},
};

template <typename E>
std::string enum_to_json(E val)
{
    return Slic3r::Biz::Algorithms::to_lower_ascii(magic_enum::enum_name(val));
}

template <typename E>
std::optional<E> enum_from_json(std::string_view val)
{
    return magic_enum::enum_cast<E>(val, magic_enum::case_insensitive);
}

template <typename E>
bool contains_enum(std::string_view val)
{
    return magic_enum::enum_contains<E>(val, magic_enum::case_insensitive);
}

} // namespace

namespace Slic3r::Domain::Preset {

void to_json(ordered_json& j, const HwToolConfig& v)
{
    j = ordered_json{
        {"type", PRINTER_TOOL_TYPE},
        {"id", v.id},
        {"name", v.name},
        {"features", v.features}
    };
}

void from_json(const ordered_json& j, HwToolConfig& v)
{
    j.at("id").get_to(v.id);
    j.at("name").get_to(v.name);
    j.at("features").get_to(v.features);
}

void to_json(ordered_json& j, const HwModel& v)
{
    j = ordered_json{{"base_model", v.base_model}, {"model", v.model}};
}

void from_json(const ordered_json& j, HwModel& v)
{
    j.at("base_model").get_to(v.base_model);
    j.at("model").get_to(v.model);
}

void to_json(ordered_json& j, const HwFeederConfig& v)
{
    j = ordered_json{
        {"id", v.id},
        {"slot_count", v.slot_count},
        {"type", enum_to_json(v.type)},
        {"model", v.model.model},
        {"base_model", v.model.base_model},
        {"features", v.features}
    };
}

void from_json(const ordered_json& j, HwFeederConfig& v)
{
    j.at("id").get_to(v.id);
    j.at("slot_count").get_to(v.slot_count);
    v.type = enum_from_json<FeederType>(j.at("type").get<std::string>()).value();
    j.get_to(v.model);
    j.at("features").get_to(v.features);
}

void to_json(ordered_json& j, const MaterialConfig& v)
{
    j["id"] = v.id;
    if (v.type)
        j["type"] = v.type.value();
    j["features"] = v.features;
}

void from_json(const ordered_json& j, MaterialConfig& v)
{
    j.at("id").get_to(v.id);
    if (j.contains("type"))
        j.at("type").get_to(v.type);
    j.at("features").get_to(v.features);
}

void to_json(ordered_json& j, const HwSheetConfig& v)
{
    j = ordered_json{
        {"id", v.id},
        {"name", v.name},
        {"type", v.type},
        {"features", v.features},
    };
}

void from_json(const ordered_json& j, HwSheetConfig& v)
{
    j.at("id").get_to(v.id);
    j.at("name").get_to(v.name);
    j.at("type").get_to(v.type);
    j.at("features").get_to(v.features);
}

constexpr const char* MATERIAL_STRUCTURE_PREFIX  = "package.material.";
constexpr const char* MATERIAL_STRUCTURE_KEY     = "material_package_instance";
constexpr const char* MATERIAL_STRUCTURE_OLD_KEY = "material";

void tools_to_json(
    ordered_json& j,
    const HwToolConfigs& tools,
    const HwFeederConfigs& feeders,
    const HwMaterialConfigs& materials
)
{
    for (size_t i = 0, n = tools.size(); i < n; ++i) {
        ordered_json ji = tools[i];
        // shift by +1
        j[std::to_string(i)] = ji;
    }

    for (const auto& [k, v] : feeders) {
        std::string key = to_string(k);
        if (!j.contains(key)) {
            j[key] = ordered_json{};
        }
        auto& ji     = j[key];
        ji["feeder"] = v;
    }

    for (const auto& [k, v] : materials) {
        MaterialConfig mat = v;

        Biz::Config::remove_structurize_features(mat.features);
        // shift by +1
        std::string key = to_string(k);
        if (!j.contains(key)) {
            j[key] = ordered_json{};
        }
        auto& ji = j[key];
        // decompose material
        ji["slicer_material"] = mat;
        ji[MATERIAL_STRUCTURE_KEY] =
            Biz::Config::features_to_structure(v.features, MATERIAL_STRUCTURE_PREFIX);
    }
}

struct ToolsNodeLoadedResult
{
    PartiallyParsedToolConfigs tools;
    PartiallyParsedFeederConfigs feeders;
    PartiallyParsedMaterialConfigs materials;
};

tl::expected<ToolsNodeLoadedResult, std::string> parse_tools(const ordered_json& json)
{
    ToolsNodeLoadedResult ret;
    for (const auto& [k, v] : json.items()) {
        bool parsed = false;
        if (v.contains("type")) {
            // read tool definition
            ret.tools.insert({k, v.get<HwToolConfig>()});
            parsed = true;
        }
        if (v.contains("feeder")) {
            // read feeder definition
            ret.feeders.insert({k, v.at("feeder").get<HwFeederConfig>()});
            parsed = true;
        }

        // compose material
        if (v.contains(MATERIAL_STRUCTURE_OLD_KEY)
            || v.contains(MATERIAL_STRUCTURE_KEY)
            || v.contains("slicer_material"))
        {
            MaterialConfig mat;
            if (v.contains("slicer_material")) {
                const auto& mat_node = v.at("slicer_material");
                // read material definition
                mat = mat_node.get<MaterialConfig>();
            }
            if (v.contains(MATERIAL_STRUCTURE_KEY)) {
                const auto& matdb_node = v.at(MATERIAL_STRUCTURE_KEY);
                mat.features.merge(
                    Biz::Config::structure_to_features(
                        matdb_node.get<JsonValue>(),
                        MATERIAL_STRUCTURE_PREFIX
                    )
                );
            } else if (v.contains(MATERIAL_STRUCTURE_OLD_KEY)) {
                const auto& matdb_node = v.at(MATERIAL_STRUCTURE_OLD_KEY);
                mat.features.merge(
                    Biz::Config::structure_to_features(matdb_node.get<JsonValue>(), "")
                );
            }
            ret.materials.insert({k, mat});
            parsed = true;
        }

        if (!parsed)
            return tl::unexpected(fmt::format("Invalid tools config item with key \"{}\"", k));
    }
    return ret;
}

void to_json(ordered_json& j, const HwPrinterConfig& v)
{
    ordered_json tools;
    tools_to_json(tools, v.tools, v.feeders, v.materials);

    j = ordered_json{
        {"config_id", v.id},
        {"printer_id", v.printer_id},
        {"vendor_id", v.vendor_id},
        {"repo_id", v.repo_id},
        {"repo_version", v.repo_version},
        {"config_name", v.name},
        {"config_short_name", v.name},
        {"technology", enum_to_json(v.technology)},
        {"model", v.model.model},
        {"base_model", v.model.base_model},
        {"tool_count", v.tool_count},
        {"features", v.features},
        {"tools", tools},
        {"sheet", v.sheet},
    };

    if (v.legacy_printer_model.has_value())
        j["legacy_printer_model"] = v.legacy_printer_model.value();
    if (v.visual.bed_model.has_value())
        j["bed_model"] = v.visual.bed_model.value();
    if (v.visual.bed_texture.has_value())
        j["bed_texture"] = v.visual.bed_texture.value();
    if (v.visual.thumbnail.has_value())
        j["thumbnail"] = v.visual.thumbnail.value();
}

} // namespace Slic3r::Domain::Preset

namespace Slic3r::Biz::Config {

using Domain::PrinterTechnology;
using Domain::Preset::FeatureValue;
using Domain::Preset::FeatureValueMap;
using Domain::Preset::FeederType;
using Domain::Preset::HwFeederConfig;
using Domain::Preset::HwFeederConfigs;
using Domain::Preset::HwMaterialConfigs;
using Domain::Preset::HwModel;
using Domain::Preset::HwPrinterConfig;
using Domain::Preset::HwSheetConfig;
using Domain::Preset::HwToolConfig;
using Domain::Preset::HwToolConfigs;
using Domain::Preset::MaterialConfig;

template <>
tl::expected<void, std::string> is_valid<HwPrinterConfig>(const nlohmann::ordered_json& json_value)
{
    for (const auto& key : std::vector<std::string>{
             "config_id",
             "printer_id",
             "vendor_id",
             "repo_id",
             "repo_version",
             "config_name",
             "config_short_name",
             "technology",
             "model",
             "base_model",
             "tool_count",
             "features",
             "tools",
             "sheet",
         })
    {
        if (!json_value.contains(key)) {
            return tl::unexpected{"'" + key + "' not present!"};
        }
    }
    return tl::expected<void, std::string>{};
}

template <>
tl::expected<void, std::string> is_valid<HwModel>(const nlohmann::ordered_json& json_value)
{
    if (!json_value.contains("base_model")) {
        return tl::unexpected{"'base_model' not present!"};
    }
    if (!json_value.at("base_model").is_string()) {
        return tl::unexpected{"'base_model' must be string!"};
    }
    if (!json_value.contains("model")) {
        return tl::unexpected{"'model' not present!"};
    }
    if (!json_value.at("model").is_string()) {
        return tl::unexpected{"'model' must be string!"};
    }
    return tl::expected<void, std::string>{};
}

template <>
tl::expected<void, std::string> is_valid<uint8_t>(const nlohmann::ordered_json& json_value)
{
    if (!json_value.is_number_unsigned()) {
        return tl::unexpected{"Value must be an unsigned number!"};
    }
    if (json_value.get<std::size_t>() > std::numeric_limits<uint8_t>::max()) {
        return tl::unexpected{"Value does not fit to uint8_t!"};
    }
    return tl::expected<void, std::string>{};
}

template <>
tl::expected<void, std::string> is_valid<FeatureValue>(const nlohmann::ordered_json& json_value)
{
    if (!json_value.is_boolean() && !json_value.is_number() && !json_value.is_string()) {
        return tl::unexpected{"Feature must be boolean, number or string!"};
    }
    return tl::expected<void, std::string>{};
}

template <>
tl::expected<void, std::string> is_valid<HwToolConfig>(const nlohmann::ordered_json& json_value)
{
    if (!json_value.contains("id")) {
        return tl::unexpected{"'id' not present!"};
    }

    if (!json_value.at("id").is_string()) {
        return tl::unexpected{"'id' is not a string!"};
    }
    if (!json_value.contains("features")) {
        return tl::unexpected{"'features' are not present"};
    }
    if (!json_value.contains("type")) {
        return tl::unexpected{"'type' is not present"};
    }
    if (json_value.at("type").get<std::string>() != PRINTER_TOOL_TYPE) {
        return tl::unexpected{"'type' has not expected value"};
    }
    const auto features_valid{is_valid_map<FeatureValueMap>(json_value.at("features"))};
    if (!features_valid) {
        return features_valid;
    }

    return tl::expected<void, std::string>{};
}

template <>
tl::expected<void, std::string> is_valid<HwFeederConfig>(const nlohmann::ordered_json& json_value)
{
    if (!json_value.contains("id")) {
        return tl::unexpected{"'id' not present!"};
    }

    if (!json_value.at("id").is_string()) {
        return tl::unexpected{"'id' is not a string!"};
    }

    if (!json_value.contains("type")) {
        return tl::unexpected{"'type' not present"};
    }
    if (!json_value.at("type").is_string()) {
        return tl::unexpected{"'type' is not a string!"};
    }

    const std::string enum_value{json_value.at("type").get<std::string>()};
    if (!contains_enum<FeederType>(enum_value)) {
        return tl::unexpected{"'" + enum_value + "' is not a valid enum value"};
    };

    if (!json_value.contains("model")) {
        return tl::unexpected{"'model' not present"};
    }
    const auto model_valid{is_valid<HwModel>(json_value)};
    if (!model_valid) {
        return model_valid;
    }
    if (!json_value.contains("slot_count")) {
        return tl::unexpected{"'slot_count' not present"};
    }
    if (!json_value.at("slot_count").is_number_unsigned()) {
        return tl::unexpected{"'slot_count' is not an unsigned number"};
    }

    if (!json_value.contains("features")) {
        return tl::unexpected{"'features' are not present"};
    }
    const auto features_valid{is_valid_map<FeatureValueMap>(json_value.at("features"))};
    if (!features_valid) {
        return features_valid;
    }

    return tl::expected<void, std::string>{};
}

template <>
tl::expected<void, std::string> is_valid<MaterialConfig>(const nlohmann::ordered_json& json_value)
{
    if (!json_value.contains("features")) {
        return tl::unexpected{"'features' are not present"};
    }
    const auto features_valid{is_valid_map<FeatureValueMap>(json_value.at("features"))};
    if (!features_valid) {
        return features_valid;
    }

    return tl::expected<void, std::string>{};
}

template <>
tl::expected<void, std::string> is_valid<HwSheetConfig>(const nlohmann::ordered_json& json_value)
{
    for (const auto& key : {"id", "name", "type", "features"}) {
        if (!json_value.contains(key)) {
            return tl::unexpected{fmt::format("'{}' not present", key)};
        }
    }
    const auto features_valid{is_valid_map<FeatureValueMap>(json_value.at("features"))};
    if (!features_valid) {
        return features_valid;
    }

    return tl::expected<void, std::string>{};
}

tl::expected<HwPrinterConfig, std::string> load_hw_config(const ordered_json& json)
{
    HwPrinterConfig result;

    const auto valid{is_valid<HwPrinterConfig>(json)};
    if (!valid) {
        return tl::unexpected{"Invalid config structure: " + valid.error()};
    }

    const auto id{parse<std::string>(json.at("config_id"))};
    if (!id) {
        return tl::unexpected{"Invalid config_id: " + id.error()};
    }
    result.id = id.value();

    const auto printer_id{parse<std::string>(json.at("printer_id"))};
    if (!printer_id) {
        return tl::unexpected{"Invalid printer_id: " + printer_id.error()};
    }
    result.printer_id = printer_id.value();

    if (json.contains("legacy_printer_model")) {
        auto legacy_printer_model = parse<std::string>(json.at("legacy_printer_model"));
        if (!legacy_printer_model) {
            return tl::unexpected{"Invalid legacy_printer_model"};
        }
        result.legacy_printer_model = legacy_printer_model.value();
    }

    const auto vendor_id{parse<std::string>(json.at("vendor_id"))};
    if (!vendor_id) {
        return tl::unexpected{"Invalid vendor_id: " + vendor_id.error()};
    }
    result.vendor_id = vendor_id.value();

    const auto repo_id{parse<std::string>(json.at("repo_id"))};
    if (!repo_id) {
        return tl::unexpected{"Invalid repo_id: " + repo_id.error()};
    }
    result.repo_id = repo_id.value();
    const auto repo_version{parse<std::string>(json.at("repo_version"))};
    if (!repo_version) {
        return tl::unexpected{"Invalid repo_version: " + repo_version.error()};
    }
    result.repo_version = repo_version.value();

    const auto name{parse<std::string>(json.at("config_name"))};
    if (!name) {
        return tl::unexpected{"Invalid config_name: " + name.error()};
    }
    result.name = name.value();

    const auto short_name{parse<std::string>(json.at("config_short_name"))};
    result.short_name = short_name.has_value() ? short_name.value() : result.name;

    const auto technology{parse<std::string>(json.at("technology"))};
    if (!technology) {
        return tl::unexpected{"Invalid technology: " + technology.error()};
    }
    const auto technology_enum{enum_from_json<PrinterTechnology>(*technology)};
    if (!technology_enum) {
        return tl::unexpected{"Invalid technology enum: " + *technology};
    }
    result.technology = technology_enum.value();

    const auto model{parse<HwModel>(json)};
    if (!model) {
        return tl::unexpected{"Invalid model: " + model.error()};
    }
    result.model = model.value();

    if (json.contains("bed_model")) {
        auto bed_model = parse<std::string>(json.at("bed_model"));
        if (!bed_model) {
            return tl::unexpected{"Invalid bed_model: " + bed_model.error()};
        }
        result.visual.bed_model = bed_model.value();
    }
    if (json.contains("bed_texture")) {
        auto bed_texture = parse<std::string>(json.at("bed_texture"));
        if (!bed_texture) {
            return tl::unexpected{"Invalid bed_texture: " + bed_texture.error()};
        }
        result.visual.bed_texture = bed_texture.value();
    }
    if (json.contains("thumbnail")) {
        auto thumbnail = parse<std::string>(json.at("thumbnail"));
        if (!thumbnail) {
            return tl::unexpected{"Invalid thumbnail: " + thumbnail.error()};
        }
        result.visual.thumbnail = thumbnail.value();
    }

    const auto tool_count{parse<uint8_t>(json.at("tool_count"))};
    if (!tool_count) {
        return tl::unexpected{"Invalid tool_count: " + tool_count.error()};
    }
    result.tool_count = tool_count.value();

    const auto features{parse<FeatureValueMap>(json.at("features"))};
    if (!features) {
        return tl::unexpected{"Invalid features: " + features.error()};
    }
    result.features = features.value();

    auto tools_result = Domain::Preset::parse_tools(json.at("tools"));
    if (!tools_result) {
        return tl::unexpected{"Invalid tools: " + tools_result.error()};
    }

    if (tools_result->tools.size() != *tool_count) {
        return tl::unexpected{fmt::format(
            "Invalid tool count stored, expecting {} but found {}",
            *tool_count,
            tools_result->tools.size()
        )};
    }

    HwToolConfigs& tools = result.tools;
    tools.resize(*tool_count);

    const bool legacy_public_address_used =
        tools_result->tools.contains("1") && !tools_result->tools.contains("0");

    for (const auto& [key, value] : tools_result->tools) {
        const auto address{to_address(key)};
        if (!address) {
            return tl::unexpected{
                "Address could not be parsed from '" + key + "': " + address.error()
            };
        }
        if (address->size() != 1) {
            return tl::unexpected{fmt::format(
                "Invalid address \"{}\" for tools, expecting exactly single component, but found {} components",
                key,
                address->size()
            )};
        }
        auto slicer_address = address->at(0);
        if (legacy_public_address_used) {
            // shift address by -1
            slicer_address = address_from_legacy_public(slicer_address);
        }
        tools[slicer_address] = value;
    }

    HwFeederConfigs& feeders = result.feeders;
    for (const auto& [key, value] : tools_result->feeders) {
        const auto address{to_address(key)};
        if (!address) {
            return tl::unexpected{
                "Address could not be parsed from '" + key + "': " + address.error()
            };
        }
        feeders.insert({*address, value});
    }

    HwMaterialConfigs& materials = result.materials;
    for (const auto& [key, value] : tools_result->materials) {
        const auto address{to_address(key)};
        if (!address) {
            return tl::unexpected{
                "Address could not be parsed from '" + key + "': " + address.error()
            };
        }
        auto slicer_address = *address;
        if (legacy_public_address_used) {
            // shift address by -1
            slicer_address = address_from_legacy_public(slicer_address);
        }
        materials.insert({slicer_address, value});
    }

    const auto sheet{parse<HwSheetConfig>(json.at("sheet"))};
    if (!sheet) {
        return tl::unexpected{"Invalid sheet: " + sheet.error()};
    }
    result.sheet = sheet.value();

    return result;
}

} // namespace Slic3r::Biz::Config
