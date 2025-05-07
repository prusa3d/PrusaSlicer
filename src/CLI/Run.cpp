#include "../PrusaSlicer.hpp"
#include "CLI.hpp"

namespace Slic3r::CLI {

int run(int argc, char** argv)
{
    Data cli;
    if (!setup(cli, argc, argv))
        return 1;

    if (process_profiles_sharing(cli))
        return 1;

    bool                start_gui          = cli.empty() || (cli.actions_config.empty() && !cli.transform_config.has("cut"));
    PrinterTechnology   printer_technology = get_printer_technology(cli.overrides_config);
    DynamicPrintConfig  print_config       = {};
    std::vector<Model>  models;

#ifdef SLIC3R_GUI
    GUI::GUI_InitParams gui_params;
    start_gui |= init_gui_params(gui_params, argc, argv, cli);

    if (gui_params.start_as_gcodeviewer)
        return start_as_gcode_viewer(gui_params);
#endif

    if (!load_print_data(models, print_config, printer_technology, cli))
        return 1;

    if (!start_gui && is_needed_post_processing(print_config))
        return 0;

    if (!process_transform(cli, print_config, models))
        return 1;

    if (!process_actions(cli, print_config, models))
        return 1;

    if (start_gui) {
#ifdef SLIC3R_GUI
        return start_gui_with_params(gui_params);
#else
        // No GUI support. Just print out a help.
        print_help(false);
        // If started without a parameter, consider it to be OK, otherwise report an error code (no action etc).
        return (argc == 0) ? 0 : 1;
#endif
    }

    return 0;
}

}