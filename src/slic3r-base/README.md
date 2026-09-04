# slic3r-base

## Purpose

This library provides general base foundation for all `slic3r-*` libs. Only bare minimum with system-level deps, intended to be shared across all libs, 
should be placed here. 

If you have something heavier (either implementaiton wise or dependency wise---like OpenGL), consider placing it into `slic3r-shared` or similar lib.

## API

### Assertion


```c++
#include <Slic3r/Assert.hpp>
```

[`libassert` library](https://github.com/jeremy-rifkin/libassert) is integrated as part of slic3r-base, for Emscripten simple implementation of libassert API is provided.

The following API is guaranteed to be available on all supported platforms:
 
- `ASSERT(x[, message])`,  `ASSERT_VAL(x[, message])` enforces condition in both debug AND release configurations (!)
- `DEBUG_ASSERT(x[, message])`, `DEBUG_ASSERT_VAL(x[, message])` enforces condition only in debug configuration.
- `PANIC(message)`

**Note:**
- `ASSERT` is evaluated in release **AND** debug configurations
- `DEBUG_ASSERT` is evaluated in debug only configuration (i.e. this is the alterantive to std C++ `assert` macro of `<cassert>`)


### Logging

```c++
#include <Slic3r/Log.hpp>
```

Integrates the [spdlog library](https://github.com/gabime/spdlog) and provides logging initialization along with setting logging level.

**Note:**
- the `SPDLOG_TRACE`, `SPDLOG_DEBUG`, `SPDLOG_INFO`, `SPDLOG_ERROR`, etc. macros
  - are compile-time enabled by `SPDLOG_ACTIVE_LEVEL` macro, you may override in your target to increase verbosity.
  - you can use [fmt](https://github.com/fmtlib/fmt) string format API