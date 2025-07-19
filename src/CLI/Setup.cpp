#include <boost/nowide/cstdlib.hpp>
#include <boost/nowide/filesystem.hpp>
#include <boost/nowide/cstdio.hpp>
#include <boost/nowide/iostream.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/dll/runtime_symbol_info.hpp>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Config.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Platform.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/Thread.hpp"
#include "libslic3r/BlacklistedLibraryCheck.hpp"
#include "libslic3r/Utils/DirectoriesUtils.hpp"

#include "CLI.hpp"

#ifdef SLIC3R_GUI
#include "slic3r/Utils/ServiceConfig.hpp"
#endif /* SLIC3R_GUI */


namespace Slic3r::CLI {

Data::Data() 
{
    input_config        = CLI_DynamicPrintConfig(Type::Input,           &cli_input_config_def);
    overrides_config    = CLI_DynamicPrintConfig(Type::Overrides,       &print_config_def);
    transform_config    = CLI_DynamicPrintConfig(Type::Transformations, &cli_transform_config_def);
    misc_config         = CLI_DynamicPrintConfig(Type::Misc,            &cli_misc_config_def);
    actions_config      = CLI_DynamicPrintConfig(Type::Actions,         &cli_actions_config_def);
}

using opts_map = std::map<std::string, std::pair<std::string, Type> >;

static opts_map get_opts_map(const Data& data)
{
    opts_map ret;

    for (const CLI_DynamicPrintConfig* config : { &data.input_config    ,
                                                  &data.overrides_config,
                                                  &data.transform_config,
                                                  &data.misc_config     ,
                                                  &data.actions_config     }) 
    {
        for (const auto& oit : config->def()->options)
            for (const std::string& t : oit.second.cli_args(oit.first))
                ret[t] = { oit.first , config->type()};
    }

    return ret;
}

static CLI_DynamicPrintConfig* get_config(Data& data, Type type)
{
    for (CLI_DynamicPrintConfig* config : { &data.input_config    ,
                                            &data.overrides_config,
                                            &data.transform_config,
                                            &data.misc_config     ,
                                            &data.actions_config })
    {
        if (type == config->type())
            return config;
    }
    
    assert(false);
    return nullptr;
}

static bool read(Data& data, int argc, const char* const argv[])
{
    // cache the CLI option => opt_key mapping
    opts_map opts = get_opts_map(data);

    bool parse_options = true;
    for (int i = 1; i < argc; ++i) {
        std::string token = argv[i];
        // Store non-option arguments in the provided vector.
        if (!parse_options || !boost::starts_with(token, "-")) {
            data.input_files.push_back(token);
            continue;
        }
#ifdef __APPLE__
        if (boost::starts_with(token, "-psn_"))
            // OSX launcher may add a "process serial number", for example "-psn_0_989382" to the command line.
            // While it is supposed to be dropped since OSX 10.9, we will rather ignore it.
            continue;
#endif /* __APPLE__ */
        // Stop parsing tokens as options when -- is supplied.
        if (token == "--") {
            parse_options = false;
            continue;
        }
        // Remove leading dashes (one or two).
        token.erase(token.begin(), token.begin() + (boost::starts_with(token, "--") ? 2 : 1));
        // Read value when supplied in the --key=value form.
        std::string value;
        {
            size_t equals_pos = token.find("=");
            if (equals_pos != std::string::npos) {
                value = token.substr(equals_pos + 1);
                token.erase(equals_pos);
            }
        }
        // Look for the cli -> option mapping.
        auto it = opts.find(token);
        bool no = false;
        if (it == opts.end()) {
            // Remove the "no-" prefix used to negate boolean options.
            std::string yes_token;
            if (boost::starts_with(token, "no-")) {
                yes_token = token.substr(3);
                it = opts.find(yes_token);
                no = true;
            }
            if (it == opts.end()) {
                boost::nowide::cerr << "Unknown option --" << token.c_str() << std::endl;
                return false;
            }
            if (no)
                token = yes_token;
        }

        const auto& [opt_key, type] = it->second;

        CLI_DynamicPrintConfig* config = get_config(data, type);
        const ConfigOptionDef*  optdef = config->option_def(opt_key);
        assert(optdef);

        // If the option type expects a value and it was not already provided,
        // look for it in the next token.
        if (value.empty() && optdef->type != coBool && optdef->type != coBools) {
            if (i == argc - 1) {
                boost::nowide::cerr << "No value supplied for --" << token.c_str() << std::endl;
                return false;
            }
            value = argv[++i];
        }

        if (no) {
            assert(optdef->type == coBool || optdef->type == coBools);
            if (!value.empty()) {
                boost::nowide::cerr << "Boolean options negated by the --no- prefix cannot have a value." << std::endl;
                return false;
            }
        }

        // Store the option value.
        const bool               existing = config->has(opt_key);
        ConfigOption* opt_base = existing ? config->option(opt_key) : optdef->create_default_option();
        if (!existing)
            config->set_key_value(opt_key, opt_base);
        ConfigOptionVectorBase* opt_vector = opt_base->is_vector() ? static_cast<ConfigOptionVectorBase*>(opt_base) : nullptr;
        if (opt_vector) {
            if (!existing)
                // remove the default values
                opt_vector->clear();
            // Vector values will be chained. Repeated use of a parameter will append the parameter or parameters
            // to the end of the value.
            if (opt_base->type() == coBools && value.empty())
                static_cast<ConfigOptionBools*>(opt_base)->values.push_back(!no);
            else
                // Deserialize any other vector value (ConfigOptionInts, Floats, Percents, Points) the same way
                // they get deserialized from an .ini file. For ConfigOptionStrings, that means that the C-style unescape
                // will be applied for values enclosed in quotes, while values non-enclosed in quotes are left to be
                // unescaped by the calling shell.
                opt_vector->deserialize(value, true);
        }
        else if (opt_base->type() == coBool) {
            if (value.empty())
                static_cast<ConfigOptionBool*>(opt_base)->value = !no;
            else
                opt_base->deserialize(value);
        }
        else if (opt_base->type() == coString) {
            // Do not unescape single string values, the unescaping is left to the calling shell.
            static_cast<ConfigOptionString*>(opt_base)->value = value;
        }
        else {
            // Just bail out if the configuration value is not understood.
            ConfigSubstitutionContext context(ForwardCompatibilitySubstitutionRule::Disable);
            // Any scalar value of a type different from Bool and String.
            if (!config->set_deserialize_nothrow(opt_key, value, context, false)) {
                boost::nowide::cerr << "Invalid value supplied for --" << token.c_str() << std::endl;
                return false;
            }
        }
    }

    // normalize override options 
    if (!data.overrides_config.empty())
        data.overrides_config.normalize_fdm();

    if (!data.misc_config.has("config_compatibility")) {
        // "config_compatibility" can be used during the loading configuration
        // So, if this option wasn't set, then initialise it from default value
        const ConfigOptionDef* optdef = cli_misc_config_def.get("config_compatibility");
        ConfigOption* opt_with_def_value = optdef->create_default_option();
        if (opt_with_def_value)
            data.misc_config.set_key_value("config_compatibility", opt_with_def_value);
    }

    return true;
}

static bool setup_common()
{
    // Mark the main thread for the debugger and for runtime checks.
    set_current_thread_name("slic3r_main");
    // Save the thread ID of the main thread.
    save_main_thread_id();

#ifdef __WXGTK__
    // On Linux, wxGTK has no support for Wayland, and the app crashes on
    // startup if gtk3 is used. This env var has to be set explicitly to
    // instruct the window manager to fall back to X server mode.
    ::setenv("GDK_BACKEND", "x11", /* replace */ true);

    // https://github.com/prusa3d/PrusaSlicer/issues/12969
    ::setenv("WEBKIT_DISABLE_COMPOSITING_MODE", "1", /* replace */ false);
    ::setenv("WEBKIT_DISABLE_DMABUF_RENDERER", "1", /* replace */ false);
#endif

    // Switch boost::filesystem to utf8.
    try {
        boost::nowide::nowide_filesystem();
    }
    catch (const std::runtime_error& ex) {
        std::string caption = std::string(SLIC3R_APP_NAME) + " Error";
        std::string text = std::string("An error occured while setting up locale.\n") + (
#if !defined(_WIN32) && !defined(__APPLE__)
            // likely some linux system
            "You may need to reconfigure the missing locales, likely by running the \"locale-gen\" and \"dpkg-reconfigure locales\" commands.\n"
#endif
            SLIC3R_APP_NAME " will now terminate.\n\n") + ex.what();
#if defined(_WIN32) && defined(SLIC3R_GUI)
        MessageBoxA(NULL, text.c_str(), caption.c_str(), MB_OK | MB_ICONERROR);
#endif
        boost::nowide::cerr << text.c_str() << std::endl;
        return false;
    }

    {
        Slic3r::set_logging_level(1);
        const char* loglevel = boost::nowide::getenv("SLIC3R_LOGLEVEL");
        if (loglevel != nullptr) {
            if (loglevel[0] >= '0' && loglevel[0] <= '9' && loglevel[1] == 0)
                set_logging_level(loglevel[0] - '0');
            else
                boost::nowide::cerr << "Invalid SLIC3R_LOGLEVEL environment variable: " << loglevel << std::endl;
        }
    }

    // Detect the operating system flavor after SLIC3R_LOGLEVEL is set.
    detect_platform();

#ifdef WIN32
    // Notify user that a blacklisted DLL was injected into PrusaSlicer process (for example Nahimic, see GH #5573).
    // We hope that if a DLL is being injected into a PrusaSlicer process, it happens at the very start of the application,
    // thus we shall detect them now.
    if (BlacklistedLibraryCheck::get_instance().perform_check()) {
        std::wstring text = L"Following DLLs have been injected into the PrusaSlicer process:\n\n";
        text += BlacklistedLibraryCheck::get_instance().get_blacklisted_string();
        text += L"\n\n"
                L"PrusaSlicer is known to not run correctly with these DLLs injected. "
                L"We suggest stopping or uninstalling these services if you experience "
                L"crashes or unexpected behaviour while using PrusaSlicer.\n"
                L"For example, ASUS Sonic Studio injects a Nahimic driver, which makes PrusaSlicer "
                L"to crash on a secondary monitor, see PrusaSlicer github issue #5573";
        MessageBoxW(NULL, text.c_str(), L"Warning"/*L"Incopatible library found"*/, MB_OK);
    }
#endif

    // See Invoking prusa-slicer from $PATH environment variable crashes #5542
    // boost::filesystem::path path_to_binary = boost::filesystem::system_complete(argv[0]);
    boost::filesystem::path path_to_binary = boost::dll::program_location();

    // Path from the Slic3r binary to its resources.
#ifdef __APPLE__
    // The application is packed in the .dmg archive as 'Slic3r.app/Contents/MacOS/Slic3r'
    // The resources are packed to 'Slic3r.app/Contents/Resources'
    boost::filesystem::path path_resources = boost::filesystem::canonical(path_to_binary).parent_path() / "../Resources";
#elif defined _WIN32
    // The application is packed in the .zip archive in the root,
    // The resources are packed to 'resources'
    // Path from Slic3r binary to resources:
    boost::filesystem::path path_resources = path_to_binary.parent_path() / "resources";
#elif defined SLIC3R_FHS
    // The application is packaged according to the Linux Filesystem Hierarchy Standard
    // Resources are set to the 'Architecture-independent (shared) data', typically /usr/share or /usr/local/share
    boost::filesystem::path path_resources = SLIC3R_FHS_RESOURCES;
#else
    // The application is packed in the .tar.bz archive (or in AppImage) as 'bin/slic3r',
    // The resources are packed to 'resources'
    // Path from Slic3r binary to resources:
    boost::filesystem::path path_resources = boost::filesystem::canonical(path_to_binary).parent_path() / "../resources";
#endif

    set_resources_dir(path_resources.string());
    set_var_dir((path_resources / "icons").string());
    set_local_dir((path_resources / "localization").string());
    set_sys_shapes_dir((path_resources / "shapes").string());
    set_custom_gcodes_dir((path_resources / "custom_gcodes").string());

    return true;
}

bool setup(Data& cli, int argc, char** argv)
{
    if (!setup_common())
        return false;

    if (!read(cli, argc, argv)) {
        // Separate error message reported by the CLI parser from the help.
        boost::nowide::cerr << std::endl;
        print_help();
        return false;
    }

    if (cli.misc_config.has("loglevel"))
    {
        int loglevel = cli.misc_config.opt_int("loglevel");
        if (loglevel != 0)
            set_logging_level(loglevel);
    }

    if (cli.misc_config.has("threads"))
        thread_count = cli.misc_config.opt_int("threads");

    set_data_dir(cli.misc_config.has("datadir") ? cli.misc_config.opt_string("datadir") : get_default_datadir());

#ifdef SLIC3R_GUI
    if (cli.misc_config.has("webdev")) {
        Utils::ServiceConfig::instance().set_webdev_enabled(cli.misc_config.opt_bool("webdev"));
    }
#endif
    return true;
}

}