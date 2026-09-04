#pragma once

#include "LogIncludeSPD.hpp"

namespace Slic3r {

struct FileLogConfig
{
    std::string log_file;
    size_t log_file_size;
};

void init_logging();

void set_log_level(unsigned level);
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
