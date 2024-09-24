///|/ Copyright (c) Prusa Research 2020 - 2023 Oleksandra Iushchenko @YuSanka, Vojtěch Bubník @bubnikv, Enrico Turri @enricoturri1966, Tomáš Mészáros @tamasmeszaros, Lukáš Matěna @lukasmatena
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "libslic3r/Technologies.hpp"
#include "GUI_Init.hpp"

#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Utils/DirectoriesUtils.hpp"

#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/3DScene.hpp"
#include "slic3r/GUI/InstanceCheck.hpp" 
#include "slic3r/GUI/format.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/I18N.hpp"


// To show a message box if GUI initialization ends up with an exception thrown.
#include <wx/msgdlg.h>

#include <boost/nowide/iostream.hpp>
#include <boost/nowide/convert.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/console.hpp>

#if __APPLE__
    #include <signal.h>
#endif // __APPLE__

namespace Slic3r {
namespace GUI {

const std::vector<std::pair<int, int>> OpenGLVersions::core    = { {3,2}, {3,3}, {4,0}, {4,1}, {4,2}, {4,3}, {4,4}, {4,5}, {4,6} };

int GUI_Run(GUI_InitParams &params)
{
#if __APPLE__
    // On OSX, we use boost::process::spawn() to launch new instances of PrusaSlicer from another PrusaSlicer.
    // boost::process::spawn() sets SIGCHLD to SIGIGN for the child process, thus if a child PrusaSlicer spawns another
    // subprocess and the subrocess dies, the child PrusaSlicer will not receive information on end of subprocess
    // (posix waitpid() call will always fail).
    // https://jmmv.dev/2008/10/boostprocess-and-sigchld.html
    // The child instance of PrusaSlicer has to reset SIGCHLD to its default, so that posix waitpid() and similar continue to work.
    // See GH issue #5507
    signal(SIGCHLD, SIG_DFL);
#endif // __APPLE__

#ifdef SLIC3R_LOG_TO_FILE
    auto sink = boost::log::add_file_log(get_default_datadir() + "/slicer.log");
    sink->locked_backend()->auto_flush();
    boost::log::add_console_log();
#endif // SLIC3R_LOG_TO_FILE
    try {
        GUI::GUI_App* gui = new GUI::GUI_App(params.start_as_gcodeviewer ? GUI::GUI_App::EAppMode::GCodeViewer : GUI::GUI_App::EAppMode::Editor);
        if (gui->get_app_mode() != GUI::GUI_App::EAppMode::GCodeViewer) {
            // G-code viewer is currently not performing instance check, a new G-code viewer is started every time.
            bool gui_single_instance_setting = gui->app_config->get_bool("single_instance");
            if (Slic3r::instance_check(params.argc, params.argv, gui_single_instance_setting)) {
                //TODO: do we have delete gui and other stuff?
                return -1;
            }
        }

        GUI::GUI_App::SetInstance(gui);
        gui->init_params = &params;
        return wxEntry(params.argc, params.argv);
    } catch (const Slic3r::Exception &ex) {
        boost::nowide::cerr << ex.what() << std::endl;
        wxMessageBox(boost::nowide::widen(ex.what()), _L("PrusaSlicer GUI initialization failed"), wxICON_STOP);
    } catch (const std::exception &ex) {
        boost::nowide::cerr << "PrusaSlicer GUI initialization failed: " << ex.what() << std::endl;
        wxMessageBox(format_wxstr(_L("Fatal error, exception catched: %1%"), ex.what()), _L("PrusaSlicer GUI initialization failed"), wxICON_STOP);
    }

    // error
    return 1;
}
    
}
}
