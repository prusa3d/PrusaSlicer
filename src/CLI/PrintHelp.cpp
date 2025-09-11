#include <string>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/nowide/iostream.hpp>

#include "CLI.hpp"

namespace Slic3r::CLI {

static void print_help(const ConfigDef& config_def, bool show_defaults, std::function<bool(const ConfigOptionDef&)> filter = [](const ConfigOptionDef &){ return true; })
{

    // prepare a function for wrapping text
    auto wrap = [](const std::string& text, size_t line_length) -> std::string {
        std::istringstream words(text);
        std::ostringstream wrapped;
        std::string word;

        if (words >> word) {
            wrapped << word;
            size_t space_left = line_length - word.length();
            while (words >> word) {
                if (space_left < word.length() + 1) {
                    wrapped << '\n' << word;
                    space_left = line_length - word.length();
                }
                else {
                    wrapped << ' ' << word;
                    space_left -= word.length() + 1;
                }
            }
        }
        return wrapped.str();
        };

    // List of opt_keys that should be hidden from the CLI help.
    const std::vector<std::string> silent_options = { "webdev", "single_instance_on_url" };

    // get the unique categories
    std::set<std::string> categories;
    for (const auto& opt : config_def.options) {
        const ConfigOptionDef& def = opt.second;
        if (filter(def))
            categories.insert(def.category);
    }

    for (const std::string& category : categories) {
        if (category != "") {
            boost::nowide::cout << category << ":" << std::endl;
        }
        else if (categories.size() > 1) {
            boost::nowide::cout << "Misc options:" << std::endl;
        }

        for (const auto& opt : config_def.options) {
            const ConfigOptionDef& def = opt.second;
            if (def.category != category || def.cli == ConfigOptionDef::nocli || !filter(def))
                continue;

            if (std::find(silent_options.begin(), silent_options.end(), opt.second.opt_key) != silent_options.end())
                continue;

            // get all possible variations: --foo, --foobar, -f...
            std::vector<std::string> cli_args = def.cli_args(opt.first);
            if (cli_args.empty())
                continue;

            for (auto& arg : cli_args) {
                arg.insert(0, (arg.size() == 1) ? "-" : "--");
                if (def.type == coFloat || def.type == coInt || def.type == coFloatOrPercent
                    || def.type == coFloats || def.type == coInts) {
                    arg += " N";
                }
                else if (def.type == coPoint) {
                    arg += " X,Y";
                }
                else if (def.type == coPoint3) {
                    arg += " X,Y,Z";
                }
                else if (def.type == coString || def.type == coStrings) {
                    arg += " ABCD";
                }
            }

            // left: command line options
            const std::string cli = boost::algorithm::join(cli_args, ", ");
            boost::nowide::cout << " " << std::left << std::setw(20) << cli;

            // right: option description
            std::string descr = def.tooltip;
            bool show_defaults_this = show_defaults || def.opt_key == "config_compatibility";
            if (show_defaults_this && def.default_value && def.type != coBool
                && (def.type != coString || !def.default_value->serialize().empty())) {
                descr += " (";
                if (!def.sidetext.empty()) {
                    descr += def.sidetext + ", ";
                }
                else if (def.enum_def && def.enum_def->has_values()) {
                    descr += boost::algorithm::join(def.enum_def->values(), ", ") + "; ";
                }
                descr += "default: " + def.default_value->serialize() + ")";
            }

            // wrap lines of description
            descr = wrap(descr, 80);
            std::vector<std::string> lines;
            boost::split(lines, descr, boost::is_any_of("\n"));

            // if command line options are too long, print description in new line
            for (size_t i = 0; i < lines.size(); ++i) {
                if (i == 0 && cli.size() > 19)
                    boost::nowide::cout << std::endl;
                if (i > 0 || cli.size() > 19)
                    boost::nowide::cout << std::string(21, ' ');
                boost::nowide::cout << lines[i] << std::endl;
            }
        }
    }
}

void print_help(bool include_print_options/* = false*/, PrinterTechnology printer_technology/* = ptAny*/)
{
    boost::nowide::cout
        << SLIC3R_BUILD_ID << " " << "based on Slic3r"
#ifdef SLIC3R_GUI
        << " (with GUI support)"
#else /* SLIC3R_GUI */
        << " (without GUI support)"
#endif /* SLIC3R_GUI */
        << std::endl
        << "https://github.com/prusa3d/PrusaSlicer" << std::endl << std::endl
        << "Usage: prusa-slicer [ INPUT ] [ OPTIONS ] [ ACTIONS ] [ TRANSFORM ] [ file.stl ... ]" << std::endl;

    boost::nowide::cout
        << std::endl
        << "Input:" << std::endl;
    print_help(cli_input_config_def, false);

    boost::nowide::cout
        << std::endl
        << "Note: To load configuration from profiles, you need to set whole banch of presets" << std::endl;

    boost::nowide::cout
        << std::endl
        << "Actions:" << std::endl;
    print_help(cli_actions_config_def, false);

    boost::nowide::cout
        << std::endl
        << "Transform options:" << std::endl;
    print_help(cli_transform_config_def, false);

    boost::nowide::cout
        << std::endl
        << "Other options:" << std::endl;
    print_help(cli_misc_config_def, false);

    boost::nowide::cout
        << std::endl
        << "Print options are processed in the following order:" << std::endl
        << "\t1) Config keys from the command line, for example --fill-pattern=stars" << std::endl
        << "\t   (highest priority, overwrites everything below)" << std::endl
        << "\t2) Config files loaded with --load" << std::endl
        << "\t3) Config values loaded from 3mf files" << std::endl;

    if (include_print_options) {
        boost::nowide::cout << std::endl;
        print_help(print_config_def, true, [printer_technology](const ConfigOptionDef& def)
            { return printer_technology == ptAny || def.printer_technology == ptAny || printer_technology == def.printer_technology; });
    }
    else {
        boost::nowide::cout
            << std::endl
            << "Run --help-fff / --help-sla to see the full listing of print options." << std::endl;
    }
}

}