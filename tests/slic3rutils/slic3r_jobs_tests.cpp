#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_template_test_macros.hpp>

#include <chrono>
#include <thread>

#include "slic3r/GUI/Jobs/BoostThreadWorker.hpp"
#include "slic3r/GUI/Jobs/UIThreadWorker.hpp"
#include "slic3r/GUI/Jobs/ProgressIndicator.hpp"

struct Progress: Slic3r::ProgressIndicator {
    int range = 100;
    int pr = 0;
    std::string statustxt;
    void set_range(int r) override { range = r; }
    void set_cancel_callback(CancelFn = CancelFn()) override {}
    void set_progress(int p) override { pr = p; }
    void set_status_text(const char *txt) override { statustxt = txt; }
    int  get_range() const override { return range; }
};

using TestClasses = std::tuple< Slic3r::GUI::UIThreadWorker, Slic3r::GUI::BoostThreadWorker >;

TEMPLATE_LIST_TEST_CASE("Empty worker should not block when queried for idle", "[Jobs]", TestClasses) {
    TestType worker{std::make_unique<Progress>()};

    worker.wait_for_idle();

    REQUIRE(worker.is_idle());
}

TEMPLATE_LIST_TEST_CASE("Empty worker should not do anything", "[Jobs]", TestClasses) {
    TestType worker{std::make_unique<Progress>()};

    REQUIRE(worker.is_idle());

    worker.wait_for_current_job();
    worker.process_events();

    REQUIRE(worker.is_idle());
}

TEMPLATE_LIST_TEST_CASE("nullptr job should be ignored", "[Jobs]", TestClasses) {
    TestType worker{std::make_unique<Progress>()};
    worker.push(nullptr);

    REQUIRE(worker.is_idle());
}

TEMPLATE_LIST_TEST_CASE("State should not be idle while running a job", "[Jobs]", TestClasses) {
    using namespace Slic3r;
    using namespace Slic3r::GUI;
    TestType worker{std::make_unique<Progress>(), "worker_thread"};

    queue_job(worker, [&worker](Job::Ctl &ctl) {
        ctl.call_on_main_thread([&worker] {
            REQUIRE(!worker.is_idle());
        }).wait();
    });

    // make sure that the job starts BEFORE the worker.wait_for_idle() is called
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    worker.wait_for_idle();

    REQUIRE(worker.is_idle());
}

TEMPLATE_LIST_TEST_CASE("Status messages should be received by the main thread during job execution", "[Jobs]", TestClasses) {
    using namespace Slic3r;
    using namespace Slic3r::GUI;
    auto pri = std::make_shared<Progress>();
    TestType worker{pri};

    queue_job(worker, [](Job::Ctl &ctl){
        for (int s = 0; s <= 100; ++s) {
            ctl.update_status(s, "Running");
        }
    });

    worker.wait_for_idle();

    REQUIRE(pri->pr == 100);
    REQUIRE(pri->statustxt == "Running");
}

TEMPLATE_LIST_TEST_CASE("Cancellation should be recognized be the worker", "[Jobs]", TestClasses) {
    using namespace Slic3r;
    using namespace Slic3r::GUI;

    auto pri = std::make_shared<Progress>();
    TestType worker{pri};

    queue_job(
        worker,
        [](Job::Ctl &ctl) {
            for (int s = 0; s <= 100; ++s) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                ctl.update_status(s, "Running");
                if (ctl.was_canceled())
                    break;
            }
        },
        [](bool cancelled, std::exception_ptr &) { // finalize
            REQUIRE(cancelled == true);
        });

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    worker.cancel();

    worker.wait_for_current_job();

    REQUIRE(pri->pr != 100);
}

TEMPLATE_LIST_TEST_CASE("cancel_all should remove all pending jobs", "[Jobs]", TestClasses) {
    using namespace Slic3r;
    using namespace Slic3r::GUI;

    auto pri = std::make_shared<Progress>();
    TestType worker{pri};

    std::array<bool, 4> jobres = {false, false, false, false};

    queue_job(worker, [&jobres](Job::Ctl &) {
        jobres[0] = true;
        // FIXME: the long wait time is needed to prevent fail in MSVC
        // where the sleep_for function is behaving stupidly.
        // see https://developercommunity.visualstudio.com/t/bogus-stdthis-threadsleep-for-implementation/58530
        std::this_thread::sleep_for(std::chrono::seconds(1));
    });
    queue_job(worker, [&jobres](Job::Ctl &) {
        jobres[1] = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    });
    queue_job(worker, [&jobres](Job::Ctl &) {
        jobres[2] = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    });
    queue_job(worker, [&jobres](Job::Ctl &) {
        jobres[3] = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    });

    // wait until the first job's half time to be sure, the cancel is made
    // during the first job's execution.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    worker.cancel_all();

    REQUIRE(jobres[0] == true);
    REQUIRE(jobres[1] == false);
    REQUIRE(jobres[2] == false);
    REQUIRE(jobres[3] == false);
}

TEMPLATE_LIST_TEST_CASE("Exception should be properly forwarded to finalize()", "[Jobs]", TestClasses) {
    using namespace Slic3r;
    using namespace Slic3r::GUI;

    auto pri = std::make_shared<Progress>();
    TestType worker{pri};

    queue_job(
        worker, [](Job::Ctl &) { throw std::runtime_error("test"); },
        [](bool /*canceled*/, std::exception_ptr &eptr) {
            REQUIRE(eptr != nullptr);
            try {
                std::rethrow_exception(eptr);
            } catch (std::runtime_error &e) {
                REQUIRE(std::string(e.what()) == "test");
            }

            eptr = nullptr;
        });

    worker.wait_for_idle();
    REQUIRE(worker.is_idle());
}
