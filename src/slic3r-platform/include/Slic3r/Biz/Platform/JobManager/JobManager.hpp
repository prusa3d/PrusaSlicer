#pragma once

#include "Slic3r/Biz/Platform/IMainThreadDispatcher.hpp"
#include <jthread/JThread.hpp>
#include <map>
#include "Slic3r/Biz/Platform/WithListeners.hpp"
#include "Slic3r/Biz/Platform/JobManager/IJobManagerStatusChangedListener.hpp"
#include "Slic3r/Biz/Platform/JobManager/Job.hpp"
#include "Slic3r/Log.hpp" // IWYU pragma: keep

namespace Slic3r::Biz::Platform::JobManager {

namespace Impl {
template <typename T>
struct function_traits;

template <typename R, typename... Args>
struct function_traits<std::function<R(Args...)>>
{
    using return_type = R;
    using args_type   = std::tuple<Args...>;
};

template <std::size_t Index, typename Tuple, typename T>
constexpr bool is_same_at_index_v = false;

template <std::size_t Index, typename Tuple, typename T>
    requires(Index < std::tuple_size_v<Tuple>)
constexpr bool is_same_at_index_v<Index, Tuple, T> = std::is_same_v<std::tuple_element_t<Index, Tuple>, T>;

template <typename Tuple>
struct without_stop_token
{};

template <typename StopToken, typename... Args>
struct without_stop_token<std::tuple<StopToken, Args...>>
{
    using type = std::tuple<Args...>;
};

template <typename T>
struct remove_rv_ref
{
    using type = T;
};

template <typename T>
struct remove_rv_ref<T&&>
{
    using type = T;
};

template <typename... Args>
struct remove_rv_ref<std::tuple<Args...>>
{
    using type = std::tuple<typename remove_rv_ref<Args>::type...>;
};

template <typename T>
using remove_rv_ref_t = typename remove_rv_ref<T>::type;
} // namespace Impl

template <typename T>
struct DebugType;

template <typename T>
void debug_type()
{
    static_assert(sizeof(DebugType<T>) == 0, "Check the template type here");
}

class JobManager : public WithListeners<IJobManagerStatusChangedListener>
{
public:
    JobManager(IMainThreadDispatcher& dispatcher) : m_dispatcher{dispatcher} {}

    /**
     * @brief Creates a job in a thread.
     *
     * Jobs can be cancelled, return values, report progress, dispatch code to the main thread, and handle exceptions.
     * If a job with the same name already exists, it is cancelled and replaced. The job is automatically
     * removed from the manager once finished.
     *
     * The job function signature:
     * - The job function **must** take a `StopToken` as its **first argument**.
     * - If used, `IMainThreadDispatcher&` and `ProgressTracker` must appear in that **exact order** after `StopToken`.
     * - Any custom arguments can be passed after that
     * - A signature of the job function may look something like this:
     *   [](StopToken, IMainThreadDispatcher&, ProgressTracker, InputData) -> OutputData {}
     *   or as simple as this [](StopToken) {} or anything between.
     *
     * See JobManagerTests.cpp for usage examples.
     *
     * @param name Unique name identifying the job.
     * @param function The task to be performed asynchronously.
     * @param Args Any number of custom arguments for the job function.
     *   Be careful with what you pass as a parameter as you would be with a simple std::jthread.
     * @return A reference to the created job. Optionally specify how the job should behave
     *   using .on_result(...).on_exception(...) and than call .start() on it.
     */
    template <typename F, typename... Args>
    auto& create_job(const std::string& name, F&& function, Args&&... args)
    {
        auto job{init_job(name, std::forward<F>(function), std::forward<Args>(args)...)};
        job->call_after([this, name](size_t id) { 
            auto it = m_jobs.find(name);
            if (it != m_jobs.end() &&
                it->second->get_id() == id) {
                m_jobs.erase(it);
            }
        });
        auto& job_ref{*job.get()};
        m_jobs.insert_or_assign(name, std::move(job));
        return job_ref;
    }

    /**
     * @brief Cancels a job and waits for its thread to finish before returning.
     */
    void cancel_job(const std::string& name)
    {
        m_jobs.erase(name);
    }

    void cancel_jobs_for_project(const Domain::SelectionId project_id)
    {
        std::erase_if(m_jobs, [project_id](const auto& job) {
            return job.second->progress_tracker().get_progress().project_id == project_id;
        });
    }

    /**
     * @brief Asks a job to stop without waiting for it.
     *
     * @return false if no job of that name is registered.
     */
    bool request_job_stop(const std::string& name)
    {
        const auto it = m_jobs.find(name);
        if (it == m_jobs.end()) {
            return false;
        }
        it->second->cancel();
        return true;
    }

    bool has_job(const std::string& name) const
    {
        return m_jobs.find(name) != m_jobs.end();
    }

private:
    IMainThreadDispatcher& m_dispatcher;
    std::map<std::string, std::unique_ptr<JobBase>> m_jobs;
    JobManagerStatus m_status;

    template <typename ArgsTuple, typename... Args>
    ArgsTuple get_args_tuple(const ProgressTracker& progress_tracker, Args&&... args)
    {
        static_assert(!Impl::is_same_at_index_v<0, ArgsTuple, ProgressTracker&> && !Impl::is_same_at_index_v<1, ArgsTuple, ProgressTracker&>, "Progress tracker must not be a reference!");

        static_assert(!Impl::is_same_at_index_v<0, ArgsTuple, IMainThreadDispatcher>, "IMainThread dispatcher must be a reference!");

        if constexpr (Impl::is_same_at_index_v<0, ArgsTuple, IMainThreadDispatcher&> && Impl::is_same_at_index_v<1, ArgsTuple, ProgressTracker>)
        {
            static_assert(sizeof...(args) + 2 == std::tuple_size_v<ArgsTuple>);
            return std::tuple_cat(std::forward_as_tuple(m_dispatcher), std::make_tuple(progress_tracker), std::forward_as_tuple(args...));
        } else if constexpr (Impl::is_same_at_index_v<0, ArgsTuple, ProgressTracker>) {
            static_assert(sizeof...(args) + 1 == std::tuple_size_v<ArgsTuple>);
            return std::tuple_cat(std::make_tuple(progress_tracker), std::forward_as_tuple(args...));
        } else if constexpr (Impl::is_same_at_index_v<0, ArgsTuple, IMainThreadDispatcher&>) {
            static_assert(sizeof...(args) + 1 == std::tuple_size_v<ArgsTuple>);
            return std::tuple_cat(std::forward_as_tuple(m_dispatcher), std::forward_as_tuple(args...));
        } else {
            static_assert(sizeof...(args) == std::tuple_size_v<ArgsTuple>);
            return ArgsTuple{std::forward<Args...>(args)...};
        }
    }

    template <typename F, typename... Args>
    auto init_job(const std::string& name, F&& function, Args&&... args)
    {
        std::function std_function{std::forward<F>(function)};

        using AllArgsTuple = typename Impl::function_traits<decltype(std_function)>::args_type;
        static_assert(Impl::is_same_at_index_v<0, AllArgsTuple, JThread::StopToken>, "First argument of the job must be stop token!");

        using ArgsTuple = typename Impl::without_stop_token<AllArgsTuple>::type;

        const ProgressTracker progress_tracker{ProgressTracker(
            m_dispatcher,
            [this, name](const Progress progress)
            {
                ASSERT(progress.status != Domain::JobStatus::None);
                m_status.insert_or_assign(name, progress);
                invoke_on_change();
                if (progress.status == Domain::JobStatus::Finished || progress.status == Domain::JobStatus::Failed)
                {
                    m_status.erase(name);
                }
            }
        )};

        ArgsTuple args_tuple{get_args_tuple<ArgsTuple>(progress_tracker, std::forward<Args>(args)...)};
        Impl::remove_rv_ref_t<decltype(args_tuple)> no_rv_ref{std::move(args_tuple)};

        Job job_value{name, std::move(std_function), m_dispatcher, progress_tracker, std::move(no_rv_ref)};
        return std::make_unique<decltype(job_value)>(std::move(job_value));
    }

    void invoke_on_change()
    {
        invoke_listeners<IJobManagerStatusChangedListener>(
            [&](auto* listener) { listener->on_job_manager_status_changed(m_status); }
        );
    }
};

} // namespace Slic3r::Biz::Platform::JobManager
