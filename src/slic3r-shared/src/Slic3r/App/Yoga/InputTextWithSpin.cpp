#include "Slic3r/App/Yoga/InputTextWithSpin.hpp"
#include "Slic3r/App/Yoga/SpinButton.hpp"

#include "Slic3r/App/Yoga/Validator.hpp"
#include <fmt/format.h>

#include <imgui_internal.h>

namespace Slic3r::App::Yoga {

InputTextWithSpin::InputTextWithSpin(
    std::unique_ptr<Validator> validator_in,
    double step,
    double step_fast,
    const std::string& name
) :
    InputTextField(name),
    m_step(step),
    m_step_fast(step_fast)
{
    set_object_name("InputTextWithSpin");
    set_validator(std::move(validator_in));
    set_orientation(Orientation::Horizontal);

    InputTextField::callbacks().text_edited = [this]()
    {
        try {
            set_last_value();
        } catch ([[maybe_unused]] const Biz::Expr::ParseError& error) {
        } catch ([[maybe_unused]] const Biz::Expr::EvalError& error) {
        }

        if (m_callbacks.text_edited) {
            m_callbacks.text_edited();
        }
    };

    set_padding(0);
    Item* spins = emplace_back<Item>();
    spins->set_gap(2);
    spins->set_orientation(Orientation::Vertical);
    spins->set_justify_content(YGJustifyCenter);
    spins->set_padding(Paddings(0, 0, 4, 0));
    spins->set_flex_shrink(0.f);

    m_increase_button                     = spins->emplace_back<SpinButton>(ImGuiDir_Up);
    m_increase_button->callbacks().action = [this]() -> void { increase_value(); };
    m_increase_button->set_width(10);
    m_increase_button->set_height(10);

    m_decrease_button                     = spins->emplace_back<SpinButton>(ImGuiDir_Down);
    m_decrease_button->callbacks().action = [this]() -> void { decrease_value(); };
    m_decrease_button->set_width(10);
    m_decrease_button->set_height(10);
}

InputTextWithSpin::Callbacks& InputTextWithSpin::callbacks()
{
    return m_callbacks;
}

double InputTextWithSpin::step()
{
    return m_step;
}

double InputTextWithSpin::step_fast()
{
    return m_step_fast;
}

void InputTextWithSpin::set_step(double step)
{
    m_step = step;
}

void InputTextWithSpin::set_step_fast(double step_fast)
{
    m_step_fast = step_fast;
}

void InputTextWithSpin::set_default(double default_value)
{
    InputTextField::set_default(default_value);
}

void InputTextWithSpin::set_text(const std::string& text)
{
    InputTextField::set_text(text);
    set_last_value();
}

void InputTextWithSpin::set_override_label(const std::string& override_label)
{
    input_text()->set_override_label(override_label);
}

void InputTextWithSpin::increase_value()
{
    m_last_value += GImGui->IO.KeyCtrl ? m_step_fast : m_step;
    set_text(fmt::format("{:.10g}", m_last_value));
    if (m_callbacks.text_edited) {
        m_callbacks.text_edited();
    }
}

void InputTextWithSpin::decrease_value()
{
    m_last_value -= GImGui->IO.KeyCtrl ? m_step_fast : m_step;
    set_text(fmt::format("{:.10g}", m_last_value));
    if (m_callbacks.text_edited) {
        m_callbacks.text_edited();
    }
}

void InputTextWithSpin::text_updated_internal()
{
    try {
        m_last_value =
            boost::get<double>(m_eval.eval(m_parser.parse(validator()->string_without_unit())));
    } catch ([[maybe_unused]] const Biz::Expr::ParseError& error) {
    } catch ([[maybe_unused]] const Biz::Expr::EvalError& error) {
    }
}

void InputTextWithSpin::set_last_value()
{
    m_last_value =
        boost::get<double>(m_eval.eval(m_parser.parse(validator()->string_without_unit())));
}

} // namespace Slic3r::App::Yoga
