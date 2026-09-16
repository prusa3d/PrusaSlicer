#include "Slic3r/App/MsgBoxHandler.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"

#include "Slic3r/App/Navigator.hpp"
#include "Slic3r/App/Yoga/ColorPicker.hpp"
#include "Slic3r/App/Yoga/Dialog.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/InputTextField.hpp"
#include "Slic3r/App/Yoga/RootItem.hpp"
#include "Slic3r/App/NumberEntryDialog.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

class ColorPickerModal : public Dialog
{
public:
    ColorPickerModal(
        const std::string& title,
        const Domain::ColorRGB& input_color,
        MsgBoxHandler::FnColorPicked fn_color_picked,
        MsgBoxHandler::FnRejected fn_rejected,
        MsgBoxHandler::DialogButtons dialog_buttons,
        Navigator& navigator
    ) :
        Dialog({title}, "ColorPickerModal"),
        m_fn_rejected(fn_rejected),
        m_navigator(navigator)
    {
        set_closable(dialog_buttons & MsgBoxHandler::CancelButton);
        content_item()->set_modal(true);

        content()->set_orientation(Orientation::Vertical);
        content()->set_gap(5_fpx);

        m_picker = content()->emplace_back<ColorPicker>();
        m_picker->set_color({input_color.r(), input_color.g(), input_color.b()});
        Item* button_row = content()->emplace_back<Item>();
        button_row->set_gap(5_fpx);
        button_row->set_justify_content(YGJustifyFlexEnd);

        if (dialog_buttons & MsgBoxHandler::AcceptButton) {
            LayoutButton* btn_ok       = button_row->emplace_back<LayoutButton>(Biz::_u8L("Ok"));
            btn_ok->callbacks().action = [=, this]
            {
                if (fn_color_picked) {
                    fn_color_picked(m_picker->color_rgb());
                    m_navigator.set_modal_dialog(ModalDialog::None);
                }
            };
        }

        if (dialog_buttons & MsgBoxHandler::CancelButton) {
            LayoutButton* btn_cancel = button_row->emplace_back<LayoutButton>(Biz::_u8L("Cancel"));
            btn_cancel->callbacks().action = [this] { close_action(); };
        }
    }

protected:
    void close_action() override
    {
        if (m_fn_rejected) {
            m_fn_rejected();
        }
        m_navigator.set_modal_dialog(ModalDialog::None);
    }

private:
    MsgBoxHandler::FnRejected m_fn_rejected;

    Navigator& m_navigator;

    ColorPicker* m_picker{nullptr};
};

class InputTextModal : public Dialog
{
public:
    InputTextModal(
        const std::string& title,
        const std::string& text,
        const std::string& input,
        MsgBoxHandler::FnAcceptedText fn_accepted_text,
        MsgBoxHandler::FnCancelled fn_cancelled,
        MsgBoxHandler::DialogButtons dialog_buttons,
        Navigator& navigator,
        bool multiline
    ) :
        Dialog({title}, "InputTextModal"),
        m_fn_cancelled(fn_cancelled),
        m_navigator(navigator)
    {
        set_closable(dialog_buttons & MsgBoxHandler::CancelButton);
        content_item()->set_modal(true);

        content()->set_orientation(Orientation::Vertical);
        content()->set_gap(5_fpx);
        content()->emplace_back<Text>(text);

        InputTextField* input_text = content()->emplace_back<InputTextField>();
        input_text->set_text(input);
        if (multiline) {
            input_text->set_input_flags(ImGuiInputTextFlags_Multiline);
            input_text->set_min_height(40_fpx);
            input_text->set_resizable(true);
        }

        Item* button_row = content()->emplace_back<Item>();
        button_row->set_gap(5_fpx);
        button_row->set_justify_content(YGJustifyFlexEnd);

        if (dialog_buttons & MsgBoxHandler::AcceptButton) {
            LayoutButton* btn_ok       = button_row->emplace_back<LayoutButton>(Biz::_u8L("Ok"));
            btn_ok->callbacks().action = [this, fn_accepted_text, input_text]
            {
                if (fn_accepted_text) {
                    fn_accepted_text(input_text->text());
                    m_navigator.set_modal_dialog(ModalDialog::None);
                }
            };
        }

        if (dialog_buttons & MsgBoxHandler::CancelButton) {
            LayoutButton* btn_cancel = button_row->emplace_back<LayoutButton>(Biz::_u8L("Cancel"));
            btn_cancel->callbacks().action = [this] { close_action(); };
        }
    }

protected:
    void close_action() override
    {
        if (m_fn_cancelled) {
            m_fn_cancelled();
        }
        m_navigator.set_modal_dialog(ModalDialog::None);
    }

private:
    MsgBoxHandler::FnCancelled m_fn_cancelled;

    Navigator& m_navigator;
};

class YesNoCancelDialog : public Dialog
{
public:
    YesNoCancelDialog(
        const std::string& title,
        const std::string& text,
        MsgBoxHandler::FnAccepted fn_accepted,
        MsgBoxHandler::FnRejected fn_rejected,
        MsgBoxHandler::FnCancelled fn_cancelled,
        MsgBoxHandler::DialogButtons dialog_buttons,
        Navigator& navigator
    ) :
        Dialog({title}, "YesNoCancel"),
        m_fn_cancelled(fn_cancelled),
        m_navigator(navigator)
    {
        set_closable(dialog_buttons & MsgBoxHandler::CancelButton);
        content_item()->set_modal(true);

        content()->set_orientation(Orientation::Vertical);
        content()->emplace_back<Text>(text);

        Item* button_row = content()->emplace_back<Item>();
        button_row->set_gap(5_fpx);
        content()->set_gap(5_fpx);
        button_row->set_justify_content(YGJustifyFlexEnd);

        if (dialog_buttons & MsgBoxHandler::AcceptButton) {
            LayoutButton* btn_yes       = button_row->emplace_back<LayoutButton>(Biz::_u8L("Yes"));
            btn_yes->callbacks().action = [this, fn_accepted]
            {
                if (fn_accepted) {
                    fn_accepted();
                    m_navigator.set_modal_dialog(ModalDialog::None);
                }
            };
        }

        if (dialog_buttons & MsgBoxHandler::RejectButton) {
            LayoutButton* btn_no       = button_row->emplace_back<LayoutButton>(Biz::_u8L("No"));
            btn_no->callbacks().action = [this, fn_rejected]
            {
                if (fn_rejected) {
                    fn_rejected();
                    m_navigator.set_modal_dialog(ModalDialog::None);
                }
            };
        }

        if (dialog_buttons & MsgBoxHandler::CancelButton) {
            LayoutButton* btn_cancel = button_row->emplace_back<LayoutButton>(Biz::_u8L("Cancel"));
            btn_cancel->callbacks().action = [this] { close_action(); };
        }
    }

protected:
    void close_action() override
    {
        if (m_fn_cancelled) {
            m_fn_cancelled();
        }
        m_navigator.set_modal_dialog(ModalDialog::None);
    }

private:
    MsgBoxHandler::FnCancelled m_fn_cancelled;

    Navigator& m_navigator;
};

MsgBoxHandler::MsgBoxHandler(Navigator& navigator, Yoga::RootItem& root_item) :
    m_navigator(navigator),
    m_root_item(root_item)
{}

void MsgBoxHandler::pick_color(
    const std::string& title,
    const Domain::ColorRGB& input_color,
    FnColorPicked fn_color_picked,
    FnCancelled fn_cancelled,
    DialogButtons dialog_buttons
)
{
    ASSERT(!m_current_modal, "There is already modal window opened");
    m_current_modal = m_root_item.emplace_back<ColorPickerModal>(
        title,
        input_color,
        fn_color_picked,
        fn_cancelled,
        dialog_buttons,
        m_navigator
    );
}

void MsgBoxHandler::input_text(
    const std::string& title,
    const std::string& text,
    const std::string& input_text,
    FnAcceptedText fn_accepted_text,
    FnCancelled fn_cancelled,
    DialogButtons buttons,
    bool multiline
)
{
    ASSERT(!m_current_modal, "There is already modal window opened");
    m_current_modal = m_root_item.emplace_back<InputTextModal>(
        title,
        text,
        input_text,
        fn_accepted_text,
        fn_cancelled,
        buttons,
        m_navigator,
        multiline
    );
}

void MsgBoxHandler::input_number(
    const std::string& title,
    const std::string& text,
    const std::string& prompt,
    int value,
    int min,
    int max,
    FnAcceptedNumber fn_accepted_number,
    FnCancelled fn_cancelled,
    DialogButtons buttons
)
{
    ASSERT(!m_current_modal, "There is already modal window opened");
    m_current_modal = m_root_item.emplace_back<NumberEntryDialog>(
        title,
        text,
        prompt,
        value,
        min,
        max,
        fn_accepted_number,
        fn_cancelled,
        buttons,
        m_navigator
    );
}

void MsgBoxHandler::yes_no_cancel(
    const std::string& title,
    const std::string& text,
    FnAccepted fn_accepted,
    FnRejected fn_rejected,
    FnCancelled fn_cancelled,
    DialogButtons buttons
)
{
    ASSERT(!m_current_modal, "There is already modal window opened");
    m_current_modal = m_root_item.emplace_back<YesNoCancelDialog>(
        title,
        text,
        fn_accepted,
        fn_rejected,
        fn_cancelled,
        buttons,
        m_navigator
    );
}

void MsgBoxHandler::open_modal()
{
    m_current_modal->attach_to_center();
    m_current_modal->open();
}

void MsgBoxHandler::close_modal()
{
    if (m_current_modal) {
        m_current_modal->close();
        m_root_item.remove_later(m_current_modal);
        m_current_modal = nullptr;
    }
}

} // namespace Slic3r::App
