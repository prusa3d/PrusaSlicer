#pragma once

#include <string>
#include <vector>

#include "libslic3r/Model.hpp"
#include "CLI_DynamicPrintConfig.hpp"

#ifdef SLIC3R_GUI
#include "slic3r/GUI/GUI_Init.hpp"
#endif

namespace Slic3r::CLI
{
    // struct which is filled from comand line input
    struct Data
    {
        Data();

        CLI_DynamicPrintConfig      input_config;
        CLI_DynamicPrintConfig      overrides_config;
        CLI_DynamicPrintConfig      transform_config;
        CLI_DynamicPrintConfig      misc_config;
        CLI_DynamicPrintConfig      actions_config;

        std::vector<std::string>    input_files;

        bool empty() {
            return input_files.empty()
                && input_config.empty()
                && overrides_config.empty()
                && transform_config.empty()
                && actions_config.empty();
        }
    };

    // Implemented in PrintHelp.cpp

    void    print_help(bool include_print_options = false, PrinterTechnology printer_technology = ptAny);

    // Implemented in Setup.cpp

    bool    setup(Data& cli, int argc, char** argv);

    // Implemented in LoadPrintData.cpp

    PrinterTechnology   get_printer_technology(const DynamicConfig& config);
    bool                load_print_data(std::vector<Model>& models,
                                        DynamicPrintConfig& print_config,
                                        PrinterTechnology& printer_technology,
                                        Data& cli);
    bool                is_needed_post_processing(const DynamicPrintConfig& print_config);

    // Implemented in ProcessTransform.cpp

    bool    process_transform(Data& cli, const DynamicPrintConfig& print_config, std::vector<Model>& models);

    // Implemented in ProcessActions.cpp

    bool    has_full_config_from_profiles(const Data& cli);
    bool    process_profiles_sharing(const Data& cli);
    bool    process_actions(Data& cli, const DynamicPrintConfig& print_config, std::vector<Model>& models);

    // Implemented in GuiParams.cpp
#ifdef SLIC3R_GUI
            // set data for init GUI parameters
            // and return state of start_gui
    bool    init_gui_params(GUI::GUI_InitParams& gui_params, int argc, char** argv, Data& cli);
    int     start_gui_with_params(GUI::GUI_InitParams& params);
    int     start_as_gcode_viewer(GUI::GUI_InitParams& gui_params);
#endif

}
