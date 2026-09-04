#pragma once

#include "Slic3r/TypeInfo.hpp"
#include "Slic3r/App/Platform/KeyCode.hpp"
#include "Slic3r/Biz/Expr/Eval.hpp"
#include "Slic3r/Domain/Preset/HwConfig.hpp"
#include "Slic3r/Domain/Preset/EvaluatedPreset.hpp"
#include "Slic3r/Log.hpp"

namespace Slic3r::Biz::Preset {

namespace Details {
template <typename T, typename V>
struct IsOneOfVariantTypes : std::false_type
{};

template <typename T, typename... Vs>
struct IsOneOfVariantTypes<T, std::variant<Vs...>> : std::disjunction<std::is_same<T, Vs>...>
{};

template <typename T, typename... Vs>
struct IsOneOfVariantTypes<T, boost::variant<Vs...>> : std::disjunction<std::is_same<T, Vs>...>
{};

template <typename T, typename... Vs>
struct FindFirstConvertibleType;

template <typename T>
struct FindFirstConvertibleType<T>
{
    using Type = void;
};

template <typename T, typename V, typename... Vs>
struct FindFirstConvertibleType<T, V, Vs...>
{
    using Type = std::conditional_t<
        std::is_convertible_v<T, V>,
        V,
        typename FindFirstConvertibleType<T, Vs...>::Type>;
};

template <typename T, typename V>
struct IsOneOfVariantConvertibleTypes
{
    using Type                  = void;
    static constexpr bool value = false;
};

template <typename T, typename... Vs>
struct IsOneOfVariantConvertibleTypes<T, std::variant<Vs...>> //: std::disjunction<std::is_convertible<T, Vs> ...>
{
    using Type                  = typename FindFirstConvertibleType<T, Vs...>::Type;
    static constexpr bool value = !std::is_same_v<Type, void>;
};

template <typename T, typename... Vs>
struct IsOneOfVariantConvertibleTypes<T, boost::variant<Vs...>> //: std::disjunction<std::is_convertible<T, Vs> ...>
{
    using Type                  = typename FindFirstConvertibleType<T, Vs...>::Type;
    static constexpr bool value = !std::is_same_v<Type, void>;
};
} // namespace Details

template <typename... Ts>
void append_value(Expr::ValueMap& values, const std::string& name, const std::variant<Ts...>& v)
{
    Expr::Value val;
    if (std::holds_alternative<std::string>(v)) {
        val = std::get<std::string>(v);
    } else if (std::holds_alternative<double>(v)) {
        val = std::get<double>(v);
    } else if (std::holds_alternative<bool>(v)) {
        val = std::get<bool>(v);
    } else {
        PANIC("unsupported type");
    }
    values[name] = val;
}

template <typename T>
concept ValueTypeCompatible = Details::IsOneOfVariantTypes<T, Expr::Value>::value;

template <typename T>
concept NumberType = (std::is_integral_v<T> || std::is_floating_point_v<T>)
    && !ValueTypeCompatible<T>;

template <typename T>
concept ValueTypeConvertible = Details::IsOneOfVariantConvertibleTypes<T, Expr::Value>::value
    && !NumberType<T>
    && !ValueTypeCompatible<T>;

template <typename T>
concept StringConvertable = requires(T t) {
    {
        std::to_string(t)
    } -> std::same_as<std::string>;
};

// && !NumberType<T>
// && !ValueTypeCompatible<T>
// && !ValueTypeConvertible<T>;

template <ValueTypeCompatible T>
void append_value(Expr::ValueMap& values, const std::string& name, const T& v)
{
    values[name] = v;
}

template <NumberType T>
void append_value(Expr::ValueMap& values, const std::string& name, const T& v)
{
    values[name] = static_cast<double>(v);
}

template <ValueTypeConvertible T>
void append_value(Expr::ValueMap& values, const std::string& name, const T& v)
{
    values[name] = static_cast<typename Details::IsOneOfVariantConvertibleTypes<T, Expr::Value>::Type>(
        v
    );
}

template <StringConvertable T>
void append_value_as_string(Expr::ValueMap& values, const std::string& name, const T& v)
{
    SPDLOG_CRITICAL("ValueCompatible<{}>: {}", Slic3r::type_name(v), ValueTypeConvertible<T>);
    values[name] = std::to_string(v);
}

inline void append_value(Expr::ValueMap& values, const std::string& name, const std::string& v)
{
    values[name] = v;
}

void append_value(Expr::ValueMap& values, const char* prefix, const Domain::Preset::HwModel& v);

void append_values(
    Expr::ValueMap& values,
    const char* prefix,
    const Domain::Preset::FeatureValueMap& features
);

void append_printer_values(Expr::ValueMap& values, const Domain::Preset::HwPrinterConfig& printer);

void append_print_values(Expr::ValueMap& values, const Domain::ConfigBox& print_preset);

void append_tool_values(Expr::ValueMap& values, const Domain::Preset::HwToolConfig& tool);

void append_sheet_values(Expr::ValueMap& values, const Domain::Preset::HwSheetConfig& sheet);

} // namespace Slic3r::Biz::Preset
