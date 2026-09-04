#ifdef _WIN32
#include <windows.h>
#endif
#ifdef SLIC3R_GUI
#include "Slic3r/App/Desktop/Run.hpp"
#endif

#include "Slic3r/App/CLI/CLIApp.hpp"
#include "Slic3r/App/Launcher/ReadCLI.hpp"
#include "Slic3r/Biz/Network/ServiceConfig.hpp"
#include "Slic3r/Assert.hpp"
#include "Slic3r/Directories.hpp"
#include "Slic3r/Log.hpp"
#include "Slic3r/App/AppConfig.hpp"
#include "Slic3r/App/AppServices.hpp"

#include "SentryScope.hpp"

#include <cstdlib>
#include <string>

#include <CLI/CLI.hpp>

#include <boost/filesystem/path.hpp>

#include "Slic3r/Version.hpp"
#include "libslic3r/Utils.hpp"

#if !defined(__has_feature)
#define __has_feature(x) 0
#endif

#if __has_feature(address_sanitizer) || defined(__SANITIZE_ADDRESS__)

extern "C" {

    const char* __lsan_default_suppressions();

    const char* __lsan_default_suppressions()
    {
        return "leak:libfontconfig\n" // FontConfig looks like it leaks, but it doesn't.
               "leak:libglib-2.0.so\n"
               "leak:TPrsStd_DriverTable::Get\n"; // OpenCASCADE singleton, not a real leak.
    }
}

#endif

namespace {
constexpr bool has_gui_support =
#ifdef SLIC3R_GUI
    true;
#else
    false;
#endif

const std::string app_name = "prusa-slicer-launcher";

std::string app_description()
{
    std::stringstream description;
    description << SLIC3R_BUILD_ID << " " << "based on Slic3r";

    if constexpr (has_gui_support) {
        description << " (with GUI support)";
    } else {
        description << " (without GUI support)";
    }

    description << std::endl;
    description << "https://github.com/prusa3d/PrusaSlicer";

    return description.str();
}

} // namespace


int main(int argc, char** argv)
{
    CLI::App app{app_description(), app_name};
    argv = app.ensure_utf8(argv);

    Slic3r::App::init_common();

    Slic3r::App::InitParams init_params =
        Slic3r::App::Launcher::read_cli(app, SLIC3R_VERSION, argc, argv);
    if (init_params.exit_code.has_value()) {
        return init_params.exit_code.value();
    }

    Slic3r::App::init_paths(init_params);

    boost::filesystem::path log_path = boost::filesystem::path{Slic3r::get_default_datadir()} / "shared_runtime" / "log.txt";
    Slic3r::FileLogConfig file_log_config;
    file_log_config.log_file = log_path.string();
    file_log_config.log_file_size = 1 * 1024 * 1024;
    Slic3r::set_file_log_config(file_log_config);
    Slic3r::init_logging();

    if (init_params.misc.loglevel.has_value()) {
        Slic3r::set_log_level(init_params.misc.loglevel.value());
    } else {
        Slic3r::set_log_level(4);
    }

    if (init_params.misc.threads.has_value()) {
        Slic3r::thread_count = init_params.misc.threads.value();
    }

    Slic3r::init_assert();

    auto& app_services{Slic3r::App::AppServices::instance()};
    app_services.set_app_config(Slic3r::App::AppConfig::create_app_config());

    const std::optional<Slic3r::App::Launcher::SentryScope> sentry{
        app_services.app_config().is_sentry_enabled() ?
            std::optional<Slic3r::App::Launcher::SentryScope>{std::in_place} :
            std::nullopt};

#ifdef SLIC3R_GUI
    if (init_params.misc.webdev.has_value()) {
        Slic3r::Biz::Network::ServiceConfig::instance().set_webdev_enabled(
            init_params.misc.webdev.value()
        );
    }
#endif

    if (!init_params.action.has_any_action() || init_params.action.gcode_viewer) {
        if constexpr (has_gui_support) {
#ifdef _WIN32
            // We are a console-subsystem exe so that CLI usage from an existing
            // terminal blocks synchronously and inherits stdio as expected.
            // When launched without a parent console (double-click, Explorer,
            // Start Menu), Windows allocates us a fresh one; detach it before
            // showing the GUI so no console window is visible. A console we
            // inherited from a shell has the shell attached too, so the
            // process count there is at least 2 and we leave it alone.
            DWORD console_procs[2];
            if (GetConsoleProcessList(console_procs, 2) <= 1) {
                FreeConsole();
            }
#endif
            return Slic3r::App::Desktop::run(init_params, app_services);
        } else {
            SPDLOG_ERROR("PrusaSlicer was built without GUI support. Quitting.");
            return EXIT_FAILURE;
        }
    }

    return Slic3r::App::CLI::run(init_params);
}
