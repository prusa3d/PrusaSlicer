#include "Slic3r/App/Yoga/SliderWithInput.hpp"
#include "Slic3r/App/Yoga/Slider.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/InputTextField.hpp"
#include "Slic3r/App/Yoga/Validator.hpp"
#include <fmt/format.h>

namespace Slic3r::App::Yoga {

SliderWithInput::Callbacks& SliderWithInput::callbacks()
{
    return m_callbacks;
}

SliderWithInput::SliderWithInput() : Item()
{
    Create();
}

SliderWithInput::SliderWithInput(const std::string& unit) : Item()
{
    Create();
    if (!unit.empty()) {
        m_unit->set_visible(true);
        set_unit(unit);
    }
}

void SliderWithInput::Create()
{
    set_gap(10.f);
    set_align_items(YGAlignCenter);

    m_input = emplace_back<InputTextField>("SliderInputTextField");
    m_input->set_input_flags(ImGuiInputTextFlags_CharsDecimal);
    m_input->callbacks().text_edited = [this]() {
        const std::string& input_value = m_input->text();

        if (DoubleValidator* double_validator = dynamic_cast<DoubleValidator*>(m_input->validator());
            double_validator && double_validator->precision())
        {
            std::string slider_value = fmt::format("{1:.{0}f}", double_validator->precision().value(), m_slider->value());
            if (input_value == slider_value) {
                return;
            }
        }
        m_slider->set_value(std::stod(input_value));
    };

    // By default we use DefaultValidator for the slider
    std::unique_ptr<DoubleValidator> validator = std::make_unique<DoubleValidator>();
    validator->set_precision(2);
    m_input->set_validator(std::move(validator));

    m_unit = emplace_back<Text>("");
    m_unit->set_visible(false);

    m_slider = emplace_back<Slider>();
    m_slider->set_flex_grow(1);
    m_slider->callbacks().value_changed = [this](double value) {
        if (DoubleValidator* double_validator = dynamic_cast<DoubleValidator*>(m_input->validator());
            double_validator && double_validator->precision())
        {
            m_input->set_text(fmt::format("{1:.{0}f}", double_validator->precision().value(), value));
        } else {
            m_input->set_text(fmt::format("{:.10g}", value));
        }
        if (callbacks().value_changed)
            callbacks().value_changed(value);
    };
}

void SliderWithInput::set_input_width(double width)
{
    m_input->set_width(width);
}

void SliderWithInput::set_input_width_percent(double width_percent)
{
    m_input->set_width_percent(width_percent);
}

double SliderWithInput::value() const
{
    return m_slider->value();
}

void SliderWithInput::set_value(const double value)
{
    m_slider->set_value(value);
}

double SliderWithInput::begin() const
{
    return m_slider->begin();
}

void SliderWithInput::set_begin_value(double begin)
{
    m_slider->set_begin_value(begin);
    if (IntValidator* int_validator = dynamic_cast<IntValidator*>(validator())) {
        int_validator->set_from(static_cast<int>(begin));
    } else {
        dynamic_cast<DoubleValidator*>(validator())->set_from(begin);
    }
}

double SliderWithInput::end() const
{
    return m_slider->end();
}

void SliderWithInput::set_end_value(double end)
{
    m_slider->set_end_value(end);

    // The Validator needs to process the 'from' and 'to' values in the condition: from < to.
    const bool need_to_revert = m_slider->begin() > m_slider->end();

    if (IntValidator* int_validator = dynamic_cast<IntValidator*>(validator())) {
        if (need_to_revert) {
            int_validator->set_to(int_validator->from());
            int_validator->set_from(static_cast<int>(end));
        } else {
            int_validator->set_to(static_cast<int>(end));
        }
    } else if (DoubleValidator* double_validator = dynamic_cast<DoubleValidator*>(validator())) {
        if (need_to_revert) {
            double_validator->set_to(double_validator->from());
            double_validator->set_from(end);
        } else {
            double_validator->set_to(end);
        }
    }
}

double SliderWithInput::step() const
{
    return m_slider->step();
}

void SliderWithInput::set_step(double step)
{
    m_slider->set_step(step);
}

Validator* SliderWithInput::validator() const
{
    return m_input->validator();
}

void SliderWithInput::set_validator(std::unique_ptr<Validator> validator_in)
{
    m_input->set_validator(std::move(validator_in));
    if (DoubleValidator* double_validator = dynamic_cast<DoubleValidator*>(validator())) {
        m_slider->set_begin_value(double_validator->from());
        m_slider->set_end_value(double_validator->to());
    } else if (IntValidator* int_validator = dynamic_cast<IntValidator*>(validator())) {
        m_slider->set_begin_value(int_validator->from());
        m_slider->set_end_value(int_validator->to());
    }
}

void SliderWithInput::set_validator_precision(int precision)
{
    if (DoubleValidator* double_validator = dynamic_cast<DoubleValidator*>(validator())) {
        double_validator->set_precision(precision);
    } else {
        // do nothing
    }
}

void SliderWithInput::set_default(double def_value)
{
    // propagate revert button into m_input
    if (revert_button() && !m_input->revert_button()) {
        m_input->set_revert_button(revert_button());
    }
    m_input->set_default(def_value);
}

void SliderWithInput::validate_default(bool is_valid)
{
    m_input->validate_default(is_valid);
}

void SliderWithInput::reset()
{
    m_input->reset();
}

void SliderWithInput::set_undef_value()
{
    // ToDo! set empty value and don't process the value_changed
}

const std::string& SliderWithInput::unit() const
{
    return m_unit->text();
}

void SliderWithInput::set_unit(const std::string& unit)
{
    m_unit->set_text(unit);
}

} // namespace Slic3r::App::Yoga
