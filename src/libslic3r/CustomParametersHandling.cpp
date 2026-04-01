#include "CustomParametersHandling.hpp"

#include "nlohmann/json.hpp"

#include <exception>

namespace Slic3r {

std::optional<CustomParameterValues> parse_custom_parameters(const std::string& input)
{
    if (input.empty())
        return CustomParameterValues();

    using json = nlohmann::json;

    try {
        json j = json::parse(input);

        if (!j.is_object()) {
            return std::nullopt;
        }

        CustomParameterValues values;

        for (auto& [key, val] : j.items()) {
            if (val.is_null()) {
                values[key] = std::monostate{};
            } else if (val.is_string()) {
                values[key] = val.get<std::string>();
            } else if (val.is_number_integer()) {
                values[key] = val.get<int>();
            } else if (val.is_number_float()) {
                values[key] = val.get<double>();
            } else if (val.is_boolean()) {
                values[key] = val.get<bool>();
            } else if (val.is_array()) {
                    return std::nullopt;
            } else {
                // Unsupported type
                return std::nullopt;
            }
        }
        return values;
    } catch (const json::parse_error&) {
        return std::nullopt;
    }

    return std::nullopt;
}



static bool check_filament_key_types(const std::vector<CustomParameterValues>& parsed_filaments)
{
    std::map<std::string, size_t> key_types;
    for (const auto& filament_params : parsed_filaments) {
        for (const auto& [key, value] : filament_params) {
            if (std::holds_alternative<std::monostate>(value))
                continue;
            auto it = key_types.find(key);
            if (it == key_types.end()) {
                key_types[key] = value.index();
            } else {
                if (it->second != value.index()) {
                    return false; // Type mismatch for the same key
                }
            }
        }
    }
    return true;
}


bool check_custom_parameters(const std::string& cp_print, const std::string& cp_printer, const std::vector<std::string>& cp_filaments, std::string* error)
{
    if (!parse_custom_parameters(cp_print) || !parse_custom_parameters(cp_printer)) {
        if (error)
            *error = "print or printer JSON issue";
        return false;
    }

    std::vector<CustomParameterValues> parsed_filaments;
    for (const std::string& s : cp_filaments) {
        auto map = parse_custom_parameters(s);
        if (! map) {
            if (error)
                *error = "filament JSON issue";
            return false;
        }
        parsed_filaments.emplace_back(*map);
    }

    // Now check that same keys for different filaments have the same type.
    // This is separate function so we can check it separately where needed
    // to avoid parsing the JSON twice.
    if (check_filament_key_types(parsed_filaments))
        return true;
    else {
        if (error)
            *error = "type mismatch for different filaments";
        return false;
    }
}



DynamicConfig parse_custom_parameters_to_dynamic_config(
    const std::string& cp_print,
    const std::string& cp_printer,
    const std::vector<std::string>& cp_filaments)
{
    CustomParameterValues parsed_print;
    CustomParameterValues parsed_printer;
    std::vector<CustomParameterValues> parsed_filaments;

    {
        auto print_opt = parse_custom_parameters(cp_print);
        auto printer_opt = parse_custom_parameters(cp_printer);
        if (! print_opt || ! printer_opt)
            throw std::runtime_error("print or printer JSON issue");
        parsed_print = *print_opt;
        parsed_printer = *printer_opt;
        for (const std::string& s : cp_filaments) {
            auto filament_opt = parse_custom_parameters(s);
            if (! filament_opt)
                throw std::runtime_error("filament JSON issue");
            parsed_filaments.emplace_back(std::move(*filament_opt));
        }
        if (! check_filament_key_types(parsed_filaments))
            throw std::runtime_error("type mismatch for different filaments");
    }

    DynamicConfig config;
    for (const auto& [map, prefix] : { std::make_pair(parsed_print, std::string("custom_parameter_print")), 
                                       std::make_pair(parsed_printer, std::string("custom_parameter_printer")) }) {
        for (const auto& [key, v] : map) {
            std::string full_key = prefix + "_" + key;
            if (const int* val = std::get_if<int>(&v))
                config.set_key_value(full_key, new ConfigOptionInt(*val));
            else if (const double* val = std::get_if<double>(&v))
                config.set_key_value(full_key, new ConfigOptionFloat(*val));
            else if (const bool* val = std::get_if<bool>(&v))
                config.set_key_value(full_key, new ConfigOptionBool(*val));
            else if (const std::string* val = std::get_if<std::string>(&v))
                config.set_key_value(full_key, new ConfigOptionString(*val));
            else {
                // nulls are typed as ints but assigned nil value, so the only thing they can do is fail.
                config.set_key_value(full_key, new ConfigOptionIntNullable(ConfigOptionInt::nil_value()));
            }
        }
    }
    size_t idx = 0;
    for (const CustomParameterValues& map : parsed_filaments) {
        for (const auto& [key, v] : map) {
            std::string full_key = std::string("custom_parameter_filament_") + key;
            if (! config.has(full_key)) {
                if (const int* val = std::get_if<int>(&v))
                    config.set_key_value(full_key, new ConfigOptionIntsNullable(parsed_filaments.size(), ConfigOptionIntsNullable::nil_value()));
                else if (const double* val = std::get_if<double>(&v))
                    config.set_key_value(full_key, new ConfigOptionFloatsNullable(parsed_filaments.size(), ConfigOptionFloatsNullable::nil_value()));
                else if (const bool* val = std::get_if<bool>(&v)) {
                    config.set_key_value(full_key, new ConfigOptionBoolsNullable(parsed_filaments.size(), false));
                    // The existing constructor casts the argument to bool, do not touch it and instead set it element-wise:
                    auto* o = config.opt<ConfigOptionBoolsNullable>(full_key);
                    for (auto& val : o->values)
                        val = ConfigOptionBoolsNullable::nil_value();
                }
                else if (const std::string* val = std::get_if<std::string>(&v)) {
                    // Strings default to empty string instead of being rejected as nil. Slicer 2.9.2 cannot reasonably
                    // serialize nil strings, which is why they are not supported at all.
                    config.set_key_value(full_key, new ConfigOptionStrings(parsed_filaments.size(), ""));
                }
            }
            if (const int* val = std::get_if<int>(&v))
                config.option<ConfigOptionIntsNullable>(full_key)->values[idx] = *val;
            else if (const double* val = std::get_if<double>(&v))
                config.option<ConfigOptionFloatsNullable>(full_key)->values[idx] = *val;
            else if (const bool* val = std::get_if<bool>(&v))
                config.option<ConfigOptionBoolsNullable>(full_key)->values[idx] = *val;
            else if (const std::string* val = std::get_if<std::string>(&v))
                config.option<ConfigOptionStrings>(full_key)->values[idx] = *val;
        }
        ++idx;
    }
    return config;
}

std::string merge_json(const std::string& bottom_json, const std::string& top_json)
{
    if (bottom_json.empty())
        return top_json;
    if (top_json.empty())
        return bottom_json;

    using json = nlohmann::json;

    try {
        json j_bottom = json::parse(bottom_json);
        json j_top = json::parse(top_json);

        if (!j_bottom.is_object() || !j_top.is_object()) {
            return top_json;
        }

        j_bottom.merge_patch(j_top);
        return j_bottom.dump();
    }
    catch (const json::parse_error&) {
        return top_json;
    }
}

} // namespace Slic3r
