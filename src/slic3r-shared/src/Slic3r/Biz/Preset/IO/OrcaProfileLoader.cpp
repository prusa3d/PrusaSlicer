#include "Slic3r/Biz/Preset/IO/OrcaProfileLoader.hpp"
#include "OrcaProfileConverter.hpp"
#include "Slic3r/Biz/Preset/IO/PresetLoader.hpp"
#include "Slic3r/Domain/ConfigBoxesFDM.hpp"
#include "Slic3r/Directories.hpp"
#include "Slic3r/Log.hpp"

#include <boost/filesystem/operations.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/nowide/cstdlib.hpp>
#include <set>
#include <type_traits>

namespace Slic3r::Biz::Preset::IO {
namespace {
using Orca::Json;
namespace D = Domain;

template<class T> Json type_schema(const T& value)
{
    if constexpr (D::is_std_vector_v<T>) {
        auto result = type_schema(typename T::value_type{});
        result["vector"] = true;
        return result;
    } else if constexpr (std::is_same_v<T, D::EnumWrapper> || std::is_same_v<T, D::EnumVectorWrapper>) {
        Json values = Json::array();
        for (const auto& entry : value.def()) values.push_back(entry.str_serialized);
        return {{"type", "enum"}, {"vector", std::is_same_v<T, D::EnumVectorWrapper>}, {"values", values}};
    } else {
        std::string type;
        if constexpr (std::is_same_v<T, bool>) type = "bool";
        else if constexpr (std::is_same_v<T, int>) type = "int";
        else if constexpr (std::is_same_v<T, std::optional<int>>) type = "optional_int";
        else if constexpr (std::is_same_v<T, double>) type = "float";
        else if constexpr (std::is_same_v<T, std::string>) type = "string";
        else if constexpr (std::is_same_v<T, D::Vec2d>) type = "point";
        else if constexpr (std::is_same_v<T, D::Percentage>) type = "percent";
        else if constexpr (std::is_same_v<T, D::FloatOrPercentage>) type = "float_or_percent";
        else static_assert(!sizeof(T), "Unhandled configuration type");
        return {{"type", type}};
    }
}

Json schema_for(const D::ConfigBox& box)
{
    Json result = Json::object();
    const auto append = [&](const auto& items) {
        for (const auto& item : items) {
            auto spec = item.visit([](const auto& value) { return type_schema(value); });
            spec["tool_parity"] = item.def().require_tool_parity;
            result[item.name()] = std::move(spec);
        }
    };
    append(box.items.all_items());
    append(box.overrides.all_items());
    return result;
}

D::Preset::VendorData hardware(const Orca::Vendor& source)
{
    D::Preset::VendorData vendor;
    vendor.info.id = source.id;
    vendor.info.repo_id = source.id;
    vendor.info.name = source.name;
    vendor.info.version = source.version.empty() ? "1.0.0" : source.version;
    auto& defs = vendor.defs[D::PrinterTechnology::FFF];
    defs.technology = D::PrinterTechnology::FFF;
    D::Preset::HwSheetConfigDef sheet;
    sheet.id = "orca-default";
    sheet.name = "High Temp Plate (OrcaSlicer)";
    sheet.type = "pei_smooth";
    defs.sheets.emplace(sheet.id, sheet);
    for (const auto& machine : source.machines) {
        const auto name = machine.at("name").get<std::string>();
        const auto count = machine.at("__tools").get<uint8_t>();
        D::Preset::HwPrinterConfigDef printer;
        printer.id = name;
        printer.name = name;
        printer.technology = D::PrinterTechnology::FFF;
        printer.model = {name, machine.value("printer_model", name)};
        printer.tool_count = count;
        if (machine.contains("__visual")) {
            const auto& visual = machine.at("__visual");
            if (visual.contains("bed_model")) printer.visual.bed_model = visual.at("bed_model").get<std::string>();
            if (visual.contains("bed_texture")) printer.visual.bed_texture = visual.at("bed_texture").get<std::string>();
            if (visual.contains("thumbnail")) printer.visual.thumbnail = visual.at("thumbnail").get<std::string>();
        }
        printer.features["multi_extruder"].default_value = count > 1;
        defs.printers.emplace(name, std::move(printer));
        D::Preset::HwPrinterConfigTemplate config;
        config.id = source.id + "/" + name;
        config.name = name;
        config.printer = name;
        config.sheet = sheet.id;
        config.tool_count = count;
        // Orca stores one nozzle for an MMU printer. PS3 represents its five
        // materials with a feeder attached to that tool, not five tool heads.
        if (source.id == "Orca-Prusa" && count == 1
            && machine.value("printer_model", name).find("MMU3") != std::string::npos) {
            D::Preset::HwFeederConfigDef feeder;
            feeder.id = "orca-prusa-mmu3";
            feeder.name = "MMU3";
            feeder.technology = D::PrinterTechnology::FFF;
            feeder.type = D::Preset::FeederType::MMU;
            feeder.model = {"MMU3", "MMU3"};
            feeder.slot_count = 5;
            defs.feeders.emplace(feeder.id, feeder);
            config.feeders.push_back({feeder.id, {0}, {}});
        }
        for (const auto& nozzle : machine.at("nozzle_diameter")) {
            const auto diameter = nozzle.is_number() ? nozzle.get<double>() : std::stod(nozzle.get<std::string>());
            const auto id = "nozzle-" + std::to_string(diameter);
            D::Preset::HwToolConfigDef tool;
            tool.id = id;
            tool.name = std::to_string(diameter) + " mm";
            tool.technology = D::PrinterTechnology::FFF;
            tool.features["nozzle_diameter"].default_value = diameter;
            defs.tools.emplace(id, std::move(tool));
            config.tools.push_back({id, {}});
        }
        vendor.printer_configs.push_back(std::move(config));
    }
    return vendor;
}
} // namespace

std::vector<boost::filesystem::path> orca_profile_roots(const BundlePaths& paths)
{
    namespace fs = boost::filesystem;
    std::vector<fs::path> roots;
    if (const auto external = boost::nowide::getenv("PRUSASLICER_ORCA_PROFILES"); external && *external)
        roots.emplace_back(external);
    if (!paths.local_bundle_path.empty()) roots.emplace_back(paths.local_bundle_path);
    if (!paths.app_bundle_path.empty()) {
        roots.emplace_back(paths.app_bundle_path);
        roots.push_back(fs::path(paths.app_bundle_path).parent_path() / "profiles");
    }
    return roots;
}

void load_orca_profiles(const BundlePaths& paths, D::Preset::Bundle& bundle, bool include_prusa)
{
    namespace fs = boost::filesystem;
    const Orca::Schema schema{
        {"printer", schema_for(D::PrinterSettings{})}, {"print", schema_for(D::PrintSettings{})},
        {"tool_print", schema_for(D::ToolPrintSettings{})}, {"filament", schema_for(D::FilamentSettings{})}
    };
    Json reports = Json::array();
    // Explicit sources and local drop-ins override app drop-ins.
    // Native PS3 vendor IDs are never reused.
    std::set<std::string> loaded;
    for (const auto& [id, vendor] : bundle.vendor_bundles) loaded.insert(id);
    std::set<fs::path> visited_roots;
    const auto cache_root = paths.populate_local_bundle && !paths.local_bundle_path.empty()
        ? std::filesystem::u8path(paths.local_bundle_path) : std::filesystem::path{};
    for (const auto& root : orca_profile_roots(paths)) {
        if (root.empty()) continue;
        try {
            if (!visited_roots.insert(fs::weakly_canonical(root)).second) continue;
            for (auto& source : Orca::convert(std::filesystem::u8path(root.string()), schema, include_prusa, cache_root, loaded)) {
                if (bundle.vendor_bundles.contains(source.id) || !loaded.insert(source.id).second) continue;
                try {
                    if (!source.machines.empty()) {
                        D::Preset::VendorBundle vendor;
                        vendor.vendor_data = hardware(source);
                        if (paths.populate_local_bundle && !paths.local_bundle_path.empty()) {
                            const auto assets = fs::path(paths.local_bundle_path) / source.id / source.id / "assets";
                            for (const auto& [name, original] : source.assets) {
                                const auto destination = assets / name;
                                if (!source.cache_hit || !fs::is_regular_file(destination)) {
                                    fs::create_directories(destination.parent_path());
                                    fs::copy_file(fs::path(original.string()), destination, fs::copy_options::overwrite_existing);
                                }
                            }
                        }
                        PresetLoader loader;
                        for (const auto& preset : source.presets) loader.load_from_string(preset.dump());
                        const auto user_dir = paths.user_preset_dir_path(source.id, source.id);
                        if (!paths.user_bundle_path.empty() && fs::is_directory(user_dir)) {
                            try { loader.load_dir(user_dir.string(), D::Preset::PresetOrigin::User); }
                            catch (const std::exception& e) {
                                source.diagnostics.push_back({{"message", e.what()}, {"key", "user_presets"}});
                                SPDLOG_ERROR("Unable to load Orca user presets in {}: {}", user_dir.string(), e.what());
                            }
                        }
                        std::tie(vendor.presets, vendor.preset_names) = loader.release();
                        bundle.vendor_bundles.emplace(source.id, std::move(vendor));
                        SPDLOG_INFO("Loaded Orca vendor {}: {} printers ({})", source.name, source.machines.size(),
                            source.cache_hit ? "cached conversion" : "converted source");
                    }
                } catch (const std::exception& e) {
                    source.diagnostics.push_back({{"message", e.what()}, {"key", "vendor"}});
                    SPDLOG_ERROR("Unable to load Orca vendor {}: {}", source.id, e.what());
                }
                if (!source.diagnostics.empty())
                    SPDLOG_WARN("Orca vendor {} has {} import diagnostics; see orca-profile-import-report.json", source.id, source.diagnostics.size());
                reports.push_back({{"source", root.string()}, {"vendor", source.id}, {"printers", source.machines.size()},
                    {"version", source.version}, {"cached", source.cache_hit},
                    {"loaded", bundle.vendor_bundles.contains(source.id)},
                    {"diagnostics", std::move(source.diagnostics)}});
            }
        } catch (const std::exception& e) {
            SPDLOG_ERROR("Unable to read Orca profiles in {}: {}", root.string(), e.what());
            reports.push_back({{"source", root.string()}, {"loaded", false}, {"diagnostics", {{{"message", e.what()}}}}});
        }
    }
    if (!reports.empty() && paths.populate_local_bundle && !Slic3r::data_dir().empty()) {
        const auto path = fs::path(Slic3r::data_dir()) / "orca-profile-import-report.json";
        boost::nowide::ofstream report(path.string(), std::ios::binary | std::ios::trunc);
        if (report) report << reports.dump(2) << '\n';
        else SPDLOG_WARN("Unable to write {}", path.string());
    }
}
} // namespace Slic3r::Biz::Preset::IO
