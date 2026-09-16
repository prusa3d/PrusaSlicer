#pragma once

#include "Slic3r/Domain/Color.hpp"

#include <functional>

namespace Slic3r::App {

class IMsgBoxProvider
{
public:
    using FnColorPicked    = std::function<void(const Domain::ColorRGB& color)>;
    using FnAcceptedText   = std::function<void(const std::string& text)>;
    using FnAcceptedNumber = std::function<void(int number)>;
    using FnAccepted       = std::function<void()>;
    using FnRejected       = std::function<void()>;
    using FnCancelled      = std::function<void()>;

    enum DialogButtons
    {
        AcceptButton              = 0b1,
        CancelButton              = 0b10,
        RejectButton              = 0b100,
        AcceptCancelButtons       = AcceptButton | CancelButton,
        AcceptRejectCancelButtons = AcceptButton | RejectButton | CancelButton
    };

    virtual void pick_color(
        const std::string& title,
        const Domain::ColorRGB& input_color = Domain::ColorRGB{0.f, 0.f, 0.f},
        FnColorPicked fn_color_picked       = nullptr,
        FnCancelled fn_cancelled            = nullptr,
        DialogButtons buttons               = AcceptCancelButtons
    ) = 0;

    virtual void input_text(
        const std::string& title,
        const std::string& text,
        const std::string& input_text   = std::string{},
        FnAcceptedText fn_accepted_text = nullptr,
        FnCancelled fn_cancelled        = nullptr,
        DialogButtons buttons           = AcceptCancelButtons,
        bool multiline                  = false
    ) = 0;

    virtual void yes_no_cancel(
        const std::string& title,
        const std::string& text,
        FnAccepted fn_accepted   = nullptr,
        FnRejected fn_rejected   = nullptr,
        FnCancelled fn_cancelled = nullptr,
        DialogButtons buttons    = AcceptRejectCancelButtons
    ) = 0;

    virtual void input_number(
        const std::string& title,
        const std::string& text,
        const std::string& prompt,
        int value,
        int min,
        int max,
        FnAcceptedNumber fn_accepted_number = nullptr,
        FnCancelled fn_cancelled            = nullptr,
        DialogButtons buttons               = AcceptCancelButtons
    ) = 0;
};

} // namespace Slic3r::App
