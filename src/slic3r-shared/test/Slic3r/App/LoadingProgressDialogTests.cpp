#include <catch2/catch_test_macros.hpp>

#include "Slic3r/App/LoadingProgressDialog.hpp"
#include "Slic3r/App/Navigator.hpp"
#include "Slic3r/App/Platform/StdMainThreadDispatcher.hpp"
#include "Slic3r/Biz/FileOpen/FileLoadJob.hpp"
#include "Slic3r/Biz/Platform/JobManager/JobManager.hpp"
#include "Slic3r/Biz/Platform/TimerQueue.hpp"
#include "Slic3r/TestUtils/DispatchUtils.hpp"

#include "Yoga/ImGuiFixture.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

using Slic3r::App::LoadingProgressDialog;
using Slic3r::Biz::FileOpen::FileLoadProgressPayload;
using Slic3r::Biz::FileOpen::FileLoadStage;
using Slic3r::Biz::JThread::StopToken;
using Slic3r::Biz::Platform::JobManager::JobManagerStatus;
using Slic3r::Biz::Platform::JobManager::Progress;
using Slic3r::Domain::JobStatus;

namespace {

const std::string LOAD_JOB_1{std::string(Slic3r::Biz::FileOpen::FILE_LOAD_JOB_PREFIX) + "_models_1"};
const std::string LOAD_JOB_2{std::string(Slic3r::Biz::FileOpen::FILE_LOAD_JOB_PREFIX) + "_models_2"};

Progress make_progress(JobStatus status, FileLoadStage stage, FileLoadProgressPayload payload, double fraction)
{
    Progress progress{.status = status};
    progress.progress_detail = {static_cast<int>(stage), std::move(payload)};
    progress.percent         = Slic3r::Domain::Percentage{fraction};
    return progress;
}
/**
 * @brief Job which runs until it is asked to stop, it records that.
 */
struct StoppableJob
{
    std::shared_ptr<std::atomic<bool>> stopped{std::make_shared<std::atomic<bool>>(false)};
    std::shared_ptr<std::atomic<bool>> ended{std::make_shared<std::atomic<bool>>(false)};

    bool was_stopped() const { return stopped->load(); }
    bool has_ended() const { return ended->load(); }

    void start(Slic3r::Biz::Platform::JobManager::JobManager& job_manager, const std::string& name)
    {
        job_manager
            .create_job(
                name,
                [stopped = stopped](StopToken stop_token)
                {
                    const auto start = std::chrono::steady_clock::now();
                    while (!stop_token.stop_requested()) {
                        if (std::chrono::steady_clock::now() - start > std::chrono::seconds(10)) {
                            return;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                    *stopped = true;
                }
            )
            .on_result([ended = ended]() { *ended = true; })
            .start();
    }
};

/**
 * @brief Without the render modules the Navigator doesn't open the dialog, the loading tracking
 * independent of the opened dialog is tested.
 */
struct LoadingProgressDialogFixture : ImGuiFixture
{
    Slic3r::App::Platform::StdMainThreadDispatcher dispatcher;
    Slic3r::Biz::Platform::JobManager::JobManager job_manager{dispatcher};
    Slic3r::Biz::Platform::TimerQueue timer_queue{dispatcher};
    Slic3r::App::Navigator navigator;
    LoadingProgressDialog dialog{navigator, job_manager, timer_queue};

    ~LoadingProgressDialogFixture()
    {
        dialog.shutdown();
        dispatcher.close();
    }

    template <typename Predicate>
    bool wait_until(Predicate predicate)
    {
        return ::Tests::dispatch_until(dispatcher, predicate);
    }

    void dispatch_for(std::chrono::milliseconds duration)
    {
        ::Tests::dispatch_for(dispatcher, duration);
    }
};

} // namespace

TEST_CASE_METHOD(LoadingProgressDialogFixture, "LoadingProgressDialog shows the progress of a loading job", "[LoadingProgressDialog]")
{
    SECTION("reading one of more files") {
        dialog.on_job_manager_status_changed(JobManagerStatus{
            {LOAD_JOB_1, make_progress(JobStatus::Started, FileLoadStage::Reading, {"a.stl", 0, 2}, 0.25)}
        });
        CHECK(dialog.label() == "Reading a.stl (1 of 2)");
        CHECK(dialog.percent() == 25);
    }
    SECTION("reading a single file") {
        dialog.on_job_manager_status_changed(JobManagerStatus{
            {LOAD_JOB_1, make_progress(JobStatus::Started, FileLoadStage::Reading, {"a.stl", 0, 1}, 0.1)}
        });
        CHECK(dialog.label() == "Reading a.stl");
        CHECK(dialog.percent() == 10);
    }
    SECTION("adding more files to the scene") {
        dialog.on_job_manager_status_changed(JobManagerStatus{
            {LOAD_JOB_1, make_progress(JobStatus::Started, FileLoadStage::AddingToScene, {"", 1, 2}, 0.95)}
        });
        CHECK(dialog.label() == "Adding the files to the scene");
        CHECK(dialog.percent() == 95);
    }
    SECTION("before the first file is processed") {
        dialog.on_job_manager_status_changed(JobManagerStatus{{LOAD_JOB_1, Progress{.status = JobStatus::Started}}});
        CHECK(dialog.label() == "Loading...");
        CHECK(dialog.percent() == 0);
    }
}

TEST_CASE_METHOD(LoadingProgressDialogFixture, "LoadingProgressDialog ignores other jobs", "[LoadingProgressDialog]")
{
    dialog.on_job_manager_status_changed(JobManagerStatus{
        {"slicing", make_progress(JobStatus::Started, FileLoadStage::Reading, {"a.stl", 0, 1}, 0.5)},
        {LOAD_JOB_1, make_progress(JobStatus::Finished, FileLoadStage::Reading, {"b.stl", 0, 1}, 1.0)}
    });
    CHECK(dialog.label().empty());
    CHECK(dialog.percent() == 0);

    dialog.cancel();
    CHECK(dialog.label().empty());
}

TEST_CASE_METHOD(LoadingProgressDialogFixture, "LoadingProgressDialog keeps the shown job while it runs", "[LoadingProgressDialog]")
{
    dialog.on_job_manager_status_changed(JobManagerStatus{
        {LOAD_JOB_2, make_progress(JobStatus::Started, FileLoadStage::Reading, {"b.stl", 0, 1}, 0.1)}
    });
    REQUIRE(dialog.label() == "Reading b.stl");

    // LOAD_JOB_1 is first in the status, the shown job doesn't change anyway.
    dialog.on_job_manager_status_changed(JobManagerStatus{
        {LOAD_JOB_1, make_progress(JobStatus::Started, FileLoadStage::Reading, {"a.stl", 0, 1}, 0.1)},
        {LOAD_JOB_2, make_progress(JobStatus::Started, FileLoadStage::Converting, {"b.stl", 0, 1}, 0.7)}
    });
    CHECK(dialog.label() == "Processing b.stl");
    CHECK(dialog.percent() == 70);

    // The shown job ended, the other one is shown.
    dialog.on_job_manager_status_changed(JobManagerStatus{
        {LOAD_JOB_1, make_progress(JobStatus::Started, FileLoadStage::AddingToScene, {"a.stl", 0, 1}, 0.9)}
    });
    CHECK(dialog.label() == "Adding a.stl to the scene");
}

TEST_CASE_METHOD(LoadingProgressDialogFixture, "LoadingProgressDialog cancel stops the loading jobs", "[LoadingProgressDialog]")
{
    StoppableJob first;
    StoppableJob second;
    first.start(job_manager, LOAD_JOB_1);
    second.start(job_manager, LOAD_JOB_2);
    REQUIRE(wait_until([&] { return dialog.label() == "Loading..."; }));

    dialog.cancel();
    CHECK(dialog.label() == "Cancelling...");
    REQUIRE(wait_until([&] { return first.has_ended() && second.has_ended(); }));
    CHECK(first.was_stopped());
    CHECK(second.was_stopped());
    // The progress isn't shown anymore after the cancel.
    CHECK(dialog.label() == "Cancelling...");
}

TEST_CASE_METHOD(LoadingProgressDialogFixture, "LoadingProgressDialog cancel stops a job which follows the cancelled one", "[LoadingProgressDialog]")
{
    StoppableJob first;
    StoppableJob follower;
    first.start(job_manager, LOAD_JOB_1);
    REQUIRE(wait_until([&] { return !dialog.label().empty(); }));

    dialog.cancel();
    REQUIRE(wait_until([&] { return first.has_ended(); }));
    // Started before the close delay elapsed, like a project which follows the cancelled models.
    follower.start(job_manager, LOAD_JOB_2);

    REQUIRE(wait_until([&] { return follower.has_ended(); }));
    CHECK(follower.was_stopped());
}

TEST_CASE_METHOD(LoadingProgressDialogFixture, "LoadingProgressDialog doesn't stop a job started after the cancelled loading finished", "[LoadingProgressDialog]")
{
    StoppableJob first;
    first.start(job_manager, LOAD_JOB_1);
    REQUIRE(wait_until([&] { return !dialog.label().empty(); }));
    dialog.cancel();
    REQUIRE(wait_until([&] { return first.has_ended(); }));
    // Lets the close delay elapse.
    dispatch_for(std::chrono::milliseconds(300));

    StoppableJob next;
    next.start(job_manager, LOAD_JOB_2);
    REQUIRE(wait_until([&] { return dialog.label() == "Loading..."; }));
    dispatch_for(std::chrono::milliseconds(100));
    CHECK_FALSE(next.was_stopped());

    job_manager.request_job_stop(LOAD_JOB_2);
    REQUIRE(wait_until([&] { return next.has_ended(); }));
}
