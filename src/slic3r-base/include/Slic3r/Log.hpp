#pragma once

#include "LogIncludeSPD.hpp"

#include <cstdint>

namespace Slic3r {

enum class LogLevel : std::uint8_t
{
    Critical = 0,
    Error    = 1,
    Warning  = 2,
    Info     = 3,
    Debug    = 4,
    Trace    = 5,
};

struct FileLogConfig
{
    std::string log_file;
    size_t log_file_size;
};

void init_logging();

void set_log_level(LogLevel level);
const FileLogConfig& get_file_log_config();
void set_file_log_config(const FileLogConfig& config);

void flush_logs();

class LogScopeTimer
{
public:
    explicit LogScopeTimer(std::string_view name);
    ~LogScopeTimer();

private:
    std::string_view m_name;
    spdlog::stopwatch m_stopwatch;
};

}
