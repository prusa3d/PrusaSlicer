#include <catch2/catch_test_macros.hpp>
#include <set>
#include <tl/expected.hpp>
#include "Slic3r/App/Platform/StdMainThreadDispatcher.hpp"
#include "Slic3r/Biz/Platform/JobManager/JobManager.hpp"

using Slic3r::App::Platform::StdMainThreadDispatcher;
using Slic3r::Biz::JThread::StopToken;
using Slic3r::Biz::Platform::IMainThreadDispatcher;
using Slic3r::Biz::Platform::JobManager::JobManager;
using namespace std::chrono_literals;
using std::chrono::high_resolution_clock;
using std::chrono::seconds;
using namespace std::chrono_literals;
using Slic3r::Biz::Platform::JobManager::IJobManagerStatusChangedListener;
using Slic3r::Biz::Platform::JobManager::JobManagerStatus;
using Slic3r::Biz::Platform::JobManager::Progress;
using Slic3r::Biz::Platform::JobManager::ProgressTracker;
using Slic3r::Domain::JobStatus;
using Slic3r::Domain::Percentage;

struct MyData
{
    std::unique_ptr<int> data;
};

struct JobManagerStatusListener : public Slic3r::Biz::Platform::JobManager::IJobManagerStatusChangedListener
{
    void on_job_manager_status_changed(const JobManagerStatus& job_manager_status) override
    {
        REQUIRE(job_manager_status.size() == 1);
        REQUIRE(job_manager_status.contains("test_job"));
        progress.push_back(job_manager_status.at("test_job"));
        statuses.insert(progress.back().status);
    }

    std::vector<Progress> progress;
    std::set<JobStatus> statuses;
};

struct JobManagerFixture
{
    JobManagerFixture()
    {
        job_manager.add_listener<IJobManagerStatusChangedListener>(&listener);
    }

    ~JobManagerFixture()
    {
        dispatcher.close();
    }

    JobManagerStatusListener listener;
    StdMainThreadDispatcher dispatcher;
    JobManager job_manager{dispatcher};

    bool wait_for_status(const JobStatus status, const seconds& timeout)
    {
        const auto start{high_resolution_clock::now()};
        while (true) {
            dispatcher.dispatch_enqueued();
            if (listener.statuses.contains(status)) {
                return true;
            }
            const auto now{high_resolution_clock::now()};
            if (now - start > timeout) {
                return false;
            }
        }
    }
};

TEST_CASE_METHOD(JobManagerFixture, "JobManager job works with move only data", "[JobManager]")
{
    const auto job{[&](StopToken, MyData&& my_data) -> MyData { return std::move(my_data); }};

    bool result_recieved{false};
    job_manager.create_job("test_job", job, MyData{})
        .on_result([&](const MyData& result) { result_recieved = true; })
        .start();

    REQUIRE(wait_for_status(JobStatus::Finished, 3s));
    CHECK(result_recieved);
}

TEST_CASE_METHOD(JobManagerFixture, "JobManager job works with value copy", "[JobManager]")
{
    const auto job{[&](StopToken, const std::string data) { return data; }};

    bool result_recieved{false};
    const std::string data{"Some reasonably long data in a string!"};
    job_manager.create_job("test_job", job, data)
        .on_result([&](const std::string& data) { result_recieved = true; })
        .start();

    REQUIRE(wait_for_status(JobStatus::Finished, 3s));
    CHECK(result_recieved);
}

TEST_CASE_METHOD(JobManagerFixture, "JobManager job can be cancelled", "[JobManager]")
{
    const auto job{
        [&](StopToken stop_token)
        {
            const auto start{high_resolution_clock::now()};
            while (!stop_token.stop_requested()) {
                std::this_thread::sleep_for(1ms);
                const auto now{high_resolution_clock::now()};
                if (now - start > 5s) {
                    throw std::runtime_error{"Timeout!"};
                }
            }
        }
    };

    bool result_recieved{false};
    job_manager.create_job("test_job", job).on_result([&]() { result_recieved = true; }).start();
    std::this_thread::sleep_for(10ms);
    job_manager.cancel_job("test_job");

    REQUIRE(wait_for_status(JobStatus::Finished, 3s));
    CHECK(result_recieved);
}

struct StopRequested : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

TEST_CASE_METHOD(JobManagerFixture, "JobManager job can be cancelled by starting annother job", "[JobManager]")
{
    const auto job{
        [&](StopToken stop_token)
        {
            const auto start{high_resolution_clock::now()};
            while (true) {
                std::this_thread::sleep_for(1ms);

                if (stop_token.stop_requested()) {
                    throw StopRequested{""};
                }

                const auto now{high_resolution_clock::now()};
                if (now - start > 5s) {
                    throw std::runtime_error{"Timeout!"};
                }
            }
        }
    };

    bool stop_requested{true};
    job_manager.create_job("test_job", job)
        .on_exception(
            [&](const std::exception_ptr& exception)
            {
                try {
                    std::rethrow_exception(exception);
                } catch (const StopRequested&) {
                    stop_requested = true;
                }
            }
        )
        .start();
    std::this_thread::sleep_for(10ms);

    bool result_recieved{false};
    job_manager.create_job("test_job", [](StopToken) {})
        .on_result([&]() { result_recieved = true; })
        .start();

    REQUIRE(wait_for_status(JobStatus::Finished, 3s));
    CHECK(stop_requested);
    CHECK(result_recieved);
}

TEST_CASE_METHOD(JobManagerFixture, "JobManager job can use the dispatcher", "[JobManager]")
{
    std::size_t event_recieved{0};
    const auto job{
        [&](StopToken, IMainThreadDispatcher& dispatcher)
        {
            for (std::size_t _{}; _ < 42; ++_) {
                (void) dispatcher.dispatch_on_main_thread([&]() { event_recieved++; });
                std::this_thread::sleep_for(1ms);
            }
        }
    };

    job_manager.create_job("test_job", job).on_result([&]() {}).start();

    REQUIRE(wait_for_status(JobStatus::Finished, 3s));
    CHECK(event_recieved == 42);
}

TEST_CASE_METHOD(JobManagerFixture, "JobManager job can use the progress", "[JobManager]")
{
    const auto job{[&](StopToken, ProgressTracker progress) { progress.set(Percentage{10}); }};

    job_manager.create_job("test_job", job).on_result([&]() {}).start();

    REQUIRE(wait_for_status(JobStatus::Finished, 3s));
    REQUIRE(listener.progress.size() == 3);
    CHECK(listener.progress[1].percent == Percentage{10});
}

TEST_CASE_METHOD(JobManagerFixture, "JobManager job can use the progress and dispatcher", "[JobManager]")
{
    bool event_recieved{false};
    const auto job{
        [&](StopToken, IMainThreadDispatcher& dispatcher, ProgressTracker progress)
        {
            progress.set(Percentage{10});
            (void) dispatcher.dispatch_on_main_thread([&]() { event_recieved = true; });
        }
    };

    job_manager.create_job("test_job", job).on_result([&]() {}).start();

    REQUIRE(wait_for_status(JobStatus::Finished, 3s));
    REQUIRE(listener.progress.size() == 3);
    CHECK(listener.progress[1].percent == Percentage{10});
    CHECK(event_recieved);
}

TEST_CASE_METHOD(JobManagerFixture, "By default exception thrown in the thread is rethrown in main thread", "[JobManager]")
{
    const auto job{[&](StopToken) { throw std::runtime_error{""}; }};

    job_manager.create_job("test_job", job).on_result([&]() { REQUIRE(false); }).start();
    CHECK_THROWS_AS(wait_for_status(JobStatus::Failed, 3s), std::runtime_error);
}

TEST_CASE_METHOD(JobManagerFixture, "Exception handling can be configured (swallowed in this case)", "[JobManager]")
{
    const auto job{[&](StopToken) { throw std::runtime_error{"My error"}; }};

    bool exception_recieved{false};
    job_manager.create_job("test_job", job)
        .on_exception(
            [&](const std::exception_ptr) { exception_recieved = true; }
        )
        .on_result([&]() { REQUIRE(false); })
        .start();
    REQUIRE(wait_for_status(JobStatus::Failed, 3s));
    CHECK(exception_recieved);
}
