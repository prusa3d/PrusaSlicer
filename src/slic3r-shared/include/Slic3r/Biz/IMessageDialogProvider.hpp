#pragma once

#include <string>
#include <functional>
#include <optional>

namespace Slic3r::Biz {

struct StepLoadDialogResult {
    bool    do_not_show_again = false;
    double  linear_precision  = 0.1;
    double  angle_precision   = 5.0;
    bool    apply_to_all      = false;
};

class IMessageDialogProvider
{
public:
    using YesNoCallback           = std::function<void(bool answer)>;
    using CheckBoxCheckedCallback = std::function<void(bool checked)>;
    using Button                  = std::pair<std::string, std::function<void()>>;

    IMessageDialogProvider()          = default;
    virtual ~IMessageDialogProvider() = default;

    virtual void show_yesno_dialog(
        const std::string& title,
        const std::string& text,
        const YesNoCallback& callback
    ) = 0;
    virtual void show_rich_yesno_dialog(
        const std::string& title,
        const std::string& text,
        const std::string& check_text,
        const YesNoCallback& callback,
        const CheckBoxCheckedCallback& checked_callback
    ) = 0;
    virtual void show_yesnocancel_dialog(
        const std::string& title,
        const std::string& text,
        const Button& yes,
        const Button& no,
        const Button& cancel
    ) = 0;
    virtual void
    show_info_dialog(const std::string& text, const std::string& title = std::string(), bool is_marked = false) = 0;
    virtual void
    show_warning_dialog(const std::string& text, const std::string& title = std::string()) = 0;
    virtual void
    show_error_dialog(const std::string& text, const std::string& title = std::string()) = 0;

    
    virtual std::optional<StepLoadDialogResult> show_load_step_dialog(
        const std::string& filename,
        double linear_precision,
        double angle_precision,
        bool multiple
    ) = 0;
};

} // namespace Slic3r::Biz
