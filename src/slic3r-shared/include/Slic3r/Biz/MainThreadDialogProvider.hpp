#pragma once

#include "Slic3r/Biz/IMessageDialogProvider.hpp"

#include <jthread/JThread.hpp>

#include <functional>
#include <optional>

namespace Slic3r::Biz::Platform {
class IMainThreadDispatcher;
} // namespace Slic3r::Biz::Platform

namespace Slic3r::Biz {

/**
 * @brief Shows dialogs of the wrapped provider on the main thread, for code which runs in a job.
 *
 * A call waits until the dialog is closed, and the callbacks are invoked in the calling thread.
 * When the job is asked to stop meanwhile, the call gives up as if the dialog was dismissed,
 * so the main thread waiting for the job can't deadlock.
 *
 * @note The wrapped provider has to be blocking: its dialogs return after they are closed and
 * their callbacks are invoked before the return.
 */
class MainThreadDialogProvider : public IMessageDialogProvider
{
public:
    MainThreadDialogProvider(
        IMessageDialogProvider& provider,
        Platform::IMainThreadDispatcher& dispatcher,
        JThread::StopToken stop_token
    );

    void show_yesno_dialog(
        const std::string& title,
        const std::string& text,
        const YesNoCallback& callback
    ) override;
    void show_rich_yesno_dialog(
        const std::string& title,
        const std::string& text,
        const std::string& check_text,
        const YesNoCallback& callback,
        const CheckBoxCheckedCallback& checked_callback
    ) override;
    void show_yesnocancel_dialog(
        const std::string& title,
        const std::string& text,
        const Button& yes,
        const Button& no,
        const Button& cancel
    ) override;
    void show_info_dialog(const std::string& text, const std::string& title, bool is_marked)
        override;
    void show_warning_dialog(const std::string& text, const std::string& title) override;
    void show_error_dialog(const std::string& text, const std::string& title) override;
    std::optional<StepLoadDialogResult> show_load_step_dialog(
        const std::string& filename,
        double linear_precision,
        double angle_precision,
        bool multiple
    ) override;

private:
    /**
     * @return std::nullopt when the call couldn't be dispatched or the job was asked to stop.
     */
    template <typename Result>
    std::optional<Result> call_and_wait(std::function<Result(IMessageDialogProvider&)> call);

    /**
     * @brief Dispatches a dialog which returns nothing without waiting for it.
     */
    void call_later(std::function<void(IMessageDialogProvider&)> call);

private:
    IMessageDialogProvider& m_provider;
    Platform::IMainThreadDispatcher& m_dispatcher;
    JThread::StopToken m_stop_token;
};

} // namespace Slic3r::Biz
