#pragma once
#include "Slic3r/App/Yoga/SliderWithInput.hpp"
#include "Slic3r/App/Yoga/InputTextWithSpin.hpp"
#include "Slic3r/App/Yoga/Validator.hpp" // DoubleValidator

namespace Slic3r::App {

constexpr double MM_TO_INCH = 25.4;
constexpr double INCH_TO_MM = 1. / MM_TO_INCH;
constexpr double RAD_TO_DEG = 180. / M_PI;
constexpr double DEG_TO_RAD = 1. / RAD_TO_DEG;

inline void set_spin_limits(
    Yoga::InputTextWithSpin* spin,
    double from,
    double to,
    double step,
    double step_fast)
{
    Yoga::DoubleValidator* validator = dynamic_cast<Yoga::DoubleValidator*>(spin->validator());
    validator->set_from(from);
    validator->set_to(to);
    spin->set_step(step);
    spin->set_step_fast(step_fast);
}

inline void set_limit_step(Yoga::SliderWithInput* slider, double max_val, double step)
{
    slider->set_begin_value(-max_val);
    slider->set_end_value(max_val);
    slider->set_step(step);
}

} // namespace Slic3r::App::Yoga
