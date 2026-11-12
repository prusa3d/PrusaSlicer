#include "Slic3r/App/NumberEntryDialog.hpp"

#include "Slic3r/App/Navigator.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Validator.hpp"
#include "Slic3r/App/Yoga/InputTextWithSpin.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"
#include <fmt/format.h>

namespace Slic3r::App {

NumberEntryDialog::NumberEntryDialog(
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
) :
    Yoga::Dialog("NumberEntryDialog"),
    m_navigator(navigator),
    m_fn_accepted_number(fn_accepted_number),
    m_fn_cancelled(fn_cancelled)
{
    using namespace Yoga;

    set_closable(true);
    content_item()->set_modal(true);

    append_tab(title);

    content()->set_orientation(Orientation::Vertical);
    if (!text.empty()) {
        content()->emplace_back<Text>(text);
    }

    constexpr Unit row_padding = 5_fpx;
    constexpr Unit row_gap     = 10_fpx;
    const Paddings button_padding(15_fpx, 5_fpx);

    Item* input_row = content()->emplace_back<Item>();
    input_row->set_orientation(Orientation::Horizontal);
    input_row->set_padding(row_padding);
    input_row->set_gap(row_gap);
    input_row->set_align_items(YGAlignCenter);
    input_row->set_flex_grow(1.f);

    if (!prompt.empty()) {
        input_row->emplace_back<Text>(prompt);
    }
    m_input = input_row->emplace_back<InputTextWithSpin>(std::make_unique<IntValidator>());
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
        if (m_fn_accepted_number) {
            m_fn_accepted_number(m_input_validator->value());
        }
        close_action();
    };

    LayoutButton* cancel_btn = buttons_row->emplace_back<LayoutButton>(Biz::_u8L("Cancel"));
    cancel_btn->set_content_padding(button_padding);
    cancel_btn->callbacks().action = [this]() { close_action(); };

    m_input_validator->set_from(min);
    m_input_validator->set_to(max);

    m_input->set_text(fmt::format("{}", value));
}

void NumberEntryDialog::close_action()
{
    if (m_fn_cancelled) {
        m_fn_cancelled();
    }
    m_navigator.set_modal_dialog(ModalDialog::None);
}

} // namespace Slic3r::App
