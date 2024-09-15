///|/ Copyright (c) Prusa Research 2020 - 2023 Vojtěch Bubník @bubnikv
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef GUI_THREAD_HPP
#define GUI_THREAD_HPP

#include <boost/thread.hpp>
#include <tbb/task_scheduler_observer.h>
#include <tbb/enumerable_thread_specific.h>
#include <oneapi/tbb/task_scheduler_observer.h>
#include <boost/thread/thread.hpp>
#include <utility>
#include <string>
#include <thread>
#include <random>
#include <optional>

namespace Slic3r {

// Set / get thread name.
// Returns false if the API is not supported.
//
// It is a good idea to name the main thread before spawning children threads, because dynamic linking is used on Windows 10
// to initialize Get/SetThreadDescription functions, which is not thread safe.
//
// pthread_setname_np supports maximum 15 character thread names! (16th character is the null terminator)
// 
// Methods taking the thread as an argument are not supported by OSX.
// Naming threads is only supported on newer Windows 10.

bool set_thread_name(std::thread &thread, const char *thread_name);
inline bool set_thread_name(std::thread &thread, const std::string &thread_name) { return set_thread_name(thread, thread_name.c_str()); }
bool set_thread_name(boost::thread &thread, const char *thread_name);
inline bool set_thread_name(boost::thread &thread, const std::string &thread_name) { return set_thread_name(thread, thread_name.c_str()); }
bool set_current_thread_name(const char *thread_name);
inline bool set_current_thread_name(const std::string &thread_name) { return set_current_thread_name(thread_name.c_str()); }

// To be called at the start of the application to save the current thread ID as the main (UI) thread ID.
void save_main_thread_id();
// Retrieve the cached main (UI) thread ID.
boost::thread::id get_main_thread_id();
// Checks whether the main (UI) thread is active.
bool is_main_thread_active();

// OSX specific: Set Quality of Service to "user initiated", so that the threads will be scheduled to high performance
// cores if available.
void set_current_thread_qos();

// Returns nullopt if not supported.
// Not supported by OSX.
// Naming threads is only supported on newer Windows 10.
std::optional<std::string> get_current_thread_name();

// To be called somewhere before the TBB threads are spinned for the first time, to
// give them names recognizible in the debugger.
// Also it sets locale of the worker threads to "C" for the G-code generator to produce "." as a decimal separator.
void name_tbb_thread_pool_threads_set_locale();

template<class Fn>
inline boost::thread create_thread(boost::thread::attributes &attrs, Fn &&fn)
{
    // Duplicating the stack allocation size of Thread Building Block worker
    // threads of the thread pool: allocate 4MB on a 64bit system, allocate 2MB
    // on a 32bit system by default.
    
    attrs.set_stack_size((sizeof(void*) == 4) ? (2048 * 1024) : (4096 * 1024));
    return boost::thread{attrs, std::forward<Fn>(fn)};
}

template<class Fn> inline boost::thread create_thread(Fn &&fn)
{
    boost::thread::attributes attrs;
    return create_thread(attrs, std::forward<Fn>(fn));    
}

class ThreadData {
public:
    std::mt19937&   random_generator() {
        if (! m_random_generator_initialized) {
            std::random_device rd;
            m_random_generator.seed(rd());
            m_random_generator_initialized = true;
        }
        return m_random_generator;
    }

    void            tbb_worker_thread_set_c_locales();

private:
    std::mt19937    m_random_generator;
    bool            m_random_generator_initialized { false };
    bool            m_tbb_worker_thread_c_locales_set { false };
};

ThreadData& thread_data();

// For unknown reasons and in sporadic cases when GCode export is processing, some participating thread
// in tbb::parallel_pipeline has not set locales to "C", probably because this thread is newly spawned.
// So in this class method on_scheduler_entry is called for every thread before it starts participating
// in tbb::parallel_pipeline to ensure that locales are set correctly
//
// For tbb::parallel_pipeline, it seems that on_scheduler_entry is called for every layer and every filter.
// We ensure using thread-local storage that locales will be set to "C" just once for any participating thread.
class TBBLocalesSetter : public tbb::task_scheduler_observer
{
public:
    TBBLocalesSetter() { this->observe(true); }
    ~TBBLocalesSetter() override { this->observe(false); };
    void on_scheduler_entry(bool /* is_worker */) override { thread_data().tbb_worker_thread_set_c_locales(); }
};

}

#endif // GUI_THREAD_HPP
