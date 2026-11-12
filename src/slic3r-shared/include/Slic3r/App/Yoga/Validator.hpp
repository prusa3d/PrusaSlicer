#pragma once

#include "Slic3r/Biz/Expr/Parser.hpp"
#include "Slic3r/Biz/Expr/Eval.hpp"

#include <limits>
#include <string>

namespace Slic3r::App::Yoga {

class Validator
{
public:
    virtual ~Validator() = default;

    virtual std::string process(const std::string& input);

    std::string string_without_unit() const;
    std::string string_with_unit() const;

    void set_units(const std::vector<std::string>& units);
    const std::string* detected_unit() const;

protected:
    void split_string(const std::string& input);

protected:
    std::vector<std::string> m_units;
    std::string m_string_without_unit;
    const std::string* m_detected_unit{};
};

class IntValidator : public Validator
{
public:
    IntValidator(
        int from = std::numeric_limits<int>::min(),
        int to   = std::numeric_limits<int>::max()
    );

    std::string process(const std::string& input) override;

    int from() const;
    void set_from(int from);

    int to() const;
    void set_to(int to);

    int value() const;

private:
    Slic3r::Biz::Expr::Parser m_parser;
    Slic3r::Biz::Expr::Eval m_eval;

    int m_value = 0; // holds parsed value
    int m_from  = 0;
    int m_to    = 0;
};

class DoubleValidator : public Validator
{
public:
    DoubleValidator(
        double from = std::numeric_limits<double>::lowest(),
        double to   = std::numeric_limits<double>::max()
    );

    std::string process(const std::string& input) override;
    double from() const;
    void set_from(double from);

    double to() const;
    void set_to(double to);

    double value() const;

    std::optional<int> precision() const;
    void set_precision(int precision);

private:
    Slic3r::Biz::Expr::Parser m_parser;
    Slic3r::Biz::Expr::Eval m_eval;

    double m_value = 0; // holds parsed value
    double m_from  = 0;
    double m_to    = 0;
    std::optional<int> m_precision;
};

} // namespace Slic3r::App::Yoga
