#pragma once

#include "Slic3r/App/IMsgBoxProvider.hpp"

namespace Slic3r::App::Yoga {
class RootItem;
class Popup;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

class Navigator;

/**
 * @note keeps maximum 1 msg box active at all times
 */
class MsgBoxHandler : public IMsgBoxProvider
{
public:
    explicit MsgBoxHandler(Navigator& navigator, Yoga::RootItem& root_item);

    void pick_color(
        const std::string& title,
        const Domain::ColorRGB& input_color = Domain::ColorRGB{0.f, 0.f, 0.f},
        FnColorPicked fn_color_picked       = nullptr,
        FnCancelled fn_cancelled            = nullptr,
        DialogButtons buttons               = AcceptCancelButtons
    ) override;

    void input_text(
        const std::string& title,
        const std::string& text,
        const std::string& input_text   = std::string{},
        FnAcceptedText fn_accepted_text = nullptr,
        FnCancelled fn_cancelled        = nullptr,
        DialogButtons buttons           = AcceptCancelButtons,
        bool multiline                  = false
    ) override;

    void input_number(
        const std::string& title,
        const std::string& text,
        const std::string& prompt,
        int value,
        int min,
        int max,
        FnAcceptedNumber fn_accepted_number = nullptr,
        FnCancelled fn_cancelled            = nullptr,
        DialogButtons buttons               = AcceptRejectCancelButtons
    ) override;

    void yes_no_cancel(
        const std::string& title,
        const std::string& text,
        FnAccepted fn_accepted   = nullptr,
        FnRejected fn_rejected   = nullptr,
        FnCancelled fn_cancelled = nullptr,
        DialogButtons buttons    = AcceptRejectCancelButtons
    ) override;

    void open_modal();
    void close_modal();

private:
    Yoga::Popup* m_current_modal{nullptr};
    Navigator& m_navigator;
    Yoga::RootItem& m_root_item;
};

} // namespace Slic3r::App
