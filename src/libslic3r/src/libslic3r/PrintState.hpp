#pragma once

#include <array>
#include <cassert>
#include <cstdint>
#include <mutex>

namespace Slic3r {

// State of one print / print object milestone (step).
enum class PrintStepState : uint8_t
{
    // The step has no data, either it was never executed or its stale data
    // was already cleaned up. May transit to Started.
    Fresh,
    // The step is being executed by the worker thread. May transit to Done
    // with valid data, or to Dirty when it gets canceled or invalidated.
    Started,
    // The step finished successfully and its data is valid. May transit
    // to Dirty when it gets invalidated, or to Started.
    Done,
    // The step has stale data awaiting the cleanup, either because it was
    // canceled while running or its finished data got invalidated.
    // May transit to Fresh when the data is cleaned up, or to Started.
    Dirty,
};

// Tracks which steps were done and which steps are enabled for the processing.
// To be instantiated over FDMPrintStep or FDMPrintObjectStep enums.
template <class StepType, size_t COUNT>
class PrintState
{
public:
    PrintState()
    {
        m_step_enabled.fill(true);
    }

    bool is_done(StepType step, std::mutex& mtx) const
    {
        std::scoped_lock<std::mutex> lock(mtx);
        return m_step_states[step] == PrintStepState::Done;
    }

    bool is_started_unguarded(StepType step) const
    {
        return m_step_states[step] == PrintStepState::Started;
    }

    bool is_done_unguarded(StepType step) const
    {
        return m_step_states[step] == PrintStepState::Done;
    }

    void enable_unguarded(StepType step, bool enable)
    {
        m_step_enabled[step] = enable;
    }

    void enable_all_unguarded(bool enable)
    {
        m_step_enabled.fill(enable);
    }

    bool is_enabled_unguarded(StepType step) const
    {
        return m_step_enabled[step];
    }

    // Set the step as started. Block on mutex while the Print / PrintObject / PrintRegion objects are being
    // modified by the UI thread.
    // This is necessary to block until the Print::apply() updates its state, which may
    // influence the processing step being entered.
    // Returns false if the step is not enabled or if the step has already been finished (it is done).
    template <typename ThrowIfCanceled>
    bool set_started(StepType step, std::mutex& mtx, ThrowIfCanceled throw_if_canceled)
    {
        std::scoped_lock<std::mutex> lock(mtx);
        // If canceled, throw before changing the step state.
        throw_if_canceled();

        if (!m_step_enabled[step] || m_step_states[step] == PrintStepState::Done) {
            return false;
        }
        m_step_states[step] = PrintStepState::Started;
        return true;
    }

    // Set the step as done. Block on mutex while the Print / PrintObject / PrintRegion objects are being
    // modified by the UI thread.
    template <typename ThrowIfCanceled>
    void set_done(StepType step, std::mutex& mtx, ThrowIfCanceled throw_if_canceled)
    {
        std::scoped_lock<std::mutex> lock(mtx);
        // If canceled, throw before changing the step state.
        throw_if_canceled();
        assert(m_step_states[step] == PrintStepState::Started);
        m_step_states[step] = PrintStepState::Done;
    }

    // Make the step invalid.
    // PrintBase::m_state_mutex should be locked at this point, guarding access to the states.
    bool invalidate(StepType step)
    {
        return try_invalidate(m_step_states[step]);
    }

    template <typename StepTypeIterator>
    bool invalidate_multiple(StepTypeIterator step_begin, StepTypeIterator step_end)
    {
        bool invalidated = false;
        for (StepTypeIterator it = step_begin; it != step_end; ++it) {
            if (try_invalidate(m_step_states[*it])) {
                invalidated = true;
            }
        }
        return invalidated;
    }

    // Make all steps invalid.
    // PrintBase::m_state_mutex should be locked at this point, guarding access to the states.
    bool invalidate_all()
    {
        bool invalidated = false;
        for (PrintStepState& step_state : m_step_states) {
            if (try_invalidate(step_state)) {
                invalidated = true;
            }
        }
        return invalidated;
    }

    // If the step is Dirty, return true and turn its state to Fresh.
    // The caller is responsible for releasing the data of the step that is no more valid.
    bool query_reset_dirty_unguarded(StepType step)
    {
        if (m_step_states[step] == PrintStepState::Dirty) {
            m_step_states[step] = PrintStepState::Fresh;
            return true;
        }
        return false;
    }

    // To be called after the background thread was stopped by the user pressing the Cancel button,
    // which in turn stops the background thread without adjusting state of the step being executed.
    // This method fixes the state of the canceled step by setting it to the Dirty state.
    void mark_canceled_unguarded()
    {
        for (PrintStepState& step_state : m_step_states) {
            if (step_state == PrintStepState::Started) {
                step_state = PrintStepState::Dirty;
            }
        }
    }

private:
    // A Started step holds partial data and a Done step holds data that is no more
    // valid, so both turn Dirty and their data awaits the cleanup.
    static bool try_invalidate(PrintStepState& step_state)
    {
        const bool invalidated =
            step_state == PrintStepState::Started || step_state == PrintStepState::Done;
        if (invalidated) {
            step_state = PrintStepState::Dirty;
        }
        return invalidated;
    }

    // Both arrays are indexed by StepType.
    std::array<PrintStepState, COUNT> m_step_states{};
    std::array<bool, COUNT> m_step_enabled;
};

} // namespace Slic3r
