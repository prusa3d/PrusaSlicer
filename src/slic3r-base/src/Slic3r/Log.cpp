#include "Slic3r/Log.hpp"

#include <boost/nowide/convert.hpp>

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
#include <spdlog/sinks/msvc_sink.h>
#endif
#include <spdlog/sinks/rotating_file_sink.h>

namespace Slic3r {

static spdlog::level::level_enum s_current_log_level = spdlog::level::info;
static FileLogConfig file_log_config;

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
static std::shared_ptr<spdlog::sinks::msvc_sink_mt> msvc_sink;
#endif
static std::shared_ptr<spdlog::sinks::rotating_file_sink_mt> file_logger;

static spdlog::level::level_enum log_level_to_spdlog(LogLevel level)
{
    switch (level) {
    // Report critical errors only.
    case LogLevel::Critical:
        return spdlog::level::critical;
    // Report critical errors and errors.
    case LogLevel::Error:
        return spdlog::level::err;
    // Report critical errors, errors and warnings.
    case LogLevel::Warning:
        return spdlog::level::warn;
    // Report all errors, warnings and infos.
    case LogLevel::Info:
        return spdlog::level::info;
    // Report all errors, warnings, infos and debugging.
    case LogLevel::Debug:
        return spdlog::level::debug;
    // Report everything including fine level tracing information.
    case LogLevel::Trace:
        return spdlog::level::trace;
    default:
        return spdlog::level::info;
    }
}

void init_logging()
{
    // TODO: add optional logging to file here

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
    msvc_sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
    spdlog::apply_all([&](std::shared_ptr<spdlog::logger> l) { l->sinks().push_back(msvc_sink); });
#endif
    spdlog::set_level(s_current_log_level);
    if (!file_log_config.log_file.empty()) {
#ifdef WIN32
        std::wstring os_specific_path = boost::nowide::widen(file_log_config.log_file);
#else
        const std::string& os_specific_path = file_log_config.log_file;
#endif
        file_logger = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            os_specific_path,
            file_log_config.log_file_size,
            1
        );
        spdlog::apply_all([&](std::shared_ptr<spdlog::logger> l)
                          { l->sinks().push_back(file_logger); });
    }
}

void set_log_level(LogLevel level)
{
    s_current_log_level = log_level_to_spdlog(level);
    spdlog::set_level(s_current_log_level);
}

const FileLogConfig& get_file_log_config()
{
    return file_log_config;
}

void set_file_log_config(const FileLogConfig& config)
{
    file_log_config = config;
}

void flush_logs()
{
    spdlog::apply_all([&](std::shared_ptr<spdlog::logger> logger) { logger->flush(); });
}

LogScopeTimer::LogScopeTimer(std::string_view name) : m_name(name) {}

LogScopeTimer::~LogScopeTimer()
{
    std::chrono::milliseconds ms = m_stopwatch.elapsed_ms();
    std::chrono::nanoseconds ns  = ms;
    std::chrono::microseconds us = ms;
    SPDLOG_INFO("{} took {} ms ({} us, {} ns)", m_name, ms.count(), us.count(), ns.count());
}

} // namespace Slic3r
