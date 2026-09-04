#include "Slic3r/Timer.hpp"
#include "Slic3r/Log.hpp"

using namespace std::chrono;

Slic3r::Timer::Timer(const std::string &name) : m_name(name), m_start(steady_clock::now()) {}

Slic3r::Timer::~Timer()
{
    SPDLOG_DEBUG("Timer '{}' spend {}ms", m_name, duration_cast<milliseconds>(steady_clock::now() - m_start).count());
}


namespace Slic3r::Timing {

void TimeLimitAlarm::report_time_exceeded() const {
    SPDLOG_ERROR("Time limit exceeded for {}: {}s", m_limit_exceeded_message, m_timer.elapsed_seconds());
}

} // namespace Slic3r::Timing
