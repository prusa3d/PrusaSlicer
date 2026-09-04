#include "Slic3r/App/ConfigModelDump.hpp"

#include "Slic3r/Domain/Config.hpp"
#include "Slic3r/Domain/ConfigDef.hpp"
#include "Slic3r/Domain/ConfigBoxesFDM.hpp"
#include "Slic3r/Domain/ConfigBoxesSLA.hpp"
#include "Slic3r/Biz/Config/ConfigJson.hpp"

#include <nlohmann/json.hpp>
#include <boost/nowide/fstream.hpp>
#include <magic_enum/magic_enum.hpp>
#include <boost/preprocessor/variadic/to_seq.hpp>
#include <boost/preprocessor/seq/for_each.hpp>

using json = nlohmann::ordered_json;

namespace {

template <typename T>
concept WithToJsonDef = requires(const T& val, json& j) {
    {
        to_json_def(j, val)
    } -> std::same_as<void>;
};

template <WithToJsonDef T>
json unpack_def(const T& val)
{
    json j;
    to_json_def(j, val);
    return j;
}

} // namespace

namespace Slic3r::Domain {

void to_json(json& j, const ConfigLocation& config_location)
{
    j = std::visit(
        overloaded{
            [](const auto& v) { return magic_enum::enum_name(v); },
            [](const PhysicalPrinterLocation& v) -> std::string_view {
                return "physical_printer";
            },
            [](const AppConfigLocation& v) -> std::string_view
            {
              return "app_config";
            }
        },
        config_location
    );
}

void to_json_def(json& j, const ConfigItemDef& cd)
{
    j = json{{
        "name",
        cd.name,
    }};

    if (cd.max != FLT_MAX)
        j["max"] = cd.max;
    if (cd.min != -FLT_MAX)
        j["min"] = cd.min;
}

static std::vector<std::string> to_strings(const std::vector<std::string_view>& strings)
{
    std::vector<std::string> out;
    out.insert(out.end(), strings.begin(), strings.end());
    return out;
}

void to_json_def(json& json_value, const ConfigValue& val)
{
    val.visit([&]<typename T0>(T0&& item_value) -> void {
        using ValueType = std::remove_cvref_t<T0>;
        if constexpr (std::is_same_v<ValueType, EnumWrapper>) {
            json_value = item_value.get_string();
        } else if constexpr (std::is_same_v<ValueType, EnumVectorWrapper>) {
            json_value = to_strings(item_value.get_strings());
        } else {
            json_value = val.get<ValueType>();
        }
    });
}

namespace {
template <typename T>
struct TypeName
{
    static std::string name()
    {
        return "unknown";
    }

    static bool is_array()
    {
        return false;
    }
};

template <>
struct TypeName<EnumWrapper>
{
    static std::string name()
    {
        return "Enum";
    }

    static bool is_array()
    {
        return false;
    }
};

template <>
struct TypeName<EnumVectorWrapper>
{
    static std::string name()
    {
        return "Enum";
    }

    static bool is_array()
    {
        return true;
    }
};

#define DEF_TYPE_NAME(T)                                                    \
template <>                                                                 \
struct TypeName<T>                                                          \
{                                                                           \
    static std::string name() { return #T; }                                \
    static bool is_array() { return false; }                                \
};                                                                          \
template <>                                                                 \
struct TypeName<std::vector<T>>                                             \
{                                                                           \
    static std::string name() { return #T; }                                \
    static bool is_array() { return true; }                                 \
};

#define DEF_TYPE_NAME_FOREACH_INVOKE(r, data, elem) \
DEF_TYPE_NAME(elem)

#define DEF_TYPE_NAMES(...) \
BOOST_PP_SEQ_FOR_EACH(DEF_TYPE_NAME_FOREACH_INVOKE, _, BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__))


// EnumWrapper,
// bool,
// int,
// std::optional<int>,
// double,
// std::string,
// Domain::Vec2d,
// FloatOrPercentage,
// Percentage,
// EnumVectorWrapper,
// std::vector<bool>,
// std::vector<int>,
// std::vector<std::optional<int>>,
// std::vector<double>,
// std::vector<std::string>,
// std::vector<Domain::Vec2d>,
// std::vector<FloatOrPercentage>,
// std::vector<Percentage>
using String = std::string;
using OptInt = std::optional<int>;
using Int = int;
using Float = double;
using Bool = bool;
DEF_TYPE_NAMES(
    Bool,
    Int,
    OptInt,
    Float,
    String,
    Vec2d,
    FloatOrPercentage,
    Percentage
);

#undef DEF_TYPE_NAME
#undef DEF_TYPE_NAME_FOREACH_INVOKE
#undef DEF_TYPE_NAMES



}

void to_json_def(json& j, const EnumValueDefs& eds)
{
    std::vector<std::string> enum_values;
    for (const auto& evd : eds) {
        enum_values.push_back(evd.str_serialized);
    }
    j = enum_values;
}


void to_json_def(json& j, const ConfigItem& ci)
{
    const auto& def = ci.def();
    to_json_def(j, def);
    auto [type_name, is_array] = ci.visit([ci]<typename T>(const T&) {
        return std::make_pair(TypeName<T>::name(), TypeName<T>::is_array());
    });
    if (is_array) {
        j["type"] = "Array";
        j["element_type"] = type_name;
    } else {
        j["type"] = type_name;
    }
    if (ci.holds_alternative<EnumWrapper>()) {
        auto ew = ci.get<EnumWrapper>();
        j["enum_values"] = unpack_def(ew.def());
    } else if (ci.holds_alternative<EnumVectorWrapper>()) {
        auto ew = ci.get<EnumVectorWrapper>();
        j["enum_values"] = unpack_def(ew.def());
    }
    j["location"] = def.location;
    j["overrides_in"] = def.overrides_in;

    j["value"] = unpack_def(ci.value());
}

void to_json_def(json& j, const ConfigItems& c)
{
    j = json::array();
    for (const auto& i : c.all_items())
        j.emplace_back(unpack_def(i));
}

void to_json_def(json& j, const ConfigOverrides& co)
{
    j = json::array();
    for (const auto& o : co.overridden_items())
        j.emplace_back(unpack_def(o));
}

void to_json_def(json& j, const ConfigBox& cb)
{
    j = json{
        {"location", cb.location},
        {"items", unpack_def(cb.items)},
        {"overrides", unpack_def(cb.overrides)}
    };
}

struct ConfigBoxDefs
{
    PrinterSettings printer_settings;
    PrintSettings print_settings;
    ToolPrintSettings tool_print_settings;
    FilamentSettings filament_settings;
    SLAPrinterSettings sla_printer_settings;
    SLAPrintSettings sla_print_settings;
    SLAMaterialSettings sla_material_settings;
};

void to_json_def(json& j, const ConfigBoxDefs& cbd)
{
    j = json{
        {"printer", unpack_def(cbd.printer_settings)},
        {"print", unpack_def(cbd.print_settings)},
        {"tool_print", unpack_def(cbd.tool_print_settings)},
        {"filament", unpack_def(cbd.filament_settings)},
        {"sla_printer", unpack_def(cbd.sla_printer_settings)},
        {"sla_print", unpack_def(cbd.sla_print_settings)},
        {"sla_material", unpack_def(cbd.sla_material_settings)},
    };
}

void fill_overrides(const ConfigBox& source, ConfigBox& print, ConfigBox* tool_print, ConfigBox& material)
{
    auto insert_print_override = [&print](const ConfigItem& item) {
        item.visit([&](const auto& v) {
            print.overrides.set(item.name(), v);
        });
    };
    auto insert_tool_print_override = [&tool_print](const ConfigItem& item) {
        if (tool_print /*&& tool_print->contains(item.name()).item == nullptr*/)
            item.visit([&](const auto& v) {
                tool_print->overrides.set(item.name(), v);
            });
    };
    auto insert_material_override = [&material](const ConfigItem& item) {
        item.visit([&](const auto& v) {
            material.overrides.set(item.name(), v);
        });
    };

    for (const auto& ci : source.items.all_items()) {
        for (const auto& loc : ci.def().overrides_in)
            std::visit(
                overloaded{
                    [&](const FDMConfigLocation& l) {
                        switch (l) {
                        case FDMConfigLocation::Print:
                            insert_print_override(ci);
                            break;
                        case FDMConfigLocation::Tool:
                            insert_tool_print_override(ci);
                            break;
                        case FDMConfigLocation::Filament:
                            insert_material_override(ci);
                            break;
                        default:
                            break;
                        }
                    },
                    [&](const SLAConfigLocation& l) {
                        switch (l) {
                        case SLAConfigLocation::Print:
                            insert_print_override(ci);
                            break;
                        case SLAConfigLocation::Material:
                            insert_material_override(ci);
                            break;
                        default:
                            break;
                        }
                    },
                    [](const PhysicalPrinterLocation&) {},
                    [](const AppConfigLocation&) {},
                },
                loc
            );
    }
}


} // namespace Slic3r::Domain

namespace Slic3r::App {

void dump_config_model(const std::string& json_path)
{
    Domain::ConfigBoxDefs defs;
    for (auto& box : std::vector<std::reference_wrapper<Domain::ConfigBox>>{
             defs.printer_settings,
             defs.print_settings,
             defs.tool_print_settings,
             defs.filament_settings
         })
    {
        Domain::fill_overrides(
            box,
            defs.print_settings,
            &defs.tool_print_settings,
            defs.filament_settings
        );
    }

    for (auto& box : std::vector<std::reference_wrapper<Domain::ConfigBox>>{
             defs.sla_printer_settings,
             defs.sla_print_settings,
             defs.sla_material_settings
         })
    {
        Domain::fill_overrides(
            box,
            defs.sla_print_settings,
            nullptr,
            defs.sla_material_settings
        );
    }

    json j;
    Domain::to_json_def(j, defs);

    boost::nowide::ofstream ofs(json_path, std::ios::out | std::ios::binary);
    ofs << j;
    ofs.close();
}

} // namespace Slic3r::App
