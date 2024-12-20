///|/ Copyright (c) Prusa Research 2018 - 2023 Lukáš Matěna @lukasmatena, Vojtěch Bubník @bubnikv, Tomáš Mészáros @tamasmeszaros, Pavel Mikuš @Godrak, Roman Beránek @zavorka
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_PrintBase_hpp_
#define slic3r_PrintBase_hpp_

#include "libslic3r.h"
#include <set>
#include <vector>
#include <string>
#include <functional>
#include <atomic>
#include <mutex>

#include "ObjectID.hpp"
#include "Model.hpp"
#include "PlaceholderParser.hpp"
#include "PrintConfig.hpp"

namespace Slic3r {

class CanceledException : public std::exception {
public:
   const char* what() const throw() { return "Background processing has been canceled"; }
};

class PrintStateBase {
public:
    enum class State {
        // Fresh state, either the object is new or the data of that particular milestone was cleaned up.
        // Fresh state may transit to Started.
        Fresh,
        // Milestone was started and now it is being executed.
        // Started state may transit to Canceled with invalid data or Done with valid data.
        Started,
        // Milestone was being executed, but now it is canceled and not yet cleaned up.
        // Canceled state may transit to Fresh state if its invalid data is cleaned up
        // or to Started state.
        // Canceled and Invalidated states are of similar nature: Canceled step was Started but canceled,
        // while Invalidated state was Done but invalidated.
        Canceled,
        // Milestone was finished successfully, it's data is now valid.
        // Done state may transit to Invalidated state if its data is no more valid
        // or to a Started state.
        Done,
        // Milestone was finished successfully (done), but now it is invalidated and it's data is no more valid.
        // Invalidated state may transit to Fresh if its invalid data is cleaned up,
        // or to state Started.
        // Canceled and Invalidated states are of similar nature: Canceled step was Started but canceled,
        // while Invalidated state was Done but invalidated.
        Invalidated,
    };

    enum class WarningLevel {
        NON_CRITICAL,
        CRITICAL
    };

    typedef size_t TimeStamp;

    // A new unique timestamp is being assigned to the step every time the step changes its state.
    struct StateWithTimeStamp
    {
        State       state { State::Fresh };
        TimeStamp   timestamp { 0 };
        bool        enabled { true };

        bool        is_done() const { return state == State::Done; }
        // The milestone may have some data available, but it is no more valid and it should be cleaned up to conserve memory.
        bool        is_dirty() const { return state == State::Canceled || state == State::Invalidated; }

        // If the milestone is Started or Done, invalidate it:
        // Turn Started to Canceled, turn Done to Invalidated.
        // Update timestamp of this milestone.
        bool        try_invalidate() {
            bool invalidated = this->state == State::Started || this->state == State::Done;
            if (invalidated) {
                this->state = this->state == State::Started ? State::Canceled : State::Invalidated;
                this->timestamp = ++ g_last_timestamp;
            }
            return invalidated;
        }
    };

    struct Warning
    {
    	// Critical warnings will be displayed on G-code export in a modal dialog, so that the user cannot miss them.
        WarningLevel    level;
        // If the warning is not current, then it is in an unknown state. It may or may not be valid.
        // A current warning will become non-current if its milestone gets invalidated.
        // A non-current warning will either become current or it will be removed at the end of a milestone.
        bool 			current;
        // Message to be shown to the user, UTF8, localized.
        std::string     message;
        // If message_id == 0, then the message is expected to identify the warning uniquely.
        // Otherwise message_id identifies the message. For example, if the message contains a varying number, then
        // it cannot itself identify the message type.
        int 			message_id;
    };

    struct StateWithWarnings : public StateWithTimeStamp
    {
    	void 	mark_warnings_non_current() { for (auto &w : warnings) w.current = false; }
        std::vector<Warning>    warnings;
    };

protected:
    //FIXME last timestamp is shared between Print & SLAPrint,
    // and if multiple Print or SLAPrint instances are executed in parallel, modification of g_last_timestamp
    // is not synchronized!
    static size_t g_last_timestamp;
};

// To be instantiated over PrintStep or PrintObjectStep enums.
template <class StepType, size_t COUNT>
class PrintState : public PrintStateBase
{
public:
    PrintState() {}

    StateWithTimeStamp state_with_timestamp(StepType step, std::mutex &mtx) const {
        std::scoped_lock<std::mutex> lock(mtx);
        StateWithTimeStamp state = m_state[step];
        return state;
    }

    StateWithWarnings state_with_warnings(StepType step, std::mutex &mtx) const {
        std::scoped_lock<std::mutex> lock(mtx);
        StateWithWarnings state = m_state[step];
        return state;
    }

    bool is_started(StepType step, std::mutex &mtx) const {
        return this->state_with_timestamp(step, mtx).state == State::Started;
    }

    bool is_done(StepType step, std::mutex &mtx) const {
        return this->state_with_timestamp(step, mtx).state == State::Done;
    }

    StateWithTimeStamp state_with_timestamp_unguarded(StepType step) const { 
        return m_state[step];
    }

    bool is_started_unguarded(StepType step) const {
        return this->state_with_timestamp_unguarded(step).state == State::Started;
    }

    bool is_done_unguarded(StepType step) const {
        return this->state_with_timestamp_unguarded(step).state == State::Done;
    }

    void enable_unguarded(StepType step, bool enable) {
        m_state[step].enabled = enable;
    }

    void enable_all_unguarded(bool enable) {
        for (size_t istep = 0; istep < COUNT; ++ istep)
            m_state[istep].enabled = enable;
    }

    bool is_enabled_unguarded(StepType step) const {
        return this->state_with_timestamp_unguarded(step).enabled;
    }

    // Set the step as started. Block on mutex while the Print / PrintObject / PrintRegion objects are being
    // modified by the UI thread.
    // This is necessary to block until the Print::apply() updates its state, which may
    // influence the processing step being entered.
    // Returns false if the step is not enabled or if the step has already been finished (it is done).
    template<typename ThrowIfCanceled>
    bool set_started(StepType step, std::mutex &mtx, ThrowIfCanceled throw_if_canceled) {
        std::scoped_lock<std::mutex> lock(mtx);
        // If canceled, throw before changing the step state.
        throw_if_canceled();
#ifndef NDEBUG
// The following test is not necessarily valid after the background processing thread
// is stopped with throw_if_canceled(), as the CanceledException is not being catched
// by the Print or PrintObject to update m_step_active or m_state[...].state.
// This should not be a problem as long as the caller calls set_started() / set_done() /
// active_step_add_warning() consistently. From the robustness point of view it would be
// be better to catch CanceledException and do the updates. From the performance point of view,
// the current implementation is optimal.
//
//        assert(m_step_active == -1);
//        for (int i = 0; i < int(COUNT); ++ i)
//            assert(m_state[i].state != State::Started);
#endif // NDEBUG
        PrintStateBase::StateWithWarnings &state = m_state[step];
        if (! state.enabled || state.state == State::Done)
            return false;
        state.state = State::Started;
        state.timestamp = ++ g_last_timestamp;
        state.mark_warnings_non_current();
        m_step_active = static_cast<int>(step);
        return true;
    }

    // Set the step as done. Block on mutex while the Print / PrintObject / PrintRegion objects are being
    // modified by the UI thread.
    // Return value:
    // 		Timestamp when this step entered the Done state.
    // 		bool indicates whether the UI has to update the slicing warnings of this step or not.
	template<typename ThrowIfCanceled>
	std::pair<TimeStamp, bool> set_done(StepType step, std::mutex &mtx, ThrowIfCanceled throw_if_canceled) {
        std::scoped_lock<std::mutex> lock(mtx);
        // If canceled, throw before changing the step state.
        throw_if_canceled();
        assert(m_state[step].state == State::Started);
        assert(m_step_active == static_cast<int>(step));
        PrintStateBase::StateWithWarnings &state = m_state[step];
        state.state = State::Done;
        state.timestamp = ++ g_last_timestamp;
        m_step_active = -1;
        // Remove all non-current warnings.
    	auto it = std::remove_if(state.warnings.begin(), state.warnings.end(), [](const auto &w) { return ! w.current; });
    	bool update_warning_ui = false;
        if (it != state.warnings.end()) {
        	state.warnings.erase(it, state.warnings.end());
        	update_warning_ui = true;
        }
        return std::make_pair(state.timestamp, update_warning_ui);
    }

    // Make the step invalid.
    // PrintBase::m_state_mutex should be locked at this point, guarding access to m_state.
    // In case the step has already been entered or finished, cancel the background
    // processing by calling the cancel callback.
    template<typename CancelationCallback>
    bool invalidate(StepType step, CancelationCallback cancel) {
        if (PrintStateBase::StateWithWarnings &state = m_state[step]; state.try_invalidate()) {
#if 0
            if (mtx.state != mtx.HELD) {
                printf("Not held!\n");
            }
#endif
            // Raise the mutex, so that the following cancel() callback could cancel
            // the background processing.
            // Internally the cancel() callback shall unlock the PrintBase::m_status_mutex to let
            // the working thread proceed.
            cancel();
            // Now the worker thread should be stopped, therefore it cannot write into the warnings field.
            // It is safe to modify it.
            state.mark_warnings_non_current();
            m_step_active = -1;
            return true;
        } else
            return false;
    }

    template<typename CancelationCallback, typename StepTypeIterator>
    bool invalidate_multiple(StepTypeIterator step_begin, StepTypeIterator step_end, CancelationCallback cancel) {
        bool invalidated = false;
        for (StepTypeIterator it = step_begin; it != step_end; ++ it)
            if (m_state[*it].try_invalidate())
                invalidated = true;
        if (invalidated) {
#if 0
            if (mtx.state != mtx.HELD) {
                printf("Not held!\n");
            }
#endif
            // Raise the mutex, so that the following cancel() callback could cancel
            // the background processing.
            // Internally the cancel() callback shall unlock the PrintBase::m_status_mutex to let
            // the working thread to proceed.
            cancel();
            // Now the worker thread should be stopped, therefore it cannot write into the warnings field.
            // It is safe to modify the warnings.
            for (StepTypeIterator it = step_begin; it != step_end; ++ it)
                m_state[*it].mark_warnings_non_current();
            m_step_active = -1;
        }
        return invalidated;
    }

    // Make all steps invalid.
    // PrintBase::m_state_mutex should be locked at this point, guarding access to m_state.
    // In case any step has already been entered or finished, cancel the background
    // processing by calling the cancel callback.
    template<typename CancelationCallback>
    bool invalidate_all(CancelationCallback cancel) {
        bool invalidated = false;
        for (size_t i = 0; i < COUNT; ++ i)
            if (m_state[i].try_invalidate())
                invalidated = true;
        if (invalidated) {
            cancel();
            // Now the worker thread should be stopped, therefore it cannot write into the warnings field.
            // It is safe to modify the warnings.
            for (size_t i = 0; i < COUNT; ++ i)
                m_state[i].mark_warnings_non_current();
            m_step_active = -1;
        }
        return invalidated;
    }

    // If the milestone is Canceled or Invalidated, return true and turn the state of the milestone to Fresh.
    // The caller is responsible for releasing the data of the milestone that is no more valid.
    bool query_reset_dirty_unguarded(StepType step) {
        if (PrintStateBase::StateWithWarnings &state = m_state[step]; state.is_dirty()) {
            state.state = State::Fresh;
            return true;
        } else
            return false;
    }

    // To be called after the background thread was stopped by the user pressing the Cancel button,
    // which in turn stops the background thread without adjusting state of the milestone being executed.
    // This method fixes the state of the canceled milestone by setting it to a Canceled state.
    void mark_canceled_unguarded() {
        for (size_t i = 0; i < COUNT; ++ i) {
            if (State &state = m_state[i].state; state == State::Started)
                state = State::Canceled;
        }
    }

    // Update list of warnings of the current milestone with a new warning.
    // The warning may already exist in the list, marked as current or not current.
    // If it already exists, mark it as current.
    // Return value:
    // 		Current milestone (StepType).
    // 		bool indicates whether the UI has to be updated or not.
    std::pair<StepType, bool> active_step_add_warning(PrintStateBase::WarningLevel warning_level, const std::string &message, int message_id, std::mutex &mtx)
    {
        std::scoped_lock<std::mutex> lock(mtx);
        assert(m_step_active != -1);
        StateWithWarnings &state = m_state[m_step_active];
        assert(state.state == State::Started);
        std::pair<StepType, bool> retval(static_cast<StepType>(m_step_active), true);
        // Does a warning of the same level and message or message_id exist already?
		auto it = (message_id == 0) ? 
            std::find_if(state.warnings.begin(), state.warnings.end(), [&message](const auto &w) { return w.message_id == 0 && w.message == message; }) :
            std::find_if(state.warnings.begin(), state.warnings.end(), [message_id](const auto& w) { return w.message_id == message_id; });
    	if (it == state.warnings.end())
    		// No, create a new warning and update UI.
        	state.warnings.emplace_back(PrintStateBase::Warning{ warning_level, true, message, message_id });
        else if (it->message != message || it->level != warning_level) {
        	// Yes, however it needs an update.
        	it->message = message;
        	it->level 	= warning_level;
        	it->current = true;
        } else if (it->current)
        	// Yes, and it is current. Don't update UI.
        	retval.second = false;
        else
        	// Yes, but it is not current. Mark it as current.
        	it->current = true;
        return retval;
    }

private:
    StateWithWarnings   m_state[COUNT];
    // Active class StepType or -1 if none is active.
    // If the background processing is canceled, m_step_active may not be resetted
    // to -1, see the comment in this->set_started().
    int                 m_step_active = -1;
};

class PrintBase;

class PrintObjectBase : public ObjectBase
{
public:
    const ModelObject*      model_object() const    { return m_model_object; }
    ModelObject*            model_object()          { return m_model_object; }

protected:
    PrintObjectBase(ModelObject *model_object) : m_model_object(model_object) {}
    virtual ~PrintObjectBase() {}
    // Declared here to allow access from PrintBase through friendship.
	static std::mutex&                  state_mutex(PrintBase *print);
	static std::function<void()>        cancel_callback(PrintBase *print);
	// Notify UI about a new warning of a milestone "step" on this PrintObjectBase.
	// The UI will be notified by calling a status callback registered on print.
	// If no status callback is registered, the message is printed to console.
	void 				   				status_update_warnings(PrintBase *print, int step, PrintStateBase::WarningLevel warning_level, const std::string &message);

    ModelObject                  *m_model_object;
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
class PrintBase : public ObjectBase
{
public:
	PrintBase() : m_placeholder_parser(&m_full_print_config) { this->restart(); }
    inline virtual ~PrintBase() {}

    virtual PrinterTechnology technology() const noexcept = 0;

    // Reset the print status including the copy of the Model / ModelObject hierarchy.
    virtual void            clear() = 0;
    // The Print is empty either after clear() or after apply() over an empty model,
    // or after apply() over a model, where no object is printable (all outside the print volume).
    virtual bool            empty() const = 0;
    // List of existing PrintObject IDs, to remove notifications for non-existent IDs.
    virtual std::vector<ObjectID> print_object_ids() const = 0;

    // Validate the print, return empty string if valid, return error if process() cannot (or should not) be started.
    virtual std::string     validate(std::vector<std::string>* warnings = nullptr) const { return std::string(); }

    enum ApplyStatus {
        // No change after the Print::apply() call.
        APPLY_STATUS_UNCHANGED,
        // Some of the Print / PrintObject / PrintObjectInstance data was changed,
        // but no result was invalidated (only data influencing not yet calculated results were changed).
        APPLY_STATUS_CHANGED,
        // Some data was changed, which in turn invalidated already calculated steps.
        APPLY_STATUS_INVALIDATED,
    };
    virtual ApplyStatus     apply(const Model &model, DynamicPrintConfig config) = 0;
    const Model&            model() const { return m_model; }

    struct TaskParams {
		TaskParams() : single_model_object(0), single_model_instance_only(false), to_object_step(-1), to_print_step(-1) {}
        // If non-empty, limit the processing to this ModelObject.
        ObjectID                single_model_object;
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
    virtual void            process() = 0;
    // Clean up after process() finished, either with success, error or if canceled.
    // The adjustments on the Print / PrintObject data due to set_task() are to be reverted here.
    virtual void            finalize() = 0;
    // Clean up print step / print object step data after
    // 1) some print step / print object step was invalidated inside PrintBase::apply() while holding the milestone mutex locked.
    // 2) background thread finished being canceled.
    virtual void            cleanup() = 0;

    struct SlicingStatus {
		SlicingStatus(int percent, const std::string &text, unsigned int flags = 0) : percent(percent), text(text), flags(flags) {}
        SlicingStatus(const PrintBase &print, int warning_step) : 
            flags(UPDATE_PRINT_STEP_WARNINGS), warning_object_id(print.id()), warning_step(warning_step) {}
        SlicingStatus(const PrintObjectBase &print_object, int warning_step) : 
            flags(UPDATE_PRINT_OBJECT_STEP_WARNINGS), warning_object_id(print_object.id()), warning_step(warning_step) {}
        int             percent { -1 };
        std::string     text;
        // Bitmap of flags.
        enum FlagBits {
            DEFAULT                             = 0,
            RELOAD_SCENE                        = 1 << 1,
            RELOAD_SLA_SUPPORT_POINTS           = 1 << 2,
            RELOAD_SLA_PREVIEW                  = 1 << 3,
            // UPDATE_PRINT_STEP_WARNINGS is mutually exclusive with UPDATE_PRINT_OBJECT_STEP_WARNINGS.
            UPDATE_PRINT_STEP_WARNINGS          = 1 << 4,
            UPDATE_PRINT_OBJECT_STEP_WARNINGS   = 1 << 5
        };
        // Bitmap of FlagBits
        unsigned int    flags;
        // set to an ObjectID of a Print or a PrintObject based on flags
        // (whether UPDATE_PRINT_STEP_WARNINGS or UPDATE_PRINT_OBJECT_STEP_WARNINGS is set).
        ObjectID        warning_object_id;
        // For which Print or PrintObject step a new warning is being issued?
        int             warning_step { -1 };
    };
    typedef std::function<void(const SlicingStatus&)>  status_callback_type;
    // Default status console print out in the form of percent => message.
    void                    set_status_default() { m_status_callback = nullptr; }
    // No status output or callback whatsoever, useful mostly for automatic tests.
    void                    set_status_silent() { m_status_callback = [](const SlicingStatus&){}; }
    // Register a custom status callback.
    void                    set_status_callback(status_callback_type cb) { m_status_callback = cb; }
    // Calls a registered callback to update the status, or print out the default message.
    void                    set_status(int percent, const std::string &message, unsigned int flags = SlicingStatus::DEFAULT) {
		if (m_status_callback) m_status_callback(SlicingStatus(percent, message, flags));
        else { printf("%d => %s\n", percent, message.c_str()); std::fflush(stdout); }
    }

    typedef std::function<void()>  cancel_callback_type;
    // Various methods will call this callback to stop the background processing (the Print::process() call)
    // in case a successive change of the Print / PrintObject / PrintRegion instances changed
    // the state of the finished or running calculations.
    void                       set_cancel_callback(cancel_callback_type cancel_callback) { m_cancel_callback = cancel_callback; }
    // Has the calculation been canceled?
	enum CancelStatus {
		// No cancelation, background processing should run.
		NOT_CANCELED = 0,
		// Canceled by user from the user interface (user pressed the "Cancel" button or user closed the application).
		CANCELED_BY_USER = 1,
		// Canceled internally from Print::apply() through the Print/PrintObject::invalidate_step() or ::invalidate_all_steps().
		CANCELED_INTERNAL = 2
	};
    CancelStatus               cancel_status() const { return m_cancel_status.load(std::memory_order_acquire); }
    // Has the calculation been canceled?
	bool                       canceled() const { return m_cancel_status.load(std::memory_order_acquire) != NOT_CANCELED; }
    // Cancel the running computation. Stop execution of all the background threads.
	void                       cancel() { m_cancel_status = CANCELED_BY_USER; }
	void                       cancel_internal() { m_cancel_status = CANCELED_INTERNAL; }
    // Cancel the running computation. Stop execution of all the background threads.
	void                       restart() { m_cancel_status = NOT_CANCELED; }
    // Returns true if the last step was finished with success.
    virtual bool               finished() const = 0;

    const PlaceholderParser&   placeholder_parser() const { return m_placeholder_parser; }
    const DynamicPrintConfig&  full_print_config() const { return m_full_print_config; }

    virtual std::string        output_filename(const std::string &filename_base = std::string()) const = 0;
    // If the filename_base is set, it is used as the input for the template processing. In that case the path is expected to be the directory (may be empty).
    // If filename_set is empty, than the path may be a file or directory. If it is a file, then the macro will not be processed.
    std::string                output_filepath(const std::string &path, const std::string &filename_base = std::string()) const;

protected:
	friend class PrintObjectBase;
    friend class BackgroundSlicingProcess;

    std::mutex&            state_mutex() const { return m_state_mutex; }
    std::function<void()>  cancel_callback() { return m_cancel_callback; }
	void				   call_cancel_callback() { m_cancel_callback(); }
	// Notify UI about a new warning of a milestone "step" on this PrintBase.
	// The UI will be notified by calling a status callback.
	// If no status callback is registered, the message is printed to console.
    void 				   status_update_warnings(int step, PrintStateBase::WarningLevel warning_level, const std::string &message, const PrintObjectBase* print_object = nullptr);

    // If the background processing stop was requested, throw CanceledException.
    // To be called by the worker thread and its sub-threads (mostly launched on the TBB thread pool) regularly.
    void                   throw_if_canceled() const { if (m_cancel_status.load(std::memory_order_acquire)) throw CanceledException(); }
    // Wrapper around this->throw_if_canceled(), so that throw_if_canceled() may be passed to a function without making throw_if_canceled() public.
    PrintTryCancel         make_try_cancel() const { return PrintTryCancel(this); }

    // To be called by this->output_filename() with the format string pulled from the configuration layer.
    std::string            output_filename(const std::string &format, const std::string &default_ext, const std::string &filename_base, const DynamicConfig *config_override = nullptr) const;
    // Update "scale", "input_filename", "input_filename_base" placeholders from the current printable ModelObjects.
    void                   update_object_placeholders(DynamicConfig &config, const std::string &default_output_ext) const;

	Model                                   m_model;
	DynamicPrintConfig						m_full_print_config;
    PlaceholderParser                       m_placeholder_parser;

    // Callback to be evoked regularly to update state of the UI thread.
    status_callback_type                    m_status_callback;

private:
    std::atomic<CancelStatus>               m_cancel_status;

    // Callback to be evoked to stop the background processing before a state is updated.
    cancel_callback_type                    m_cancel_callback = [](){};

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
	PrintStateBase::StateWithTimeStamp step_state_with_timestamp(PrintStepEnum step) const { return m_state.state_with_timestamp(step, this->state_mutex()); }
    PrintStateBase::StateWithWarnings  step_state_with_warnings(PrintStepEnum step) const { return m_state.state_with_warnings(step, this->state_mutex()); }

protected:
    bool            set_started(PrintStepEnum step) { return m_state.set_started(step, this->state_mutex(), [this](){ this->throw_if_canceled(); }); }
	PrintStateBase::TimeStamp set_done(PrintStepEnum step) { 
		std::pair<PrintStateBase::TimeStamp, bool> status = m_state.set_done(step, this->state_mutex(), [this](){ this->throw_if_canceled(); });
        if (status.second)
            this->status_update_warnings(static_cast<int>(step), PrintStateBase::WarningLevel::NON_CRITICAL, std::string());
        return status.first;
	}
    bool            invalidate_step(PrintStepEnum step)
		{ return m_state.invalidate(step, this->cancel_callback()); }
    template<typename StepTypeIterator>
    bool            invalidate_steps(StepTypeIterator step_begin, StepTypeIterator step_end) 
        { return m_state.invalidate_multiple(step_begin, step_end, this->cancel_callback()); }
    bool            invalidate_steps(std::initializer_list<PrintStepEnum> il) 
        { return m_state.invalidate_multiple(il.begin(), il.end(), this->cancel_callback()); }
    bool            invalidate_all_steps() 
        { return m_state.invalidate_all(this->cancel_callback()); }

	bool            is_step_started_unguarded(PrintStepEnum step) const { return m_state.is_started_unguarded(step); }
	bool            is_step_done_unguarded(PrintStepEnum step) const { return m_state.is_done_unguarded(step); }

    // Add a slicing warning to the active Print step and send a status notification.
    // This method could be called multiple times between this->set_started() and this->set_done().
    void            active_step_add_warning(PrintStateBase::WarningLevel warning_level, const std::string &message, int message_id = 0) {
    	std::pair<PrintStepEnum, bool> active_step = m_state.active_step_add_warning(warning_level, message, message_id, this->state_mutex());
    	if (active_step.second)
    		// Update UI.
            this->status_update_warnings(static_cast<int>(active_step.first), warning_level, message);
    }


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
                    // Step was skipped, cancel.
                    break;
                if (print_object->is_step_started_unguarded(PrintObjectStepEnum(istep))) {
                    // No step was skipped, and a wanted step is being processed. Don't cancel.
                    running = true;
                    break;
                }
            }
            if (! running)
                this->call_cancel_callback();

            // Now the background process is either stopped, or it is inside one of the print object steps to be calculated anyway.
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
            bool running = false;
            for (PrintObject *print_object : print_objects)
                for (int istep = 0; istep < n_object_steps; ++ istep) {
                    if (! print_object->is_step_enabled_unguarded(PrintObjectStepEnum(istep))) {
                        // Step may have been skipped. Restart.
                        goto loop_end;
                    }
                    if (print_object->is_step_started_unguarded(PrintObjectStepEnum(istep))) {
                        // This step is running, and the state cannot be changed due to the this->state_mutex() being locked.
                        // It is safe to manipulate m_stepmask of other PrintObjects and Print now.
                        running = true;
                        goto loop_end;
                    }
                }
        loop_end:
            if (! running)
                this->call_cancel_callback();
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
    PrintStateBase::StateWithTimeStamp step_state_with_timestamp(PrintObjectStepEnum step) const { return m_state.state_with_timestamp(step, PrintObjectBase::state_mutex(m_print)); }
    PrintStateBase::StateWithWarnings  step_state_with_warnings(PrintObjectStepEnum step) const { return m_state.state_with_warnings(step, PrintObjectBase::state_mutex(m_print)); }

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
	PrintObjectBaseWithState(PrintType *print, ModelObject *model_object) : PrintObjectBase(model_object), m_print(print) {}

    bool            set_started(PrintObjectStepEnum step) 
        { return m_state.set_started(step, PrintObjectBase::state_mutex(m_print), [this](){ this->throw_if_canceled(); }); }
	PrintStateBase::TimeStamp set_done(PrintObjectStepEnum step) { 
		std::pair<PrintStateBase::TimeStamp, bool> status = m_state.set_done(step, PrintObjectBase::state_mutex(m_print), [this](){ this->throw_if_canceled(); });
        if (status.second)
            this->status_update_warnings(m_print, static_cast<int>(step), PrintStateBase::WarningLevel::NON_CRITICAL, std::string());
        return status.first;
	}

    bool            invalidate_step(PrintObjectStepEnum step)
        { return m_state.invalidate(step, PrintObjectBase::cancel_callback(m_print)); }
    template<typename StepTypeIterator>
    bool            invalidate_steps(StepTypeIterator step_begin, StepTypeIterator step_end) 
        { return m_state.invalidate_multiple(step_begin, step_end, PrintObjectBase::cancel_callback(m_print)); }
    bool            invalidate_steps(std::initializer_list<PrintObjectStepEnum> il) 
        { return m_state.invalidate_multiple(il.begin(), il.end(), PrintObjectBase::cancel_callback(m_print)); }
    bool            invalidate_all_steps() 
        { return m_state.invalidate_all(PrintObjectBase::cancel_callback(m_print)); }

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

    // Add a slicing warning to the active PrintObject step and send a status notification.
    // This method could be called multiple times between this->set_started() and this->set_done().
    void            active_step_add_warning(PrintStateBase::WarningLevel warning_level, const std::string &message, int message_id = 0) {
    	std::pair<PrintObjectStepEnum, bool> active_step = m_state.active_step_add_warning(warning_level, message, message_id, PrintObjectBase::state_mutex(m_print));
    	if (active_step.second)
    		this->status_update_warnings(m_print, static_cast<int>(active_step.first), warning_level, message);
    }

protected:
    // If the background processing stop was requested, throw CanceledException.
    // To be called by the worker thread and its sub-threads (mostly launched on the TBB thread pool) regularly.
    void            throw_if_canceled() { if (m_print->canceled()) throw CanceledException(); }

    friend PrintType;
    PrintType                                *m_print;

private:
    PrintState<PrintObjectStepEnum, COUNT>    m_state;
};

} // namespace Slic3r

#endif /* slic3r_PrintBase_hpp_ */
