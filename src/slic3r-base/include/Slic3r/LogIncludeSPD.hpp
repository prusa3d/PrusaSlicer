#pragma once

// The purpose of this file is to have a single place where we set the
// SPDLOG_ACTIVE_LEVEL macro, which we can use from Log.hpp and pch.hpp.
// It is nicer than repeating the logic in both files and keeping it in sync.

#ifndef SPDLOG_ACTIVE_LEVEL
#ifndef NDEBUG
#define SPDLOG_ACTIVE_LEVEL 0//SPDLOG_LEVEL_TRACE
#else
#define SPDLOG_ACTIVE_LEVEL 2//SPDLOG_LEVEL_INFO
#endif
#endif

#include <spdlog/spdlog.h>
#include <spdlog/stopwatch.h>
