#include "Slic3r/Biz/Preset/IO/OrcaProfileLoader.hpp"
#include "Slic3r/Biz/Preset/HwConfigEvaluator.hpp"
#include "Slic3r/Biz/Preset/PresetEvaluator.hpp"
#include "Slic3r/Biz/Parser/PlaceholderParser.hpp"
#include "Slic3r/Domain/FullConfigFDM.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
namespace D = Slic3r::Domain;
namespace P = Slic3r::Biz::Preset;
using Json = nlohmann::json;

static void require(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}

static void check_prusa_gcode(const D::Preset::HwPrinterConfig& hw,
    const D::Preset::EvaluatedPrinterPreset& printer, const D::Preset::EvaluatedPrintPreset& print)
{
    D::ConfigPackFDM pack(static_cast<int>(hw.material_slot_count()));
    pack.printer = std::get<D::PrinterSettings>(printer.preset.values);
    pack.print = std::get<D::PrintSettings>(print.preset.values);
    std::vector<std::string> filament_names;
    for (size_t i = 0; i < pack.filament.size(); ++i) {
        const auto& materials = print.materials.at(i);
        auto selected = std::find_if(materials.begin(), materials.end(), [](const auto& f) {
            return f.preset.name.starts_with("Prusa") && f.preset.name.find("PLA") != std::string::npos;
        });
        if (selected == materials.end()) selected = materials.begin();
        pack.filament[i] = std::get<D::FilamentSettings>(selected->preset.values);
        filament_names.push_back(selected->preset.name);
    }
    auto config = std::make_shared<D::FullConfigFDM>(pack, std::vector<unsigned>{0}, hw);
    D::ConfigView view(config, {});
    view.finalize();
    Slic3r::Biz::Parser::PlaceholderParser parser(Slic3r::Biz::Parser::IO::get_parser_config(view));
    for (const auto* key : {"initial_tool", "current_extruder", "previous_extruder", "filament_extruder_id", "layer_num"}) parser.set(key, 0);
    parser.set("next_extruder", hw.tool_count > 1 ? 1 : 0);
    parser.set("filament_settings_id", filament_names);
    parser.set("printer_settings_id", printer.preset.name);
    parser.set("print_settings_id", print.preset.name);
    parser.set("num_extruders", static_cast<int>(hw.material_slot_count()));
    parser.set("total_layer_count", 100);
    parser.set("extruded_weight_total", 0.0);
    parser.set("extruded_volume_total", 0.0);
    parser.set("extruded_weight", std::vector<double>(hw.material_slot_count(), 0.0));
    parser.set("extruded_volume", std::vector<double>(hw.material_slot_count(), 0.0));
    parser.set("has_wipe_tower", false);
    parser.set("has_single_extruder_multi_material_priming", false);
    parser.set("total_toolchanges", 0);
    parser.set("zhop", 0.0);
    parser.set("layer_z", 0.2);
    parser.set("max_layer_z", 20.0);
    parser.set("first_layer_print_min", std::vector<double>{30, 30});
    parser.set("first_layer_print_max", std::vector<double>{70, 70});
    parser.set("print_bed_min", std::vector<double>{0, 0});
    parser.set("print_bed_max", std::vector<double>{180, 180});
    std::vector<bool> used(255, false);
    used[0] = true;
    parser.set("is_extruder_used", used);
    for (const auto* key : {"start_gcode", "before_layer_gcode", "layer_gcode", "toolchange_gcode", "end_gcode"}) {
        const auto script = pack.printer.find(key).item->value().get<std::string>();
        for (bool sequential : {false, true}) {
            parser.set("complete_objects", sequential);
            Slic3r::Biz::Parser::IO::Config outputs;
            outputs.set("position", std::vector<double>{40, 40, 0.2});
            outputs.set("e_retracted", std::vector<double>(hw.material_slot_count(), 0.0));
            outputs.set("e_restart_extra", std::vector<double>(hw.material_slot_count(), 0.0));
            outputs.set("e_position", std::vector<double>(hw.material_slot_count(), 0.0));
            Slic3r::Biz::Parser::PlaceholderParser::ContextData context;
            try { parser.process(script, 0, nullptr, &outputs, &context); }
            catch (const std::exception& e) { throw std::runtime_error(hw.name + " / " + print.preset.name + " / " + key + ": " + e.what()); }
        }
    }
}

int main(int argc, char** argv)
{
    const auto root = fs::temp_directory_path() / ("ps-orca-native-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        const auto write = [&](const char* name, const Json& value) {
            const auto path = root / name;
            fs::create_directories(path.parent_path());
            std::ofstream(path) << value.dump();
        };
        write("profiles/NativeTest.json", {
            {"name", "Native Test Vendor"},
            {"machine_model_list", {{{"name", "Test model"}, {"sub_path", "machine/model.json"}}}},
            {"machine_list", {{{"name", "Test"}, {"sub_path", "machine/test.json"}}}},
            {"process_list", {{{"name", "Normal"}, {"sub_path", "process/normal.json"}}}},
            {"filament_list", {{{"name", "PLA"}, {"sub_path", "filament/pla.json"}}}}
        });
        write("profiles/NativeTest/machine/test.json", {
            {"printer_model", "Test model"},
            {"type", "machine"}, {"name", "Native test printer"}, {"instantiation", "true"},
            {"nozzle_diameter", {"0.4", "0.4", "0.4", "0.4"}}, {"gcode_flavor", "klipper"},
            {"thumbnails", "48x48"},
            {"printable_area", {"0,0", "270,0", "270,270", "0,270"}}, {"printable_height", "270"},
            {"bed_mesh_min", "3,3"}, {"bed_mesh_max", "267,267"}, {"bed_mesh_probe_distance", "50,50"},
            {"machine_start_gcode", "M104 T[initial_extruder] S[nozzle_temperature_initial_layer]\nBED_MESH_CALIBRATE mesh_min={adaptive_bed_mesh_min[0]},{adaptive_bed_mesh_min[1]} mesh_max={adaptive_bed_mesh_max[0]},{adaptive_bed_mesh_max[1]} PROBE_COUNT={bed_mesh_probe_count[0]},{bed_mesh_probe_count[1]} ALGORITHM={bed_mesh_algo}\n"}
        });
        write("profiles/NativeTest/machine/model.json", {
            {"type", "machine_model"}, {"name", "Test model"},
            {"bed_model", "bed.stl"}, {"bed_texture", "bed.svg"}
        });
        for (const auto* asset : {"bed.stl", "bed.svg", "Test model_cover.png"})
            std::ofstream(root / "profiles/NativeTest" / asset) << "asset fixture " << asset;
        write("profiles/NativeTest/process/normal.json", {
            {"type", "process"}, {"name", "Normal"}, {"instantiation", "true"},
            {"wall_loops", "3"}, {"layer_height", "0.2"}, {"compatible_printers", {"Native test printer"}}
        });
        write("profiles/NativeTest/filament/pla.json", {
            {"type", "filament"}, {"name", "PLA"}, {"instantiation", "true"},
            {"filament_retract_before_wipe", {"nil", "nil", "nil", "nil"}},
            {"nozzle_temperature", {"210"}}, {"nozzle_temperature_initial_layer", {"215"}},
            {"filament_shrink", {"100%"}}, {"filament_shrinkage_compensation_z", {"99.5%"}},
            {"filament_flush_temp", {"245"}}, {"filament_flush_volumetric_speed", {"18"}}
        });
        for (bool presets_layout : {false, true}) {
            if (presets_layout) fs::rename(root / "profiles", root / "presets");
            D::Preset::Bundle bundle;
            P::IO::BundlePaths paths;
            paths.app_bundle_path = (root / "presets").string();
            paths.local_bundle_path = (root / "local").string();
            paths.populate_local_bundle = true;
            D::Preset::Bundle catalog;
            P::IO::load_orca_profiles(paths, catalog, false, true);
            const auto& catalog_vendor = catalog.vendor_bundles.at("Orca-NativeTest");
            P::HwConfigEvaluator catalog_hw_eval;
            const auto catalog_hw = catalog_hw_eval.create_printer_config(
                catalog_vendor.vendor_data.printer_configs.at(0), catalog_vendor.vendor_data);
            P::PresetEvaluator catalog_eval(catalog_vendor.presets);
            const auto catalog_printers = catalog_eval.evaluate(catalog_hw, true, true);
            require(catalog_printers.size() == 1 && catalog_printers[0].prints.empty(),
                "catalog evaluates printer identity without slicing presets");
            require(catalog_hw.visual.thumbnail == "Test model_cover.png", "catalog retains thumbnail");
            paths.orca_selected_printers["Orca-NativeTest"].insert("Native test printer");
            P::IO::load_orca_profiles(paths, bundle, false, true);
            const auto& vendor = bundle.vendor_bundles.at("Orca-NativeTest");
            P::HwConfigEvaluator hw_eval;
            const auto hw = hw_eval.create_printer_config(vendor.vendor_data.printer_configs.at(0), vendor.vendor_data);
            require(vendor.vendor_data.info.name == "Native Test Vendor (OrcaSlicer)", "vendor display name comes from manifest");
            require(hw.visual.bed_model == "bed.stl" && hw.visual.bed_texture == "bed.svg"
                && hw.visual.thumbnail == "Test model_cover.png", "model artwork survives hardware evaluation");
            for (const auto* asset : {"bed.stl", "bed.svg", "Test model_cover.png"})
                require(fs::is_regular_file(root / "local/Orca-NativeTest/Orca-NativeTest/assets" / asset), "artwork is staged for the resource resolver");
            require(hw.tool_count == 4 && hw.tools.size() == 4, "four physical tools survive import");
            P::PresetEvaluator eval(vendor.presets);
            const auto printers = eval.evaluate(hw);
            require(printers.size() == 1 && printers[0].prints.size() == 1, "printer and compatible process evaluate");
            const auto& print = printers[0].prints[0];
            const auto& filament = std::get<D::FilamentSettings>(print.materials.at(0).at(0).preset.values);
            require(filament.find("filament_shrinkage_compensation_xy").item->value().get<D::Percentage>().value == 0.0,
                "Orca neutral XY shrinkage must not enlarge the sliced model");
            require(filament.find("filament_shrinkage_compensation_z").item->value().get<D::Percentage>().value == 0.5,
                "Orca retained Z dimensions convert to lost percentage");
            const auto parameters = Json::parse(filament.find("custom_parameters_filament").item->value().get<std::string>());
            require(parameters.at("orca_flush_temperature") == 245.0 && parameters.at("orca_flush_speed") == 18.0,
                "filament purge settings survive as editable native custom parameters");
            require(print.tools.size() == 4 && !print.materials.empty(), "tool and material choices evaluate");
            for (const auto& tool : print.tools) require(!tool.empty(), "each tool has a process preset");
            const auto& settings = std::get<D::PrinterSettings>(printers[0].preset.values);
            require(settings.find("thumbnails").item->value().get<std::string>() == "48x48/PNG", "single thumbnail remains a string");
            require(settings.find("bed_shape").item->value().get<D::Vec2ds>().size() == 4, "bed polygon converts to native points");
            const auto script = settings.find("start_gcode").item->value().get<std::string>();
            Slic3r::Biz::Parser::PlaceholderParser parser;
            parser.set("initial_tool", 2);
            parser.set("first_layer_temperature", std::vector<int>{200, 205, 215, 220});
            parser.set("first_layer_print_min", std::vector<double>{1, 20});
            parser.set("first_layer_print_max", std::vector<double>{269, 240});
            const auto gcode = parser.process(script);
            require(gcode.find("M104 T2 S215") != std::string::npos, "native parser uses selected tool temperature");
            require(gcode.find("mesh_min=3,20 mesh_max=267,240 PROBE_COUNT=7,6 ALGORITHM=bicubic") != std::string::npos,
                "native parser computes bounded mesh and ceiling probe counts");
        }
        write("presets/Prusa.json", {
            {"machine_list", {{{"name", "MMU"}, {"sub_path", "machine/mmu.json"}}}},
            {"process_list", {{{"name", "Normal"}, {"sub_path", "process/normal.json"}}}},
            {"filament_list", {{{"name", "PLA"}, {"sub_path", "filament/pla.json"}}}}
        });
        write("presets/Prusa/machine/mmu.json", {
            {"type", "machine"}, {"name", "Prusa CORE One MMU3 0.4 nozzle"},
            {"printer_model", "Prusa CORE One MMU3"}, {"instantiation", "true"},
            {"nozzle_diameter", {"0.4"}}, {"single_extruder_multi_material", "1"},
            {"before_layer_change_gcode", "G92 E0.0\n"}
        });
        write("presets/Prusa/process/normal.json", {
            {"type", "process"}, {"name", "Normal"}, {"instantiation", "true"}, {"layer_height", "0.2"}
        });
        write("presets/Prusa/filament/pla.json", {
            {"type", "filament"}, {"name", "PLA"}, {"instantiation", "true"}, {"nozzle_temperature", {"215"}}
        });
        {
            D::Preset::Bundle bundle;
            P::IO::BundlePaths paths;
            paths.app_bundle_path = (root / "presets").string();
            P::IO::load_orca_profiles(paths, bundle);
            require(!bundle.vendor_bundles.contains("Orca-Prusa"), "Prusa conversion is excluded by default");
            P::IO::load_orca_profiles(paths, bundle, true);
            const auto& vendor = bundle.vendor_bundles.at("Orca-Prusa");
            P::HwConfigEvaluator hw_eval;
            const auto hw = hw_eval.create_printer_config(vendor.vendor_data.printer_configs.at(0), vendor.vendor_data);
            require(hw.tool_count == 1 && hw.material_slot_count() == 5, "Prusa MMU has one nozzle and five material slots");
            P::PresetEvaluator eval(vendor.presets);
            const auto evaluated = eval.evaluate(hw);
            require(evaluated.at(0).prints.at(0).materials.size() == 5, "all MMU material slots have preset choices");
            check_prusa_gcode(hw, evaluated.at(0), evaluated.at(0).prints.at(0));
        }
        fs::remove_all(root);
        std::cout << "Native Orca loader, preset evaluator and G-code parser checks passed\n";
        if (argc > 1) {
            D::Preset::Bundle bundle;
            P::IO::BundlePaths paths;
            paths.app_bundle_path = argv[1];
            P::IO::load_orca_profiles(paths, bundle, true);
            size_t vendors = 0, printers = 0;
            for (const auto& [id, vendor] : bundle.vendor_bundles) {
                P::HwConfigEvaluator hw_eval;
                P::PresetEvaluator eval(vendor.presets);
                ++vendors;
                for (const auto& templ : vendor.vendor_data.printer_configs) {
                    const auto hw = hw_eval.create_printer_config(templ, vendor.vendor_data);
                    const auto evaluated = eval.evaluate(hw);
                    if (evaluated.empty() || evaluated.front().prints.empty())
                        throw std::runtime_error("No usable process for " + templ.name);
                    for (const auto& printer : evaluated) for (const auto& print : printer.prints) {
                        if (print.tools.size() != hw.tool_count || print.materials.size() != hw.material_slot_count())
                            throw std::runtime_error("Missing tool or material slots for " + templ.name);
                        for (const auto& tool : print.tools)
                            if (tool.empty()) throw std::runtime_error("No tool process for " + templ.name);
                        for (const auto& material : print.materials)
                            if (material.empty()) throw std::runtime_error("No material for " + templ.name + " / " + print.preset.name);
                        if (id == "Orca-Prusa") check_prusa_gcode(hw, printer, print);
                    }
                    ++printers;
                }
            }
            require(vendors > 0 && printers > 0, "profile tree contains selectable printers");
            std::cout << "Native corpus evaluation passed: " << vendors << " vendors, " << printers << " printers\n";
        }
        return 0;
    } catch (const std::exception& e) {
        fs::remove_all(root);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
