#include <string>

#include <boost/filesystem.hpp>
#include <boost/nowide/iostream.hpp>
#include <boost/nowide/cstdlib.hpp>

#include "CLI.hpp"

#ifdef SLIC3R_GUI

namespace Slic3r::CLI {

bool init_gui_params(GUI::GUI_InitParams& gui_params, int argc, char** argv, Data& cli)
{
    bool start_gui = false;

    gui_params.argc = argc;
    gui_params.argv = argv;
    gui_params.input_files = cli.input_files;

    if (cli.misc_config.has("opengl-aa")) {
        start_gui = true;
        gui_params.opengl_aa = true;
    }

    // are we starting as gcodeviewer ?
    if (cli.actions_config.has("gcodeviewer")) {
        start_gui = true;
        gui_params.start_as_gcodeviewer = true;
    }
#ifndef _WIN32
    else {
        // On Unix systems, the prusa-slicer binary may be symlinked to give the application a different meaning.
        gui_params.start_as_gcodeviewer = boost::algorithm::iends_with(boost::filesystem::path(argv[0]).filename().string(), "gcodeviewer");
    }
#endif // _WIN32

#if !SLIC3R_OPENGL_ES
    if (cli.misc_config.has("opengl-version")) {
        const Semver opengl_minimum = Semver(3, 2, 0);
        const std::string opengl_version_str = cli.misc_config.opt_string("opengl-version");
        boost::optional<Semver> semver = Semver::parse(opengl_version_str);
        if (semver.has_value() && (*semver) >= opengl_minimum) {
            std::pair<int, int>& version = gui_params.opengl_version;
            version.first = semver->maj();
            version.second = semver->min();
            if (std::find(Slic3r::GUI::OpenGLVersions::core.begin(), Slic3r::GUI::OpenGLVersions::core.end(), std::make_pair(version.first, version.second)) == Slic3r::GUI::OpenGLVersions::core.end()) {
                version = { 0, 0 };
                boost::nowide::cerr << "Required OpenGL version " << opengl_version_str << " not recognized.\n Option 'opengl-version' ignored." << std::endl;
            }
        }
        else
            boost::nowide::cerr << "Required OpenGL version " << opengl_version_str << " is invalid. Must be greater than or equal to " <<
            opengl_minimum.to_string() << "\n Option 'opengl-version' ignored." << std::endl;
        start_gui = true;
    }

    if (cli.misc_config.has("opengl-compatibility")) {
        start_gui = true;
        gui_params.opengl_compatibility_profile = true;
        // reset version as compatibility profile always take the highest version
        // supported by the graphic card
        gui_params.opengl_version = std::make_pair(0, 0);
    }

    if (cli.misc_config.has("opengl-debug")) {
        start_gui = true;
        gui_params.opengl_debug = true;
    }
#endif // SLIC3R_OPENGL_ES

    if (cli.misc_config.has("delete-after-load")) {
        gui_params.delete_after_load = true;
    }

    if (!gui_params.start_as_gcodeviewer && !cli.input_config.has("load")) {
        // Read input file(s) if any and check if can start GcodeViewer
        if (cli.input_files.size() == 1 && is_gcode_file(cli.input_files[0]) && boost::filesystem::exists(cli.input_files[0]))
            gui_params.start_as_gcodeviewer = true;
    }

    if (has_full_config_from_profiles(cli)) {
        gui_params.selected_presets = Slic3r::GUI::CLISelectedProfiles{ cli.input_config.opt_string("print-profile"),
                                                                        cli.input_config.opt_string("printer-profile") ,
                                                                        cli.input_config.option<ConfigOptionStrings>("material-profile")->values };
    }

    if (!cli.overrides_config.empty())
        gui_params.extra_config = cli.overrides_config;

    if (cli.input_config.has("load"))
        gui_params.load_configs = cli.input_config.option<ConfigOptionStrings>("load")->values;

    for (const std::string& file : cli.input_files) {
        if (boost::starts_with(file, "prusaslicer://")) {
            gui_params.start_downloader = true;
            gui_params.download_url = file;
            break;
        }
    }

    return start_gui;
}

int start_gui_with_params(GUI::GUI_InitParams& params)
{
#if !defined(_WIN32) && !defined(__APPLE__)
    // likely some linux / unix system
    const char* display = boost::nowide::getenv("DISPLAY");
    // const char *wayland_display = boost::nowide::getenv("WAYLAND_DISPLAY");
    //if (! ((display && *display) || (wayland_display && *wayland_display))) {
    if (!(display && *display)) {
        // DISPLAY not set.
        boost::nowide::cerr << "DISPLAY not set, GUI mode not available." << std::endl << std::endl;
        print_help(false);
        // Indicate an error.
        return 1;
    }
#endif // some linux / unix system
    return Slic3r::GUI::GUI_Run(params);
}

int start_as_gcode_viewer(GUI::GUI_InitParams& gui_params)
{
    if (gui_params.input_files.size() > 1) {
        boost::nowide::cerr << "You can open only one .gcode file at a time in GCodeViewer" << std::endl;
        return 1;
    }

    if (!gui_params.input_files.empty()) {
        const std::string& file = gui_params.input_files[0];
        if (!is_gcode_file(file) || !boost::filesystem::exists(file)) {
            boost::nowide::cerr << "Input file isn't a .gcode file or doesn't exist. GCodeViewer can't be start." << std::endl;
            return 1;
        }
    }

    return start_gui_with_params(gui_params);
}

}
#else // SLIC3R_GUI
    // If there is no GUI, we shall ignore the parameters. Remove them from the list.
#endif // SLIC3R_GUI