#include "Slic3r/Biz/PresetUpdater/PresetUpdaterTestFixture.hpp"

#include "Slic3r/Biz/Platform/JobManager/IJobManagerStatusChangedListener.hpp"

#include <algorithm>
#include <atomic>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace Slic3r::Biz;
using namespace Slic3r::Biz::PresetUpdater::TestSupport;
using namespace std::chrono_literals;

namespace {

const std::string k_queue_suffix = "queue";

/// Holds the worker inside its first network request, so the test has a deterministic window in
/// which the first job is running and everything submitted after it is still queued behind it.
/// The returned mutator owns its state, so it stays valid however the caller orders its locals.
ResponseMutator slow_first_request(std::chrono::milliseconds hold = 1500ms)
{
    auto seen = std::make_shared<std::atomic<bool>>(false);
    return [seen, hold](const std::string&, std::string&, unsigned&) {
        if (!seen->exchange(true)) {
            std::this_thread::sleep_for(hold);
        }
    };
}

/// Throws out of the worker on the first network request only.
ResponseMutator fail_first_request(const std::string& message)
{
    auto requests = std::make_shared<std::atomic<int>>(0);
    return [requests, message](const std::string&, std::string&, unsigned&) {
        if (requests->fetch_add(1) == 0) {
            throw std::runtime_error(message);
        }
    };
}

/// Remembers every job name the job manager ever reported progress for.
struct JobStatusRecorder : Platform::JobManager::IJobManagerStatusChangedListener
{
    std::set<std::string> names;

    void on_job_manager_status_changed(
        const Platform::JobManager::JobManagerStatus& status
    ) override
    {
        for (const auto& [name, progress] : status) {
            names.insert(name);
        }
    }

    bool saw(const std::string& name) const { return names.find(name) != names.end(); }
};

size_t count_finished(const CaptureListener& listener, PresetUpdater::JobId job_id)
{
    const auto order = listener.finished_order();
    return static_cast<size_t>(std::count(order.begin(), order.end(), job_id));
}

} // namespace

TEST_CASE("PresetUpdater queue runs every submitted job in submission order", "[preset_updater]")
{
    Fixture fx(k_queue_suffix);
    fx.put_server("100");

    CaptureListener listener(fx.interactor());

    // The old interactor cancelled whatever was running on every public call, so all but the last
    // of these would have been silently dropped. The queue is what makes them all arrive.
    std::vector<PresetUpdater::JobId> submitted;
    for (int i = 0; i < 4; ++i) {
        submitted.push_back(fx.interactor().list_repositories(true));
    }

    REQUIRE(fx.wait_for_finished(listener, submitted.size()));

    CHECK(listener.finished_order() == submitted);
    CHECK(listener.payload_order() == submitted);
    for (PresetUpdater::JobId job_id : submitted) {
        CHECK(job_id != PresetUpdater::k_invalid_job_id);
        CHECK(listener.state_of(job_id) == PresetUpdater::JobState::Succeeded);
        CHECK(listener.payload_count(job_id) == 1);
        CHECK(count_finished(listener, job_id) == 1);
    }

    const std::set<PresetUpdater::JobId> unique(submitted.begin(), submitted.end());
    CHECK(unique.size() == submitted.size());
    CHECK(fx.interactor().pending_job_count() == 0);
}

TEST_CASE("PresetUpdater queue mixes operation kinds without losing results", "[preset_updater]")
{
    Fixture fx(k_queue_suffix);
    fx.put_server("100");

    CaptureListener listener(fx.interactor());

    const PresetUpdater::JobId list = fx.interactor().list_repositories(true);
    const PresetUpdater::JobId check = fx.interactor()
        .build_update_sync_and_reconfiguration_check(true, PresetUpdater::VerboseStyle::NoProgress, true);
    const PresetUpdater::JobId forced = fx.interactor().check_forced_reconfigurations();
    const PresetUpdater::JobId cleanup = fx.interactor().cleanup_update_sync();

    const std::vector<PresetUpdater::JobId> submitted{list, check, forced, cleanup};
    REQUIRE(fx.wait_for_finished(listener, submitted.size()));

    CHECK(listener.finished_order() == submitted);
    for (PresetUpdater::JobId job_id : submitted) {
        CHECK(listener.state_of(job_id) == PresetUpdater::JobState::Succeeded);
        CHECK(listener.payload_count(job_id) == 1);
    }
}

TEST_CASE("PresetUpdater queue reports pending jobs while one is running", "[preset_updater]")
{
    Fixture fx(k_queue_suffix);
    fx.put_server("100");

    fx.set_mutator(slow_first_request());

    CaptureListener listener(fx.interactor());

    const PresetUpdater::JobId running = fx.interactor()
        .build_update_sync_and_reconfiguration_check(true, PresetUpdater::VerboseStyle::NoProgress, true);
    const PresetUpdater::JobId queued_a = fx.interactor().list_repositories(true);
    const PresetUpdater::JobId queued_b = fx.interactor().list_repositories(true);

    CHECK(fx.interactor().pending_job_count() == 3);
    CHECK(fx.interactor().is_job_pending(running));
    CHECK(fx.interactor().is_job_pending(queued_a));
    CHECK(fx.interactor().is_job_pending(queued_b));

    REQUIRE(fx.wait_for_finished(listener, 3));
    CHECK(fx.interactor().pending_job_count() == 0);
    CHECK_FALSE(fx.interactor().is_job_pending(queued_b));
}

TEST_CASE("PresetUpdater cancels the running job and keeps the queue", "[preset_updater]")
{
    Fixture fx(k_queue_suffix);
    fx.put_server("100");

    fx.set_mutator(slow_first_request());

    CaptureListener listener(fx.interactor());

    const PresetUpdater::JobId running = fx.interactor()
        .build_update_sync_and_reconfiguration_check(true, PresetUpdater::VerboseStyle::NoProgress, true);
    const PresetUpdater::JobId queued_a = fx.interactor().list_repositories(true);
    const PresetUpdater::JobId queued_b = fx.interactor().list_repositories(true);

    REQUIRE(fx.interactor().cancel_job(running));

    REQUIRE(fx.wait_for_finished(listener, 3));

    CHECK(listener.state_of(running) == PresetUpdater::JobState::Canceled);
    CHECK(listener.payload_count(running) == 0);
    CHECK(listener.state_of(queued_a) == PresetUpdater::JobState::Succeeded);
    CHECK(listener.state_of(queued_b) == PresetUpdater::JobState::Succeeded);
    CHECK(listener.payload_count(queued_a) == 1);
    CHECK(listener.payload_count(queued_b) == 1);
}

TEST_CASE("PresetUpdater drops one queued job without disturbing the rest", "[preset_updater]")
{
    Fixture fx(k_queue_suffix);
    fx.put_server("100");

    fx.set_mutator(slow_first_request());

    CaptureListener listener(fx.interactor());

    const PresetUpdater::JobId running = fx.interactor()
        .build_update_sync_and_reconfiguration_check(true, PresetUpdater::VerboseStyle::NoProgress, true);
    const PresetUpdater::JobId doomed   = fx.interactor().list_repositories(true);
    const PresetUpdater::JobId survivor = fx.interactor().list_repositories(true);

    REQUIRE(fx.interactor().is_job_pending(doomed));
    REQUIRE(fx.interactor().cancel_job(doomed));

    REQUIRE(fx.wait_for_finished(listener, 3));

    CHECK(listener.state_of(doomed) == PresetUpdater::JobState::Canceled);
    CHECK(listener.payload_count(doomed) == 0);
    CHECK(listener.state_of(running) == PresetUpdater::JobState::Succeeded);
    CHECK(listener.state_of(survivor) == PresetUpdater::JobState::Succeeded);
    CHECK(listener.payload_count(survivor) == 1);
}

TEST_CASE("PresetUpdater cancel_all_jobs empties the queue", "[preset_updater]")
{
    Fixture fx(k_queue_suffix);
    fx.put_server("100");

    fx.set_mutator(slow_first_request());

    CaptureListener listener(fx.interactor());

    const PresetUpdater::JobId running = fx.interactor()
        .build_update_sync_and_reconfiguration_check(true, PresetUpdater::VerboseStyle::NoProgress, true);
    const PresetUpdater::JobId queued_a = fx.interactor().list_repositories(true);
    const PresetUpdater::JobId queued_b = fx.interactor().list_repositories(true);

    fx.interactor().cancel_all_jobs();

    REQUIRE(fx.wait_for_finished(listener, 3));

    for (PresetUpdater::JobId job_id : {running, queued_a, queued_b}) {
        CHECK(listener.state_of(job_id) == PresetUpdater::JobState::Canceled);
        CHECK(listener.payload_count(job_id) == 0);
        CHECK(count_finished(listener, job_id) == 1);
    }
    CHECK(fx.interactor().pending_job_count() == 0);
    CHECK_FALSE(listener.has_result());
}

TEST_CASE("PresetUpdater cancel_job rejects unknown and finished jobs", "[preset_updater]")
{
    Fixture fx(k_queue_suffix);
    fx.put_server("100");

    CaptureListener listener(fx.interactor());

    CHECK_FALSE(fx.interactor().cancel_job(PresetUpdater::k_invalid_job_id));
    CHECK_FALSE(fx.interactor().cancel_job(9999));

    const PresetUpdater::JobId done = fx.interactor().list_repositories(true);
    REQUIRE(fx.wait_for_finished(listener, 1));
    CHECK(listener.state_of(done) == PresetUpdater::JobState::Succeeded);

    CHECK_FALSE(fx.interactor().cancel_job(done));
    CHECK(count_finished(listener, done) == 1);
}

TEST_CASE("PresetUpdater queue survives a failing job", "[preset_updater]")
{
    Fixture fx(k_queue_suffix);
    fx.put_server("100");

    fx.set_mutator(fail_first_request("injected worker failure"));

    CaptureListener listener(fx.interactor());

    const PresetUpdater::JobId failing   = fx.interactor().list_repositories(true);
    const PresetUpdater::JobId following = fx.interactor().list_repositories(true);

    REQUIRE(fx.wait_for_finished(listener, 2));

    // A job that throws must not take the worker or the jobs behind it down with it.
    CHECK(listener.state_of(failing) == PresetUpdater::JobState::Failed);
    CHECK(listener.state_of(following) == PresetUpdater::JobState::Succeeded);
    CHECK(listener.got_error());
    CHECK(listener.error_body().find("injected worker failure") != std::string::npos);
    CHECK(fx.interactor().pending_job_count() == 0);
}

TEST_CASE("PresetUpdater runs its work through the app job manager", "[preset_updater]")
{
    Fixture fx(k_queue_suffix);
    fx.put_server("100");
    fx.set_mutator(slow_first_request());

    CaptureListener listener(fx.interactor());
    JobStatusRecorder recorder;

    Platform::JobManager::JobManager& jobs = Platform::PlatformServices::instance().job_manager();
    jobs.add_listener<Platform::JobManager::IJobManagerStatusChangedListener>(&recorder);

    const PresetUpdater::JobId running = fx.interactor()
        .build_update_sync_and_reconfiguration_check(true, PresetUpdater::VerboseStyle::NoProgress, true);
    const PresetUpdater::JobId queued = fx.interactor().list_repositories(true);

    const std::string running_name =
        PresetUpdater::PresetUpdaterInteractor::job_manager_name(running);
    const std::string queued_name =
        PresetUpdater::PresetUpdaterInteractor::job_manager_name(queued);

    // Only the running job is handed to the job manager; the queued one is held back, which is
    // the whole reason this class has a queue of its own.
    CHECK(jobs.has_job(running_name));
    CHECK_FALSE(jobs.has_job(queued_name));

    REQUIRE(fx.wait_for_finished(listener, 2));

    CHECK(listener.state_of(running) == PresetUpdater::JobState::Succeeded);
    CHECK(listener.state_of(queued) == PresetUpdater::JobState::Succeeded);

    // Both showed up in the global job status while they ran, so preset updater work is visible
    // to anything watching the job manager.
    CHECK(recorder.saw(running_name));
    CHECK(recorder.saw(queued_name));

    // Finished jobs remove themselves from the manager.
    CHECK_FALSE(jobs.has_job(running_name));
    CHECK_FALSE(jobs.has_job(queued_name));

    jobs.remove_listener<Platform::JobManager::IJobManagerStatusChangedListener>(&recorder);
}

TEST_CASE("PresetUpdater shuts down cleanly with work still queued", "[preset_updater]")
{
    {
        Fixture fx(k_queue_suffix);
        fx.put_server("100");
        fx.set_mutator(slow_first_request());

        CaptureListener listener(fx.interactor());

        fx.interactor().build_update_sync_and_reconfiguration_check(
            true, PresetUpdater::VerboseStyle::NoProgress, true
        );
        fx.interactor().list_repositories(true);
        fx.interactor().list_repositories(true);

        REQUIRE(fx.interactor().pending_job_count() == 3);
        // Leaving the scope must stop the running job and drop the queue without hanging or
        // asserting. The job body holds the interactor, so destruction has to join it.
    }

    SUCCEED("interactor destroyed with a running job and a non-empty queue");
}
