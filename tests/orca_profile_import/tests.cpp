#include "OrcaProfileConverter.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace O = Slic3r::Biz::Preset::IO::Orca;
namespace fs = std::filesystem;
using O::Json;

void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void write(const fs::path& path, const Json& value)
{
    fs::create_directories(path.parent_path());
    std::ofstream(path) << value.dump();
}

O::Schema schema()
{
    return {
        {"printer", {{"start_gcode", {{"type", "string"}}}, {"layer_gcode", {{"type", "string"}}},
            {"before_layer_gcode", {{"type", "string"}}}, {"thumbnails", {{"type", "string"}}},
            {"use_relative_e_distances", {{"type", "bool"}}}, {"max_print_height", {{"type", "float"}}}}},
        {"print", {{"perimeters", {{"type", "int"}}}, {"retract_length", {{"type", "float"}}},
            {"preheat_time", {{"type", "float"}}}, {"preheat_steps", {{"type", "int"}}},
            {"prime_volume", {{"type", "float"}}},
            {"orca_fixed_prime_volume", {{"type", "bool"}}},
            {"orca_matrix_flush", {{"type", "bool"}}},
            {"orca_matrix_flush_multiplier", {{"type", "float"}}},
            {"wipe_tower_max_purge_speed", {{"type", "float"}}},
            {"orca_wipe_compatibility", {{"type", "bool"}}},
            {"role_based_wipe_speed", {{"type", "bool"}}},
            {"wipe_speed", {{"type", "float_or_percent"}}},
            {"wipe_distance", {{"type", "float"}}},
            {"retract_after_wipe", {{"type", "percent"}}},
            {"small_perimeter_threshold", {{"type", "float"}}},
            {"small_perimeter_speed", {{"type", "float_or_percent"}}},
            {"perimeter_speed", {{"type", "float"}}},
            {"external_perimeter_speed", {{"type", "float_or_percent"}}},
            {"slowdown_for_curled_perimeters", {{"type", "bool"}}},
            {"orca_perimeter_speed_compatibility", {{"type", "bool"}}},
            {"bridge_acceleration", {{"type", "float"}}},
            {"first_layer_infill_speed", {{"type", "float_or_percent"}}},
            {"first_layer_solid_infill_speed", {{"type", "float_or_percent"}}},
            {"overhang_speed_0", {{"type", "float_or_percent"}}},
            {"overhang_speed_1", {{"type", "float_or_percent"}}},
            {"overhang_speed_2", {{"type", "float_or_percent"}}},
            {"overhang_speed_3", {{"type", "float_or_percent"}}},
            {"enable_dynamic_overhang_speeds", {{"type", "bool"}}},
            {"fill_pattern", {{"type", "enum"}, {"values", {"grid", "stars"}}}},
            {"automatic_infill_combination", {{"type", "bool"}}},
            {"gcode_label_objects", {{"type", "enum"}, {"values", {"disabled", "octoprint", "firmware"}}}},
            {"ensure_vertical_shell_thickness", {{"type", "enum"}, {"values", {"disabled", "partial", "enabled"}}}},
            {"default_tool_print", {{"type", "string"}}}, {"default_material", {{"type", "string"}}}}},
        {"tool_print", Json::object()},
        {"filament", {{"temperature", {{"type", "int"}}}, {"extrusion_multiplier", {{"type", "float"}}},
            {"wipe_distance", {{"type", "float"}}}, {"retract_after_wipe", {{"type", "percent"}}},
            {"idle_temperature", {{"type", "optional_int"}}}}}
    };
}

int main(int argc, char** argv)
{
    const auto root = fs::temp_directory_path() / ("ps-orca-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        check(O::rewrite_gcode("M104 S[nozzle_temperature_initial_layer]") == "M104 S{first_layer_temperature[initial_tool]}", "legacy temperature placeholder");
        check(O::rewrite_gcode("{nozzle_temperature[2]}") == "{temperature[2]}", "explicit tool index");
        check(O::rewrite_gcode("{initial_extruder}") == "{initial_tool}", "initial tool alias");
        check(O::rewrite_gcode("{outer_wall_speed}") == "{external_perimeter_speed[initial_tool]}", "process vectors require a tool index");
        check(O::rewrite_gcode("{if idle_temperature[initial_extruder] == 0}70{else}{idle_temperature[initial_extruder]}{endif}")
            == "{if is_nil(idle_temperature[initial_tool])}70{else}{idle_temperature[initial_tool]}{endif}", "idle zero sentinel becomes a nil check");
        check(O::rewrite_gcode("{idle_temperature[2] != 0 ? idle_temperature[2] : 70}")
            == "{!is_nil(idle_temperature[2]) ? idle_temperature[2] : 70}", "explicit idle temperature sentinel inequality");
        check(O::rewrite_gcode("{is_nil(idle_temperature[2])}") == "{is_nil(idle_temperature[2])}", "native nil check stays unchanged");
        check(O::rewrite_gcode("{idle_temperature[2] == 0 + 1}") == "{idle_temperature[2] == 0 + 1}", "idle arithmetic is not a sentinel check");
        check(O::rewrite_gcode("{temperature[2] + idle_temperature[2] == 0}")
            == "{temperature[2] + idle_temperature[2] == 0}", "idle arithmetic on the left is not a sentinel check");
        check(O::rewrite_gcode("{\"idle_temperature[2] == 0\"}") == "{\"idle_temperature[2] == 0\"}",
            "quoted idle checks stay literal");
        const auto flush = O::rewrite_gcode("{flush_volumetric_speeds[initial_extruder]}");
        check(flush == "{(custom_parameter_filament_orca_flush_speed[initial_tool] > 0 ? custom_parameter_filament_orca_flush_speed[initial_tool] : filament_max_volumetric_speed[initial_tool])}",
            "purge speed preserves a selected filament override and runtime fallback");
        check(O::rewrite_gcode(flush) == flush, "purge expression rewrite is idempotent");
        check(O::rewrite_gcode("{if !spiral_mode}M74 W[extruded_weight_total]{endif}") == "{if !spiral_vase}M74 W[extruded_weight_total]{endif}", "vase-mode condition uses native variable");
        check(O::rewrite_gcode("[nozzle_temperature_2]") == "{temperature[2]}", "legacy indexed alias");
        check(O::rewrite_gcode("[nozzle_temperature_[initial_extruder]]") == "{temperature[initial_tool]}", "nested legacy index");
        check(O::rewrite_gcode("; nozzle_temperature\n{\"nozzle_temperature print_sequence\"}") == "; nozzle_temperature\n{\"nozzle_temperature print_sequence\"}", "quoted strings and plain comments preserved");
        const std::string original = "{if temperature[initial_tool] > 0}M104 S{temperature[initial_tool]}{endif}";
        check(O::rewrite_gcode(original) == original, "existing PS syntax preserved");
        const auto rewritten = O::rewrite_gcode("{if print_sequence == \"by object\"}G28{endif}");
        check(O::rewrite_gcode(rewritten) == rewritten, "rewrite is idempotent");
        write(root / "Example.json", {{"machine_list", {{{"name", "Base"}, {"sub_path", "machine/base.json"}},
            {{"name", "Printer"}, {"sub_path", "machine/printer.json"}}}},
            {"process_list", {{{"name", "Normal"}, {"sub_path", "process/normal.json"}}}},
            {"filament_list", {{{"name", "PLA"}, {"sub_path", "filament/pla.json"}}}}});
        write(root / "Example/machine/base.json", {{"type", "machine"}, {"name", "Base"}, {"instantiation", "false"},
            {"printable_height", "250"}, {"retraction_length", {"0.8"}}, {"nozzle_diameter", {"0.4"}},
            {"default_filament_profile", {"PLA"}}});
        write(root / "Example/machine/printer.json", {{"type", "machine"}, {"name", "Printer"}, {"instantiation", "true"},
            {"inherits", "Base"}, {"printable_height", "300"}, {"machine_start_gcode", "M104 S[nozzle_temperature_initial_layer]"}});
        write(root / "Example/process/normal.json", {{"type", "process"}, {"name", "Normal"}, {"instantiation", "true"},
            {"wall_loops", "3"}, {"compatible_printers", {"Printer"}}});
        write(root / "Example/filament/pla.json", {{"type", "filament"}, {"name", "PLA"}, {"instantiation", "true"},
            {"nozzle_temperature", {"215"}}, {"filament_flow_ratio", {"0.98"}}});
        std::map<std::string, std::set<std::string>> selected;
        const auto catalog_cache = root / "catalog-cache";
        fs::rename(root / "Example/process/normal.json", root / "Example/process/normal.saved");
        auto catalog = O::convert(root, {}, false, catalog_cache, {}, &selected);
        check(catalog[0].diagnostics.empty(), "catalog does not attempt to read process files");
        fs::rename(root / "Example/process/normal.saved", root / "Example/process/normal.json");
        check(catalog.size() == 1 && catalog[0].machines.size() == 1, "catalog lists printer without a conversion schema");
        check(catalog[0].presets.size() == 1 && catalog[0].presets[0]["values"].empty(),
            "catalog contains only a printer identity, no converted slicing settings");
        check(!fs::exists(catalog_cache), "catalog does not create a full conversion cache");
        selected["Orca-Example"].insert("Printer");
        auto activated = O::convert(root, schema(), false, catalog_cache, {}, &selected);
        check(activated[0].presets[0]["values"]["max_print_height"] == 300.0,
            "adding printer converts its settings");
        check(activated[0].presets.back()["values"]["temperature"] == 215,
            "adding printer converts compatible material settings");
        selected.clear();
        catalog = O::convert(root, {}, false, catalog_cache, {}, &selected);
        check(!catalog[0].cache_hit && catalog[0].presets[0]["values"].empty(),
            "browsing ignores an existing full conversion cache");
        const auto original_manifest = Json::parse(std::ifstream(root / "Example.json"));
        auto two_printers = original_manifest;
        two_printers["machine_list"].push_back({{"name", "Second"}, {"sub_path", "machine/second.json"}});
        write(root / "Example.json", two_printers);
        write(root / "Example/machine/second.json", {{"type", "machine"}, {"name", "Second"},
            {"instantiation", "true"}, {"inherits", "Base"}, {"printable_height", "400"}});
        selected["Orca-Example"].insert("Printer");
        activated = O::convert(root, schema(), false, catalog_cache, {}, &selected);
        check(activated[0].machines.size() == 2, "adding one printer retains other catalog entries");
        check(activated[0].presets[0]["name"] == "Second" && activated[0].presets[0]["values"].empty(),
            "another printer in the same vendor is not converted");
        selected["Orca-Example"].insert("Second");
        activated = O::convert(root, schema(), false, catalog_cache, {}, &selected);
        check(!activated[0].cache_hit && activated[0].presets[1]["values"]["max_print_height"] == 400.0,
            "adding another printer invalidates the selection cache and converts it");
        write(root / "Example.json", original_manifest);
        auto vendors = O::convert(root, schema());
        check(vendors.size() == 1, "vendor discovery");
        auto& v = vendors.front();
        check(v.id == "Orca-Example", "vendor namespace");
        check(v.machines.size() == 1, "templates not selectable");
        check(v.machines[0]["nozzle_diameter"] == Json::array({"0.4"}), "inheritance preserves arrays");
        check(v.presets[0]["values"]["max_print_height"] == 300.0, "child overrides parent and converts numeric string");
        check(v.presets[0]["values"]["layer_gcode"].get<std::string>().starts_with("G92 E0"), "relative extrusion reset");
        check(v.presets[1]["values"]["perimeters"] == 3, "process aliases and typed values");
        check(v.presets[1]["variants"][0]["values"]["retract_length"] == 0.8, "machine retraction defaults retained");
        check(v.presets[1]["variants"][0]["values"]["default_material"] == "PLA", "machine default filament is retained");
        check(v.presets.back()["values"]["temperature"] == 215, "filament scalar conversion");
        check(v.presets[1]["values"].at("small_perimeter_threshold") == 0.0,
            "missing small-perimeter threshold preserves Orca disabled default");
        check(v.presets[1]["values"]["preheat_time"] == 30.0 && v.presets[1]["values"]["preheat_steps"] == 1,
            "missing preheat settings use Orca defaults");
        const auto filament_path = root / "Example/filament/pla.json";
        const auto original_filament = Json::parse(std::ifstream(filament_path));
        check(v.presets[1]["values"].at("orca_wipe_compatibility") == true
            && v.presets[1]["values"].at("role_based_wipe_speed") == true
            && v.presets[1]["values"].at("wipe_speed") == "80%",
            "Orca wipe policy and process speed defaults are imported");
        check(v.presets[1]["variants"][0]["values"].at("wipe_distance") == 1.,
            "machine wipe distance supplies the captured one-millimetre default");
        for (double distance : {0., 1.5}) {
            auto fixture = original_filament;
            fixture["filament_wipe_distance"] = Json::array({std::to_string(distance)});
            fixture["filament_retract_after_wipe"] = Json::array({"25%"});
            write(filament_path, fixture);
            const auto converted = O::convert(root, schema());
            check(converted[0].presets.back()["values"].at("wipe_distance") == distance
                && converted[0].presets.back()["values"].at("retract_after_wipe") == "25%",
                "filament wipe overrides preserve explicit zero and after-wipe percentage");
        }
        write(filament_path, original_filament);
        for (const auto& idle : {Json("0"), Json(0), Json("170"), Json("nil"), Json(nullptr)}) {
            auto fixture = original_filament;
            fixture["idle_temperature"] = Json::array({idle});
            write(filament_path, fixture);
            const auto converted = O::convert(root, schema());
            if (idle.is_null() || idle == "nil") {
                check(converted[0].presets.back()["values"].at("idle_temperature").is_null(),
                    "unset idle temperature uses Orca's unspecified default rather than native tuning");
                continue;
            }
            check(converted[0].presets.back()["values"].at("idle_temperature") == (idle == "170" ? Json(170) : Json(nullptr)),
                "idle zero maps to unspecified while explicit idle temperatures survive");
        }
        write(filament_path, original_filament);
        const auto process_path = root / "Example/process/normal.json";
        const auto original_process = Json::parse(std::ifstream(process_path));
        check(v.presets[1]["values"].at("prime_volume") == 45.0, "prime volume uses the captured Orca default");
        check(v.presets[1]["values"].at("wipe_tower_max_purge_speed") == 90.,
            "tower speed limit uses the captured source default");
        check(v.presets[1]["values"].at("orca_matrix_flush_multiplier") == 0.3,
            "matrix flush uses the Orca project default separately from the profile snapshot");
        for (double multiplier : {0., 0.3, 2.}) {
            auto fixture = original_process;
            fixture["flush_multiplier"] = Json::array({std::to_string(multiplier), "7"});
            write(process_path, fixture);
            const auto converted = O::convert(root, schema());
            check(converted[0].presets[1]["values"].at("orca_matrix_flush_multiplier") == multiplier,
                "type-2 matrix flushing uses slot zero and preserves explicit zero");
        }
        for (double volume : {0.0, 36.0, 45.0}) {
            auto fixture = original_process;
            fixture["prime_volume"] = std::to_string(volume);
            write(process_path, fixture);
            const auto converted = O::convert(root, schema());
            check(converted[0].presets[1]["values"].at("prime_volume") == volume,
                "prime volume preserves explicit zero and process values");
        }
        write(process_path, original_process);
        const auto machine_path = root / "Example/machine/printer.json";
        const auto original_machine = Json::parse(std::ifstream(machine_path));
        for (const auto* tower : {"type1", "type2"}) for (bool semm : {false, true}) for (bool purge : {false, true}) {
            auto fixture = original_machine;
            fixture["wipe_tower_type"] = Json::array({tower});
            fixture["single_extruder_multi_material"] = Json::array({semm ? "1" : "0"});
            fixture["purge_in_prime_tower"] = Json::array({purge ? "1" : "0"});
            write(machine_path, fixture);
            const auto converted = O::convert(root, schema());
            check(converted[0].presets[1]["variants"][0]["values"].at("orca_fixed_prime_volume")
                == (std::string(tower) == "type2" && !(semm && purge)),
                "machine variant selects fixed type-2 priming only for the matching source branch");
            check(converted[0].presets[1]["variants"][0]["values"].at("orca_matrix_flush")
                == (std::string(tower) == "type2" && semm && purge),
                "matrix flushing is selected only for type-2 shared-nozzle tower purging");
        }
        write(machine_path, original_machine);
        auto bbl_machine = original_machine;
        bbl_machine["single_extruder_multi_material"] = "0";
        bbl_machine["wipe_tower_type"] = "type2";
        fs::copy(root / "Example", root / "BBL", fs::copy_options::recursive);
        write(root / "BBL.json", original_manifest);
        write(root / "BBL/machine/printer.json", bbl_machine);
        const auto bbl = O::convert(root, schema());
        check(bbl[0].id == "Orca-BBL" && !bbl[0].presets[1]["variants"][0]["values"].at("orca_fixed_prime_volume").get<bool>(),
            "BBL vendor remains type 1 even with a stored type-2 setting");
        fs::remove(root / "BBL.json");
        fs::remove_all(root / "BBL");
        for (double seconds : {0.0, 30.0, 31.0}) {
            auto fixture = original_process;
            fixture["preheat_time"] = std::to_string(seconds);
            write(process_path, fixture);
            const auto converted = O::convert(root, schema());
            check(converted[0].presets[1]["values"].at("preheat_time") == seconds,
                "explicit preheat timing including disabled survives import");
        }
        for (double threshold : {0.0, 6.5, 8.0}) {
            auto fixture = original_process;
            fixture["small_perimeter_threshold"] = std::to_string(threshold);
            write(process_path, fixture);
            const auto converted = O::convert(root, schema());
            check(converted[0].presets[1]["values"].at("small_perimeter_threshold") == threshold,
                "explicit small-perimeter thresholds override the Orca default");
        }
        write(process_path, original_process);
        // Both startup and online-source reloads use this persistent per-vendor
        // cache. A second independent vendor exposes accidental global invalidation.
        const auto cache = root / "presets/local";
        write(root / "Other.json", {{"version", "1.0"}, {"machine_list", {{{"sub_path", "machine/printer.json"}}}}});
        write(root / "Other/machine/printer.json", {{"type", "machine"}, {"name", "Other"},
            {"instantiation", "true"}, {"nozzle_diameter", {"0.4"}}});
        auto cached = O::convert(root, schema(), false, cache);
        check(cached.size() == 2 && !cached[0].cache_hit && !cached[1].cache_hit, "first load converts both vendors");
        const auto cache_file = cache / "Orca-Example/Orca-Example/orca-conversion-cache.json";
        check(fs::is_regular_file(cache_file), "conversion persisted under presets/local/Orca-*");
        const auto cache_time = fs::last_write_time(cache_file);
        auto reload = O::convert(root, schema(), false, cache);
        check(reload[0].cache_hit && reload[1].cache_hit, "startup reuses persistent conversions");
        check(reload[0].presets == cached[0].presets && reload[0].machines == cached[0].machines
            && reload[0].diagnostics == cached[0].diagnostics, "cached conversion preserves presets, hardware and diagnostics");
        write(cache / "Native/Native/vendor.yaml", {{"version", "updated online"}});
        fs::last_write_time(root / "Example/machine/base.json", fs::file_time_type::clock::now());
        reload = O::convert(root, schema(), false, cache);
        check(reload[0].cache_hit && reload[1].cache_hit, "online native updates and timestamp-only changes preserve Orca caches");
        check(fs::last_write_time(cache_file) == cache_time, "cache hits do not rewrite the local conversion");
        auto example_manifest = Json::parse(std::ifstream(root / "Example.json"));
        example_manifest["version"] = "2.0";
        write(root / "Example.json", example_manifest);
        reload = O::convert(root, schema(), false, cache);
        check(!reload[0].cache_hit && reload[0].version == "2.0" && reload[1].cache_hit,
            "version change reprocesses only its vendor");
        reload = O::convert(root, schema(), false, cache);
        check(reload[0].cache_hit, "replacement cache is reused after version change");
        const auto base_file = root / "Example/machine/base.json";
        const auto base_time = fs::last_write_time(base_file);
        auto base_profile = Json::parse(std::ifstream(base_file));
        base_profile["retraction_length"] = {"0.9"};
        write(base_file, base_profile);
        fs::last_write_time(base_file, base_time);
        reload = O::convert(root, schema(), false, cache);
        check(!reload[0].cache_hit && reload[1].cache_hit, "same-version same-timestamp inherited content change invalidates only its vendor");
        check(reload[0].presets[1]["variants"][0]["values"]["retract_length"] == 0.9, "changed inheritance is reprocessed");
        base_profile["retraction_length"] = {"0.8"};
        write(base_file, base_profile);
        write(root / "OrcaFilamentLibrary.json", {{"filament_list", {{{"sub_path", "filament/shared.json"}}}}});
        write(root / "OrcaFilamentLibrary/filament/shared.json", {{"type", "filament"},
            {"name", "Shared"}, {"instantiation", "false"}, {"nozzle_temperature", {"200"}}});
        reload = O::convert(root, schema(), false, cache);
        check(!reload[0].cache_hit && !reload[1].cache_hit, "adding shared library invalidates its dependent vendors");
        reload = O::convert(root, schema(), false, cache);
        check(reload[0].cache_hit && reload[1].cache_hit, "shared library fingerprint is stable");
        write(root / "OrcaFilamentLibrary/filament/shared.json", {{"type", "filament"},
            {"name", "Shared"}, {"instantiation", "false"}, {"nozzle_temperature", {"210"}}});
        reload = O::convert(root, schema(), false, cache);
        check(!reload[0].cache_hit && !reload[1].cache_hit, "shared inherited content change invalidates dependent vendors");
        std::ofstream(cache_file, std::ios::trunc) << "incomplete cache";
        reload = O::convert(root, schema(), false, cache);
        check(!reload[0].cache_hit && reload[1].cache_hit, "corrupt cache recovers only the affected vendor");
        auto changed_schema = schema();
        changed_schema["printer"]["printer_notes"] = {{"type", "string"}};
        reload = O::convert(root, changed_schema, false, cache);
        check(!reload[0].cache_hit && !reload[1].cache_hit, "changed conversion schema cannot reuse obsolete results");
        check(O::convert(root, schema(), false, cache, {"Orca-Example", "Orca-Other"}).empty(),
            "lower-priority duplicate vendors are not converted or cached");
        const auto blocked_cache = root / "not-a-directory";
        std::ofstream(blocked_cache) << "file";
        reload = O::convert(root, schema(), false, blocked_cache);
        check(reload.size() == 2 && !reload[0].cache_hit && !reload[0].machines.empty(),
            "cache write failure does not prevent source conversion");
        fs::remove(root / "Other.json");
        write(root / "Example/process/normal.json", {{"type", "process"}, {"name", "Normal"}, {"instantiation", "true"},
            {"exclude_object", "1"}, {"gcode_label_objects", true}, {"ensure_vertical_shell_thickness", {true}},
            {"infill_combination", "1"}, {"bridge_acceleration", {"50%"}}, {"outer_wall_acceleration", {"3000"}}});
        vendors = O::convert(root, schema());
        const auto converted = vendors.front().presets[1]["values"];
        check(converted["gcode_label_objects"] == "firmware", "firmware labels take precedence over legacy labels");
        check(converted["ensure_vertical_shell_thickness"] == "enabled", "singleton boolean converts to shell enum");
        check(converted["automatic_infill_combination"] == true, "automatic infill remains automatic");
        check(converted["bridge_acceleration"] == 1500.0, "bridge percent uses outer wall acceleration");
        check(converted["enable_dynamic_overhang_speeds"] == true,
            "omitted overhang switch retains Orca's enabled engine default");
        check(converted["small_perimeter_speed"] == "50%" && converted["small_perimeter_threshold"] == 0.0
            && converted["slowdown_for_curled_perimeters"] == false && converted["orca_perimeter_speed_compatibility"] == true,
            "import uses the complete source speed defaults with Orca perimeter semantics");
        check(!converted.contains("retract_length"), "process defaults must not erase machine-owned retraction settings");
        write(root / "Example/process/normal.json", {{"type", "process"}, {"name", "Normal"}, {"instantiation", "true"},
            {"small_perimeter_speed", "0"}, {"enable_overhang_speed", {"nil"}},
            {"perimeter_speed", "123"}, {"first_layer_solid_infill_speed", "17"}});
        vendors = O::convert(root, schema());
        const auto explicit_speeds = vendors.front().presets[1]["values"];
        check(explicit_speeds["small_perimeter_speed"] == 0.0 && explicit_speeds["perimeter_speed"] == 123.0
            && explicit_speeds["first_layer_solid_infill_speed"] == 17.0,
            "explicit zero and accepted target names take precedence over aliased defaults");
        check(explicit_speeds["enable_dynamic_overhang_speeds"] == true,
            "nullable process values resolve to Orca defaults rather than PS3 defaults");
        write(root / "Example/process/normal.json", {{"type", "process"}, {"name", "Normal"}, {"instantiation", "true"},
            {"enable_overhang_speed", {"0"}}});
        vendors = O::convert(root, schema());
        check(vendors.front().presets[1]["values"]["enable_dynamic_overhang_speeds"] == false,
            "explicit disabled overhang switch overrides the imported default");
        write(root / "Example/process/normal.json", {{"type", "process"}, {"name", "Normal"}, {"instantiation", "true"},
            {"initial_layer_infill_speed", "25"}, {"enable_overhang_speed", "1"},
            {"overhang_1_4_speed", "0"}, {"overhang_2_4_speed", "40"},
            {"overhang_3_4_speed", "20"}, {"overhang_4_4_speed", "10"},
            {"sparse_infill_pattern", "crosshatch"}});
        vendors = O::convert(root, schema());
        const auto speeds = vendors.front().presets[1]["values"];
        check(speeds["first_layer_infill_speed"] == 25.0 && speeds["first_layer_solid_infill_speed"] == 25.0,
            "first-layer solid and sparse infill both retain Orca's speed");
        check(speeds["enable_dynamic_overhang_speeds"] == true && speeds["overhang_speed_0"] == 10.0
            && speeds["overhang_speed_1"] == 20.0 && speeds["overhang_speed_2"] == 40.0
            && speeds["overhang_speed_3"] == "100%", "overhang speeds follow increasing overlap and preserve zero semantics");
        check(speeds["fill_pattern"] == "grid", "crosshatch gets an explicit grid approximation");
        write(root / "Example/machine/printer.json", {{"type", "machine"}, {"name", "Printer"}, {"instantiation", "true"},
            {"inherits", "Base"}, {"printer_notes", "first line\nPRINTER_MODEL_TEST\nlast line"},
            {"thumbnails", {"48x48", "300x300"}}, {"before_layer_change_gcode", "G92 E0.0\n"}});
        vendors = O::convert(root, schema());
        check(vendors.front().presets[0]["values"]["layer_gcode"] == "", "existing before-layer reset is not duplicated");
        check(vendors.front().presets[0]["values"]["thumbnails"] == "48x48/PNG, 300x300/PNG", "thumbnail arrays are formatted lists, not per-tool values");
        write(root / "Example/process/normal.json", {{"type", "process"}, {"name", "Normal"}, {"instantiation", "true"},
            {"wall_loops", "3"}, {"compatible_printers_condition", "nozzle_diameter[0] == 0.4 and name =~ /Print.*/ and printer_notes =~ /.*PRINTER_MODEL_TEST.*/"}});
        vendors = O::convert(root, schema());
        check(vendors.front().presets[1]["kind"] == "print", "regex and numeric compatibility matching");
        write(root / "Example/process/normal.json", {{"type", "process"}, {"name", "Normal"}, {"instantiation", "true"},
            {"wall_loops", "3"}, {"compatible_printers_condition", "nozzle_diameter[0] == 0.6"}});
        vendors = O::convert(root, schema());
        check(vendors.front().presets[1]["kind"] == "tool_print", "incompatible nozzle is excluded");
        // Missing/cyclic inheritance must not silently select machine defaults.
        write(root / "Example/machine/base.json", {{"type", "machine"}, {"name", "Base"}, {"inherits", "Printer"}, {"instantiation", "false"}});
        vendors = O::convert(root, schema());
        check(vendors.front().machines.empty(), "cyclic printer excluded");
        check(!vendors.front().diagnostics.empty(), "cyclic inheritance diagnosed");
        // Manifest entries may not traverse outside the vendor tree.
        write(root / "Example.json", {{"machine_list", {{{"name", "escape"}, {"sub_path", "../../outside.json"}}}}});
        vendors = O::convert(root, schema());
        check(vendors.front().machines.empty(), "traversal excluded");
        check(vendors.front().diagnostics[0]["message"].get<std::string>().find("escapes") != std::string::npos, "traversal diagnosed");
        fs::remove_all(root);
        std::cout << "Orca importer regression checks passed\n";
        if (argc > 1) {
            const auto real = O::convert(fs::u8path(argv[1]), schema());
            size_t machines = 0, diagnostics = 0;
            for (const auto& vendor : real) { machines += vendor.machines.size(); diagnostics += vendor.diagnostics.size(); }
            std::cout << "Corpus discovery: " << real.size() << " vendors, " << machines << " printers, " << diagnostics << " diagnostics (minimal test schema)\n";
        }
        return 0;
    } catch (const std::exception& e) {
        fs::remove_all(root);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
