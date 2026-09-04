#include "Slic3r/App/NumberEntryDialog.hpp"
#include "Slic3r/App/Navigator.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Validator.hpp"
#include "Slic3r/App/Yoga/InputTextWithSpin.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"
#include <fmt/format.h>

namespace Slic3r::App {

NumberEntryDialog::NumberEntryDialog(Navigator& navigator) :
    Yoga::Dialog("NumberEntryDialog"),
    m_navigator(navigator)
{
    using namespace Yoga;

    this->set_closable(true);
    content_item()->set_modal(true);

    m_title_button = append_tab("Some Title");

    content()->set_orientation(Orientation::Vertical);
    m_message = content()->emplace_back<Text>("msg");

    const float row_padding = 5.f;
    const float row_gap = 10.f;
    const Paddings button_padding(15.f, 5.f);

    Item* input_row = content()->emplace_back<Item>();
    input_row->set_orientation(Orientation::Horizontal);
    input_row->set_padding(row_padding);
    input_row->set_gap(row_gap);
    input_row->set_align_items(YGAlignCenter);
    input_row->set_flex_grow(1.f);

    m_prompt = input_row->emplace_back<Text>("prompt");
    m_input  = input_row->emplace_back<InputTextWithSpin>(std::make_unique<IntValidator>());
    m_input->set_flex_grow(1.f);
    m_input_validator = dynamic_cast<IntValidator*>(m_input->validator());
    ASSERT(m_input_validator);

    Item* buttons_row = content()->emplace_back<Item>();
    buttons_row->set_orientation(Orientation::Horizontal);
    buttons_row->set_flex_grow(1.f);
    buttons_row->set_padding(row_padding);
    buttons_row->set_gap(row_gap);
    buttons_row->set_justify_content(YGJustifyCenter);

    LayoutButton* ok_btn = buttons_row->emplace_back<LayoutButton>(Biz::_u8L("OK"));
    ok_btn->set_content_padding(button_padding);
    ok_btn->callbacks().action = [this]()
    {
        if (m_on_process) {
            m_on_process(m_input_validator->value());
        }
        close_action();
    };
    
    LayoutButton* cancel_btn = buttons_row->emplace_back<LayoutButton>(Biz::_u8L("Cancel"));
    cancel_btn->set_content_padding(button_padding);
    cancel_btn->callbacks().action = [this]()
    { close_action(); };
}

void NumberEntryDialog::get_user_number_and_process(
    const std::string& message,
    const std::string& prompt,
    const std::string& title,
    int value,
    int min,
    int max,
    std::function<void(int)> on_process
)
{
    m_title_button->set_label(title);
    m_prompt->set_text(prompt);
    m_message->set_text(message);

    m_input_validator->set_from(min);
    m_input_validator->set_to(max);

    m_input->set_text(fmt::format("{}", value));

    m_on_process = on_process;
}

void NumberEntryDialog::close_action()
{
    m_on_process = nullptr;
    m_navigator.set_modal_dialog(ModalDialog::None);
}

} // namespace Slic3r::App
