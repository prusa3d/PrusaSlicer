#include "Slic3r/Domain/ConfigValue.hpp"

namespace Slic3r::Domain {

void EnumVectorWrapper::set_strings(const std::vector<std::string>& values) {
    std::vector<int> payload(values.size());

    size_t i = 0;
    bool ok = false;
    for (const std::string& value : values) {
        for (const EnumValueDef& evd : *m_def) {
            if (evd.str_serialized == value) {
                payload[i] = evd.enum_value;
                ok = true;
                break;
            }
        }
        ASSERT(ok);
        ++i;
    }

    m_values = payload;
}

const std::type_info* EnumVectorWrapper::type() const {
    return m_type;
}

const std::vector<int>& EnumVectorWrapper::values() const {
    return m_values;
}

const EnumValueDefs& EnumVectorWrapper::def() const {
    return *m_def;
}

std::vector<std::string_view> EnumVectorWrapper::get_strings() const {
    std::vector<std::string_view> out;
    for (int value : m_values) {
        auto it = std::ranges::find_if(*m_def,
            [value](const Domain::EnumValueDef& evd) { return evd.enum_value == value; });
        ASSERT(it != m_def->end());
        out.push_back(it->str_serialized);
    }
    return out;
}

std::vector<size_t> EnumVectorWrapper::get_indexes() const
{
    std::vector<size_t> indexes;
    indexes.reserve(m_values.size());
    std::transform(m_values.cbegin(), m_values.cend(), std::back_inserter(indexes), [this](int value) {
        EnumValueDefs::const_iterator it = std::ranges::find_if(
            *m_def,
            [value](const Domain::EnumValueDef& evd) { return evd.enum_value == value; }
        );
        ASSERT(it != m_def->end());
        return std::distance(m_def->cbegin(), it);
    });
    return indexes;
}

void EnumVectorWrapper::set_indexes(const std::vector<size_t>& indexes)
{
    ASSERT(indexes.size() == m_values.size());
    for (size_t i = 0; i < indexes.size(); ++i) {
        ASSERT(indexes.at(i) < m_def->size());
        m_values[i] = m_def->at(indexes.at(i)).enum_value;
    }
}

std::vector<int>& EnumVectorWrapper::raw()
{
    return m_values;
}

bool EnumVectorWrapper::operator==(const EnumVectorWrapper&) const = default;

void EnumWrapper::set_string(const std::string& value)
{
    for (const EnumValueDef& evd : *m_def) {
        if (evd.str_serialized == value) {
            m_value = evd.enum_value;
            return;
        }
    }

    // Maybe should be an exception, as serialized value can be user input.
    PANIC("Failed to set enum from string!");
}

void EnumWrapper::set_index(size_t index)
{
    ASSERT(index < m_def->size());

    m_value = m_def->at(index).enum_value;
}

const std::type_info* EnumWrapper::type() const { return m_type; }

int EnumWrapper::value() const { return m_value; }

size_t EnumWrapper::index_of_value(int value) const
{
    EnumValueDefs::const_iterator it = std::find_if(
        m_def->cbegin(),
        m_def->cend(),
        [value](const EnumValueDef& def) { return value == def.enum_value; }
    );

    ASSERT(m_def->cend() != it);

    return std::distance(m_def->cbegin(), it);
}

const EnumValueDefs& EnumWrapper::def() const {
    return *m_def;
};

std::string_view EnumWrapper::get_string() const
{
    for (const EnumValueDef& evd : *m_def) {
        if (evd.enum_value == m_value) {
            return evd.str_serialized;
        }
    }
    PANIC("Failed to get enum as string!");
}

bool EnumWrapper::operator==(const EnumWrapper&) const = default;

ConfigValue::ConfigValue(const char* str): ConfigValue(std::string{str}) {}


namespace {

template <class T>
bool config_value_equal(const T& lhs, const T& rhs)
{
    return lhs == rhs;
}

bool config_value_equal(double lhs, double rhs)
{
    return Domain::fuzzy_compare(lhs, rhs);
}

bool config_value_equal(const Domain::Vec2d& lhs, const Domain::Vec2d& rhs)
{
    return Domain::fuzzy_compare(lhs[0], rhs[0]) && Domain::fuzzy_compare(lhs[1], rhs[1]);
}

template <class T>
bool config_value_equal(const std::vector<T>& lhs, const std::vector<T>& rhs)
{
    return lhs.size() == rhs.size()
        && std::equal(
               lhs.begin(),
               lhs.end(),
               rhs.begin(),
               [](const auto& a, const auto& b) { return config_value_equal(a, b); }
        );
}

} // namespace

bool ConfigValue::operator==(const ConfigValue& rhs) const
{
    // Custom comparison of all doubles using Domain::fuzzy_compare
    return std::visit(
        [](const auto& lhs_value, const auto& rhs_value) -> bool
        {
            using Lhs = std::remove_cvref_t<decltype(lhs_value)>;
            using Rhs = std::remove_cvref_t<decltype(rhs_value)>;

            if constexpr (std::is_same_v<Lhs, Rhs>) {
                return config_value_equal(lhs_value, rhs_value);
            } else {
                return false;
            }
        },
        m_value,
        rhs.m_value
    );
}

}
