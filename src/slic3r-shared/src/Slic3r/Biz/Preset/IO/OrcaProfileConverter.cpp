#include "OrcaProfileConverter.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <cctype>
#include <fstream>
#include <functional>
#include <regex>
#include <set>
#include <stdexcept>

namespace Slic3r::Biz::Preset::IO::Orca {
namespace {
namespace fs = std::filesystem;
#include "OrcaProfileMappings.inc"

std::string text(const Json& v)
{
    return v.is_string() ? v.get<std::string>() : v.dump();
}

bool enabled(const Json& v)
{
    const auto s = text(v.is_array() && !v.empty() ? v.front() : v);
    return s == "1" || s == "true" || s == "enabled";
}

Json read(const fs::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot read " + path.generic_string());
    return Json::parse(stream);
}

// Stable FNV-1a content checksum: timestamps and directory enumeration order
// must not invalidate imports when an online updater recopies identical files.
class Checksum
{
public:
    void append(std::string_view value)
    {
        for (const unsigned char byte : value) {
            m_value ^= byte;
            m_value *= UINT64_C(1099511628211);
        }
    }
    void field(std::string_view value) { append(std::to_string(value.size())); append(":"); append(value); }
    void file(const fs::path& path)
    {
        field(std::to_string(fs::file_size(path)));
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error("Cannot checksum " + path.generic_string());
        char buffer[65536];
        while (input.read(buffer, sizeof(buffer)) || input.gcount())
            append(std::string_view(buffer, static_cast<size_t>(input.gcount())));
        if (input.bad()) throw std::runtime_error("Cannot checksum " + path.generic_string());
    }
    std::string value() const { return std::to_string(m_value); }
private:
    uint64_t m_value = UINT64_C(14695981039346656037);
};

std::string source_checksum(const fs::path& root, const std::string& name)
{
    Checksum checksum;
    const auto manifest = root / fs::u8path(name + ".json");
    if (!fs::exists(manifest)) return "missing";
    checksum.file(manifest);
    const auto base = root / fs::u8path(name);
    std::vector<fs::path> files;
    if (fs::is_directory(base)) {
        for (const auto& entry : fs::recursive_directory_iterator(base))
            if (entry.is_regular_file()) files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    for (const auto& file : files) {
        checksum.field(file.lexically_relative(base).generic_string());
        checksum.file(file);
    }
    return checksum.value();
}

bool restore_conversion(const fs::path& path, const Json& identity, Vendor& vendor)
{
    try {
        const auto cached = read(path);
        if (cached.at("identity") != identity) return false;
        Vendor restored;
        restored.id = cached.at("id").get<std::string>();
        restored.name = cached.at("name").get<std::string>();
        restored.version = cached.at("version").get<std::string>();
        restored.machines = cached.at("machines");
        restored.presets = cached.at("presets");
        restored.diagnostics = cached.at("diagnostics");
        if (restored.id != vendor.id || !restored.machines.is_array()
            || !restored.presets.is_array() || !restored.diagnostics.is_array()) return false;
        for (const auto& [name, source] : cached.at("assets").items())
            restored.assets.emplace(name, fs::u8path(source.get<std::string>()));
        restored.cache_hit = true;
        vendor = std::move(restored);
        return true;
    } catch (const std::exception&) {
        return false; // Missing, obsolete or interrupted caches are recoverable.
    }
}

void save_conversion(const fs::path& path, const Json& identity, const Vendor& vendor)
{
    static std::atomic<unsigned long long> sequence{0};
    auto temporary = path;
    temporary += ".tmp-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
        + "-" + std::to_string(sequence++);
    try {
        Json assets = Json::object();
        for (const auto& [name, source] : vendor.assets) assets[name] = source.generic_string();
        const Json cached = {{"identity", identity}, {"id", vendor.id}, {"name", vendor.name},
            {"version", vendor.version}, {"machines", vendor.machines}, {"presets", vendor.presets},
            {"diagnostics", vendor.diagnostics}, {"assets", std::move(assets)}};
        fs::create_directories(path.parent_path());
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output.exceptions(std::ios::failbit | std::ios::badbit);
        output << cached.dump();
        output.close();
        fs::rename(temporary, path);
    } catch (const std::exception&) {
        // A read-only data directory must not prevent loading source profiles.
        std::error_code ec;
        fs::remove(temporary, ec);
    }
}

void issue(Vendor& vendor, const std::string& profile, const std::string& key, const std::string& message)
{
    auto [it, inserted] = vendor.diagnostic_indices.emplace(std::make_pair(key, message), vendor.diagnostics.size());
    if (inserted) vendor.diagnostics.push_back({{"key", key}, {"message", message}, {"count", 0}, {"examples", Json::array()}});
    auto& diagnostic = vendor.diagnostics[it->second];
    diagnostic["count"] = diagnostic["count"].get<size_t>() + 1;
    auto& examples = diagnostic["examples"];
    if (examples.size() < 5 && std::find(examples.begin(), examples.end(), profile) == examples.end()) examples.push_back(profile);
}

struct Index
{
    std::map<std::string, Json> profiles;
    std::map<std::string, Json> flattened;
    std::set<std::string> active;

    Json flatten(const std::string& key)
    {
        if (auto it = flattened.find(key); it != flattened.end()) return it->second;
        const auto it = profiles.find(key);
        if (it == profiles.end()) throw std::runtime_error("Missing inherited profile: " + key);
        if (!active.insert(key).second) throw std::runtime_error("Cyclic inheritance: " + key);
        try {
            Json result = Json::object();
            const auto parent = it->second.value("inherits", "");
            if (!parent.empty()) result = flatten(it->second.value("type", "") + "/" + parent);
            result.update(it->second); // Arrays replace parents; never concatenate them.
            active.erase(key);
            flattened.emplace(key, result);
            return result;
        } catch (...) {
            active.erase(key);
            throw;
        }
    }
};

void load_manifest(Index& index, const fs::path& root, const std::string& vendor, Vendor& report)
{
    const auto manifest = read(root / fs::u8path(vendor + ".json"));
    const auto base = fs::weakly_canonical(root / fs::u8path(vendor));
    for (const auto& list : {"machine_model_list", "machine_list", "process_list", "filament_list"}) {
        if (!manifest.contains(list)) continue;
        for (const auto& entry : manifest.at(list)) {
            try {
                const auto rel = fs::u8path(entry.at("sub_path").get<std::string>());
                const auto path = fs::weakly_canonical(base / rel);
                const auto checked = path.lexically_relative(base);
                if (rel.is_absolute() || checked.empty() || *checked.begin() == "..")
                    throw std::runtime_error("Profile path escapes vendor directory");
                auto profile = read(path);
                const auto kind = profile.at("type").get<std::string>();
                if (kind != "machine_model" && kind != "machine" && kind != "process" && kind != "filament") continue;
                const auto key = kind + "/" + profile.at("name").get<std::string>();
                index.profiles[key] = std::move(profile);
            } catch (const std::exception& e) {
                issue(report, entry.value("name", vendor), list, e.what());
            }
        }
    }
}

double number(const Json& v)
{
    if (v.is_number()) return v.get<double>();
    const auto s = text(v);
    size_t end = 0;
    const auto result = std::stod(s, &end);
    if (end != s.size() || !std::isfinite(result)) throw std::runtime_error("Invalid number: " + s);
    return result;
}

Json coerce(Json v, const Json& spec)
{
    if (spec.value("vector", false)) {
        if (!v.is_array()) v = Json::array({v});
        if (v.empty() && spec.value("tool_parity", false))
            throw std::runtime_error("Empty per-tool setting");
        auto scalar_spec = spec;
        scalar_spec["vector"] = false;
        for (auto& element : v) element = coerce(element, scalar_spec);
        return v;
    }
    if (v.is_array()) {
        if (v.empty()) throw std::runtime_error("Empty scalar setting");
        for (const auto& element : v)
            if (element != v.front()) throw std::runtime_error("Unequal per-tool values require separate tool presets");
        v = Json(v.front());
    }
    const auto type = spec.at("type").get<std::string>();
    if (type == "bool") {
        const auto s = text(v);
        if (s != "0" && s != "1" && s != "true" && s != "false")
            throw std::runtime_error("Invalid boolean: " + s);
        return enabled(v);
    }
    if (type == "point") {
        const auto s = text(v);
        const auto split = s.find_first_of("x,");
        if (split == std::string::npos) throw std::runtime_error("Invalid coordinate: " + s);
        return Json(number(s.substr(0, split))).dump() + "x" + Json(number(s.substr(split + 1))).dump();
    }
    if (type == "string" || type == "enum") {
        const auto s = text(v);
        if (type == "enum" && std::find(spec.at("values").begin(), spec.at("values").end(), s) == spec.at("values").end())
            throw std::runtime_error("Unsupported enum: " + s);
        return s;
    }
    if (type == "percent" || type == "float_or_percent") {
        auto s = text(v);
        if (s.ends_with('%')) {
            number(s.substr(0, s.size() - 1));
            return s;
        }
        if (type == "percent") return Json(number(v)).dump() + "%";
    }
    if (type == "optional_int" && (v.is_null() || v == "nil")) return nullptr;
    const double n = number(v);
    if (type == "int" || type == "optional_int") {
        if (n != std::trunc(n) || n < -2147483648.0 || n > 2147483647.0)
            throw std::runtime_error("Invalid integer");
        return static_cast<int>(n);
    }
    return n;
}

// Token substitution operates only inside placeholders. Printer commands,
// comments outside expressions and quoted string literals remain intact.
std::string rewrite_expression(const std::string& source, const std::map<std::string, std::string>& context)
{
    static const std::string width = "(external_perimeter_extrusion_width[initial_tool] > 0 ? external_perimeter_extrusion_width[initial_tool] : (extrusion_width[initial_tool] > 0 ? extrusion_width[initial_tool] : nozzle_diameter[initial_tool] * 1.05))";
    static const std::string flow = "max(0, external_perimeter_speed[initial_tool] * (layer_height * (" + width + " - layer_height) + 3.141592653589793 * layer_height * layer_height / 4))";
    static const std::map<std::string, std::string> aliases{
        {"bed_temperature_initial_layer_single", "first_layer_bed_temperature[initial_tool]"},
        {"bed_temperature_initial_layer", "first_layer_bed_temperature"},
        {"nozzle_temperature_initial_layer", "first_layer_temperature"},
        {"nozzle_temperature", "temperature"}, {"initial_extruder", "initial_tool"},
        {"initial_layer_print_height", "first_layer_height"},
        {"spiral_mode", "spiral_vase"},
        {"outer_wall_speed", "external_perimeter_speed"},
        {"outer_wall_line_width", "external_perimeter_extrusion_width"},
        {"inner_wall_speed", "perimeter_speed"}, {"sparse_infill_density", "fill_density"},
        {"outer_wall_volumetric_speed", "min((filament_max_volumetric_speed[initial_tool] > 0 ? filament_max_volumetric_speed[initial_tool] : 1e9), " + flow + ")"}
    };
    static const std::set<std::string> vectors{
        "temperature", "first_layer_temperature", "bed_temperature", "first_layer_bed_temperature",
        "filament_type", "chamber_temperature", "idle_temperature", "external_perimeter_speed",
        "external_perimeter_extrusion_width", "perimeter_speed", "filament_max_volumetric_speed",
        "filament_diameter", "nozzle_diameter"
    };
    std::string result;
    for (size_t i = 0; i < source.size();) {
        const char ch = source[i];
        if (ch == '\'' || ch == '"') {
            const char quote = ch;
            result += source[i++];
            while (i < source.size()) {
                const char c = source[i++];
                result += c;
                if (c == '\\' && i < source.size()) result += source[i++];
                else if (c == quote) break;
            }
        } else if (std::isalpha(static_cast<unsigned char>(ch)) || ch == '_') {
            size_t end = i + 1;
            while (end < source.size() && (std::isalnum(static_cast<unsigned char>(source[end])) || source[end] == '_')) ++end;
            auto token = source.substr(i, end - i);
            auto index_begin = source.find_first_not_of(" \t", end);
            bool contextual = false;
            if (index_begin != std::string::npos && source[index_begin] == '[') {
                const auto index_end = source.find(']', index_begin);
                if (index_end != std::string::npos) {
                    auto indexed = token + source.substr(index_begin, index_end - index_begin + 1);
                    if (token == "flush_volumetric_speeds" || token == "flush_temperatures") {
                        const auto index = "[" + rewrite_expression(source.substr(index_begin + 1, index_end - index_begin - 1), context) + "]";
                        const bool speed = token == "flush_volumetric_speeds";
                        const auto custom = std::string("custom_parameter_filament_orca_") + (speed ? "flush_speed" : "flush_temperature") + index;
                        token = "(" + custom + " > 0 ? " + custom + " : "
                            + (speed ? "filament_max_volumetric_speed" + index : "custom_parameter_filament_orca_temperature_high" + index) + ")";
                        end = index_end + 1;
                        contextual = true;
                    } else if (auto it = context.find(indexed); it != context.end()) {
                        token = it->second;
                        end = index_end + 1;
                        contextual = true;
                    } else if (auto it = context.find(token + "[*]"); it != context.end()) {
                        token = it->second;
                        end = index_end + 1;
                        contextual = true;
                    }
                }
            }
            if (!contextual) {
                if (auto it = context.find(token); it != context.end()) token = it->second;
                else if (auto it = aliases.find(token); it != aliases.end()) token = it->second;
                else if (const auto separator = token.rfind('_'); separator != std::string::npos) {
                    const auto base = token.substr(0, separator);
                    const auto index = token.substr(separator + 1);
                    if (auto it = aliases.find(base); it != aliases.end()) {
                        if (!index.empty() && index.find_first_not_of("0123456789") == std::string::npos)
                            token = it->second + "[" + index + "]";
                        else if (index.empty() && index_begin != std::string::npos && source[index_begin] == '[')
                            token = it->second;
                    }
                }
            }
            if (token == "print_sequence") token = "(complete_objects ? \"by object\" : \"by layer\")";
            auto next = source.find_first_not_of(" \t\r\n", end);
            if (vectors.contains(token) && (next == std::string::npos || source[next] != '['))
                token += "[" + (context.contains("__implicit_tool") ? context.at("__implicit_tool") : "initial_tool") + "]";
            result += token;
            i = end;
        } else result += source[i++];
    }
    return result;
}

std::map<std::string, std::string> gcode_context(const Json& machine, const Json& schema)
{
    std::map<std::string, std::string> result;
    // Orca-only, profile-owned values used by custom G-code can remain literal
    // expressions even when the PS3 settings UI has no corresponding option.
    // Never freeze mapped settings: those must follow edits made inside PS3.
    const auto literal = [](const Json& value, const std::string& key) {
        if (key.starts_with("enable_") || key.starts_with("activate_") || key.starts_with("use_"))
            return enabled(value) ? std::string("true") : std::string("false");
        try { return Json(number(value)).dump(); }
        catch (const std::exception&) { return value.dump(); }
    };
    for (auto it = machine.begin(); it != machine.end(); ++it) {
        if (metadata.contains(it.key()) || key_map.contains(it.key()) || schema.contains(it.key())
            || it.key().ends_with("gcode") || it.value().is_object() || it.value().is_null()) continue;
        if (it.value().is_array()) {
            for (size_t i = 0; i < it.value().size(); ++i)
                result[it.key() + "[" + std::to_string(i) + "]"] = literal(it.value()[i], it.key());
            if (!it.value().empty() && std::all_of(it.value().begin(), it.value().end(), [&](const Json& v) { return v == it.value().front(); })) {
                result[it.key()] = literal(it.value().front(), it.key());
                result[it.key() + "[*]"] = literal(it.value().front(), it.key());
            }
        } else result[it.key()] = literal(it.value(), it.key());
    }
    // These expressions use the actual first-layer hull calculated by PS3,
    // clamped to this printer's configured probe reach. No U1-specific bounds.
    const auto point = [&](const std::string& key, double fallback, int axis) {
        if (!machine.contains(key)) return fallback;
        const auto& v = machine.at(key);
        if (v.is_array()) return number(v.at(axis));
        const auto s = text(v);
        const auto split = s.find_first_of(",x");
        if (split == std::string::npos) throw std::runtime_error("Invalid mesh coordinate: " + key);
        return number(axis == 0 ? s.substr(0, split) : s.substr(split + 1));
    };
    if (machine.value("type", "") == "machine") {
        const auto margin = text(machine.value("adaptive_bed_mesh_margin", Json(0)));
        for (int axis = 0; axis < 2; ++axis) {
            const auto index = "[" + std::to_string(axis) + "]";
            const auto lo = "max(" + std::to_string(point("bed_mesh_min", -99999, axis)) + ", first_layer_print_min" + index + " - " + margin + ")";
            const auto hi = "min(" + std::to_string(point("bed_mesh_max", 99999, axis)) + ", first_layer_print_max" + index + " + " + margin + ")";
            result["adaptive_bed_mesh_min" + index] = lo;
            result["adaptive_bed_mesh_max" + index] = hi;
            const auto minimum = machine.value("gcode_flavor", "") == "klipper" ? "4" : "3";
            const auto count = "((" + hi + " - " + lo + ") / " + std::to_string(std::max(1.0, point("bed_mesh_probe_distance", 50, axis))) + ")";
            // PS3 has int(), but not ceil(). Counts are nonnegative here.
            result["bed_mesh_probe_count" + index] = "max(" + std::string(minimum) + ", int(" + count + ") + (" + count + " > int(" + count + ") ? 1 : 0) + 1)";
        }
        result["bed_mesh_algo"] = "\"bicubic\"";
        // The imported hardware initially uses Orca's hot/smooth-PEI bed
        // temperatures, matching hot_plate_temp and hot_plate_temp_initial_layer.
        result["curr_bed_type"] = "\"High Temp Plate\"";
    }
    result["support_air_filtration"] = "custom_parameter_printer_orca_support_air_filtration";
    result["during_print_exhaust_fan_speed_num[*]"] = "(custom_parameter_filament_orca_exhaust_fan_speed[current_extruder] * 255 / 100)";
    return result;
}

Json values(const Json& flat, const Json& schema, Vendor& vendor)
{
    Json result = Json::object();
    const auto context = gcode_context(flat, schema);
    for (auto it = flat.begin(); it != flat.end(); ++it) {
        const auto& src = it.key();
        if (metadata.contains(src) || src.starts_with("__")) continue;
        if (src == "post_process") {
            if (!it.value().empty()) issue(vendor, flat.value("name", ""), src, "Imported external post-processing commands are not enabled automatically");
            continue;
        }
        auto key = key_map.contains(src) ? key_map.at(src) : src;
        Json v = it.value();
        if (src == "support_style" || src == "support_type") key = "support_material_style";
        if (!schema.contains(key)) continue; // Report unknowns once, across all kinds.
        if (v == "nil" || v.is_null() || (v.is_array() && !v.empty()
            && std::all_of(v.begin(), v.end(), [](const Json& element) { return element == "nil" || element.is_null(); }))) continue;
        try {
            const auto map_elements = [](Json value, const auto& fn) -> Json {
                if (value.is_array()) for (auto& element : value) element = fn(element);
                else value = fn(value);
                return value;
            };
            const auto bool_enum = [&](const char* yes, const char* no) {
                return map_elements(v, [&](const Json& element) -> Json {
                    const auto s = text(element);
                    if (s == yes || s == no) return s;
                    return coerce(element, {{"type", "bool"}}).get<bool>() ? yes : no;
                });
            };
            if (src == "filament_shrink" || src == "filament_shrinkage_compensation_z") {
                // Orca stores retained dimensions (99% means 1% shrinkage).
                // PS3 stores the lost percentage, with 0% as the neutral value.
                v = map_elements(v, [](const Json& element) -> Json {
                    auto value = text(element);
                    if (value.ends_with('%')) value.pop_back();
                    return 100.0 - number(value);
                });
            }
            else if (src == "thumbnails") {
                // A bare single "48x48" is a point in the PS3 YAML schema.
                // Include the image format to preserve its string type.
                const auto format = flat.value("thumbnails_format", "PNG");
                auto thumbnails = v.is_array() ? v : Json::array({v});
                std::string joined;
                for (const auto& thumbnail : thumbnails) {
                    if (!joined.empty()) joined += ", ";
                    auto entry = text(thumbnail);
                    if (!entry.empty() && entry.find_first_of(",/") == std::string::npos) entry += "/" + format;
                    joined += entry;
                }
                v = joined;
            }
            else if (src == "emit_machine_limits_to_gcode") v = bool_enum("emit_to_gcode", "time_estimate_only");
            else if (src == "enable_arc_fitting") v = bool_enum("emit_center", "disabled");
            else if (src == "exclude_object") v = bool_enum("firmware", "disabled");
            else if (src == "gcode_label_objects") {
                // Legacy Orca boolean means OctoPrint labels; exclude_object
                // controls firmware cancellation and takes precedence.
                if (flat.contains("exclude_object") && enabled(flat.at("exclude_object"))) continue;
                v = map_elements(v, [](const Json& element) -> Json {
                    if (element == "firmware" || element == "octoprint" || element == "disabled") return element;
                    return coerce(element, {{"type", "bool"}}).get<bool>() ? "octoprint" : "disabled";
                });
            }
            else if (src == "ensure_vertical_shell_thickness") {
                v = map_elements(v, [](const Json& element) -> Json {
                    const auto s = text(element);
                    if (s == "0" || s == "false" || s == "none") return "disabled";
                    if (s == "1" || s == "true" || s == "ensure_all") return "enabled";
                    return element;
                });
            }
            else if (src == "enable_support") v = bool_enum("everywhere", "none");
            else if (src == "enable_pressure_advance") v = bool_enum("enabled", "disabled");
            else if (src.starts_with("overhang_") && src.ends_with("_4_speed")) {
                v = map_elements(v, [](const Json& element) -> Json {
                    const auto s = text(element);
                    // Orca's zero means the regular wall speed, not a stop.
                    return number(s.ends_with('%') ? Json(s.substr(0, s.size() - 1)) : element) == 0
                        ? Json("100%") : element;
                });
            }
            else if (src == "sparse_infill_pattern" && v == "crosshatch") {
                v = "grid";
                issue(vendor, flat.value("name", ""), src, "Crosshatch is approximated with grid infill");
            }
            else if (src == "print_sequence") v = text(v) == "by object";
            else if (src == "support_type") v = text(v).find("tree") != std::string::npos ? "organic" : "grid";
            else if (src == "support_style") {
                auto s = text(v);
                v = s.starts_with("tree") ? "organic" : s == "default" ? "grid" : s;
            } else if (src == "top_surface_pattern" || src == "bottom_surface_pattern") {
                if (v == "monotonicline") v = "monotoniclines";
            } else if (src == "support_base_pattern") {
                if (v == "default") v = "rectilinear";
                else if (v == "grid") v = "rectilinear-grid";
            } else if (src == "ironing_type") {
                const auto s = text(v);
                if (schema.contains("ironing")) result["ironing"] = s != "no ironing";
                if (s == "no ironing") continue;
                if (s == "top surfaces") v = "top";
                else if (s == "topmost surface") v = "topmost";
                else if (s == "all solid surfaces") v = "solid";
            }
            if (src == "bridge_acceleration" || src == "internal_solid_infill_acceleration" || src == "sparse_infill_acceleration") {
                const auto resolve = [&](Json value, size_t index) -> Json {
                    const auto s = text(value);
                    if (!s.ends_with('%')) return value;
                    const auto base_key = src == "bridge_acceleration" ? "outer_wall_acceleration" : "default_acceleration";
                    if (!flat.contains(base_key)) throw std::runtime_error("Percentage acceleration has no source base value");
                    auto base = flat.at(base_key);
                    if (base.is_array() && base.empty()) throw std::runtime_error("Empty acceleration base value");
                    if (base.is_array()) base = Json(base.at(std::min(index, base.size() - 1)));
                    return number(base) * number(s.substr(0, s.size() - 1)) / 100.0;
                };
                if (v.is_array()) for (size_t index = 0; index < v.size(); ++index) v[index] = resolve(v[index], index);
                else v = resolve(v, 0);
            }
            if (key.ends_with("gcode") || key == "output_filename_format") {
                if (text(v).find("outer_wall_volumetric_speed") != std::string::npos)
                    issue(vendor, flat.value("name", ""), src, "Outer-wall purge flow is estimated from PS3 process settings and capped by filament volumetric speed");
                auto field_context = context;
                field_context["__implicit_tool"] = key == "start_gcode" || key == "output_filename_format" ? "initial_tool"
                    : key == "toolchange_gcode" ? "next_extruder"
                    : key == "start_filament_gcode" || key == "end_filament_gcode" ? "filament_extruder_id" : "current_extruder";
                if (v.is_array()) for (auto& element : v) element = rewrite_gcode(text(element), field_context);
                else v = rewrite_gcode(text(v), field_context);
            }
            result[key] = coerce(v, schema.at(key));
            // Orca uses one speed for both sparse and solid first-layer infill.
            if (src == "initial_layer_infill_speed" && schema.contains("first_layer_solid_infill_speed"))
                result["first_layer_solid_infill_speed"] = coerce(v, schema.at("first_layer_solid_infill_speed"));
        } catch (const std::exception& e) {
            issue(vendor, flat.value("name", ""), src, e.what());
        }
    }
    const auto kind = flat.value("type", "");
    const auto parameter_key = kind == "machine" ? "custom_parameters_printer" : "custom_parameters_filament";
    if ((kind == "machine" || kind == "filament") && schema.contains(parameter_key)) {
        Json parameters = Json::object();
        if (result.contains(parameter_key) && !result.at(parameter_key).get<std::string>().empty())
            parameters = Json::parse(result.at(parameter_key).get<std::string>());
        const auto scalar_number = [&](const char* key, double fallback) {
            auto value = flat.value(key, Json(fallback));
            if (value.is_array()) value = value.empty() ? Json(fallback) : Json(value.front());
            auto s = text(value);
            if (s.ends_with('%')) s.pop_back();
            return number(s);
        };
        if (kind == "machine") parameters["orca_support_air_filtration"] = enabled(flat.value("support_air_filtration", Json(false)));
        else {
            parameters["orca_flush_speed"] = scalar_number("filament_flush_volumetric_speed", 0);
            parameters["orca_flush_temperature"] = scalar_number("filament_flush_temp", 0);
            parameters["orca_temperature_high"] = scalar_number("nozzle_temperature_range_high", scalar_number("nozzle_temperature", 200));
            parameters["orca_exhaust_fan_speed"] = scalar_number("during_print_exhaust_fan_speed", 0);
        }
        result[parameter_key] = parameters.dump();
    }
    return result;
}

// Orca compatibility predicates are evaluated against the flattened source
// printer, rather than guessed from a PS3 setting with a similar name.
class Compatibility
{
    std::string source;
    const Json& machine;
    size_t pos = 0;
    void space() { while (pos < source.size() && std::isspace(static_cast<unsigned char>(source[pos]))) ++pos; }
    bool take(const std::string& token) {
        space();
        if (source.compare(pos, token.size(), token) != 0) return false;
        const auto end = pos + token.size();
        if (std::isalpha(static_cast<unsigned char>(token.back())) && end < source.size()
            && (std::isalnum(static_cast<unsigned char>(source[end])) || source[end] == '_')) return false;
        pos = end;
        return true;
    }
    Json operand() {
        space();
        if (pos >= source.size()) throw std::runtime_error("Incomplete compatibility condition");
        if (source[pos] == '"') {
            const auto begin = pos++;
            while (pos < source.size()) {
                if (source[pos] == '\\') { pos += 2; continue; }
                if (source[pos++] == '"') return Json::parse(source.substr(begin, pos - begin));
            }
            throw std::runtime_error("Unterminated condition string");
        }
        const auto begin = pos;
        if (std::isdigit(static_cast<unsigned char>(source[pos])) || source[pos] == '-' || source[pos] == '.') {
            while (pos < source.size() && (std::isdigit(static_cast<unsigned char>(source[pos])) || source[pos] == '.' || source[pos] == '-')) ++pos;
            return number(source.substr(begin, pos - begin));
        }
        while (pos < source.size() && (std::isalnum(static_cast<unsigned char>(source[pos])) || source[pos] == '_')) ++pos;
        const auto key = source.substr(begin, pos - begin);
        if (key == "true") return true;
        if (key == "false") return false;
        Json value;
        if (machine.contains(key)) value = machine.at(key);
        else if (key == "printer_notes") value = "";
        else if (key == "printer_preset") value = machine.at("name");
        else if (key == "num_extruders") value = machine.at("__tools");
        else throw std::runtime_error("Unknown compatibility variable: " + key);
        if (take("[")) {
            const auto index = operand();
            if (!index.is_number() || !take("]") || !value.is_array()) throw std::runtime_error("Invalid compatibility index");
            value = Json(value.at(index.get<size_t>()));
        }
        return value;
    }
    bool comparison() {
        if (take("not") || take("!")) return !comparison();
        if (take("(")) {
            const bool v = disjunction();
            if (!take(")")) throw std::runtime_error("Unclosed compatibility condition");
            return v;
        }
        const auto lhs = operand();
        std::string op;
        for (const auto& candidate : {"==", "!=", "=~", "!~", ">=", "<=", ">", "<"})
            if (take(candidate)) { op = candidate; break; }
        if (op.empty()) return enabled(lhs);
        if (op == "=~" || op == "!~") {
            if (!take("/")) throw std::runtime_error("Expected regex literal");
            std::string regex;
            bool closed = false;
            while (pos < source.size()) {
                const char c = source[pos++];
                if (c == '/') { closed = true; break; }
                regex += c;
                if (c == '\\' && pos < source.size()) regex += source[pos++];
            }
            if (!closed) throw std::runtime_error("Unterminated compatibility regex");
            // Orca's Boost.Regex predicates let dot match newlines, which is
            // essential for the multi-line printer_notes model markers.
            std::string pattern;
            bool character_class = false;
            for (size_t i = 0; i < regex.size(); ++i) {
                const char c = regex[i];
                if (c == '\\' && i + 1 < regex.size()) { pattern += c; pattern += regex[++i]; }
                else if (c == '[') { character_class = true; pattern += c; }
                else if (c == ']') { character_class = false; pattern += c; }
                else if (c == '.' && !character_class) pattern += R"([\s\S])";
                else pattern += c;
            }
            const bool match = std::regex_match(text(lhs), std::regex(pattern));
            return op == "=~" ? match : !match;
        }
        const auto rhs = operand();
        if (op == "==" || op == "!=") {
            bool equal;
            if (rhs.is_number() || lhs.is_number()) equal = number(lhs) == number(rhs);
            else if (rhs.is_boolean() || lhs.is_boolean()) equal = enabled(lhs) == enabled(rhs);
            else equal = lhs == rhs;
            return op == "==" ? equal : !equal;
        }
        const auto a = number(lhs), b = number(rhs);
        return op == ">=" ? a >= b : op == "<=" ? a <= b : op == ">" ? a > b : a < b;
    }
    bool conjunction() {
        bool value = comparison();
        while (take("and") || take("&&")) { const bool rhs = comparison(); value = value && rhs; }
        return value;
    }
    bool disjunction() {
        bool value = conjunction();
        while (take("or") || take("||")) { const bool rhs = conjunction(); value = value || rhs; }
        return value;
    }
public:
    Compatibility(std::string expression, const Json& printer) : source(std::move(expression)), machine(printer) {}
    bool evaluate() {
        const bool value = disjunction();
        space();
        if (pos != source.size()) throw std::runtime_error("Unsupported compatibility syntax: " + source.substr(pos));
        return value;
    }
};

std::string condition(const Json& profile, const Json& machines)
{
    const auto compatible = profile.value("compatible_printers", Json::array());
    const auto expression = profile.value("compatible_printers_condition", "");
    if (compatible.empty() && expression.empty()) return "true";
    std::string result;
    for (const auto& m : machines) {
        if (!compatible.empty() && std::find(compatible.begin(), compatible.end(), m.at("name")) == compatible.end()) continue;
        // Orca consults the expression only when there is no explicit list.
        if (compatible.empty() && !expression.empty() && !Compatibility(expression, m).evaluate()) continue;
        if (!result.empty()) result += " or ";
        result += "printer.model == " + m.at("name").dump();
    }
    return result.empty() ? "false" : result;
}
} // namespace

std::string rewrite_gcode(const std::string& source, const std::map<std::string, std::string>& context)
{
    std::string result;
    for (size_t i = 0; i < source.size();) {
        if (source[i] != '{' && source[i] != '[') { result += source[i++]; continue; }
        const char open = source[i];
        const char close = open == '{' ? '}' : ']';
        const size_t begin = ++i;
        int depth = 1;
        char quote = 0;
        while (i < source.size()) {
            const char c = source[i];
            if (quote) {
                if (c == '\\' && i + 1 < source.size()) { i += 2; continue; }
                if (c == quote) quote = 0;
            } else if (c == '"' || c == '\'') quote = c;
            else if (c == open) ++depth;
            else if (c == close && --depth == 0) break;
            ++i;
        }
        if (i == source.size()) { result += source.substr(begin - 1); break; }
        auto expr = source.substr(begin, i++ - begin);
        // Legacy [vector_N] syntax is retained; expression syntax is used for
        // translated vector indexing to avoid ambiguous nested brackets.
        const auto rewritten = rewrite_expression(expr, context);
        if (open == '[' && rewritten != expr) result += "{" + rewritten + "}";
        else result += open + rewritten + close;
    }
    return result;
}

std::vector<Vendor> convert(const fs::path& root, const Schema& schema, bool include_prusa,
    const fs::path& cache_root, const std::set<std::string>& loaded_vendors)
{
    std::vector<Vendor> vendors;
    if (root.empty() || !fs::is_directory(root)) return vendors;
    std::vector<fs::path> manifests;
    for (const auto& entry : fs::directory_iterator(root))
        if (entry.is_regular_file() && entry.path().extension() == ".json" && fs::is_directory(root / entry.path().stem()))
            manifests.push_back(entry.path());
    std::sort(manifests.begin(), manifests.end());
    // The library is inherited by each vendor, but unrelated online/native
    // repositories and other Orca vendors are deliberately absent from this key.
    std::string library_checksum;
    Checksum schema_checksum;
    if (!cache_root.empty()) {
        library_checksum = source_checksum(root, "OrcaFilamentLibrary");
        schema_checksum.field(Json(schema).dump());
    }
    for (const auto& path : manifests) {
        const auto vendor_name = path.stem().string();
        if (vendor_name == "Prusa" && !include_prusa) continue;
        Vendor vendor;
        vendor.id = "Orca-" + vendor_name;
        if (loaded_vendors.contains(vendor.id)) continue;
        vendor.name = vendor_name + " (OrcaSlicer)";
        try {
            const auto manifest = read(path);
            vendor.name = manifest.value("name", vendor_name) + " (OrcaSlicer)";
            if (!manifest.contains("machine_list")) continue;
            vendor.version = text(manifest.value("version", Json("")));
            fs::path cache_path;
            Json identity;
            if (!cache_root.empty()) {
                cache_path = cache_root / fs::u8path(vendor.id) / fs::u8path(vendor.id) / "orca-conversion-cache.json";
                identity = {{"format", 1}, {"source", fs::weakly_canonical(root).generic_string()},
                    {"version", vendor.version}, {"checksum", source_checksum(root, vendor_name)},
                    {"library_checksum", library_checksum}, {"schema_checksum", schema_checksum.value()}};
                if (restore_conversion(cache_path, identity, vendor)) {
                    vendors.push_back(std::move(vendor));
                    continue;
                }
            }
            Index index;
            if (fs::exists(root / "OrcaFilamentLibrary.json") && vendor_name != "OrcaFilamentLibrary")
                load_manifest(index, root, "OrcaFilamentLibrary", vendor);
            load_manifest(index, root, vendor_name, vendor);
            std::vector<Json> processes, filaments;
            for (const auto& [key, raw] : index.profiles) {
                if (!enabled(raw.value("instantiation", Json(false)))) continue;
                try {
                    auto flat = index.flatten(key);
                    const auto kind = flat.at("type").get<std::string>();
                    if (kind == "machine") {
                        if (flat.value("printer_technology", "FFF") != "FFF")
                            throw std::runtime_error("Orca SLA JSON schema is not supported");
                        flat["use_relative_e_distances"] = flat.value("use_relative_e_distances", Json(true));
                        const auto nozzle = flat.value("nozzle_diameter", Json::array({"0.4"}));
                        if (!nozzle.is_array() || nozzle.empty() || nozzle.size() > 255)
                            throw std::runtime_error("Invalid nozzle list");
                        for (const auto& diameter : nozzle)
                            if (number(diameter) <= 0) throw std::runtime_error("Invalid nozzle diameter");
                        flat["nozzle_diameter"] = nozzle;
                        flat["__tools"] = nozzle.size();
                        const auto model_name = flat.value("printer_model", flat.at("name").get<std::string>());
                        if (const auto model = index.profiles.find("machine_model/" + model_name); model != index.profiles.end()) {
                            const auto base = fs::weakly_canonical(root / fs::u8path(vendor_name));
                            Json visual = Json::object();
                            for (const auto* key : {"bed_model", "bed_texture", "thumbnail"}) {
                                const auto filename = model->second.value(key, std::string(key) == "thumbnail" ? model_name + "_cover.png" : "");
                                if (filename.empty()) continue;
                                const auto asset = fs::weakly_canonical(base / fs::u8path(filename));
                                const auto relative = asset.lexically_relative(base);
                                if (relative.empty() || *relative.begin() == ".." || fs::u8path(filename).is_absolute()) {
                                    issue(vendor, model_name, key, "Asset path escapes vendor directory");
                                } else if (fs::is_regular_file(asset)) {
                                    visual[key] = relative.generic_string();
                                    vendor.assets.emplace(relative.generic_string(), asset);
                                } else issue(vendor, model_name, key, "Model asset is missing: " + filename);
                            }
                            flat["__visual"] = std::move(visual);
                        }
                        vendor.machines.push_back(std::move(flat));
                    } else if (kind == "process") processes.push_back(std::move(flat));
                    else if (kind == "filament") filaments.push_back(std::move(flat));
                } catch (const std::exception& e) { issue(vendor, key, "inherits", e.what()); }
            }
            const auto emit = [&](const Json& flat, const std::string& kind, const std::string& cond, Json v) {
                vendor.presets.push_back({{"kind", kind}, {"id", kind + "/" + flat.at("name").get<std::string>()},
                    {"name", flat.at("name")}, {"condition", cond}, {"values", std::move(v)}});
            };
            for (const auto& m : vendor.machines) {
                auto v = values(m, schema.at("printer"), vendor);
                v.erase("nozzle_diameter"); // Owned by the hardware definition.
                if (v.value("use_relative_e_distances", false)) {
                    auto layer = v.value("layer_gcode", "");
                    const auto layer_scripts = v.value("before_layer_gcode", "") + "\n" + layer;
                    if (!std::regex_search(layer_scripts, std::regex(R"((^|\n)\s*G92\s+E[+]?0(?:\.0*)?(?:\s|;|$))", std::regex::icase)))
                        v["layer_gcode"] = "G92 E0 ; reset relative extrusion distance each layer\n" + layer;
                }
                emit(m, "printer", "printer.model == " + m.at("name").dump(), std::move(v));
            }
            std::map<std::string, Json> machine_print_defaults;
            for (const auto& m : vendor.machines) {
                auto defaults = values(m, schema.at("print"), vendor);
                if (schema.at("print").contains("default_material") && m.contains("default_filament_profile")) {
                    auto material = m.at("default_filament_profile");
                    if (material.is_array()) material = material.empty() ? Json("") : Json(material.front());
                    for (const auto& f : filaments) {
                        if (f.at("name") != material) continue;
                        try {
                            if (condition(f, Json::array({m})) != "false") defaults["default_material"] = material;
                        } catch (const std::exception& e) {
                            issue(vendor, text(m.at("name")), "default_filament_profile", e.what());
                        }
                        break;
                    }
                }
                machine_print_defaults.emplace(text(m.at("name")), std::move(defaults));
            }
            for (const auto& p : processes) {
                std::string cond;
                try { cond = condition(p, vendor.machines); }
                catch (const std::exception& e) { issue(vendor, text(p.at("name")), "compatibility", e.what()); continue; }
                if (cond == "false") continue;
                auto v = values(p, schema.at("print"), vendor);
                v["default_tool_print"] = "Orca tool defaults";
                // Machine retraction defaults belong to the print settings in PS3.
                // Keep them as machine-conditioned variants, never average tools.
                Json variants = Json::array();
                for (const auto& m : vendor.machines) {
                    if (condition(p, Json::array({m})) == "false") continue;
                    auto defaults = machine_print_defaults.at(text(m.at("name")));
                    for (auto it = v.begin(); it != v.end(); ++it) defaults.erase(it.key());
                    if (defaults.empty()) continue;
                    variants.push_back({{"condition", "printer.model == " + m.at("name").dump()}, {"values", defaults}});
                }
                emit(p, "print", cond, std::move(v));
                vendor.presets.back()["variants"] = std::move(variants);
            }
            vendor.presets.push_back({{"kind", "tool_print"}, {"id", "orca-tool-defaults"},
                {"name", "Orca tool defaults"}, {"values", Json::object()}});
            for (const auto& f : filaments) {
                std::string cond;
                try { cond = condition(f, vendor.machines); }
                catch (const std::exception& e) { issue(vendor, text(f.at("name")), "compatibility", e.what()); continue; }
                if (cond == "false") continue;
                if (!f.value("compatible_prints_condition", "").empty() || !f.value("compatible_prints", Json::array()).empty()) {
                    issue(vendor, text(f.at("name")), "compatibility", "Expression compatibility requires translation; preset skipped");
                    continue;
                }
                emit(f, "filament", cond, values(f, schema.at("filament"), vendor));
            }
            for (const auto& [key, flat] : index.flattened) {
                for (auto it = flat.begin(); it != flat.end(); ++it) {
                    if (metadata.contains(it.key()) || it.key().starts_with("__")) continue;
                    const auto mapped = key_map.contains(it.key()) ? key_map.at(it.key()) : it.key();
                    bool known = false;
                    for (const auto& [kind, defs] : schema) known = known || defs.contains(mapped);
                    if (!known) issue(vendor, flat.value("name", key), it.key(), "No PrusaSlicer equivalent; source setting was not applied");
                }
            }
            if (!cache_path.empty() && !vendor.machines.empty()) save_conversion(cache_path, identity, vendor);
        } catch (const std::exception& e) { issue(vendor, vendor_name, "manifest", e.what()); }
        if (!vendor.machines.empty() || !vendor.diagnostics.empty()) vendors.push_back(std::move(vendor));
    }
    return vendors;
}
} // namespace Slic3r::Biz::Preset::IO::Orca
