#include "Slic3r/Biz/MainThreadDialogProvider.hpp"

#include "Slic3r/Biz/Platform/IMainThreadDispatcher.hpp"

#include <atomic>
#include <chrono>
#include <future>
#include <memory>

namespace Slic3r::Biz {

namespace {

constexpr std::chrono::milliseconds STOP_CHECK_INTERVAL{50};

/**
 * @brief Answers of a yes/no dialog, collected on the main thread and replayed in the job.
 */
struct YesNoAnswer
{
    std::optional<bool> answer;
    std::optional<bool> checked;
};

enum class YesNoCancelAnswer
{
    None,
    Yes,
    No,
    Cancel
};

} // namespace

MainThreadDialogProvider::MainThreadDialogProvider(
    IMessageDialogProvider& provider,
    Platform::IMainThreadDispatcher& dispatcher,
    JThread::StopToken stop_token
) :
    m_provider(provider),
    m_dispatcher(dispatcher),
    m_stop_token(stop_token)
{}

template <typename Result>
std::optional<Result> MainThreadDialogProvider::call_and_wait(
    std::function<Result(IMessageDialogProvider&)> call
)
{
    struct State
    {
        std::promise<Result> promise;
        /// The job doesn't wait anymore, the dialog isn't shown when not shown yet.
        std::atomic<bool> abandoned{false};
    };

    auto state{std::make_shared<State>()};
    std::future<Result> future{state->promise.get_future()};

    const bool dispatched{m_dispatcher.dispatch_on_main_thread(
        [state, call = std::move(call), &provider = m_provider]()
        {
            if (state->abandoned) {
                return;
            }
            try {
                state->promise.set_value(call(provider));
            } catch (...) {
                state->promise.set_exception(std::current_exception());
            }
        }
    )};
    if (!dispatched) {
        return std::nullopt;
    }

    while (future.wait_for(STOP_CHECK_INTERVAL) != std::future_status::ready) {
        if (m_stop_token.stop_requested()) {
            state->abandoned = true;
            return std::nullopt;
        }
    }
    return future.get();
}

void MainThreadDialogProvider::call_later(std::function<void(IMessageDialogProvider&)> call)
{
    m_dispatcher.dispatch_on_main_thread([call = std::move(call), &provider = m_provider]()
                                         { call(provider); });
}

void MainThreadDialogProvider::show_yesno_dialog(
    const std::string& title,
    const std::string& text,
    const YesNoCallback& callback
)
{
    const std::optional<std::optional<bool>> answer{call_and_wait<std::optional<bool>>(
        [title, text](IMessageDialogProvider& provider)
        {
            std::optional<bool> result;
            provider.show_yesno_dialog(title, text, [&result](bool answer) { result = answer; });
            return result;
        }
    )};
    if (answer && answer->has_value() && callback) {
        callback(answer->value());
    }
}

void MainThreadDialogProvider::show_rich_yesno_dialog(
    const std::string& title,
    const std::string& text,
    const std::string& check_text,
    const YesNoCallback& callback,
    const CheckBoxCheckedCallback& checked_callback
)
{
    const std::optional<YesNoAnswer> answer{call_and_wait<YesNoAnswer>(
        [title, text, check_text](IMessageDialogProvider& provider)
        {
            YesNoAnswer result;
            provider.show_rich_yesno_dialog(
                title,
                text,
                check_text,
                [&result](bool answer) { result.answer = answer; },
                [&result](bool checked) { result.checked = checked; }
            );
            return result;
        }
    )};
    if (!answer) {
        return;
    }
    // Same order as the dialog calls them, the checked callback may use the answer.
    if (answer->answer && callback) {
        callback(*answer->answer);
    }
    if (answer->checked && checked_callback) {
        checked_callback(*answer->checked);
    }
}

void MainThreadDialogProvider::show_yesnocancel_dialog(
    const std::string& title,
    const std::string& text,
    const Button& yes,
    const Button& no,
    const Button& cancel
)
{
    const std::optional<YesNoCancelAnswer> answer{call_and_wait<YesNoCancelAnswer>(
        [title, text, yes_label = yes.first, no_label = no.first, cancel_label = cancel.first](
            IMessageDialogProvider& provider
        )
        {
            YesNoCancelAnswer result{YesNoCancelAnswer::None};
            provider.show_yesnocancel_dialog(
                title,
                text,
                {yes_label, [&result]() { result = YesNoCancelAnswer::Yes; }},
                {no_label, [&result]() { result = YesNoCancelAnswer::No; }},
                {cancel_label, [&result]() { result = YesNoCancelAnswer::Cancel; }}
            );
            return result;
        }
    )};
    if (!answer) {
        return;
    }

    const auto invoke{[](const Button& button)
                      {
                          if (button.second) {
                              button.second();
                          }
                      }};
    switch (*answer) {
    case YesNoCancelAnswer::Yes: invoke(yes); break;
    case YesNoCancelAnswer::No: invoke(no); break;
    case YesNoCancelAnswer::Cancel: invoke(cancel); break;
    case YesNoCancelAnswer::None: break;
    }
}

void MainThreadDialogProvider::show_info_dialog(
    const std::string& text,
    const std::string& title,
    bool is_marked
)
{
    call_later([text, title, is_marked](IMessageDialogProvider& provider)
               { provider.show_info_dialog(text, title, is_marked); });
}

void MainThreadDialogProvider::show_warning_dialog(const std::string& text, const std::string& title)
{
    call_later([text, title](IMessageDialogProvider& provider)
               { provider.show_warning_dialog(text, title); });
}

void MainThreadDialogProvider::show_error_dialog(const std::string& text, const std::string& title)
{
    call_later([text, title](IMessageDialogProvider& provider)
               { provider.show_error_dialog(text, title); });
}

std::optional<StepLoadDialogResult> MainThreadDialogProvider::show_load_step_dialog(
    const std::string& filename,
    double linear_precision,
    double angle_precision,
    bool multiple
)
{
    const std::optional<std::optional<StepLoadDialogResult>> result{
        call_and_wait<std::optional<StepLoadDialogResult>>(
            [filename, linear_precision, angle_precision, multiple](IMessageDialogProvider& provider)
            {
                return provider.show_load_step_dialog(
                    filename,
                    linear_precision,
                    angle_precision,
                    multiple
                );
            }
        )
    };
    return result ? *result : std::nullopt;
}

} // namespace Slic3r::Biz
