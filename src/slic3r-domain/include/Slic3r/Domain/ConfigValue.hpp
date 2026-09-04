#pragma once

#include "Slic3r/Domain/Percentage.hpp"
#include "Slic3r/Domain/TemplateUtils.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Assert.hpp"
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <algorithm>

namespace Slic3r::Domain {
    struct ConfigBox;
}

namespace cereal {
    template<class Archive> void serialize(Archive&, Slic3r::Domain::ConfigBox&);
    class BinaryInputArchive;
    class BinaryOutputArchive;
}

namespace Slic3r::Domain {

namespace Impl {
template <typename T>
consteval bool is_enum_vector() {
    return false;
}

template <typename T>
requires is_std_vector_v<T>
consteval bool is_enum_vector() {
    return std::is_enum_v<typename T::value_type>;
}
}

struct EnumValueDef
{
    int enum_value;
    std::string str_serialized;
    std::string str_ui;
    bool operator<(const EnumValueDef& other) const { return enum_value < other.enum_value; }
};

using EnumValueDefs = std::vector<EnumValueDef>;
using EnumValueDefsPtr = std::unique_ptr<EnumValueDefs>;

inline const EnumValueDefs* check_enum_def(const EnumValueDefs* def) {
    ASSERT(!ASSERT_VAL(def)->empty());
    ASSERT(std::is_sorted(def->begin(), def->end()));
    ASSERT(
        std::adjacent_find(
            def->begin(), def->end(), // check for duplicates
            [](const auto& a, const auto& b) { return a.enum_value == b.enum_value; }
        ) == def->end()
    );
    return def;
}

struct EnumVectorWrapper
{
    template<typename T>
    EnumVectorWrapper(const T& values, const EnumValueDefs* def)
        : m_values{to_ints(values)}
        , m_type{&typeid(typename T::value_type)}
        , m_def{check_enum_def(def)}
    {}

    EnumVectorWrapper(std::vector<int> values, const std::type_info* type, const EnumValueDefs& def)
        : m_values{values}, m_type{type}, m_def{&def}
    {}

    template<typename T>
    T get() const {
        ASSERT(typeid(typename T::value_type) == *m_type);
        T result;
        for (const int value : m_values) {
            result.push_back(static_cast<typename T::value_type>(value));
        }
        return result;
    }

    template<typename T>
    void set(const T& values) {
        ASSERT(typeid(typename T::value_type) == *m_type);
        m_values.clear();
        for (const auto& value : values) {
            m_values.push_back(static_cast<int>(value));
        }
    }
    void set_strings(const std::vector<std::string>& values);
    std::vector<std::string_view> get_strings() const;

    std::vector<size_t> get_indexes() const;
    void set_indexes(const std::vector<size_t>& indexes);

    std::vector<int>& raw();

    const std::type_info* type() const;
    const std::vector<int>& values() const;
    const EnumValueDefs& def() const;


    bool operator==(const EnumVectorWrapper&) const;

private:
    std::vector<int> m_values{};
    const std::type_info* m_type{nullptr};
    const EnumValueDefs* m_def{nullptr};

    template <typename T>
    static std::vector<int> to_ints(const T& values) {
        std::vector<int> result;

        std::ranges::transform(values, std::back_inserter(result), [](const auto& value){
            return static_cast<int>(value);
        });
        return result;
    }
};

struct EnumWrapper
{
    template<typename T>
    explicit EnumWrapper(const T& value, const EnumValueDefs* def)
        : m_value{static_cast<int>(value)}, m_type{&typeid(T)}, m_def{check_enum_def(def)}
    {}

    EnumWrapper(int value, const std::type_info* type, const EnumValueDefs& def)
        : m_value{value}, m_type{type}, m_def{&def}
    {}

    template<typename T>
    T get() const
    {
        ASSERT(typeid(T) == *m_type);
        return static_cast<T>(m_value);
    }

    template<typename T>
    void set(const T& value)
    {
        ASSERT(typeid(T) == *m_type);
        m_value = static_cast<int>(value);
    }

    void set_string(const std::string& value);
    void set_index(size_t index);

    const std::type_info* type() const;

    int value() const;

    size_t index_of_value(int value) const;

    const EnumValueDefs& def() const;

    std::string_view get_string() const;

    bool operator==(const EnumWrapper&) const;

private:
    int m_value{};
    const std::type_info* m_type{nullptr};
    const EnumValueDefs* m_def{nullptr};
};

struct ConfigValue {
    ConfigValue() = default;

    explicit ConfigValue(const char* str);

    template <typename T>
    explicit ConfigValue(const T& value): m_value{value} {}

    template <typename T>
    requires (!std::is_enum_v<T> && !Impl::is_enum_vector<T>())
    T get() const {
        return std::get<T>(m_value);
    }

    template <typename T>
    requires std::is_enum_v<T>
    T get() const {
        return std::get<EnumWrapper>(m_value).get<T>();
    }

    template<typename T>
    requires (Impl::is_enum_vector<T>())
    T get() const
    {
        return std::get<EnumVectorWrapper>(m_value).get<T>();
    }

    template <typename T>
    requires (!std::is_enum_v<T> && !Impl::is_enum_vector<T>())
    void set(const T& value) {
        bool value_set = false;
        if constexpr (std::is_same_v<T, std::string>) {
            if (holds_alternative<EnumWrapper>()) {
                std::get<EnumWrapper>(m_value).set_string(value);
                value_set = true;
            }
        } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
            if (holds_alternative<EnumVectorWrapper>()) {
                std::get<EnumVectorWrapper>(m_value).set_strings(value);
                value_set = true;
            }
        }
        if (!value_set) {
            ASSERT(std::holds_alternative<T>(m_value));
            m_value = value;
        }
    }

    void set(const ConfigValue& value) {
        *this = value;
    }

    void set(const char* value) {
        ASSERT(std::holds_alternative<std::string>(m_value));
        m_value = std::string{value};
    }

    template <typename T>
    requires std::is_enum_v<T>
    void set(const T& value) {
        ASSERT(std::holds_alternative<EnumWrapper>(m_value));
        std::get<EnumWrapper>(m_value).set<T>(value);
    }

    template<typename T>
    requires (Impl::is_enum_vector<T>())
    void set(const T& values)
    {
        ASSERT(std::holds_alternative<EnumVectorWrapper>(m_value));
        std::get<EnumVectorWrapper>(m_value).set(values);
    }

    template <typename Visitor>
    auto visit(Visitor&& visitor) const {
        return std::visit(std::forward<Visitor>(visitor), m_value);
    }

    template <typename Visitor>
    auto visit(Visitor&& visitor) {
        return std::visit(std::forward<Visitor>(visitor), m_value);
    }

    template<typename T>
    bool holds_alternative() const {
        return std::holds_alternative<T>(m_value);
    }

    bool operator==(const ConfigValue& rhs) const;

private:
    std::variant<
        EnumWrapper,
        bool,
        int,
        std::optional<int>,
        double,
        std::string,
        Domain::Vec2d,
        FloatOrPercentage,
        Percentage,
        EnumVectorWrapper,
        std::vector<bool>,
        std::vector<int>,
        std::vector<std::optional<int>>,
        std::vector<double>,
        std::vector<std::string>,
        std::vector<Domain::Vec2d>,
        std::vector<FloatOrPercentage>,
        std::vector<Percentage>
    > m_value{bool{}};
};
}
