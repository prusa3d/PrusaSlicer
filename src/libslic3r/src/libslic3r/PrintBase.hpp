#ifndef slic3r_PrintBase_hpp_
#define slic3r_PrintBase_hpp_

#include <algorithm>
#include <set>
#include <vector>
#include <string>
#include <functional>
#include <mutex>

#include <jthread/JThread.hpp>

#include "Slic3r/Domain/BedInstance.hpp"
#include "Slic3r/Domain/ConfigPack.hpp"
#include "Slic3r/Domain/ObjectID.hpp"
#include "Slic3r/Domain/Preset/SelectedPreset.hpp"
#include "Slic3r/Domain/Preset/HwConfig.hpp"
#include "Slic3r/Biz/Parser/PlaceholderParser.hpp"
#include "Slic3r/Domain/PrintStatistics.hpp"
#include "Slic3r/Domain/SlicingId.hpp"
#include "Slic3r/Domain/SLA/PrintStatistics.hpp"

#include "libslic3r/IPrint.hpp"
#include "libslic3r/PrintState.hpp"
#include "libslic3r/SlicingStatus.hpp"
#include "libslic3r/CanceledException.hpp"
#include "libslic3r/IThumbnailImageGenerator.hpp"
#include "libslic3r/SerializedConfig.hpp"
#include "libslic3r/WipeTowerGeometry.hpp"

namespace Slic3r {
class PrintConfigView;

class PrintBase;

class PrintObjectBase : public Domain::ObjectBase
{
public:
    const Domain::ModelObject* model_object() const    { return m_model_object; }
    Domain::ModelObject*       model_object()          { return m_model_object; }

protected:
    PrintObjectBase(Domain::ModelObject *model_object) : m_model_object(model_object) {}
    virtual ~PrintObjectBase() {}
    // Declared here to allow access from PrintBase through friendship.
	static std::mutex&                  state_mutex(PrintBase *print);

    Domain::ModelObject                *m_model_object;
};

// Wrapper around the private PrintBase.throw_if_canceled(), so that a cancellation object could be passed
// to a non-friend of PrintBase by a PrintBase derived object.
class PrintTryCancel
{
public:
    // calls print.throw_if_canceled().
    void operator()() const;
private:
    friend PrintBase;
    PrintTryCancel() = delete;
    PrintTryCancel(const PrintBase *print) : m_print(print) {}
    const PrintBase *m_print;
};

/**
 * @brief Printing involves slicing and export of device dependent instructions.
 *
 * Every technology has a potentially different set of requirements for
 * slicing, support structures and output print instructions. The pipeline
 * however remains roughly the same:
 *      slice -> convert to instructions -> send to printer
 *
 * The PrintBase class will abstract this flow for different technologies.
 *
 */
class PrintBase : public Domain::ObjectBase, public Biz::Slicing::IPrint
{
public:
	PrintBase() = default;
    inline virtual ~PrintBase() {}

    virtual Domain::PrinterTechnology technology() const noexcept = 0;

    // Reset the print status including the copy of the Model / ModelObject hierarchy.
    virtual void            clear() = 0;
    // List of existing PrintObject IDs, to remove notifications for non-existent IDs.
    virtual std::vector<Domain::ObjectID> print_object_ids() const = 0;

    const Domain::Model&                  model() const { return m_model; }
    std::optional<Domain::ModelWipeTower> wipe_tower() const {
        return m_wipe_tower;
    }

    std::optional<std::reference_wrapper<const Domain::CustomGCode::Info>> custom_gcode() const {
        return m_custom_gcode;
    }

    struct TaskParams {
		TaskParams() : single_model_object(0), single_model_instance_only(false), to_object_step(-1), to_print_step(-1) {}
        // If non-empty, limit the processing to this ModelObject.
        Domain::ObjectID        single_model_object;
		// If set, only process single_model_object. Otherwise process everything, but single_model_object first.
		bool					single_model_instance_only;
        // If non-negative, stop processing at the successive object step.
        int                     to_object_step;
        // If non-negative, stop processing at the successive print step.
        int                     to_print_step;
    };
    // After calling the apply() function, call set_task() to limit the task to be processed by process().
    virtual void            set_task(const TaskParams &params) = 0;
    // Perform the calculation. This is the only method that is to be called at a worker thread.
    virtual void process() = 0;
    // Clean up after process() finished, either with success, error or if canceled.
    // The adjustments on the Print / PrintObject data due to set_task() are to be reverted here.
    virtual void            finalize() = 0;
    // Clean up print step / print object step data after
    // 1) some print step / print object step was invalidated inside PrintBase::apply() while holding the milestone mutex locked.
    // 2) background thread finished being canceled.
    virtual void            cleanup() = 0;

    // Calls a registered callback to update the status.
    void                    set_status(Domain::Percentage percent, Biz::Slicing::ProgressInfo message) {
        progress_callback(Biz::Slicing::Progress{percent, message});
    }

    // Has the calculation been canceled?
	bool                       canceled() const { return stop_token.stop_requested(); }
    // Returns true if the last step was finished with success.
    virtual bool               finished() const = 0;

    const Biz::Parser::PlaceholderParser& placeholder_parser() const {
        ASSERT(m_placeholder_parser, "Placeholder parser must be initialized before usage!");
        return *m_placeholder_parser;
    }

protected:
	friend class PrintObjectBase;

    std::mutex&            state_mutex() const { return m_state_mutex; }

    // If the background processing stop was requested, throw CanceledException.
    // To be called by the worker thread and its sub-threads (mostly launched on the TBB thread pool) regularly.
    void                   throw_if_canceled() const {
        if (this->canceled()) {
            throw Biz::Slicing::CanceledException();
        }
    }
    // Wrapper around this->throw_if_canceled(), so that throw_if_canceled() may be passed to a function without making throw_if_canceled() public.
    PrintTryCancel         make_try_cancel() const { return PrintTryCancel(this); }

    // To be called by this->output_filename() with the format string pulled from the configuration layer.
    std::string            output_filename(const std::string &format, const std::string &default_ext, const std::string &filename_base, const Biz::Parser::IO::Config *config_override = nullptr) const;
    // Update "scale", "input_filename", "input_filename_base" placeholders from the current printable ModelObjects.
    Biz::Parser::IO::Config get_object_placeholders() const;

    Domain::Model                           m_model;
    std::optional<Domain::ModelWipeTower>   m_wipe_tower;
    std::optional<Domain::CustomGCode::Info>m_custom_gcode;

    std::optional<Biz::Parser::PlaceholderParser>        m_placeholder_parser;

private:
    // Mutex used for synchronization of the worker thread with the UI thread:
    // The mutex will be used to guard the worker thread against entering a stage
    // while the data influencing the stage is modified.
    mutable std::mutex                      m_state_mutex;

    friend PrintTryCancel;
};

template<typename PrintStepEnumType, const size_t COUNT>
class PrintBaseWithState : public PrintBase
{
public:
    using                           PrintStepEnum       = PrintStepEnumType;
    static constexpr const size_t   PrintStepEnumSize   = COUNT;

    PrintBaseWithState() = default;

    bool            is_step_done(PrintStepEnum step) const { return m_state.is_done(step, this->state_mutex()); }

protected:
    bool            set_started(PrintStepEnum step) { return m_state.set_started(step, this->state_mutex(), [this](){ this->throw_if_canceled(); }); }
	void            set_done(PrintStepEnum step) {
		m_state.set_done(step, this->state_mutex(), [this](){ this->throw_if_canceled(); });
	}
    bool            invalidate_step(PrintStepEnum step)
		{ return m_state.invalidate(step); }
    template<typename StepTypeIterator>
    bool            invalidate_steps(StepTypeIterator step_begin, StepTypeIterator step_end)
        { return m_state.invalidate_multiple(step_begin, step_end); }
    bool            invalidate_steps(std::initializer_list<PrintStepEnum> il)
        { return m_state.invalidate_multiple(il.begin(), il.end()); }
    bool            invalidate_all_steps()
        { return m_state.invalidate_all(); }

	bool            is_step_started_unguarded(PrintStepEnum step) const { return m_state.is_started_unguarded(step); }
	bool            is_step_done_unguarded(PrintStepEnum step) const { return m_state.is_done_unguarded(step); }

    // After calling the apply() function, set_task() may be called to limit the task to be processed by process().
    template<typename PrintObject>
    void set_task_impl(const TaskParams &params, std::vector<PrintObject*> &print_objects)
    {
        static constexpr const auto PrintObjectStepEnumSize = int(PrintObject::PrintObjectStepEnumSize);
        using                       PrintObjectStepEnum     = typename PrintObject::PrintObjectStepEnum;
        // Grab the lock for the Print / PrintObject milestones.
        std::scoped_lock<std::mutex> lock(this->state_mutex());

        int n_object_steps = int(params.to_object_step) + 1;
        if (n_object_steps == 0)
            n_object_steps = PrintObjectStepEnumSize;

        if (params.single_model_object.valid()) {
            // Find the print object to be processed with priority.
            PrintObject *print_object = nullptr;
            size_t       idx_print_object = 0;
            for (; idx_print_object < print_objects.size(); ++ idx_print_object)
                if (print_objects[idx_print_object]->model_object()->id() == params.single_model_object) {
                    print_object = print_objects[idx_print_object];
                    break;
                }
            assert(print_object != nullptr);
            // Find out whether the priority print object is being currently processed.
            bool running = false;
            for (int istep = 0; istep < n_object_steps; ++ istep) {
                if (! print_object->is_step_enabled_unguarded(PrintObjectStepEnum(istep)))
                    // Step was skipped.
                    break;
                if (print_object->is_step_started_unguarded(PrintObjectStepEnum(istep))) {
                    // No step was skipped, and a wanted step is being processed.
                    running = true;
                    break;
                }
            }

            if (params.single_model_instance_only) {
                // Suppress all the steps of other instances.
                for (PrintObject *po : print_objects)
                    for (size_t istep = 0; istep < PrintObjectStepEnumSize; ++ istep)
                        po->enable_step_unguarded(PrintObjectStepEnum(istep), false);
            } else if (! running) {
                // Swap the print objects, so that the selected print_object is first in the row.
                // At this point the background processing must be stopped, so it is safe to shuffle print objects.
                if (idx_print_object != 0)
                    std::swap(print_objects.front(), print_objects[idx_print_object]);
            }
            // and set the steps for the current object.
            for (int istep = 0; istep < n_object_steps; ++ istep)
                print_object->enable_step_unguarded(PrintObjectStepEnum(istep), true);
            for (int istep = n_object_steps; istep < PrintObjectStepEnumSize; ++ istep)
                print_object->enable_step_unguarded(PrintObjectStepEnum(istep), false);
        } else {
            // Slicing all objects.
            for (PrintObject *po : print_objects) {
                for (int istep = 0; istep < n_object_steps; ++ istep)
                    po->enable_step_unguarded(PrintObjectStepEnum(istep), true);
                for (int istep = n_object_steps; istep < PrintObjectStepEnumSize; ++ istep)
                    po->enable_step_unguarded(PrintObjectStepEnum(istep), false);
            }
        }

        if (params.to_object_step != -1 || params.to_print_step != -1) {
            // Limit the print steps.
            size_t istep = (params.to_object_step != -1) ? 0 : size_t(params.to_print_step) + 1;
            for (; istep < PrintStepEnumSize; ++ istep)
                m_state.enable_unguarded(PrintStepEnum(istep), false);
        }
    }

    /**
     * @brief Limits the processing to the given model object and stops after the given print object step.
     *
     * @return False when the model object has no print object.
     */
    template <typename PrintObject>
    bool set_task_until_object_step_impl(
        const int to_object_step,
        const Domain::ObjectID model_object_id,
        std::vector<PrintObject*>& print_objects
    )
    {
        const bool model_object_found = std::any_of(
            print_objects.begin(),
            print_objects.end(),
            [&model_object_id](const PrintObject* print_object)
            { return print_object->model_object()->id() == model_object_id; }
        );
        if (!model_object_found) {
            return false;
        }

        TaskParams task;
        task.single_model_object        = model_object_id;
        task.single_model_instance_only = true;
        task.to_object_step             = to_object_step;
        this->set_task_impl(task, print_objects);

        return true;
    }

    // Clean up after process() finished, either with success, error or if canceled.
    // The adjustments on the Print / PrintObject m_stepmask data due to set_task() are to be reverted here:
    // Execution of all milestones is enabled in case some of them were suppressed for the last background execution.
    // Also if the background processing was canceled, the current milestone that was just abandoned 
    // in Started state is to be reset to Canceled state.
    template<typename PrintObject>
    void finalize_impl(std::vector<PrintObject*> &print_objects)
    {
        // Grab the lock for the Print / PrintObject milestones.
        std::scoped_lock<std::mutex> lock(this->state_mutex());
        for (auto *po : print_objects)
            po->finalize_impl();
        m_state.enable_all_unguarded(true);
        m_state.mark_canceled_unguarded();
    }

private:
    PrintState<PrintStepEnum, COUNT>    m_state;
};

template<typename PrintType, typename PrintObjectStepEnumType, const size_t COUNT>
class PrintObjectBaseWithState : public PrintObjectBase
{
public:
    using                           PrintObjectStepEnum       = PrintObjectStepEnumType;
    static constexpr const size_t   PrintObjectStepEnumSize   = COUNT;

    PrintType*       print()         { return m_print; }
    const PrintType* print() const   { return m_print; }

    typedef PrintState<PrintObjectStepEnum, COUNT> PrintObjectState;
    bool            is_step_done(PrintObjectStepEnum step) const { return m_state.is_done(step, PrintObjectBase::state_mutex(m_print)); }

    bool all_steps_done() const {
        return is_step_done(PrintObjectStepEnum(int(COUNT) - 1));
    }

    auto last_completed_step() const
    {
        static_assert(COUNT > 0, "Step count should be > 0");
        auto s = int(COUNT) - 1;

        std::lock_guard lk(state_mutex(m_print));
        while (s >= 0 && ! is_step_done_unguarded(PrintObjectStepEnum(s)))
            --s;

        if (s < 0)
            s = COUNT;

        return PrintObjectStepEnum(s);
    }

protected:
	PrintObjectBaseWithState(PrintType *print, Domain::ModelObject *model_object) : PrintObjectBase(model_object), m_print(print) {}

    bool            set_started(PrintObjectStepEnum step)
        { return m_state.set_started(step, PrintObjectBase::state_mutex(m_print), [this](){ this->throw_if_canceled(); }); }
	void            set_done(PrintObjectStepEnum step) {
		m_state.set_done(step, PrintObjectBase::state_mutex(m_print), [this](){ this->throw_if_canceled(); });
	}

    bool            invalidate_step(PrintObjectStepEnum step)
        { return m_state.invalidate(step); }
    template<typename StepTypeIterator>
    bool            invalidate_steps(StepTypeIterator step_begin, StepTypeIterator step_end)
        { return m_state.invalidate_multiple(step_begin, step_end); }
    bool            invalidate_steps(std::initializer_list<PrintObjectStepEnum> il)
        { return m_state.invalidate_multiple(il.begin(), il.end()); }
    bool            invalidate_all_steps()
        { return m_state.invalidate_all(); }

    bool            is_step_started_unguarded(PrintObjectStepEnum step) const { return m_state.is_started_unguarded(step); }
    bool            is_step_done_unguarded(PrintObjectStepEnum step) const { return m_state.is_done_unguarded(step); }

    bool            is_step_enabled_unguarded(PrintObjectStepEnum step) const { return m_state.is_enabled_unguarded(step); }
    void            enable_step_unguarded(PrintObjectStepEnum step, bool enable) { m_state.enable_unguarded(step, enable); }
    void            enable_all_steps_unguarded(bool enable) { m_state.enable_all_unguarded(enable); }
    // See the comment at PrintBaseWithState::finalize_impl()
    void            finalize_impl() { m_state.enable_all_unguarded(true); m_state.mark_canceled_unguarded(); }
    // If the milestone is Canceled or Invalidated, return true and turn the state of the milestone to Fresh.
    // The caller is responsible for releasing the data of the milestone that is no more valid.
    bool            query_reset_dirty_step_unguarded(PrintObjectStepEnum step) { return m_state.query_reset_dirty_unguarded(step); }

protected:
    // If the background processing stop was requested, throw CanceledException.
    // To be called by the worker thread and its sub-threads (mostly launched on the TBB thread pool) regularly.
    void            throw_if_canceled() { if (m_print->canceled()) throw Biz::Slicing::CanceledException(); }

    friend PrintType;
    PrintType                                *m_print;

private:
    PrintState<PrintObjectStepEnum, COUNT>    m_state;
};

} // namespace Slic3r

#endif /* slic3r_PrintBase_hpp_ */
