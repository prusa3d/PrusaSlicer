#pragma once

#include "Slic3r/Assert.hpp"
#include "Slic3r/Domain/Types.hpp"

namespace Slic3r::Domain {
    class FloatOrPercentage;
}

namespace cereal {
template <class Archive>
void serialize(Archive& archive, Slic3r::Domain::FloatOrPercentage& value);
}

namespace Slic3r::Domain {
class Percentage
{
public:
    auto operator<=> (const Percentage& other) const = default;
    double get_abs_value(double ratio_over) const { return (value / 100.) * ratio_over; }

    double value = 0.;
};

class FloatOrPercentage
{
public:
    FloatOrPercentage() = default;
    FloatOrPercentage(const FloatOrPercentage&) = default;
    FloatOrPercentage(double value) : m_value{value}, m_is_percentage{false} {}
    FloatOrPercentage(Percentage percentage) : m_value{percentage.value}, m_is_percentage{true} {}

    bool is_percentage() const { return m_is_percentage; }
    bool is_zero() const { return m_value == 0.; }
    bool is_positive() const { return m_value > 0; }

    double float_value() const
    {
        ASSERT(!is_percentage());
        return m_value;
    }
    Percentage percentage() const
    {
        ASSERT(is_percentage());
        return Percentage{m_value};
    }

    double get_abs_value(double ratio_over) const
    {
        return (is_percentage() ? percentage().get_abs_value(ratio_over) : m_value);
    }

    bool operator==(const FloatOrPercentage& other) const
    {
        return Domain::fuzzy_compare(m_value, other.m_value)
            && m_is_percentage == other.m_is_percentage;
    }

private:
    double m_value = 0.;
    bool m_is_percentage = false;

    template <class Archive>
    friend void cereal::serialize(Archive& archive, Slic3r::Domain::FloatOrPercentage& value);
};
} // namespace Slic3r::Domain
