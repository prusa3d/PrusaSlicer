///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef CANCELLABLEJOB_HPP
#define CANCELLABLEJOB_HPP

#include <atomic>

#include "Job.hpp"

namespace Slic3r { namespace GUI {

template<class JobSubclass>
class CancellableJob: public Job {
    JobSubclass m_job;
    std::atomic_bool &m_flag;

public:
    template<class... Args>
    CancellableJob(std::atomic_bool &flag, Args &&...args)
        : m_job{std::forward<Args>(args)...}, m_flag{flag}
    {}

    void process(Ctl &ctl) override
    {
        m_flag.store(false);

        struct CancelCtl : public Ctl
        {
            Ctl              &basectl;
            std::atomic_bool &cflag;

            CancelCtl (Ctl &c, std::atomic_bool &f): basectl{c}, cflag{f} {}

            void update_status(int st, const std::string &msg = "") override
            {
                basectl.update_status(st, msg);
            }

            bool was_canceled() const override
            {
                return cflag.load() || basectl.was_canceled();
            }

            std::future<void> call_on_main_thread(
                std::function<void()> fn) override
            {
                return basectl.call_on_main_thread(fn);
            }
        } cctl(ctl, m_flag);

        m_job.process(cctl);
    }

    void finalize(bool canceled, std::exception_ptr &eptr) override
    {
        m_job.finalize(m_flag.load() || canceled, eptr);
    }
};

}} // namespace Slic3r::GUI

#endif // CANCELLABLEJOB_HPP
