#pragma once

// The purpose of this file is to have a single place where we set the
// SPDLOG_ACTIVE_LEVEL macro, which we can use from Log.hpp and pch.hpp.
// It is nicer than repeating the logic in both files and keeping it in sync.

// Keep this at TRACE in all configurations, it is a compile-time limit that no runtime log level can lift.
#ifndef SPDLOG_ACTIVE_LEVEL
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#endif

#include <spdlog/spdlog.h>
#include <spdlog/stopwatch.h>
#include <spdlog/sinks/basic_file_sink.h>
