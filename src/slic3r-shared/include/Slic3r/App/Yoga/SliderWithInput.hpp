#pragma once

#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Render/ImguiTypes.hpp"
#include "Slic3r/App/Yoga/RevertableControl.hpp"

#include <string>

namespace Slic3r::App::Yoga {

class Slider;
class InputTextField;
class Validator;
class Text;

/**
 * @brief The SliderWithInput class is a composite control combining an input field and a slider.
 * Use set_direction(YGDirectionLTR) to change the alignment of the internal controls.
 */
class SliderWithInput : public Item, public Yoga::RevertableControl
{
public:
    struct Callbacks
    {
        std::function<void(double value)> value_changed{nullptr};
    };

    explicit SliderWithInput();
    explicit SliderWithInput(const std::string& unit);

    Callbacks& callbacks();

    void set_input_width(double width);
    void set_input_width_percent(double width_percent);

    double value() const;
    void set_value(double value);

    double begin() const;
    void set_begin_value(double begin);

    double end() const;
    void set_end_value(double end);

    double step() const;
    void set_step(double step);

    Validator* validator() const;
    void set_validator(std::unique_ptr<Validator> validator);

    /* @note this function do nothing for the IntValidator */
    void set_validator_precision(int precision);

    void set_default(double def_value);
    void validate_default(bool is_valid) override;
    void reset() override;

    /* @note this function just set control into invalid state, when input is empty*/
    void set_undef_value();

    const std::string& unit() const;
    void set_unit(const std::string& unit);

private:
    void Create();

private:
    Slider* m_slider = nullptr;
    InputTextField* m_input = nullptr;
    Text* m_unit;
    Callbacks m_callbacks;
};

} // namespace Slic3r::App::Yoga
