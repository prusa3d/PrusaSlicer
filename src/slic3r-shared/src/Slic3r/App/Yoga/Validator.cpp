#include "Slic3r/App/Yoga/Validator.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"
#include <Slic3r/Assert.hpp>

#include <fmt/format.h>
#include <cmath>
#include <boost/algorithm/string.hpp>

namespace Slic3r::App::Yoga {

std::string Validator::process(const std::string& input)
{
    return input;
}

std::string Validator::string_without_unit() const
{
    return m_string_without_unit;
}

std::string Validator::string_with_unit() const
{
    if (m_units.empty()) {
        return m_string_without_unit;
    } else {
        return m_string_without_unit
            + " "
            + Biz::_u8(m_detected_unit ? *m_detected_unit : m_units.front());
    }
}

void Validator::set_units(const std::vector<std::string>& units)
{
    if (m_units != units) {
        m_units         = units;
        m_detected_unit = nullptr;
        m_string_without_unit.clear();
    }
}

const std::string* Validator::detected_unit() const
{
    return m_detected_unit;
}

void Validator::split_string(const std::string& input)
{
    for (const std::string& unit : m_units) {
        auto match = boost::algorithm::ifind_last(input, unit);
        if (!match.empty()) {
            m_detected_unit       = &unit;
            m_string_without_unit = std::string(input.begin(), match.begin());
            return;
        }
    }

    m_detected_unit       = nullptr;
    m_string_without_unit = input;
}

std::string IntValidator::process(const std::string& input)
{
    split_string(input);

    double value = 0;
    try {
        value = boost::get<double>(m_eval.eval(m_parser.parse(m_string_without_unit)));
    } catch ([[maybe_unused]] const Biz::Expr::ParseError& error) {
    } catch ([[maybe_unused]] const Biz::Expr::EvalError& error) {
    }

    m_value = std::clamp(static_cast<int>(std::round(value)), m_from, m_to);

    m_string_without_unit = std::to_string(m_value);

    return string_with_unit();
}

int IntValidator::value() const
{
    return m_value;
}

IntValidator::IntValidator(int from, int to) : m_from(from), m_to(to)
{
    ASSERT(from <= to);
}

int IntValidator::from() const
{
    return m_from;
}

void IntValidator::set_from(int from)
{
    m_from = from;
}

int IntValidator::to() const
{
    return m_to;
}

void IntValidator::set_to(int to)
{
    m_to = to;
}

DoubleValidator::DoubleValidator(double from, double to) : m_from(from), m_to(to)
{
    ASSERT(from <= to);
}

double DoubleValidator::from() const
{
    return m_from;
}

void DoubleValidator::set_from(double from)
{
    m_from = from;
}

double DoubleValidator::to() const
{
    return m_to;
}

void DoubleValidator::set_to(double to)
{
    m_to = to;
}

double DoubleValidator::value() const
{
    return m_value;
}

std::string DoubleValidator::process(const std::string& input)
{
    split_string(boost::replace_all_copy(input, ",", "."));

    double value = 0;
    try {
        value = boost::get<double>(m_eval.eval(m_parser.parse(m_string_without_unit)));
    } catch ([[maybe_unused]] const Biz::Expr::ParseError& error) {
    } catch ([[maybe_unused]] const Biz::Expr::EvalError& error) {
    }

    m_value               = std::clamp(value, m_from, m_to);
    m_string_without_unit = m_precision.has_value() ?
        fmt::format("{1:.{0}f}", m_precision.value(), m_value) :
        fmt::format("{:.10g}", m_value);

    return string_with_unit();
}

std::optional<int> DoubleValidator::precision() const
{
    return m_precision;
}

void DoubleValidator::set_precision(int precision)
{
    m_precision = precision;
}

} // namespace Slic3r::App::Yoga
