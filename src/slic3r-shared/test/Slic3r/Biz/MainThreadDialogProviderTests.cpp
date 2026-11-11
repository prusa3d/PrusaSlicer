#include <catch2/catch_test_macros.hpp>

#include "Slic3r/App/Platform/StdMainThreadDispatcher.hpp"
#include "Slic3r/Biz/MainThreadDialogProvider.hpp"
#include "Slic3r/TestUtils/DispatchUtils.hpp"

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using Slic3r::Biz::IMessageDialogProvider;
using Slic3r::Biz::MainThreadDialogProvider;
using Slic3r::Biz::StepLoadDialogResult;

namespace {

/**
 * @brief Answers the dialogs with prepared values and records the thread which showed them.
 */
struct FakeDialogProvider : IMessageDialogProvider
{
    bool yes_no_answer{true};
    bool checked{false};
    /// The checkbox callback is called also when the checkbox stays unchecked.
    bool report_unchecked{false};
    std::optional<StepLoadDialogResult> step_result;

    std::vector<std::string> shown;
    std::vector<std::thread::id> shown_on;

    void record(const std::string& dialog)
    {
        shown.push_back(dialog);
        shown_on.push_back(std::this_thread::get_id());
    }

    void show_yesno_dialog(const std::string&, const std::string&, const YesNoCallback& callback) override
    {
        record("yesno");
        callback(yes_no_answer);
    }

    void show_rich_yesno_dialog(
        const std::string&,
        const std::string&,
        const std::string&,
        const YesNoCallback& callback,
        const CheckBoxCheckedCallback& checked_callback
    ) override
    {
        record("rich_yesno");
        callback(yes_no_answer);
        if (checked || report_unchecked) {
            checked_callback(checked);
        }
    }

    void show_yesnocancel_dialog(const std::string&, const std::string&, const Button&, const Button& no, const Button&)
        override
    {
        record("yesnocancel");
        no.second();
    }

    void show_info_dialog(const std::string&, const std::string&, bool) override { record("info"); }
    void show_warning_dialog(const std::string&, const std::string&) override { record("warning"); }
    void show_error_dialog(const std::string&, const std::string&) override { record("error"); }

    std::optional<StepLoadDialogResult> show_load_step_dialog(const std::string&, double, double, bool) override
    {
        record("step");
        return step_result;
    }
};

struct MainThreadDialogProviderFixture
{
    Slic3r::App::Platform::StdMainThreadDispatcher dispatcher;
    FakeDialogProvider fake;
    Slic3r::Biz::JThread::StopSource stop_source{std::make_unique<std::atomic<bool>>(false)};
    MainThreadDialogProvider provider{fake, dispatcher, Slic3r::Biz::JThread::StopToken{stop_source}};

    /**
     * @brief Runs the function in a worker thread and dispatches the main thread queue meanwhile.
     */
    template <typename Function>
    void run_in_worker(Function function)
    {
        std::atomic<bool> done{false};
        std::thread worker{[&]() {
            function();
            done = true;
        }};
        ::Tests::dispatch_until(dispatcher, [&done]() { return done.load(); });
        worker.join();
    }
};

} // namespace

TEST_CASE_METHOD(MainThreadDialogProviderFixture, "MainThreadDialogProvider shows a yes/no dialog on the main thread", "[MainThreadDialogProvider]")
{
    std::optional<bool> answer;
    std::thread::id callback_thread;
    std::thread::id worker_thread;

    run_in_worker([&]() {
        worker_thread = std::this_thread::get_id();
        provider.show_yesno_dialog("title", "text", [&](bool value) {
            answer          = value;
            callback_thread = std::this_thread::get_id();
        });
    });

    REQUIRE(fake.shown == std::vector<std::string>{"yesno"});
    CHECK(fake.shown_on.front() == std::this_thread::get_id());
    CHECK(answer == true);
    CHECK(callback_thread == worker_thread);
}

TEST_CASE_METHOD(MainThreadDialogProviderFixture, "MainThreadDialogProvider calls the rich yes/no callbacks in the dialog order", "[MainThreadDialogProvider]")
{
    fake.yes_no_answer = false;
    fake.checked       = true;
    std::vector<std::string> calls;

    run_in_worker([&]() {
        provider.show_rich_yesno_dialog(
            "title",
            "text",
            "check",
            [&](bool value) { calls.push_back(value ? "yes" : "no"); },
            [&](bool checked) { calls.push_back(checked ? "checked" : "unchecked"); }
        );
    });

    CHECK(calls == std::vector<std::string>{"no", "checked"});
}

TEST_CASE_METHOD(MainThreadDialogProviderFixture, "MainThreadDialogProvider passes an unchecked checkbox", "[MainThreadDialogProvider]")
{
    fake.report_unchecked = true;
    std::vector<std::string> calls;

    run_in_worker([&]() {
        provider.show_rich_yesno_dialog(
            "title",
            "text",
            "check",
            [&](bool value) { calls.push_back(value ? "yes" : "no"); },
            [&](bool checked) { calls.push_back(checked ? "checked" : "unchecked"); }
        );
    });

    CHECK(calls == std::vector<std::string>{"yes", "unchecked"});
}

TEST_CASE_METHOD(MainThreadDialogProviderFixture, "MainThreadDialogProvider calls the chosen button of a yes/no/cancel dialog", "[MainThreadDialogProvider]")
{
    std::string chosen;

    run_in_worker([&]() {
        provider.show_yesnocancel_dialog(
            "title",
            "text",
            {"Yes", [&]() { chosen = "yes"; }},
            {"No", [&]() { chosen = "no"; }},
            {"Cancel", [&]() { chosen = "cancel"; }}
        );
    });

    CHECK(chosen == "no");
}

TEST_CASE_METHOD(MainThreadDialogProviderFixture, "MainThreadDialogProvider returns the STEP dialog result", "[MainThreadDialogProvider]")
{
    fake.step_result = StepLoadDialogResult{.linear_precision = 0.5, .apply_to_all = true};
    std::optional<StepLoadDialogResult> result;

    run_in_worker([&]() { result = provider.show_load_step_dialog("file.step", 0.1, 5., true); });

    REQUIRE(result.has_value());
    CHECK(result->linear_precision == 0.5);
    CHECK(result->apply_to_all);
}

TEST_CASE_METHOD(MainThreadDialogProviderFixture, "MainThreadDialogProvider doesn't wait for an info dialog", "[MainThreadDialogProvider]")
{
    provider.show_info_dialog("text", "title", false);
    CHECK(fake.shown.empty());

    dispatcher.dispatch_enqueued();
    CHECK(fake.shown == std::vector<std::string>{"info"});
}

TEST_CASE_METHOD(MainThreadDialogProviderFixture, "MainThreadDialogProvider gives up when the job is asked to stop", "[MainThreadDialogProvider]")
{
    bool called{false};
    std::thread worker{[&]() {
        provider.show_yesno_dialog("title", "text", [&](bool) { called = true; });
    }};

    // The main thread doesn't dispatch, as when it waits for the job to finish.
    stop_source->store(true);
    worker.join();
    CHECK_FALSE(called);

    // The abandoned dialog isn't shown later.
    dispatcher.dispatch_enqueued();
    CHECK(fake.shown.empty());
}
