///|/ Copyright (c) Prusa Research 2020 - 2023 David Kocík @kocikdav, Enrico Turri @enricoturri1966, Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_GUI_Init_hpp_
#define slic3r_GUI_Init_hpp_

#include <libslic3r/Preset.hpp>
#include <libslic3r/PrintConfig.hpp>

namespace Slic3r {

namespace GUI {

struct OpenGLVersions
{
	static const std::vector<std::pair<int, int>> core;
};

struct CLISelectedProfiles
{
    std::string                 print;
    std::string                 printer;
    std::vector<std::string>    materials;

    bool has_valid_data() { return !print.empty() && !printer.empty() && !materials.empty(); }
};

struct GUI_InitParams
{
	int		                    argc;
	char	                  **argv;

	// Substitutions of unknown configuration values done during loading of user presets.
	PresetsConfigSubstitutions  preset_substitutions;

    std::vector<std::string>    load_configs;
    DynamicPrintConfig          extra_config;
    std::vector<std::string>    input_files;
    CLISelectedProfiles         selected_presets;

    bool                        start_as_gcodeviewer;
    bool                        start_downloader;
    bool                        delete_after_load;
    std::string                 download_url;
#if !SLIC3R_OPENGL_ES
		std::pair<int, int>         opengl_version;
		bool                        opengl_debug;
		bool                        opengl_compatibiity_profile;
#endif // !SLIC3R_OPENGL_ES
		bool                        opengl_aa;
};

int GUI_Run(GUI_InitParams &params);

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_Init_hpp_
