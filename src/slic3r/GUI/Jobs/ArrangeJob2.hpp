///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef ARRANGEJOB2_HPP
#define ARRANGEJOB2_HPP

#include <optional>

#include "Job.hpp"

#include <arrange-wrapper/Tasks/ArrangeTask.hpp>
#include <arrange-wrapper/Tasks/FillBedTask.hpp>
#include <arrange-wrapper/Items/ArrangeItem.hpp>
#include <arrange-wrapper/SceneBuilder.hpp>

namespace Slic3r {

class Model;
class DynamicPrintConfig;
class ModelInstance;

class Print;
class SLAPrint;

namespace GUI {

class Plater;

enum class ArrangeSelectionMode { SelectionOnly, Full, CurrentBedFull, CurrentBedSelectionOnly };

arr2::SceneBuilder build_scene(
    Plater &plater, ArrangeSelectionMode mode = ArrangeSelectionMode::Full);

struct ArrCtl : public arr2::ArrangeTaskBase::Ctl
{
    Job::Ctl &parent_ctl;
    int total;
    const std::string &msg;

    ArrCtl(Job::Ctl &ctl, int cnt, const std::string &m)
        : parent_ctl{ctl}, total{cnt}, msg{m}
    {}

    bool was_canceled() const override
    {
        return parent_ctl.was_canceled();
    }

    void update_status(int remaining) override
    {
        if (remaining > 0)
            parent_ctl.update_status((total - remaining) * 100 / total, msg);
    }
};

template<class ArrangeTaskT>
class ArrangeJob_ : public Job
{
public:
    using ResultType =
        typename decltype(std::declval<ArrangeTaskT>().process_native(
            std::declval<arr2::ArrangeTaskCtl>()))::element_type;

    // All callbacks are called in the main thread.
    struct Callbacks {
        // Task is prepared but not no processing has been initiated
        std::function<void(ArrangeTaskT &)> on_prepared;

        // Task has been completed but the result is not yet written (inside finalize)
        std::function<void(ArrangeTaskT &)> on_processed;

        // Task result has been written
        std::function<void(ResultType &)> on_finished;
    };

private:
    arr2::Scene m_scene;
    std::unique_ptr<ArrangeTaskT> m_task;
    std::unique_ptr<ResultType>   m_result;
    Callbacks  m_cbs;
    std::string m_task_msg;

public:
    void process(Ctl &ctl) override
    {
        ctl.call_on_main_thread([this]{
               m_task = ArrangeTaskT::create(m_scene);
               m_result.reset();
               if (m_task && m_cbs.on_prepared)
                   m_cbs.on_prepared(*m_task);
           }).wait();

        if (!m_task)
            return;

        auto count = m_task->item_count_to_process();

        if (count == 0) // Should be taken care of by plater, but doesn't hurt
            return;

        ctl.update_status(0, m_task_msg);

        auto taskctl = ArrCtl{ctl, count, m_task_msg};
        m_result = m_task->process_native(taskctl);

        ctl.update_status(100, m_task_msg);
    }

    void finalize(bool canceled, std::exception_ptr &eptr) override
    {
        if (canceled || eptr || !m_result)
            return;

        if (m_task && m_cbs.on_processed)
            m_cbs.on_processed(*m_task);

        m_result->apply_on(m_scene.model());

        if (m_task && m_cbs.on_finished)
            m_cbs.on_finished(*m_result);
    }

    explicit ArrangeJob_(arr2::Scene &&scene,
                         std::string task_msg,
                         const Callbacks &cbs = {})
        : m_scene{std::move(scene)}, m_cbs{cbs}, m_task_msg{std::move(task_msg)}
    {}
};

class ArrangeJob2: public ArrangeJob_<arr2::ArrangeTask<arr2::ArrangeItem>>
{
    using Base = ArrangeJob_<arr2::ArrangeTask<arr2::ArrangeItem>>;
public:
    ArrangeJob2(arr2::Scene &&scene, const Callbacks &cbs = {});
};

class FillBedJob2: public ArrangeJob_<arr2::FillBedTask<arr2::ArrangeItem>>
{
    using Base =  ArrangeJob_<arr2::FillBedTask<arr2::ArrangeItem>>;
public:
    FillBedJob2(arr2::Scene &&scene, const Callbacks &cbs = {});
};

} // namespace GUI
} // namespace Slic3r

#endif // ARRANGEJOB2_HPP
