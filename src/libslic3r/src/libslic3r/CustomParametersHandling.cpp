#include "libslic3r/CustomParametersHandling.hpp"
#include "Slic3r/Biz/Parser/PlaceholderParser.hpp"
#include "Slic3r/Assert.hpp"

#include "nlohmann/json.hpp"

#include <variant>

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

void add_custom_parameters_into_placeholder_parser(
    const std::string& cp_print,
    const std::string& cp_printer,
    const std::vector<std::string>& cp_filaments,
    Biz::Parser::PlaceholderParser& parser)
{
    // First handle print and printer. That is quite straightforward.
    for (const auto& [prefix, source] : {std::make_pair("custom_parameter_print_", cp_print), std::make_pair("custom_parameter_printer_", cp_printer)}) {
        std::optional<CustomParameterValues> print_or_printer = parse_custom_parameters(source);
        if (print_or_printer) {
            for (auto& [key, val] : *print_or_printer) {
                std::visit(
                    [&](const auto& value) {
                        using T = std::decay_t<decltype(value)>;
                        if constexpr (std::is_same_v<T, std::monostate>) {
                            parser.set(prefix + key, std::optional<int>{});
                        } else {
                            parser.set(prefix + key, value);
                        }
                    },
                    val);
            }
        }
    }

    // Handling filaments is a bit hairy. First parse custom parameters for all filaments
    // and store the result for each key in a concise vector.
    std::map<std::string, std::vector<CustomParameterType>> result;
    size_t idx = 0;
    for (const std::string& cp_filament : cp_filaments) {
        const std::optional<CustomParameterValues>& cp_vals = parse_custom_parameters(cp_filament);
        if (cp_vals) {
            for (const auto& [key, val] : *cp_vals) {
                result.try_emplace(key, cp_filaments.size());
                result[key][idx] = val;
            }
        }
        ++idx;
    }
    
    // Now iterate through the vector and construct the typed vector of optionals
    // that the placeholder parser can consume.
    for (auto const& [key, val] : result) {
        const CustomParameterType* first_val = nullptr;
        for (const CustomParameterType& opt : val) {
            if (! std::holds_alternative<std::monostate>(opt)) {
                first_val = &opt;
                break;
            }
        }
        if (first_val) {
            // At least one filament has a non-null for this key.
            std::visit(
                [&](const auto& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, std::monostate>) {
                        UNREACHABLE();
                    } else {
                        std::vector<std::optional<T>> out;
                        for (const CustomParameterType& opt : val) {
                            // If the config was valid, all elements in the vector can be either
                            // monostate or exactly one of the other types.
                            out.push_back(std::holds_alternative<std::monostate>(opt) ? std::optional<T>{} : std::get<T>(opt));
                        }
                        parser.set("custom_parameter_filament_" + key, out);
                    }
                }, *first_val);
        } else {
            // All filaments have a null under this key. Type them as ints, it does not matter.
            parser.set("custom_parameter_filament_" + key, std::vector<std::optional<int>>(val.size()));
        }
    }
}

} // namespace Slic3r
