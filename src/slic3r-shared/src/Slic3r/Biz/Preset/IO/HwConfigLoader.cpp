#include "Slic3r/Biz/Preset/IO/HwConfigLoader.hpp"
#include "Slic3r/Biz/Yaml/YamlSlic3rTypes.hpp"
#include "Slic3r/Log.hpp"
#include "Slic3r/Biz/Config/HwConfigJson.hpp" // IWYU pragma: keep

#include <concepts>
#include <ranges>
#include <set>

#include "Slic3r/Biz/Yaml/Yaml.hpp"

#include <charconv>
#include <nlohmann/json.hpp>
#include <boost/filesystem/directory.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/nowide/fstream.hpp>
#include <fmt/ranges.h>

using namespace Slic3r::Domain::Preset;
namespace fs = boost::filesystem;
using nlohmann::ordered_json;

STRUCT_DESC(HwModel, FIELD_DESC_SIMPLE(model), FIELD_DESC_SIMPLE(base_model));

STRUCT_DESC(HwToolConfig, FIELD_DESC_SIMPLE(id), FIELD_DESC_SIMPLE(features));

STRUCT_DESC_SIMPLE(HwFeederConfig, type, model, slot_count, features);
STRUCT_DESC_SIMPLE(MaterialConfig, features);
STRUCT_DESC_SIMPLE(HwSheetConfig, id, name, type, features);
STRUCT_DESC_SIMPLE(VisualRepresentation, bed_model, bed_texture, thumbnail);
STRUCT_DESC_SIMPLE(
    HwPrinterConfig,
    id,
    printer_id,
    vendor_id,
    name,
    short_name,
    technology,
    model,
    tool_count,
    features,
    tools,
    feeders,
    materials,
    sheet,
    visual
);

STRUCT_DESC(
    FeatureDef,
    FIELD_DESC(allowed_values, FIELD_DEFAULT, {}, FIELD_DEFAULT),
    FIELD_DESC(default_value, "default", FIELD_DEFAULT, FIELD_DEFAULT),
    FIELD_DESC(user_editable, FIELD_DEFAULT, true, FIELD_DEFAULT)
);

STRUCT_DESC(
    HwPrinterConfigDef,
    FIELD_DESC_SIMPLE(id),
    FIELD_DESC_SIMPLE(name),
    FIELD_DESC_SIMPLE(model),
    FIELD_DESC_SIMPLE(technology),
    FIELD_DESC_IMPLICIT_VALUE(legacy_printer_model, {}),
    FIELD_DESC(features, FIELD_DEFAULT, {}, FIELD_DEFAULT),
    FIELD_DESC(tool_count, FIELD_DEFAULT, 1, FIELD_DEFAULT),
    FIELD_DESC(visual, FIELD_DEFAULT, {}, FIELD_DEFAULT)
);

STRUCT_DESC(
    HwToolConfigDef,
    FIELD_DESC_SIMPLE(id),
    FIELD_DESC_SIMPLE(name),
    FIELD_DESC_SIMPLE(condition),
    FIELD_DESC_SIMPLE(technology),
    FIELD_DESC(features, FIELD_DEFAULT, {}, FIELD_DEFAULT)
);

STRUCT_DESC(
    HwFeederConfigDef,
    FIELD_DESC_SIMPLE(id),
    FIELD_DESC_SIMPLE(name),
    FIELD_DESC_SIMPLE(model),
    FIELD_DESC_SIMPLE(technology),
    FIELD_DESC(type, FIELD_DEFAULT, FeederType::MMU, FIELD_DEFAULT),
    FIELD_DESC(features, FIELD_DEFAULT, {}, FIELD_DEFAULT),
    FIELD_DESC_SIMPLE(slot_count),
    FIELD_DESC_SIMPLE(condition)
);

STRUCT_DESC(
    HwSheetConfigDef,
    FIELD_DESC_SIMPLE(id),
    FIELD_DESC_SIMPLE(name),
    FIELD_DESC_SIMPLE(type),
    FIELD_DESC_SIMPLE(condition),
    FIELD_DESC(features, FIELD_DEFAULT, {}, FIELD_DEFAULT)
)

STRUCT_DESC(
    HwToolConfigTemplate,
    FIELD_DESC(id, "tool", FIELD_DEFAULT, FIELD_DEFAULT),
    FIELD_DESC(features, FIELD_DEFAULT, {}, FIELD_DEFAULT)
);

STRUCT_DESC(
    HwFeederConfigTemplate,
    FIELD_DESC(id, "feeder", FIELD_DEFAULT, FIELD_DEFAULT),
    FIELD_DESC(features, FIELD_DEFAULT, {}, FIELD_DEFAULT),
    FIELD_DESC_SIMPLE(address)
);

STRUCT_DESC(
    HwPrinterConfigTemplate,
    FIELD_DESC_SIMPLE(id),
    FIELD_DESC_SIMPLE(name),
    FIELD_DESC_SIMPLE(printer),
    FIELD_DESC_IMPLICIT_VALUE(legacy_printer_model, {}),
    FIELD_DESC_SIMPLE(sheet),
    FIELD_DESC_SIMPLE(tool_count),
    FIELD_DESC(features, FIELD_DEFAULT, {}, FIELD_DEFAULT),
    FIELD_DESC(tools, FIELD_DEFAULT, {}, FIELD_DEFAULT),
    FIELD_DESC(feeders, FIELD_DEFAULT, {}, FIELD_DEFAULT),
    FIELD_DESC(visual, FIELD_DEFAULT, {}, FIELD_DEFAULT)
);

STRUCT_DESC(
    VendorFeatures,
    FIELD_DESC(printer, FIELD_DEFAULT, {}, FIELD_DEFAULT),
    FIELD_DESC(tool, FIELD_DEFAULT, {}, FIELD_DEFAULT),
    FIELD_DESC(feeder, FIELD_DEFAULT, {}, FIELD_DEFAULT),
    FIELD_DESC(sheet, FIELD_DEFAULT, {}, FIELD_DEFAULT)
);

STRUCT_DESC(
    VendorInfo,
    FIELD_DESC_SIMPLE(id),
    FIELD_DESC_SIMPLE(name),
    FIELD_DESC_SIMPLE(version),
    FIELD_DESC_SIMPLE(features)
);

namespace Slic3r::Biz::Preset::IO {

HwConfigLoader::HwConfigLoader()
{
    init_result();
}

void HwConfigLoader::init_result()
{
    m_result = {
        .defs = {
            {Domain::PrinterTechnology::FFF, {Domain::PrinterTechnology::FFF}},
            {Domain::PrinterTechnology::SLA, {Domain::PrinterTechnology::SLA}}
        }
    };
}

Domain::Preset::VendorData& HwConfigLoader::load(const std::string& filename)
{
    // clang-format off
    Yaml::parse_all_documents_in_file(
        filename.c_str(),
        [this](const auto& doc) {
            Yaml::parse_structs_by_discriminant(
                doc.root(),
            "kind",
                std::make_tuple(
                    "printer",
                    std::function{[this](Domain::Preset::HwPrinterConfigDef&& p) {
                        m_result.defs[p.technology].printers.emplace(p.id, p);
                    }}
                ),
            std::make_tuple(
                "tool",
                std::function{[this](Domain::Preset::HwToolConfigDef&& t) {
                    m_result.defs[t.technology].tools.emplace(t.id, t);
                }}
            ),
            std::make_tuple(
                "feeder",
                std::function{[this](Domain::Preset::HwFeederConfigDef&& f) {
                    m_result.defs[f.technology].feeders.emplace(f.id, f);
                }}
            ),
            std::make_tuple(
                "sheet",
                std::function{[this](Domain::Preset::HwSheetConfigDef&& s) {
                    m_result.defs[Domain::PrinterTechnology::FFF].sheets.emplace(s.id, s);
                }}
            ),
            std::make_tuple(
                "printer_config",
                std::function{[this](HwPrinterConfigTemplate&& p) {
                    m_result.printer_configs.emplace_back(p);
                }}
            ),
            std::make_tuple(
                "vendor",
                std::function{[this](VendorInfo&& v) {
                    m_result.info = v;
                }}
            )
        );
    });
    // clang-format on

    return m_result;
}

Domain::Preset::VendorInfo HwConfigLoader::load_info_only(const std::string& filename)
{
    VendorInfo result;

    // clang-format off
    Yaml::parse_all_documents_in_file(
        filename.c_str(),
        [&result](const auto& doc) {
            Yaml::parse_structs_by_discriminant(
                doc.root(),
            "kind",
                std::make_tuple(
                    "printer",
                    Yaml::LoaderFunc<HwPrinterConfigDef>{}
                ),
            std::make_tuple(
                "tool",
                Yaml::LoaderFunc<HwToolConfigDef>{}
            ),
            std::make_tuple(
                "feeder",
                Yaml::LoaderFunc<FeatureDef>{}
            ),
            std::make_tuple(
                "sheet",
                Yaml::LoaderFunc<HwSheetConfigDef>{}
            ),
            std::make_tuple(
                "printer_config",
                Yaml::LoaderFunc<HwPrinterConfigTemplate>{}
            ),
            std::make_tuple(
                "vendor",
                std::function{[&result](VendorInfo&& v) {
                    result = v;
                }}
            )
        );
    });
    // clang-format on

    return result;
}

Domain::Preset::HwPrinterConfigs load_vendor_user_configs(
    const std::string& dir_path,
    const Domain::Preset::VendorData& vendor_data
)
{
    HwPrinterConfigs ret;

    if (!fs::exists(dir_path)) {
        SPDLOG_DEBUG("Skipping loading vendor configs from directory '{}' as it not exists", dir_path);
        return ret;
    }

    for (const auto& entry : fs::directory_iterator{dir_path}) {
        if (!entry.is_regular_file())
            continue;

        auto p = entry.path();
        if (p.extension() != ".json")
            continue;

        boost::nowide::ifstream f(p.string());
        const ordered_json j = ordered_json::parse(f);
        const auto loading_result{Config::load_hw_config(j)};

        if (!loading_result.has_value()) {
            SPDLOG_INFO("Parsing HwPrinterConfig {} failed: {}", p.string(), loading_result.error());
        }

        HwPrinterConfig config{loading_result.value_or(HwPrinterConfig{})};
        fill_missing_features_with_default(config, vendor_data);

        ret.push_back(config);
    }

    return ret;
}

void save_vendor_user_configs(
    const Domain::Preset::HwPrinterConfigs& configs,
    const std::string& dir_path,
    const Domain::Preset::VendorData& vendor_data
)
{
    fs::path dir{dir_path};
    if (!fs::exists(dir)) {
        fs::create_directories(dir);
    }

    size_t idx = 0;

    for (const auto& base_config : configs) {
        auto config = remove_features_with_default(base_config, vendor_data);

        ordered_json j = config;

        std::string name = config.name;
        if (name.empty())
            name = fmt::format("printer{:03d}", idx++);
        std::ofstream out{(dir / (name + ".json")).string()};
        out << j;
        out.close();
    }
}

} // namespace Slic3r::Biz::Preset::IO
