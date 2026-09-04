#pragma once

#include "Slic3r/Domain/FullConfigFDM.hpp"
#include "Slic3r/Domain/FullConfigSLA.hpp"

#include "Slic3r/Assert.hpp"
#include <any>
#include <map>
#include <string>
#include <variant>
#include <vector>


namespace Slic3r::Biz::Parser::IO {

enum class Type
{
    None,
    Float,
    Floats,
    Int,
    Ints,
    IntOptional,
    FloatOptional,
    StringOptional,
    BoolOptional,
    IntOptionals,
    FloatOptionals,
    StringOptionals,
    BoolOptionals,
    String,
    Strings,
    Percent,
    Percents,
    Point,
    Points,
    Bool,
    Bools,
    Enum,
    Enums
};

struct Scalar
{
    template<typename T>
    requires (!std::is_enum_v<T>)
    explicit Scalar(const T& value);

    template<typename T>
    requires std::is_enum_v<T> explicit Scalar(const T& value, const std::string& serialized_value)
        : m_value{static_cast<int>(value)}, m_type{Type::Enum}, m_serialized_value{serialized_value}
    {}

    static Scalar init_enum(const int value, const std::string& serialized_value);

    bool operator==(const Scalar& rhs) const;

    Type type() const;
    std::string serialize() const;
    bool is_optional() const;
    bool is_nil() const;

    template<typename T>
    requires (!std::is_enum_v<T>)
    T get() const;

    template<typename T>
    requires std::is_enum_v<T>
    T get() const {
        ASSERT(m_type == Type::Enum);
        const int* result{std::any_cast<int>(&m_value)};
        ASSERT(result != nullptr);
        return static_cast<T>(*result);
    }

private:
    std::any m_value;
    Type m_type{Type::None};

    // Only used for enum.
    std::optional<std::string> m_serialized_value;
};

template<typename T>
struct is_pair : std::false_type {};

template<typename T1, typename T2>
struct is_pair<std::pair<T1, T2>> : std::true_type {};

template<typename T>
inline constexpr bool is_pair_v = is_pair<T>::value;

struct Vector
{
    template<typename T>
    requires (!is_pair_v<T>)
    explicit Vector(const std::vector<T>& values);

    template<typename T>
    requires is_pair_v<T> && std::is_enum_v<typename T::first_type>
    Vector(const std::vector<T>& values)
    {
        m_values.clear();
        m_type = Type::Enums;
        for (const auto& pair : values) {
            m_values.push_back(Scalar{pair.first, pair.second});
        }
    };

    Vector() = default;

    bool operator==(const Vector& rhs) const;

    Type type() const;
    std::size_t size() const;
    bool empty() const;
    bool holds_optionals() const;
    bool is_all_nil() const;
    bool is_nil_at(size_t index) const;

    template<typename T>
    requires (!std::is_enum_v<T>)
    std::vector<T> get() const;

    template<typename T>
    requires std::is_enum_v<T>
    std::vector<T> get() const
    {
        std::vector<T> result;
        for (const Scalar& value : m_values) {
            result.push_back(value.get<T>());
        }
        return result;
    }

    Scalar& at(const std::size_t index);
    const Scalar& at(const std::size_t index) const;

private:
    std::vector<Scalar> m_values;
    Type m_type{Type::None};
};

using Value = std::variant<Scalar, Vector>;

bool is_scalar(const Value& value);
bool is_vector(const Value& value);

Type type(const Value& value);

struct Config
{
    const Value* option(const std::string& key) const;

    Value* optptr(const std::string& key);

    template<typename T>
    void set(const std::string& key, const T& value);

    void apply(const Config& config);

private:
    std::map<std::string, Value> m_values;
    std::vector<std::string> keys() const;
};

Config get_parser_config(const Domain::ConfigView& config_view);

} // namespace Slic3r::Parser::IO
