// Test-only comparison of real native presets and explicitly enabled Orca Prusa imports.
#include "Slic3r/Biz/Preset/IO/HwConfigLoader.hpp"
#include "Slic3r/Biz/Preset/IO/PresetLoader.hpp"
#include <cctype>

template<class T> static Json comparison_value(const T& value)
{
    if constexpr (std::is_same_v<T, std::monostate>) return nullptr;
    else if constexpr (D::is_std_vector_v<T>) {
        Json result = Json::array();
        for (const auto& v : value) result.push_back(comparison_value(v));
        return result;
    } else if constexpr (std::is_same_v<T, D::EnumWrapper>) return std::string(value.get_string());
    else if constexpr (std::is_same_v<T, D::EnumVectorWrapper>) {
        Json result = Json::array();
        for (auto v : value.get_strings()) result.push_back(std::string(v));
        return result;
    } else if constexpr (std::is_same_v<T, std::optional<int>>) return value ? Json(*value) : Json(nullptr);
    else if constexpr (std::is_same_v<T, D::Percentage>) return Json{{"percent", value.value}};
    else if constexpr (std::is_same_v<T, D::FloatOrPercentage>)
        return value.is_percentage() ? Json{{"percent", value.percentage().value}} : Json(value.float_value());
    else if constexpr (std::is_same_v<T, D::Vec2d>) return Json::array({value.x(), value.y()});
    else return Json(value);
}

static Json comparison_box(const D::ConfigBox& box)
{
    Json result = Json::object();
    for (const auto& item : box.items.all_items())
        result[item.name()] = item.visit([](const auto& v) { return comparison_value(v); });
    for (const auto& ref : box.overrides.overridden_items()) {
        const auto& item = ref.get();
        result[item.name()] = item.visit([](const auto& v) { return comparison_value(v); });
    }
    return result;
}

static Json comparison_snapshot(const D::Preset::VendorBundle& vendor, const std::string& name)
{
    const auto& templates = vendor.vendor_data.printer_configs;
    auto templ = std::find_if(templates.begin(), templates.end(), [&](const auto& t) { return t.name == name; });
    if (templ == templates.end()) throw std::runtime_error("Missing comparison printer: " + name);
    P::HwConfigEvaluator hw_eval;
    auto hw = hw_eval.create_printer_config(*templ, vendor.vendor_data);
    P::PresetEvaluator eval(vendor.presets);
    auto printers = eval.evaluate(hw);
    require(printers.size() == 1, "comparison needs an unambiguous printer preset");
    const auto& printer = printers.front();
    const auto process = std::find_if(printer.prints.begin(), printer.prints.end(), [](const auto& p) {
        std::string name = p.preset.name;
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return name.find("0.20") != std::string::npos && (name.find("speed") != std::string::npos || name == "0.20mm");
    });
    if (process == printer.prints.end()) {
        std::string names;
        for (const auto& p : printer.prints) names += "\n" + p.preset.name;
        throw std::runtime_error("Missing 0.20 speed process for " + name + names);
    }
    D::ConfigPackFDM pack(static_cast<int>(hw.material_slot_count()));
    pack.printer = std::get<D::PrinterSettings>(printer.preset.values);
    pack.print = std::get<D::PrintSettings>(process->preset.values);
    pack.tool.resize(hw.tool_count);
    Json result = {{"name", name}, {"process_name", process->preset.name},
        {"hardware", {{"tools", hw.tool_count}, {"material_slots", hw.material_slot_count()},
            {"supports_tool_preheating", D::Preset::get_feature<bool>(hw.features, "supports_tool_preheating").value_or(false)},
            {"tool_preheating_m104", D::Preset::get_feature<bool>(hw.features, "tool_preheating_m104").value_or(false)}}},
        {"printer", comparison_box(std::get<D::PrinterSettings>(printer.preset.values))},
        {"process", comparison_box(std::get<D::PrintSettings>(process->preset.values))}};
    result["tools"] = Json::array();
    for (const auto& choices : process->tools) {
        require(!choices.empty(), "comparison has tool settings");
        auto speed = std::find_if(choices.begin(), choices.end(), [](const auto& p) {
            return p.preset.name.find("0.20") != std::string::npos
                && (p.preset.name.find("SPEED") != std::string::npos || p.preset.name.find("Speed") != std::string::npos);
        });
        if (speed == choices.end()) speed = choices.begin();
        pack.tool[result["tools"].size()] = std::get<D::ToolPrintSettings>(speed->preset.values);
        result["tool_name"] = speed->preset.name;
        result["tools"].push_back(comparison_box(std::get<D::ToolPrintSettings>(speed->preset.values)));
    }
    result["filaments"] = Json::array();
    for (const auto& choices : process->materials) {
        auto pla = std::find_if(choices.begin(), choices.end(), [](const auto& p) {
            return p.preset.name.find("Prusament PLA") != std::string::npos;
        });
        if (pla == choices.end()) throw std::runtime_error("Missing Prusament PLA for " + name);
        pack.filament[result["filaments"].size()] = std::get<D::FilamentSettings>(pla->preset.values);
        result["filament_name"] = pla->preset.name;
        result["filaments"].push_back(comparison_box(std::get<D::FilamentSettings>(pla->preset.values)));
    }
    std::vector<unsigned> slots;
    for (unsigned i = 0; i < hw.material_slot_count(); ++i) slots.push_back(i);
    auto full = std::make_shared<D::FullConfigFDM>(pack, slots, hw);
    D::ConfigView view(full, {});
    view.finalize();
    result["effective"] = Json::object();
    for (const auto& [key, value] : view.values())
        result["effective"][key] = value.visit([](const auto& v) { return comparison_value(v); });
    D::ConfigPackFDM defaults(static_cast<int>(hw.material_slot_count()));
    defaults.tool.resize(hw.tool_count);
    auto default_full = std::make_shared<D::FullConfigFDM>(defaults, slots, hw);
    D::ConfigView default_view(default_full, {});
    default_view.finalize();
    result["ps3_defaults_for_hardware"] = Json::object();
    for (const auto& [key, value] : default_view.values())
        result["ps3_defaults_for_hardware"][key] = value.visit([](const auto& v) { return comparison_value(v); });
    if (vendor.vendor_data.info.id == "Orca-Prusa") {
        result["converted_inputs"] = Json::object();
        for (const auto& [kind, group, selected] : {
                 std::tuple{D::Preset::PresetKind::FdmPrinter, "printer", printer.preset.name},
                 std::tuple{D::Preset::PresetKind::FdmPrint, "process", process->preset.name},
                 std::tuple{D::Preset::PresetKind::FdmToolPrint, "tool", result.at("tool_name").get<std::string>()},
                 std::tuple{D::Preset::PresetKind::FdmMaterial, "filament", result.at("filament_name").get<std::string>()}}) {
            const auto& roots = vendor.presets.at(kind);
            auto root = std::find_if(roots.begin(), roots.end(), [&](const auto& node) { return node.name == selected; });
            require(root != roots.end(), "comparison locates the emitted converter inputs");
            auto& inputs = result["converted_inputs"][group] = Json::object();
            const auto append = [&](const auto& values) {
                for (const auto& [key, value] : values)
                    inputs[key] = std::visit([](const auto& v) { return comparison_value(v); }, value);
            };
            // Converted presets have no inheritance. Process variants carry
            // machine defaults for precisely one printer; only include that branch.
            require(root->inherits.empty(), "converted input tracing assumes flat presets");
            append(root->values);
            Slic3r::Biz::Expr::ValueMap condition_values;
            condition_values.emplace("printer.model", hw.model.model);
            Slic3r::Biz::Expr::Eval condition_eval;
            for (const auto& variant : root->variants) {
                if (!variant.condition || boost::get<bool>(condition_eval.eval(variant.condition->expr.value, condition_values)))
                    append(variant.values);
            }
        }
    }
    check_prusa_gcode(hw, printer, *process);
    check_prusa_gcode(hw, printer, *process, static_cast<unsigned>(hw.material_slot_count() - 1), true);
    return result;
}

static void check_prusa_comparison(const Json& direct, const Json& imported, bool mmu)
{
    for (const auto* snapshot : {&direct, &imported}) {
        const auto& hw = snapshot->at("hardware");
        const auto& values = snapshot->at("effective");
        require(hw.at("tools") == (mmu ? 1 : 5) && hw.at("material_slots") == 5,
            "native and imported Prusa configurations keep physical tools distinct from material slots");
        require(values.at("single_extruder_multi_material") == mmu, "Prusa SEMM/MEMM mode agrees");
        require(hw.at("supports_tool_preheating") == !mmu, "MMU disables advance tool preheating; XL enables it");
        require(values.at("ooze_prevention") == !mmu, "MMU and XL preserve their ooze prevention modes");
        if (!mmu) {
            require(hw.at("tool_preheating_m104") == false, "both XL paths select Buddy scheduled preheating");
            require(values.at("preheat_time") == 120 && values.at("preheat_steps") == 10,
                "explicit Orca XL preheat settings match native XL, overriding generic Orca defaults");
        }
    }
    for (const auto* key : {"gcode_flavor", "nozzle_diameter", "wipe_tower", "wipe_tower_width",
             "cooling_tube_length", "cooling_tube_retraction", "parking_pos_retraction", "extra_loading_move",
             "filament_multitool_ramming"}) {
        if (direct.at("effective").at(key) != imported.at("effective").at(key))
            throw std::runtime_error(std::string("Prusa comparison mismatch: ") + key);
    }
    require(direct.at("ps3_defaults_for_hardware").at("small_perimeter_threshold") == Json::array({6.5, 6.5, 6.5, 6.5, 6.5}),
        "native historical small-perimeter threshold remains 6.5 mm");
    require(imported.at("effective").at("small_perimeter_threshold") == Json::array({0.0, 0.0, 0.0, 0.0, 0.0}),
        "imported Prusa profiles use Orca's disabled small-perimeter threshold, not native Prusa defaults");
    if (mmu) {
        for (const auto* key : {"temperature", "first_layer_temperature", "idle_temperature", "retract_length_toolchange"})
            if (direct.at("effective").at(key) != imported.at("effective").at(key))
                throw std::runtime_error(std::string("MMU comparison mismatch: ") + key);
    } else {
        // Explicit source differences: native Prusament PLA parks at 70 C;
        // Orca leaves idle unset and uses its -40 C standby delta.
        require(direct.at("effective").at("idle_temperature") == Json::array({70, 70, 70, 70, 70})
            && imported.at("effective").at("idle_temperature") == Json::array({nullptr, nullptr, nullptr, nullptr, nullptr}),
            "XL idle difference preserves Orca zero-as-unspecified semantics");
        require(direct.at("effective").at("standby_temperature_delta") == -110
            && imported.at("effective").at("standby_temperature_delta") == -40, "XL source standby deltas stay explicit");
    }
}

static void compare_prusa_presets(const char* native_dir, const char* orca_root, const char* output)
{
    P::IO::HwConfigLoader hw_loader;
    P::IO::PresetLoader preset_loader;
    D::Preset::VendorBundle native;
    hw_loader.load((fs::path(native_dir) / "vendor.yaml").string());
    native.vendor_data = hw_loader.release();
    preset_loader.load_dir(native_dir);
    std::tie(native.presets, native.preset_names) = preset_loader.release();
    P::IO::BundlePaths paths;
    paths.app_bundle_path = orca_root;
    D::Preset::Bundle imported;
    P::IO::load_orca_profiles(paths, imported, false, true);
    require(!imported.vendor_bundles.contains("Orca-Prusa"), "real Prusa corpus remains excluded from normal imports");
    imported.vendor_bundles.clear();
    P::IO::load_orca_profiles(paths, imported, true);
    const auto& orca = imported.vendor_bundles.at("Orca-Prusa");
    Json report = Json::object();
    for (const auto& pair : {std::pair{"Prusa CORE One MMU3", "Prusa CORE One MMU3 0.4 nozzle"},
             std::pair{"XL 5T", "Prusa XL 5T 0.4 nozzle"}}) {
        auto direct = comparison_snapshot(native, pair.first);
        auto converted = comparison_snapshot(orca, pair.second);
        check_prusa_comparison(direct, converted, std::string_view(pair.first) != "XL 5T");
        report[pair.first] = {{"native", direct}, {"imported", converted}, {"differences", Json::diff(direct, converted)}};
    }
    std::ofstream stream(output);
    stream << report.dump(2) << '\n';
    if (!stream) throw std::runtime_error("Cannot write comparison report");
    std::cout << "Prusa comparison written to " << output << '\n';
}
