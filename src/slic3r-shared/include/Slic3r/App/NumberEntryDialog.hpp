#pragma once

#include "Slic3r/App/Yoga/Dialog.hpp"
//#include "Slic3r/App/IConfigNavigable.hpp"

#include <string>

namespace Slic3r::App {

namespace Yoga {
class Text;
class InputTextWithSpin;
class LayouButton;
class IntValidator;
} // namespace Yoga

class Navigator;

class NumberEntryDialog : public Yoga::Dialog//, public IConfigNavigable
{
public:
    explicit NumberEntryDialog(Navigator& navigator);

    void get_user_number_and_process(
        const std::string& message,
        const std::string& prompt,
        const std::string& title,
        int value,
        int min,
        int max,
        std::function<void(int)> on_process
    );

protected:
    void close_action() override;

private:
    Navigator& m_navigator;

    Yoga::LayoutButton* m_title_button{nullptr};
    Yoga::Text* m_message{nullptr};
    Yoga::Text* m_prompt{nullptr};
    Yoga::InputTextWithSpin* m_input{nullptr};
    Yoga::IntValidator* m_input_validator{ nullptr };

    std::function<void(int)> m_on_process{nullptr};
};
} // namespace Slic3r::App
