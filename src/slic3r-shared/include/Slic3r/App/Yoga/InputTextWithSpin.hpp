#pragma once

#include "Slic3r/App/Yoga/InputTextField.hpp"
#include "Slic3r/Biz/Expr/Parser.hpp"
#include "Slic3r/Biz/Expr/Eval.hpp"

namespace Slic3r::App::Yoga {

class SpinButton;

class InputTextWithSpin : public InputTextField
{
public:
    explicit InputTextWithSpin(
        std::unique_ptr<Validator> validator,
        double step             = 1.,
        double step_fast        = 5.,
        const std::string& name = "InputTextWithSpin"
    );

    struct Callbacks
    {
        /**
         * @brief text_edited is fired only after editing is finished (e.g. Enter/ESC or item lost
         * it's focus) or clicked one of SpinButton
         * This is due to optional validator which is invoked just before this callback
         */
        std::function<void()> text_edited{nullptr};
    };

    // @note those callbacks hide a callbacks from InputText
    Callbacks& callbacks();

    double step();
    double step_fast();
    void set_step(double step);
    void set_step_fast(double step_fast);

    void set_default(double default_value);

    void set_text(const std::string& text);

    void set_override_label(const std::string& override_label);

private:
    void set_last_value();

private:
    void increase_value();
    void decrease_value();
    void text_updated_internal() override;

    SpinButton* m_increase_button{nullptr};
    SpinButton* m_decrease_button{nullptr};

    double m_step{1.};
    double m_step_fast{5.};
    double m_last_value{0.};

    Callbacks m_callbacks;

    Slic3r::Biz::Expr::Parser m_parser;
    Slic3r::Biz::Expr::Eval m_eval;
};

} // namespace Slic3r::App::Yoga
