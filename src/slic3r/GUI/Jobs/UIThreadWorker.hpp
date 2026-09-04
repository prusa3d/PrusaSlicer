///|/ Copyright (c) Prusa Research 2022 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef UITHREADWORKER_HPP
#define UITHREADWORKER_HPP

#include <deque>
#include <queue>

#include "Worker.hpp"
#include "ProgressIndicator.hpp"

namespace Slic3r { namespace GUI {

// Implementation of a worker which does not create any additional threads.
class UIThreadWorker : public Worker, private Job::Ctl {
    std::queue<std::unique_ptr<Job>, std::deque<std::unique_ptr<Job>>> m_jobqueue;
    std::shared_ptr<ProgressIndicator> m_progress;
    bool m_running = false;
    bool m_canceled = false;

    void process_front()
    {
        std::unique_ptr<Job> job;

        if (!m_jobqueue.empty()) {
            job = std::move(m_jobqueue.front());
            m_jobqueue.pop();
        }

        if (job) {
            std::exception_ptr eptr;
            m_running = true;

            try {
                job->process(*this);
            } catch (...) {
                eptr= std::current_exception();
            }

            job->finalize(m_canceled, eptr);

            // Unhandled exceptions are rethrown without mercy.
            if (eptr)
                std::rethrow_exception(eptr);

            m_running = false;

            m_canceled = false;
        }
    }

protected:
    // Implement Job::Ctl interface:

    void update_status(int st, const std::string &msg = "") override
    {
        if (m_progress) {
            m_progress->set_progress(st);
            m_progress->set_status_text(msg.c_str());
        }
    }

    bool was_canceled() const override { return m_canceled; }

    std::future<void> call_on_main_thread(std::function<void()> fn) override
    {
        std::future<void> ftr = std::async(std::launch::deferred, [fn]{ fn(); });

        // So, it seems that the destructor of std::future will not call the
        // packaged function. The future needs to be accessed at least ones
        // or waited upon. Calling wait() instead of get() will keep the
        // returned future's state valid.
        ftr.wait();

        return ftr;
    }

public:
    explicit UIThreadWorker(std::shared_ptr<ProgressIndicator> pri,
                            const std::string & /*name*/ = "")
        : m_progress{pri}
    {
        if (m_progress)
            m_progress->set_cancel_callback([this]() { cancel(); });
    }

    UIThreadWorker() = default;

    bool push(std::unique_ptr<Job> job) override
    {
        m_canceled = false;

        if (job)
            m_jobqueue.push(std::move(job));

        return bool(job);
    }

    bool is_idle() const override { return !m_running && m_jobqueue.empty(); }

    void cancel() override { m_canceled = true; }

    void cancel_all() override
    {
        m_canceled = true;
        process_front();

        while (!m_jobqueue.empty())
            m_jobqueue.pop();
    }

    void process_events() override {
        while (!m_jobqueue.empty())
            process_front();
    }

    bool wait_for_current_job(unsigned /*timeout_ms*/ = 0) override {
        process_front();

        return true;
    }

    bool wait_for_idle(unsigned /*timeout_ms*/ = 0) override {
        process_events();

        return true;
    }

    ProgressIndicator * get_pri() { return m_progress.get(); }
    const ProgressIndicator * get_pri() const  { return m_progress.get(); }
};

}} // namespace Slic3r::GUI

#endif // UITHREADWORKER_HPP
