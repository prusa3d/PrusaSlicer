#pragma once

#include "Slic3r/App/Yoga/Dialog.hpp"
#include "Slic3r/App/IMsgBoxProvider.hpp"

#include <string>

namespace Slic3r::App {

namespace Yoga {
class Text;
class InputTextWithSpin;
class LayouButton;
class IntValidator;
} // namespace Yoga

class Navigator;

class NumberEntryDialog : public Yoga::Dialog
{
public:
    explicit NumberEntryDialog(
        const std::string& title,
        const std::string& text,
        const std::string& prompt,
        int value,
        int min,
        int max,
        IMsgBoxProvider::FnAcceptedNumber fn_accepted_number,
        IMsgBoxProvider::FnCancelled fn_cancelled,
        IMsgBoxProvider::DialogButtons buttons,
        Navigator& navigator
    );

protected:
    void close_action() override;

private:
    Navigator& m_navigator;
    IMsgBoxProvider::FnAcceptedNumber m_fn_accepted_number;
    IMsgBoxProvider::FnCancelled m_fn_cancelled;

    Yoga::InputTextWithSpin* m_input{nullptr};
    Yoga::IntValidator* m_input_validator{nullptr};
};
} // namespace Slic3r::App
