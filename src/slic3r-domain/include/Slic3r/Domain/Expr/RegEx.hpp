#pragma once

#include <regex>
#include <boost/algorithm/string/replace.hpp>

namespace Slic3r::Domain::Expr {
struct RegEx;
} // namespace Slic3r::Domain::Expr

namespace cereal {

template <class Archive>
void save(Archive& archive, const Slic3r::Domain::Expr::RegEx& value);

template <class Archive>
void load(Archive& archive, Slic3r::Domain::Expr::RegEx& value);
} // namespace cereal

namespace Slic3r::Domain::Expr {

/**
 * @brief Regular expression
 * This is a simple wrapper around std::regex, that remembers the source string and implements
 * comparison operators based upon this string. This is helpful for expression evaluation
 * (the boost variant will have implemented all comparison operators) and error reporting.
 */
struct RegEx
{
    RegEx() = default;
    RegEx(const RegEx&) = default;
    RegEx(RegEx&&) = default;

    explicit RegEx(std::string regex)
    {
        // Remove the escaped slash as it is not needed to escape it in standard RegEx
        boost::replace_all(regex, "\\/", "/");
        m_regex = regex;
        m_source = std::move(regex);
    }

    RegEx& operator=(const RegEx&) = default;
    RegEx& operator=(RegEx&&) = default;

    bool operator==(const RegEx& rhs) const
    { return this->m_source == rhs.m_source; }
    bool operator!=(const RegEx& rhs) const
    { return !(*this == rhs); }
    bool operator<(const RegEx& rhs) const
    { return this->m_source < rhs.m_source; }
    bool operator>(const RegEx& rhs) const
    { return this->m_source > rhs.m_source; }

    bool match(std::string_view s) const
    { return std::regex_match(s.begin(), s.end(), m_regex); }

    const std::string& source() const { return m_source; }

private:
    std::regex m_regex;
    std::string m_source;

    template <class Archive>
    friend void cereal::save(Archive& archive, const Slic3r::Domain::Expr::RegEx& value);

    template <class Archive>
    friend void cereal::load(Archive& archive, Slic3r::Domain::Expr::RegEx& value);
};

}
