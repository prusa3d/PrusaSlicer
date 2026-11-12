#include "SentryScope.hpp"
#include <Slic3r/Log.hpp>
#include <boost/filesystem.hpp>

#ifdef SLIC3R_SENTRY
#include "Slic3r/Version.hpp"
#include "Slic3r/Directories.hpp"

#include <sentry.h>
#include <fmt/format.h>
#include <boost/filesystem/path.hpp>

#ifdef __APPLE__
#include <string>
#include <vector>
#include <mach-o/dyld.h>
#endif

#include "Slic3r/Biz/Network/HttpCurl.hpp"
#include "Slic3r/Directories.hpp"
#endif

namespace Slic3r::App::Launcher {

#if defined(__APPLE__) && defined(SLIC3R_SENTRY)
namespace {
boost::filesystem::path get_executable_dir() {
    uint32_t buffer_size = 0;
    // Get buffer size first
    _NSGetExecutablePath(nullptr, &buffer_size);

    std::vector<char> buffer(buffer_size);
    if (_NSGetExecutablePath(buffer.data(), &buffer_size) != 0) {
        // Handle error: Failed to get the executable path.
        return "";
    }

    using boost::filesystem::path;
    path exe_path{buffer.data()};
    return exe_path.parent_path();
}

} // namespace
#endif

#ifdef SLIC3R_SENTRY
namespace fs = boost::filesystem;
static fs::path get_database_path() {
    const fs::path cache_path{cache_dir()};
    return cache_path / ".sentry-native";
}
#endif

SentryScope::SentryScope()
{
#ifdef SLIC3R_SENTRY
#ifdef __linux__
    Biz::Network::HttpCurl::tls_global_init();
#endif

    static std::string release = fmt::format("{}@{}", APP_NAME, VERSION);

    sentry_options_t* options = sentry_options_new();
    sentry_options_set_dsn(options, SLIC3R_SENTRY_DSN);

    const fs::path database_path{get_database_path()};
    SPDLOG_INFO("Setting Sentry database path: {}", database_path.string());

#ifdef _WIN32
    // boost::fs::path::c_str() is UTF-16 encoded on Windows.
    sentry_options_set_database_pathw(options, database_path.c_str());
#else
    sentry_options_set_database_path(options, database_path.c_str());
#endif

    sentry_options_set_release(options, release.c_str());
    sentry_options_set_debug(
        options,
#ifdef NDEBUG
        0
#else
        1
#endif
    );
    const auto& file_logging = get_file_log_config();
    if (!file_logging.log_file.empty()) {
        sentry_options_add_attachment(options, file_logging.log_file.c_str());
    }

#ifdef __APPLE__
    using boost::filesystem::path;
    if (const auto exe_dir = get_executable_dir(); !exe_dir.empty()) {
        path handler_path = exe_dir / "crashpad_handler";
        sentry_options_set_handler_path(options, handler_path.c_str());
        SPDLOG_INFO("Setting crashpad handler: {}", handler_path.string());
    }
#endif

    sentry_init(options);
#endif
}

SentryScope::~SentryScope()
{
#ifdef SLIC3R_SENTRY
    // make sure everything flushes
    sentry_close();
#endif
}

} // namespace Slic3r::App::Launcher
