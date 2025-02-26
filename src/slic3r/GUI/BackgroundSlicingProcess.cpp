///|/ Copyright (c) Prusa Research 2018 - 2023 Lukáš Matěna @lukasmatena, Oleksandra Iushchenko @YuSanka, Vojtěch Bubník @bubnikv, Pavel Mikuš @Godrak, David Kocík @kocikdav, Enrico Turri @enricoturri1966, Tomáš Mészáros @tamasmeszaros, Roman Beránek @zavorka, Vojtěch Král @vojtechkral
///|/ Copyright (c) 2022 ole00 @ole00
///|/ Copyright (c) 2021 Ilya @xorza
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "BackgroundSlicingProcess.hpp"
#include "GUI_App.hpp"
#include "GUI.hpp"
#include "MainFrame.hpp"
#include "format.hpp"
#include "libslic3r/GCode/Thumbnails.hpp"

#include <wx/app.h>
#include <wx/panel.h>
#include <wx/stdpaths.h>

// For zipped archive creation
#include <wx/stdstream.h>
#include <wx/wfstream.h>
#include <wx/zipstrm.h>

#include <miniz.h>

// Print now includes tbb, and tbb includes Windows. This breaks compilation of wxWidgets if included before wx.
#include "libslic3r/Print.hpp"
#include "libslic3r/SLAPrint.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/GCode/PostProcessor.hpp"
#include "libslic3r/Format/SL1.hpp"
#include "libslic3r/Thread.hpp"
#include "libslic3r/libslic3r.h"

#include <cassert>
#include <stdexcept>
#include <cctype>

#include <boost/format/format_fwd.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/cstdio.hpp>
#include "I18N.hpp"
#include "RemovableDriveManager.hpp"

#include "slic3r/GUI/Plater.hpp"

namespace Slic3r {

bool SlicingProcessCompletedEvent::critical_error() const
{
	try {
		this->rethrow_exception();
	} catch (const Slic3r::SlicingError &) {
		// Exception derived from SlicingError is non-critical.
		return false;
	} catch (...) {
	}
	return true;
}

bool SlicingProcessCompletedEvent::invalidate_plater() const
{
	if (critical_error())
	{
		try {
			this->rethrow_exception();
		}
		catch (const Slic3r::ExportError&) {
			// Exception thrown by copying file does not ivalidate plater
			return false;
		}
		catch (...) {
		}
		return true;
	}
	return false;
}

std::pair<std::string, bool> SlicingProcessCompletedEvent::format_error_message() const
{
	std::string error;
	bool        monospace = false;
	try {
		this->rethrow_exception();
    } catch (const std::bad_alloc &ex) {
		error = GUI::format(_L("%s has encountered an error. It was likely caused by running out of memory. "
                              "If you are sure you have enough RAM on your system, this may also be a bug and we would "
                              "be glad if you reported it."), SLIC3R_APP_NAME);
        error += "\n\n" + std::string(ex.what());
    } catch (const HardCrash &ex) {
        error = GUI::format(_L("PrusaSlicer has encountered a fatal error: \"%1%\""), ex.what()) + "\n\n" +
        		_u8L("Please save your project and restart PrusaSlicer. "
                     "We would be glad if you reported the issue.");
    } catch (PlaceholderParserError &ex) {
		error = ex.what();
		monospace = true;
    } catch (std::exception &ex) {
		error = ex.what();
	} catch (...) {
		error = "Unknown C++ exception.";
	}
	return std::make_pair(std::move(error), monospace);
}

void BackgroundSlicingProcess::set_temp_output_path(int bed_idx)
{
    boost::filesystem::path temp_path(wxStandardPaths::Get().GetTempDir().utf8_str().data());
    temp_path /= (boost::format(".%1%_%2%.gcode") % get_current_pid() % bed_idx).str();
	m_temp_output_path = temp_path.string();
}

BackgroundSlicingProcess::~BackgroundSlicingProcess() 
{ 
	this->stop();
	this->join_background_thread();

	// Current m_temp_output_path corresponds to the last selected bed. Remove everything
	// in the same directory that starts the same (see set_temp_output_path).
	const auto temp_dir = boost::filesystem::path(m_temp_output_path).parent_path();
	std::string prefix = boost::filesystem::path(m_temp_output_path).filename().string();
	prefix = prefix.substr(0, prefix.find('_'));
    for (const auto& entry : boost::filesystem::directory_iterator(temp_dir)) {
        if (entry.is_regular_file()) {
            const std::string filename = entry.path().filename().string();
            if (boost::starts_with(filename, prefix) && boost::ends_with(filename, ".gcode"))
                boost::filesystem::remove(entry);
        }
    }
}

bool BackgroundSlicingProcess::select_technology(PrinterTechnology tech)
{
	bool changed = false;
	if (m_print == nullptr || m_print->technology() != tech) {
		if (m_print != nullptr)
			this->reset();
		switch (tech) {
		case ptFFF: m_print = m_fff_print; break;
		case ptSLA: m_print = m_sla_print; break;
        default: assert(false); break;
		}
		changed = true;
	}
	if (tech == ptFFF)
		m_print = m_fff_print;
	assert(m_print != nullptr);
	return changed;
}

PrinterTechnology BackgroundSlicingProcess::current_printer_technology() const
{
	return m_print->technology();
}

std::string BackgroundSlicingProcess::output_filepath_for_project(const boost::filesystem::path &project_path)
{
	assert(m_print != nullptr);
    if (project_path.empty())
        return m_print->output_filepath("");
    return m_print->output_filepath(project_path.parent_path().string(), project_path.stem().string());
}

// This function may one day be merged into the Print, but historically the print was separated
// from the G-code generator.
void BackgroundSlicingProcess::process_fff()
{
	assert(m_print == m_fff_print);
	m_print->process();
	wxCommandEvent evt(m_event_slicing_completed_id);
	// Post the Slicing Finished message for the G-code viewer to update.
	// Passing the timestamp 
	evt.SetInt((int)(m_fff_print->step_state_with_timestamp(PrintStep::psSlicingFinished).timestamp));
	wxQueueEvent(GUI::wxGetApp().mainframe->m_plater, evt.Clone());
	m_fff_print->export_gcode(m_temp_output_path, m_gcode_result, [this](const ThumbnailsParams& params) { return this->render_thumbnails(params); });
	if (this->set_step_started(bspsGCodeFinalize)) {
	    if (! m_export_path.empty()) {
			wxQueueEvent(GUI::wxGetApp().mainframe->m_plater, new wxCommandEvent(m_event_export_began_id));
			finalize_gcode(m_export_path, m_export_path_on_removable_media);
	    } else if (! m_upload_job.empty()) {
			wxQueueEvent(GUI::wxGetApp().mainframe->m_plater, new wxCommandEvent(m_event_export_began_id));
			prepare_upload(m_upload_job);
	    } else {
			m_print->set_status(100, _u8L("Slicing complete"));
	    }
		this->set_step_done(bspsGCodeFinalize);
	}
}

void BackgroundSlicingProcess::process_sla()
{
    assert(m_print == m_sla_print);
    m_print->process();
    if (this->set_step_started(bspsGCodeFinalize)) {
        if (! m_export_path.empty()) {
			wxQueueEvent(GUI::wxGetApp().mainframe->m_plater, new wxCommandEvent(m_event_export_began_id));

            const std::string export_path = m_sla_print->print_statistics().finalize_output_path(m_export_path);

			auto [thumbnails_list, errors] = GCodeThumbnails::make_and_check_thumbnail_list(current_print()->full_print_config());

			if (errors != enum_bitmask<ThumbnailError>()) {
				std::string error_str = format("Invalid thumbnails value:");
				error_str += GCodeThumbnails::get_error_string(errors);
				throw Slic3r::ExportError(error_str);
			}

			Vec2ds 	sizes;
			if (!thumbnails_list.empty()) {
				sizes.reserve(thumbnails_list.size());
				for (const auto& [format, size] : thumbnails_list)
					sizes.emplace_back(size);
			}
			ThumbnailsList thumbnails = this->render_thumbnails(ThumbnailsParams{sizes, true, true, true, true });
			m_sla_print->export_print(export_path, thumbnails);

            m_print->set_status(100, GUI::format(_L("Masked SLA file exported to %1%"), export_path));
        } else if (! m_upload_job.empty()) {
			wxQueueEvent(GUI::wxGetApp().mainframe->m_plater, new wxCommandEvent(m_event_export_began_id));
            prepare_upload(m_upload_job);
        } else {
			m_print->set_status(100, _u8L("Slicing complete"));
        }
        this->set_step_done(bspsGCodeFinalize);
    }
}

void BackgroundSlicingProcess::thread_proc()
{
	set_current_thread_name("slic3r_BgSlcPcs");
    name_tbb_thread_pool_threads_set_locale();

    // Set "C" locales and enforce OSX QoS level on all threads entering an arena.
    // The cost of the callback is quite low: The callback is called once per thread
    // entering a parallel loop and the callback is guarded with a thread local
    // variable to be executed just once.
	TBBLocalesSetter setter;

	assert(m_print != nullptr);
	assert(m_print == m_fff_print || m_print == m_sla_print);
	std::unique_lock<std::mutex> lck(m_mutex);
	// Let the caller know we are ready to run the background processing task.
	m_state = STATE_IDLE;
	lck.unlock();
	m_condition.notify_one();
	for (;;) {
		assert(m_state == STATE_IDLE || m_state == STATE_CANCELED || m_state == STATE_FINISHED);
		// Wait until a new task is ready to be executed, or this thread should be finished.
		lck.lock();
		m_condition.wait(lck, [this](){ return m_state == STATE_STARTED || m_state == STATE_EXIT; });
		if (m_state == STATE_EXIT)
			// Exiting this thread.
			break;
		// Process the background slicing task.
		m_state = STATE_RUNNING;
		lck.unlock();
		std::exception_ptr exception;
#ifdef _WIN32
		this->call_process_seh_throw(exception);
#else
		this->call_process(exception);
#endif
		m_print->finalize();
		lck.lock();
		m_state = m_print->canceled() ? STATE_CANCELED : STATE_FINISHED;
		if (m_print->cancel_status() != Print::CANCELED_INTERNAL) {
			// Only post the canceled event, if canceled by user.
			// Don't post the canceled event, if canceled from Print::apply().
			SlicingProcessCompletedEvent evt(m_event_finished_id, 0, 
				(m_state == STATE_CANCELED) ? SlicingProcessCompletedEvent::Cancelled :
				exception ? SlicingProcessCompletedEvent::Error : SlicingProcessCompletedEvent::Finished, exception);
        	wxQueueEvent(GUI::wxGetApp().mainframe->m_plater, evt.Clone());
        	// Cancelled by the user, not internally, thus cleanup() was not called yet.
        	// Otherwise cleanup() is called from Print::apply()
        	m_print->cleanup();
        }
	    m_print->restart();
		lck.unlock();
		// Let the UI thread wake up if it is waiting for the background task to finish.
	    m_condition.notify_one();
	    // Let the UI thread see the result.
	}
	m_state = STATE_EXITED;
	lck.unlock();
	// End of the background processing thread. The UI thread should join m_thread now.
}

#ifdef _WIN32
// Only these SEH exceptions will be catched and turned into Slic3r::HardCrash C++ exceptions.
static bool is_win32_seh_harware_exception(unsigned long ex) throw() {
	return
		ex == STATUS_ACCESS_VIOLATION ||
		ex == STATUS_DATATYPE_MISALIGNMENT ||
		ex == STATUS_FLOAT_DIVIDE_BY_ZERO ||
		ex == STATUS_FLOAT_OVERFLOW ||
		ex == STATUS_FLOAT_UNDERFLOW ||
#ifdef STATUS_FLOATING_RESEVERED_OPERAND
		ex == STATUS_FLOATING_RESEVERED_OPERAND ||
#endif // STATUS_FLOATING_RESEVERED_OPERAND
		ex == STATUS_ILLEGAL_INSTRUCTION ||
		ex == STATUS_PRIVILEGED_INSTRUCTION ||
		ex == STATUS_INTEGER_DIVIDE_BY_ZERO ||
		ex == STATUS_INTEGER_OVERFLOW ||
		ex == STATUS_STACK_OVERFLOW;
}

// Rethrow some SEH exceptions as Slic3r::HardCrash C++ exceptions.
static void rethrow_seh_exception(unsigned long win32_seh_catched)
{
	if (win32_seh_catched) {
		// Rethrow SEH exception as Slicer::HardCrash.
		if (win32_seh_catched == STATUS_ACCESS_VIOLATION || win32_seh_catched == STATUS_DATATYPE_MISALIGNMENT)
			throw Slic3r::HardCrash(_u8L("Access violation"));
		if (win32_seh_catched == STATUS_ILLEGAL_INSTRUCTION || win32_seh_catched == STATUS_PRIVILEGED_INSTRUCTION)
			throw Slic3r::HardCrash(_u8L("Illegal instruction"));
		if (win32_seh_catched == STATUS_FLOAT_DIVIDE_BY_ZERO || win32_seh_catched == STATUS_INTEGER_DIVIDE_BY_ZERO)
			throw Slic3r::HardCrash(_u8L("Divide by zero"));
		if (win32_seh_catched == STATUS_FLOAT_OVERFLOW || win32_seh_catched == STATUS_INTEGER_OVERFLOW)
			throw Slic3r::HardCrash(_u8L("Overflow"));
		if (win32_seh_catched == STATUS_FLOAT_UNDERFLOW)
			throw Slic3r::HardCrash(_u8L("Underflow"));
#ifdef STATUS_FLOATING_RESEVERED_OPERAND
		if (win32_seh_catched == STATUS_FLOATING_RESEVERED_OPERAND)
			throw Slic3r::HardCrash(_u8L("Floating reserved operand"));
#endif // STATUS_FLOATING_RESEVERED_OPERAND
		if (win32_seh_catched == STATUS_STACK_OVERFLOW)
			throw Slic3r::HardCrash(_u8L("Stack overflow"));
	}
}

// Wrapper for Win32 structured exceptions. Win32 structured exception blocks and C++ exception blocks cannot be mixed in the same function.
unsigned long BackgroundSlicingProcess::call_process_seh(std::exception_ptr &ex) throw()
{
	unsigned long win32_seh_catched = 0;
	__try {
		this->call_process(ex);
	} __except (is_win32_seh_harware_exception(GetExceptionCode())) {
		win32_seh_catched = GetExceptionCode();
	}
	return win32_seh_catched;
}
void BackgroundSlicingProcess::call_process_seh_throw(std::exception_ptr &ex) throw()
{
	unsigned long win32_seh_catched = this->call_process_seh(ex);
	if (win32_seh_catched) {
		// Rethrow SEH exception as Slicer::HardCrash.
		try {
			rethrow_seh_exception(win32_seh_catched);
		} catch (...) {
			ex = std::current_exception();
		}
	}
}
#endif // _WIN32

void BackgroundSlicingProcess::call_process(std::exception_ptr &ex) throw()
{
	try {
		assert(m_print != nullptr);
		switch (m_print->technology()) {
		case ptFFF: this->process_fff(); break;
		case ptSLA: this->process_sla(); break;
		default: m_print->process(); break;
		}
	} catch (CanceledException& /* ex */) {
		// Canceled, this is all right.
		assert(m_print->canceled());
		ex = std::current_exception();
	} catch (...) {
		ex = std::current_exception();
	}
}

#ifdef _WIN32
unsigned long BackgroundSlicingProcess::thread_proc_safe_seh() throw()
{
	unsigned long win32_seh_catched = 0;
	__try {
		this->thread_proc_safe();
	} __except (is_win32_seh_harware_exception(GetExceptionCode())) {
		win32_seh_catched = GetExceptionCode();
	}
	return win32_seh_catched;
}
void BackgroundSlicingProcess::thread_proc_safe_seh_throw() throw()
{
	unsigned long win32_seh_catched = this->thread_proc_safe_seh();
	if (win32_seh_catched) {
		// Rethrow SEH exception as Slicer::HardCrash.
		try {
			rethrow_seh_exception(win32_seh_catched);
		} catch (...) {
			wxTheApp->OnUnhandledException();
		}
	}
}
#endif // _WIN32

void BackgroundSlicingProcess::thread_proc_safe() throw()
{
	try {
		this->thread_proc();
	} catch (...) {
		wxTheApp->OnUnhandledException();
   	}
}

void BackgroundSlicingProcess::join_background_thread()
{
	std::unique_lock<std::mutex> lck(m_mutex);
	if (m_state == STATE_INITIAL) {
		// Worker thread has not been started yet.
		assert(! m_thread.joinable());
	} else {
		assert(m_state == STATE_IDLE);
		assert(m_thread.joinable());
		// Notify the worker thread to exit.
		m_state = STATE_EXIT;
		lck.unlock();
		m_condition.notify_one();
		// Wait until the worker thread exits.
		m_thread.join();
	}
}

bool BackgroundSlicingProcess::start()
{
	if (m_print->empty())
		// The print is empty (no object in Model, or all objects are out of the print bed).
		return false;

	std::unique_lock<std::mutex> lck(m_mutex);
	if (m_state == STATE_INITIAL) {
		// The worker thread is not running yet. Start it.
		assert(! m_thread.joinable());
		m_thread = create_thread([this]{
#ifdef _WIN32
			this->thread_proc_safe_seh_throw();
#else // _WIN32
			this->thread_proc_safe();
#endif // _WIN32
		});
		// Wait until the worker thread is ready to execute the background processing task.
		m_condition.wait(lck, [this](){ return m_state == STATE_IDLE; });
	}
	assert(m_state == STATE_IDLE || this->running());
	if (this->running())
		// The background processing thread is already running.
		return false;
	if (! this->idle())
		throw Slic3r::RuntimeError("Cannot start a background task, the worker thread is not idle.");
	m_state = STATE_STARTED;
	m_print->set_cancel_callback([this](){ this->stop_internal(); });
	lck.unlock();
	m_condition.notify_one();
	return true;
}

// To be called on the UI thread.
bool BackgroundSlicingProcess::stop()
{
	// m_print->state_mutex() shall NOT be held. Unfortunately there is no interface to test for it.
	std::unique_lock<std::mutex> lck(m_mutex);
	if (m_state == STATE_INITIAL) {
//		m_export_path.clear();
		return false;
	}
//	assert(this->running());
	if (m_state == STATE_STARTED || m_state == STATE_RUNNING) {
		// Cancel any task planned by the background thread on UI thread.
		cancel_ui_task(m_ui_task);
		m_print->cancel();
		// Wait until the background processing stops by being canceled.
		m_condition.wait(lck, [this](){ return m_state == STATE_CANCELED; });
		// In the "Canceled" state. Reset the state to "Idle".
		m_state = STATE_IDLE;
		m_print->set_cancel_callback([](){});
	} else if (m_state == STATE_FINISHED || m_state == STATE_CANCELED) {
		// In the "Finished" or "Canceled" state. Reset the state to "Idle".
		m_state = STATE_IDLE;
		m_print->set_cancel_callback([](){});
	}
//	m_export_path.clear();
	return true;
}

bool BackgroundSlicingProcess::reset()
{
	bool stopped = this->stop();
	this->reset_export();
	m_print->clear();
	this->invalidate_all_steps();
	return stopped;
}

// To be called by Print::apply() on the UI thread through the Print::m_cancel_callback to stop the background
// processing before changing any data of running or finalized milestones.
// This function shall not trigger any UI update through the wxWidgets event.
void BackgroundSlicingProcess::stop_internal()
{
	// m_print->state_mutex() shall be held. Unfortunately there is no interface to test for it.
	if (m_state == STATE_IDLE)
		// The worker thread is waiting on m_mutex/m_condition for wake up. The following lock of the mutex would block.
		return;
	std::unique_lock<std::mutex> lck(m_mutex);
	assert(m_state == STATE_STARTED || m_state == STATE_RUNNING || m_state == STATE_FINISHED || m_state == STATE_CANCELED);
	if (m_state == STATE_STARTED || m_state == STATE_RUNNING) {
		// Cancel any task planned by the background thread on UI thread.
		cancel_ui_task(m_ui_task);
		// At this point of time the worker thread may be blocking on m_print->state_mutex().
		// Set the print state to canceled before unlocking the state_mutex(), so when the worker thread wakes up,
		// it throws the CanceledException().
		m_print->cancel_internal();
		// Allow the worker thread to wake up if blocking on a milestone.
		m_print->state_mutex().unlock();
		// Wait until the background processing stops by being canceled.
		m_condition.wait(lck, [this](){ return m_state == STATE_CANCELED; });
		// Lock it back to be in a consistent state.
		m_print->state_mutex().lock();
	}
	// In the "Canceled" state. Reset the state to "Idle".
	m_state = STATE_IDLE;
	m_print->set_cancel_callback([](){});
}

// Execute task from background thread on the UI thread. Returns true if processed, false if cancelled. 
bool BackgroundSlicingProcess::execute_ui_task(std::function<void()> task)
{
	bool running = false;
	if (m_mutex.try_lock()) {
		// Cancellation is either not in process, or already canceled and waiting for us to finish.
		// There must be no UI task planned.
		assert(! m_ui_task);
		if (! m_print->canceled()) {
			running = true;
			m_ui_task = std::make_shared<UITask>();
		}
		m_mutex.unlock();
	} else {
		// Cancellation is in process.
	}

	bool result = false;
	if (running) {
		std::shared_ptr<UITask> ctx = m_ui_task;
		GUI::wxGetApp().mainframe->m_plater->CallAfter([task, ctx]() {
			// Running on the UI thread, thus ctx->state does not need to be guarded with mutex against ::cancel_ui_task().
			assert(ctx->state == UITask::Planned || ctx->state == UITask::Canceled);
			if (ctx->state == UITask::Planned) {
				task();
				std::unique_lock<std::mutex> lck(ctx->mutex);
	    		ctx->state = UITask::Finished;
	    	}
	    	// Wake up the worker thread from the UI thread.
    		ctx->condition.notify_all();
	    });

	    {
			std::unique_lock<std::mutex> lock(ctx->mutex);
	    	ctx->condition.wait(lock, [&ctx]{ return ctx->state == UITask::Finished || ctx->state == UITask::Canceled; });
	    }
	    result = ctx->state == UITask::Finished;
		m_ui_task.reset();
	}

	return result;
}

// To be called on the UI thread from ::stop() and ::stop_internal().
void BackgroundSlicingProcess::cancel_ui_task(std::shared_ptr<UITask> task)
{
	if (task) {
		std::unique_lock<std::mutex> lck(task->mutex);
		task->state = UITask::Canceled;
		lck.unlock();
		task->condition.notify_all();
	}
}

bool BackgroundSlicingProcess::empty() const
{
	assert(m_print != nullptr);
	return m_print->empty();
}

std::string BackgroundSlicingProcess::validate(std::vector<std::string>* warnings)
{
	assert(m_print != nullptr);
    return m_print->validate(warnings);
}

// Apply config over the print. Returns false, if the new config values caused any of the already
// processed steps to be invalidated, therefore the task will need to be restarted.
Print::ApplyStatus BackgroundSlicingProcess::apply(const Model &model, const DynamicPrintConfig &config, std::vector<std::string> *warnings)
{
	assert(m_print != nullptr);
	assert(config.opt_enum<PrinterTechnology>("printer_technology") == m_print->technology());
	Print::ApplyStatus invalidated = m_print->apply(model, config, warnings);
	if ((invalidated & PrintBase::APPLY_STATUS_INVALIDATED) != 0 && m_print->technology() == ptFFF &&
		!m_fff_print->is_step_done(psGCodeExport)) {
		// Some FFF status was invalidated, and the G-code was not exported yet.
		// Let the G-code preview UI know that the final G-code preview is not valid.
		// In addition, this early memory deallocation reduces memory footprint.
		if (m_gcode_result != nullptr)
			m_gcode_result->reset();
	}
	return invalidated;
}

void BackgroundSlicingProcess::set_task(const PrintBase::TaskParams &params)
{
	assert(m_print != nullptr);
	m_print->set_task(params);
}

// Set the output path of the G-code.
void BackgroundSlicingProcess::schedule_export(const std::string &path, bool export_path_on_removable_media)
{ 
	assert(m_export_path.empty());
	if (! m_export_path.empty())
		return;

	// Guard against entering the export step before changing the export path.
	std::scoped_lock<std::mutex> lock(m_print->state_mutex());
	this->invalidate_step(bspsGCodeFinalize);
	m_export_path = path;
	m_export_path_on_removable_media = export_path_on_removable_media;
}

void BackgroundSlicingProcess::schedule_upload(Slic3r::PrintHostJob upload_job)
{
	assert(m_export_path.empty());
	if (! m_export_path.empty())
		return;

	// Guard against entering the export step before changing the export path.
	std::scoped_lock<std::mutex> lock(m_print->state_mutex());
	this->invalidate_step(bspsGCodeFinalize);
	m_export_path.clear();
	m_upload_job = std::move(upload_job);
}

void BackgroundSlicingProcess::reset_export()
{
	assert(! this->running());
	if (! this->running()) {
		m_export_path.clear();
		m_export_path_on_removable_media = false;
		// invalidate_step expects the mutex to be locked.
		std::scoped_lock<std::mutex> lock(m_print->state_mutex());
		this->invalidate_step(bspsGCodeFinalize);
	}
}

bool BackgroundSlicingProcess::set_step_started(BackgroundSlicingProcessStep step)
{ 
	return m_step_state.set_started(step, m_print->state_mutex(), [this](){ this->throw_if_canceled(); });
}

void BackgroundSlicingProcess::set_step_done(BackgroundSlicingProcessStep step)
{ 
	m_step_state.set_done(step, m_print->state_mutex(), [this](){ this->throw_if_canceled(); });
}

bool BackgroundSlicingProcess::is_step_done(BackgroundSlicingProcessStep step) const
{ 
	return m_step_state.is_done(step, m_print->state_mutex());
}

bool BackgroundSlicingProcess::invalidate_step(BackgroundSlicingProcessStep step)
{
    bool invalidated = m_step_state.invalidate(step, [this](){ this->stop_internal(); });
    return invalidated;
}

bool BackgroundSlicingProcess::invalidate_all_steps()
{ 
	return m_step_state.invalidate_all([this](){ this->stop_internal(); });
}

// G-code is generated in m_temp_output_path.
// Optionally run a post-processing script on a copy of m_temp_output_path.
// Copy the final G-code to target location (possibly a SD card, if it is a removable media, then verify that the file was written without an error).
void BackgroundSlicingProcess::finalize_gcode(const std::string &path, const bool path_on_removable_media)
{
	m_print->set_status(95, _u8L("Running post-processing scripts"));

	// Perform the final post-processing of the export path by applying the print statistics over the file name.
	std::string export_path = m_fff_print->print_statistics().finalize_output_path(path);
	std::string output_path = m_temp_output_path;
	// Both output_path and export_path ar in-out parameters.
	// If post processed, output_path will differ from m_temp_output_path as run_post_process_scripts() will make a copy of the G-code to not
	// collide with the G-code viewer memory mapping of the unprocessed G-code. G-code viewer maps unprocessed G-code, because m_gcode_result 
	// is calculated for the unprocessed G-code and it references lines in the memory mapped G-code file by line numbers.
	// export_path may be changed by the post-processing script as well if the post processing script decides so, see GH #6042.
	bool post_processed = run_post_process_scripts(output_path, true, "File", export_path, m_fff_print->full_print_config());
	auto remove_post_processed_temp_file = [post_processed, &output_path]() {
		if (post_processed)
			try {
				boost::filesystem::remove(output_path);
			} catch (const std::exception &ex) {
				BOOST_LOG_TRIVIAL(error) << "Failed to remove temp file " << output_path << ": " << ex.what();
			}
	};

	//FIXME localize the messages
	std::string error_message;
	int copy_ret_val = CopyFileResult::SUCCESS;
	try
	{
		copy_ret_val = copy_file(output_path, export_path, error_message, path_on_removable_media);
		remove_post_processed_temp_file();
	}
	catch (...)
	{
		remove_post_processed_temp_file();
		throw Slic3r::ExportError(_u8L("Unknown error occured during exporting G-code."));
	}
	switch (copy_ret_val) {
	case CopyFileResult::SUCCESS: break; // no error
	case CopyFileResult::FAIL_COPY_FILE:
		throw Slic3r::ExportError(GUI::format(_L("Copying of the temporary G-code to the output G-code failed. Maybe the SD card is write locked?\nError message: %1%"), error_message));
		break;
	case CopyFileResult::FAIL_FILES_DIFFERENT:
		throw Slic3r::ExportError(GUI::format(_L("Copying of the temporary G-code to the output G-code failed. There might be problem with target device, please try exporting again or using different device. The corrupted output G-code is at %1%.tmp."), export_path));
		break;
	case CopyFileResult::FAIL_RENAMING:
		throw Slic3r::ExportError(GUI::format(_L("Renaming of the G-code after copying to the selected destination folder has failed. Current path is %1%.tmp. Please try exporting again."), export_path));
		break;
	case CopyFileResult::FAIL_CHECK_ORIGIN_NOT_OPENED:
		throw Slic3r::ExportError(GUI::format(_L("Copying of the temporary G-code has finished but the original code at %1% couldn't be opened during copy check. The output G-code is at %2%.tmp."), output_path, export_path));
		break;
	case CopyFileResult::FAIL_CHECK_TARGET_NOT_OPENED:
		throw Slic3r::ExportError(GUI::format(_L("Copying of the temporary G-code has finished but the exported code couldn't be opened during copy check. The output G-code is at %1%.tmp."), export_path));
		break;
	default:
		throw Slic3r::ExportError(_u8L("Unknown error occured during exporting G-code."));
		BOOST_LOG_TRIVIAL(error) << "Unexpected fail code(" << (int)copy_ret_val << ") durring copy_file() to " << export_path << ".";
		break;
	}

	m_print->set_status(100, GUI::format(_L("G-code file exported to %1%"), export_path));
}

// A print host upload job has been scheduled, enqueue it to the printhost job queue
void BackgroundSlicingProcess::prepare_upload(PrintHostJob &upload_job)
{
	// Generate a unique temp path to which the gcode/zip file is copied/exported
	boost::filesystem::path source_path = boost::filesystem::temp_directory_path()
		/ boost::filesystem::unique_path("." SLIC3R_APP_KEY ".upload.%%%%-%%%%-%%%%-%%%%");

	if (m_print == m_fff_print) {
		m_print->set_status(95, _u8L("Running post-processing scripts"));
		std::string error_message;
		if (copy_file(m_temp_output_path, source_path.string(), error_message) != SUCCESS)
			throw Slic3r::RuntimeError("Copying of the temporary G-code to the output G-code failed");
        upload_job.upload_data.upload_path = m_fff_print->print_statistics().finalize_output_path(upload_job.upload_data.upload_path.string());
        // Make a copy of the source path, as run_post_process_scripts() is allowed to change it when making a copy of the source file
        // (not here, but when the final target is a file). 
        std::string source_path_str = source_path.string();
        std::string output_name_str = upload_job.upload_data.upload_path.string();
		if (run_post_process_scripts(source_path_str, false, upload_job.printhost->get_name(), output_name_str, m_fff_print->full_print_config()))
			upload_job.upload_data.upload_path = output_name_str;
    } else {
        upload_job.upload_data.upload_path = m_sla_print->print_statistics().finalize_output_path(upload_job.upload_data.upload_path.string());

		auto [thumbnails_list, errors] = GCodeThumbnails::make_and_check_thumbnail_list(current_print()->full_print_config());

		if (errors != enum_bitmask<ThumbnailError>()) {
			std::string error_str = format("Invalid thumbnails value:");
			error_str += GCodeThumbnails::get_error_string(errors);
			throw Slic3r::ExportError(error_str);
		}

		Vec2ds 	sizes;
		if (!thumbnails_list.empty()) {
			sizes.reserve(thumbnails_list.size());
			for (const auto& [format, size] : thumbnails_list)
				sizes.emplace_back(size);
		}
		ThumbnailsList thumbnails = this->render_thumbnails(ThumbnailsParams{ sizes, true, true, true, true });
        m_sla_print->export_print(source_path.string(),thumbnails, upload_job.upload_data.upload_path.filename().string());
    }

    m_print->set_status(100, GUI::format(_L("Scheduling upload to `%1%`. See Window -> Print Host Upload Queue"), upload_job.printhost->get_host()));

	upload_job.upload_data.source_path = std::move(source_path);

	GUI::wxGetApp().printhost_job_queue().enqueue(std::move(upload_job));
}

// Executed by the background thread, to start a task on the UI thread.
ThumbnailsList BackgroundSlicingProcess::render_thumbnails(const ThumbnailsParams &params)
{
	ThumbnailsList thumbnails;
	if (m_thumbnail_cb)
		this->execute_ui_task([this, &params, &thumbnails](){ thumbnails = m_thumbnail_cb(params); });
	return thumbnails;
}

}; // namespace Slic3r
