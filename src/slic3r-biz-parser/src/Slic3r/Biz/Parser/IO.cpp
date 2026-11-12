#include "Slic3r/Biz/Parser/IO.hpp"
#include "Slic3r/Assert.hpp"
#include "Slic3r/Domain/Config.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "LocalesUtils.hpp"

#include <map>
#include <string>
#include <vector>

namespace Slic3r::Biz::Parser::IO {

using Domain::Percentage;
using Domain::Vec2d;
using Domain::ConfigView;
using Domain::ConfigPack;
using Domain::EnumWrapper;
using Domain::EnumVectorWrapper;
using Domain::ConfigValue;

// clang-format off
template<typename T>
struct get_option_type { static constexpr auto value{Type::None}; };
template<> struct get_option_type<double> { static constexpr auto value{Type::Float}; };
template<> struct get_option_type<std::vector<double>> { static constexpr auto value{Type::Floats}; };
template<> struct get_option_type<int> { static constexpr auto value{Type::Int}; };
template<> struct get_option_type<std::vector<int>> { static constexpr auto value{Type::Ints}; };
template<> struct get_option_type<std::optional<int>> { static constexpr auto value{Type::IntOptional}; };
template<> struct get_option_type<std::optional<double>> { static constexpr auto value{Type::FloatOptional}; };
template<> struct get_option_type<std::optional<std::string>> { static constexpr auto value{Type::StringOptional}; };
template<> struct get_option_type<std::optional<bool>> { static constexpr auto value{Type::BoolOptional}; };
template<> struct get_option_type<std::vector<std::optional<int>>> { static constexpr auto value{Type::IntOptionals}; };
template<> struct get_option_type<std::vector<std::optional<double>>> { static constexpr auto value{Type::FloatOptionals}; };
template<> struct get_option_type<std::vector<std::optional<std::string>>> { static constexpr auto value{Type::StringOptionals}; };
template<> struct get_option_type<std::vector<std::optional<bool>>> { static constexpr auto value{Type::BoolOptionals}; };
template<> struct get_option_type<std::string> { static constexpr auto value{Type::String}; };
template<> struct get_option_type<std::vector<std::string>> { static constexpr auto value{Type::Strings}; };
template<> struct get_option_type<Percentage> { static constexpr auto value{Type::Percent}; };
template<> struct get_option_type<std::vector<Percentage>> { static constexpr auto value{Type::Percents}; };
template<> struct get_option_type<Vec2d> { static constexpr auto value{Type::Point}; };
template<> struct get_option_type<std::vector<Vec2d>> { static constexpr auto value{Type::Points}; };
template<> struct get_option_type<bool> { static constexpr auto value{Type::Bool}; };
template<> struct get_option_type<std::vector<bool>> { static constexpr auto value{Type::Bools}; };
// clang-format on

static bool is_type_optional(Type type)
{
    return type == Type::FloatOptional || type == Type::IntOptional ||
           type == Type::StringOptional || type == Type::BoolOptional ||
           type == Type::FloatOptionals || type == Type::IntOptionals ||
           type == Type::StringOptionals || type == Type::BoolOptionals;
}

template<typename T>
constexpr Type get_option_type_v = get_option_type<T>::value;

template<typename T>
requires (!std::is_enum_v<T>)
Scalar::Scalar(const T& value)
{
    constexpr auto type{get_option_type_v<T>};
    static_assert(type != Type::None, "Unknown type passed to set");

    m_value = value;
    m_type = type;
}
template Scalar::Scalar(const double&);
template Scalar::Scalar(const int&);
template Scalar::Scalar(const std::optional<int>&);
template Scalar::Scalar(const std::optional<double>&);
template Scalar::Scalar(const std::optional<std::string>&);
template Scalar::Scalar(const std::optional<bool>&);
template Scalar::Scalar(const std::string&);
template Scalar::Scalar(const Percentage&);
template Scalar::Scalar(const Vec2d&);
template Scalar::Scalar(const bool&);

Scalar Scalar::init_enum(const int value, const std::string& serialized_value) {
    Scalar result{value};
    result.m_type = Type::Enum;
    result.m_serialized_value = serialized_value;
    return result;
}

bool Scalar::operator==(const Scalar& rhs) const {
    if (m_type != rhs.m_type) {
        return false;
    }
    switch (m_type) {
        case Type::None: return true;
        case Type::Float: return get<double>() == rhs.get<double>();
        case Type::Int: return get<int>() == rhs.get<int>();
        case Type::String: return get<std::string>() == rhs.get<std::string>();
        case Type::Percent: return get<Percentage>() == rhs.get<Percentage>();
        case Type::Point: return get<Vec2d>().isApprox(rhs.get<Vec2d>());
        case Type::Bool: return get<bool>() == rhs.get<bool>();
        case Type::Enum: return get<int>() == rhs.get<int>();
        default: PANIC("Programming error: invalid type reached!");
    }
}

Type Scalar::type() const { return m_type; }
bool Scalar::is_optional() const { return is_type_optional(m_type); }

std::string Scalar::serialize() const {
    switch(m_type){
    case Type::Float: return float_to_string_decimal_point(get<double>());
    case Type::Int: return std::to_string(get<int>());
    case Type::IntOptional: {
        const auto result{get<std::optional<int>>()};
        return result ? std::to_string(*result) : "nil";
    }
    case Type::String: return get<std::string>();
    case Type::Percent: {
        const auto result{get<Percentage>()};
        return float_to_string_decimal_point(result.value) + "%";
    }
    case Type::Point: {
        const auto result{get<Vec2d>()};
        return float_to_string_decimal_point(result.x()) + "," +
            float_to_string_decimal_point(result.y());
    }
    case Type::Bool: {
        const auto result{get<bool>()};
        return result ? "1" : "0";
    }
    case Type::Enum: return *ASSERT_VAL(m_serialized_value);
    default: PANIC("Programming error: invalid type reached!");
    }
}

template<typename T>
requires (!std::is_enum_v<T>)
T Scalar::get() const
{
    ASSERT(get_option_type_v<T> == m_type);
    const T* result{std::any_cast<T>(&m_value)};
    ASSERT(result != nullptr);
    return *result;
}
template double Scalar::get() const;
template int Scalar::get() const;
template std::optional<int> Scalar::get() const;
template std::optional<double> Scalar::get() const;
template std::optional<std::string> Scalar::get() const;
template std::optional<bool> Scalar::get() const;
template std::string Scalar::get() const;
template Percentage Scalar::get() const;
template Vec2d Scalar::get() const;
template bool Scalar::get() const;

template<typename T>
requires (!is_pair_v<T>)
Vector::Vector(const std::vector<T>& values)
{
    constexpr auto type{std::is_enum_v<T> ? Type::Enums : get_option_type_v<std::vector<T>>};
    static_assert(type != Type::None, "Unknown type passed to set");
    m_type = type;
    for (const T& value : values) {
        m_values.push_back(Scalar{value});
    }
};
template Vector::Vector(const std::vector<double>&);
template Vector::Vector(const std::vector<int>&);
template Vector::Vector(const std::vector<std::optional<int>>&);
template Vector::Vector(const std::vector<std::optional<double>>&);
template Vector::Vector(const std::vector<std::optional<std::string>>&);
template Vector::Vector(const std::vector<std::optional<bool>>&);
template Vector::Vector(const std::vector<std::string>&);
template Vector::Vector(const std::vector<Percentage>&);
template Vector::Vector(const std::vector<Vec2d>&);
template Vector::Vector(const std::vector<bool>&);

bool Vector::operator==(const Vector& rhs) const {
    if (m_type != rhs.m_type) {
        return false;
    }
    return m_values == rhs.m_values;
}

Type Vector::type() const { return m_type; }
std::size_t Vector::size() const { return m_values.size(); }
bool Vector::empty() const { return m_values.empty(); }
bool Vector::holds_optionals() const { return is_type_optional(m_type); }

template<typename T>
requires (!std::is_enum_v<T>)
std::vector<T> Vector::get() const
{
    std::vector<T> result;
    for (const Scalar& value : m_values) {
        result.push_back(value.get<T>());
    }
    return result;
}
template std::vector<double> Vector::get() const;
template std::vector<int> Vector::get() const;
template std::vector<std::optional<int>> Vector::get() const;
template std::vector<std::optional<double>> Vector::get() const;
template std::vector<std::optional<std::string>> Vector::get() const;
template std::vector<std::optional<bool>> Vector::get() const;
template std::vector<std::string> Vector::get() const;
template std::vector<Percentage> Vector::get() const;
template std::vector<Vec2d> Vector::get() const;
template std::vector<bool> Vector::get() const;

Scalar& Vector::at(const std::size_t index) { return m_values.at(index); }
const Scalar& Vector::at(const std::size_t index) const { return m_values.at(index); };

bool is_scalar(const Value& value) { return std::holds_alternative<Scalar>(value); }
bool is_vector(const Value& value) { return std::holds_alternative<Vector>(value); }

Type type(const Value& value)
{
    return std::visit([](auto&& value) { return value.type(); }, value);
};

const Value* Config::option(const std::string& key) const
{
    const auto value_it{m_values.find(key)};
    if (value_it == m_values.end()) {
        return nullptr;
    }
    return &value_it->second;
}

Value* Config::optptr(const std::string& key) { return const_cast<Value*>(option(key)); }

template<typename T>
void Config::set(const std::string& key, const T& value)
{
    using InputType = typename std::remove_reference_t<T>;
    if constexpr (std::is_same_v<InputType, Value>) {
        m_values.insert_or_assign(key, value);
    } else {
        if constexpr (Domain::is_std_vector_v<T>) {
            m_values.insert_or_assign(key, Vector{value});
        } else {
            m_values.insert_or_assign(key, Scalar{value});
        }
    }
}
template void Config::set(const std::string&, const Value&);
template void Config::set(const std::string&, const double&);
template void Config::set(const std::string&, const int&);
template void Config::set(const std::string&, const std::optional<int>&);
template void Config::set(const std::string&, const std::optional<double>&);
template void Config::set(const std::string&, const std::optional<std::string>&);
template void Config::set(const std::string&, const std::optional<bool>&);
template void Config::set(const std::string&, const std::string&);
template void Config::set(const std::string&, const Percentage&);
template void Config::set(const std::string&, const Vec2d&);
template void Config::set(const std::string&, const bool&);
template void Config::set(const std::string&, const std::vector<double>&);
template void Config::set(const std::string&, const std::vector<int>&);
template void Config::set(const std::string&, const std::vector<std::optional<int>>&);
template void Config::set(const std::string&, const std::vector<std::optional<bool>>&);
template void Config::set(const std::string&, const std::vector<std::optional<double>>&);
template void Config::set(const std::string&, const std::vector<std::optional<std::string>>&);
template void Config::set(const std::string&, const std::vector<std::string>&);
template void Config::set(const std::string&, const std::vector<Percentage>&);
template void Config::set(const std::string&, const std::vector<Vec2d>&);
template void Config::set(const std::string&, const std::vector<bool>&);

void Config::apply(const Config& config) {
    for (const std::string &opt_key : config.keys()) {
        set(opt_key, *config.option(opt_key));
    }
}

std::vector<std::string> Config::keys() const {
    std::vector<std::string> keys;
    keys.reserve(m_values.size());
    std::ranges::transform(m_values, std::back_inserter(keys), [](const auto& pair) {
        return pair.first;
    });
    return keys;
};

bool Scalar::is_nil() const
{
    switch (m_type) {
        case Type::BoolOptional :   return ! this->get<std::optional<bool>>().has_value();
        case Type::IntOptional :    return ! this->get<std::optional<int>>().has_value();
        case Type::FloatOptional :  return ! this->get<std::optional<double>>().has_value();
        case Type::StringOptional : return ! this->get<std::optional<std::string>>().has_value();
        default: return false;
    }
}

static bool is_nullopt_at_or_all(const auto& vec, const std::optional<size_t> index) {
    return index.has_value() ? ! vec[*index].has_value() : std::ranges::all_of(vec, [](const auto& val) { return ! val.has_value(); });
}

static bool is_nil_internal(const Vector& vector, std::optional<size_t> index)
{
    switch (vector.type()) {
        case Type::BoolOptionals : return is_nullopt_at_or_all(vector.get<std::optional<bool>>(), index);
        case Type::IntOptionals :  return is_nullopt_at_or_all(vector.get<std::optional<int>>(), index);
        case Type::FloatOptionals : return is_nullopt_at_or_all(vector.get<std::optional<double>>(), index);
        case Type::StringOptionals : return is_nullopt_at_or_all(vector.get<std::optional<std::string>>(), index);
        default: return false;
    }
}

bool Vector::is_all_nil() const
{
    return is_nil_internal(*this, std::nullopt);

}

bool Vector::is_nil_at(size_t index) const
{
    return is_nil_internal(*this, index);

}

namespace {
void copy(const std::string& name, const ConfigValue& value, Config& config)
{
    value.visit([&](auto&& item_value){
        using ValueType = std::remove_cvref_t<decltype(item_value)>;
        if constexpr (std::is_same_v<ValueType, EnumWrapper>) {
            const EnumWrapper& enum_wrapper{item_value};
            config.set(name, std::string{enum_wrapper.get_string()});
        } else if constexpr (std::is_same_v<ValueType, Domain::EnumVectorWrapper>) {
            const EnumVectorWrapper& enums_wrapper{item_value};
            const auto& values{enums_wrapper.get_strings()};
            std::vector<std::string> enum_values;
            enum_values.insert(enum_values.end(), values.begin(), values.end());
            config.set(name, enum_values);
        } else if constexpr (std::is_same_v<ValueType, Domain::FloatOrPercentage>)
        {
            ASSERT(!item_value.is_percentage());
            config.set(name, item_value.float_value());
        } else if constexpr (std::is_same_v<ValueType, std::vector<Domain::FloatOrPercentage>>)
        {
            std::vector<double> values;
            for (const Domain::FloatOrPercentage& value : item_value) {
                ASSERT(!value.is_percentage());
                values.push_back(value.float_value());
            }
            config.set(name, values);
        } else {
            config.set(name, item_value);
        }
    });
}

} // namespace

Config get_parser_config(const ConfigView& config_view)
{
    Config result;
    for (const auto& [key, value] : config_view.values()) {
        copy(key, value, result);
    }
    return result;
}

} // namespace Slic3r::Parser::IO
