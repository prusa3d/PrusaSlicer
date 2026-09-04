#include "PostProcessor.hpp"

#include "Slic3r/Biz/libpgcode/PostProcessorConfig.hpp"
#include "Slic3r/Biz/libpgcode/Processor.hpp"
#include "Slic3r/Biz/libpgcode/ProcessorResult.hpp"

#include "Slic3r/Biz/libpgcode/Utils.hpp"
#include "Slic3r/Exception.hpp"
#include "libslic3r/Extruder.hpp"
#include "Slic3r/Time.hpp"
#include "Slic3r/LegacyFormat.hpp"
#include "libslic3r/I18N_private.hpp"
#include "Slic3r/Biz/GCodeReader/GCodeReader.hpp"
#include "libslic3r/Utils.hpp"

#include <boost/algorithm/string.hpp>
#include <Slic3r/Log.hpp>
#include <boost/format.hpp>
#include <boost/filesystem.hpp>
#include <boost/nowide/cstdlib.hpp>
#include <boost/nowide/convert.hpp>
#include <boost/nowide/fstream.hpp>
#include <fmt/format.h>

#include <numeric>

#ifdef WIN32

// The standard Windows includes.
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <shellapi.h>

// https://blogs.msdn.microsoft.com/twistylittlepassagesallalike/2011/04/23/everyone-quotes-command-line-arguments-the-wrong-way/
// This routine appends the given argument to a command line such that CommandLineToArgvW will return the argument string unchanged.
// Arguments in a command line should be separated by spaces; this function does not add these spaces.
// Argument    - Supplies the argument to encode.
// CommandLine - Supplies the command line to which we append the encoded argument string.
static void quote_argv_winapi(const std::wstring &argument, std::wstring &commmand_line_out)
{
	// Don't quote unless we actually need to do so --- hopefully avoid problems if programs won't parse quotes properly.
	if (argument.empty() == false && argument.find_first_of(L" \t\n\v\"") == argument.npos)
		commmand_line_out.append(argument);
	else {
		commmand_line_out.push_back(L'"');
		for (auto it = argument.begin(); ; ++ it) {
			unsigned number_backslashes = 0;
			while (it != argument.end() && *it == L'\\') {
				++ it;
				++ number_backslashes;
			}
			if (it == argument.end()) {
				// Escape all backslashes, but let the terminating double quotation mark we add below be interpreted as a metacharacter.
				commmand_line_out.append(number_backslashes * 2, L'\\');
				break;
			} else if (*it == L'"') {
				// Escape all backslashes and the following double quotation mark.
				commmand_line_out.append(number_backslashes * 2 + 1, L'\\');
				commmand_line_out.push_back(*it);
			} else {
				// Backslashes aren't special here.
				commmand_line_out.append(number_backslashes, L'\\');
				commmand_line_out.push_back(*it);
			}
		}
		commmand_line_out.push_back(L'"');
	}
}

static DWORD execute_process_winapi(const std::wstring &command_line)
{
    // Extract the current environment to be passed to the child process.
	std::wstring envstr;
	{
		wchar_t *env = GetEnvironmentStrings();
		assert(env != nullptr);
		const wchar_t* var = env;
		size_t totallen = 0;
		size_t len;
		while ((len = wcslen(var)) > 0) {
			totallen += len + 1;
			var += len + 1;
		}
		envstr = std::wstring(env, totallen);
		FreeEnvironmentStrings(env);
	}

	STARTUPINFOW startup_info;
	memset(&startup_info, 0, sizeof(startup_info));
	startup_info.cb			 = sizeof(STARTUPINFO);
#if 0
	startup_info.dwFlags	 = STARTF_USESHOWWINDOW;
	startup_info.wShowWindow = SW_HIDE;
#endif
	PROCESS_INFORMATION process_info;
	if (! ::CreateProcessW(
            nullptr /* lpApplicationName */, (LPWSTR)command_line.c_str(), nullptr /* lpProcessAttributes */, nullptr /* lpThreadAttributes */, false /* bInheritHandles */,
			CREATE_UNICODE_ENVIRONMENT /* | CREATE_NEW_CONSOLE */ /* dwCreationFlags */, (LPVOID)envstr.c_str(), nullptr /* lpCurrentDirectory */, &startup_info, &process_info))
        throw Slic3r::RuntimeError(std::string("Failed starting the script ") + boost::nowide::narrow(command_line) + ", Win32 error: " + std::to_string(int(::GetLastError())));
	::WaitForSingleObject(process_info.hProcess, INFINITE);
	ULONG rc = 0;
	::GetExitCodeProcess(process_info.hProcess, &rc);
	::CloseHandle(process_info.hThread);
	::CloseHandle(process_info.hProcess);
	return rc;
}

// Run the script. If it is a perl script, run it through the bundled perl interpreter.
// If it is a batch file, run it through the cmd.exe.
// Otherwise run it directly.
static int run_script(const std::string &script, const std::string &gcode, std::string &/*std_err*/)
{
    // Unpack the argument list provided by the user.
    int     nArgs;
    LPWSTR *szArglist = CommandLineToArgvW(boost::nowide::widen(script).c_str(), &nArgs);
    if (szArglist == nullptr || nArgs <= 0) {
        // CommandLineToArgvW failed. Maybe the command line escapment is invalid?
		throw Slic3r::RuntimeError(std::string("Post processing script ") + script + " on file " + gcode + " failed. CommandLineToArgvW() refused to parse the command line path.");
    }

    std::wstring command_line;
    std::wstring command = szArglist[0];
	if (! boost::filesystem::exists(boost::filesystem::path(command)))
        throw Slic3r::RuntimeError(std::string("The configured post-processing script does not exist: ") + boost::nowide::narrow(command));
    if (boost::iends_with(command, L".pl")) {
        // This is a perl script. Run it through the perl interpreter.
        // The current process may be slic3r.exe or slic3r-console.exe.
        // Find the path of the process:
        wchar_t wpath_exe[_MAX_PATH + 1];
        ::GetModuleFileNameW(nullptr, wpath_exe, _MAX_PATH);
        boost::filesystem::path path_exe(wpath_exe);
        boost::filesystem::path path_perl = path_exe.parent_path() / "perl" / "perl.exe";
        if (! boost::filesystem::exists(path_perl)) {
			LocalFree(szArglist);
			throw Slic3r::RuntimeError(std::string("Perl interpreter ") + path_perl.string() + " does not exist.");
        }
        // Replace it with the current perl interpreter.
        quote_argv_winapi(boost::nowide::widen(path_perl.string()), command_line);
        command_line += L" ";
    } else if (boost::iends_with(command, ".bat")) {
        // Run a batch file through the command line interpreter.
        command_line = L"cmd.exe /C ";
    }

    for (int i = 0; i < nArgs; ++ i) {
        quote_argv_winapi(szArglist[i], command_line);
        command_line += L" ";
    }
    LocalFree(szArglist);
	quote_argv_winapi(boost::nowide::widen(gcode), command_line);
    return (int)execute_process_winapi(command_line);
}

#else
    // POSIX

#include <cstdlib>   // getenv()
#include <sstream>
#include <boost/process.hpp>

namespace process = boost::process;

static int run_script(const std::string &script, const std::string &gcode, std::string &std_err)
{
    // Try to obtain user's default shell
    const char *shell = ::getenv("SHELL");
    if (shell == nullptr) { shell = "/bin/sh"; }

    // Quote and escape the gcode path argument
    std::string command { script };
    command.append(" '");
    for (char c : gcode) {
        if (c == '\'') { command.append("'\\''"); }
        else { command.push_back(c); }
    }
    command.push_back('\'');

    SPDLOG_DEBUG("Executing script, shell: {}, command: {}", shell, command);

    process::ipstream istd_err;
    process::child child(shell, "-c", command, process::std_err > istd_err);

    std_err.clear();
    std::string line;

    while (child.running() && std::getline(istd_err, line)) {
        std_err.append(line);
        std_err.push_back('\n');
    }

    child.wait();
    return child.exit_code();
}

#endif

namespace Slic3r {

namespace GCode {

static int time_in_minutes(float time_in_seconds)
{
    assert(time_in_seconds >= 0.f);
    return int((time_in_seconds + 0.5f) / 60.0f);
}

static float time_in_last_minute(float time_in_seconds)
{
    assert(time_in_seconds <= 60.0f);
    return time_in_seconds / 60.0f;
}

static std::string format_line_M73_main(const std::string& mask, int percent, int time)
{
    char line_M73[64];
    sprintf(line_M73, mask.c_str(),
        std::to_string(percent).c_str(),
        std::to_string(time).c_str());
    return std::string(line_M73);
};

static std::string format_line_M73_stop_int(const std::string& mask, int time)
{
    char line_M73[64];
    sprintf(line_M73, mask.c_str(), std::to_string(time).c_str());
    return std::string(line_M73);
}

static std::string format_time_float(float time)
{
    return float_to_string_decimal_point(time, 2);
}

static std::string format_line_M73_stop_float(const std::string& mask, float time)
{
    char line_M73[64];
    sprintf(line_M73, mask.c_str(), format_time_float(time).c_str());
    return std::string(line_M73);
}

struct FilamentData
{
    std::vector<float> mm;
    std::vector<float> cm3;
    std::vector<float> g;
    std::vector<float> cost;
    float total_g{ 0.0f };
    float total_cost{ 0.0f };
};

// check for temporary lines
static bool is_temporary_decoration(const std::string_view gcode_line)
{
    // remove trailing '\n'
    assert(!gcode_line.empty());
    assert(gcode_line.back() == '\n');

    // return true for decorations which are used in processing the gcode but that should not be exported into the final gcode
    // i.e.:
    // bool ret = gcode_line.substr(0, gcode_line.length() - 1) == ";" + Layer_Change_Tag;
    // ...
    // return ret;
    return false;
}

using namespace Biz::libpgcode;
using Biz::GCodeReader::GCodeReader;
using GCodeLine = GCodeReader::GCodeLine;

struct FilamentStatistics {
    FilamentStatistics(std::size_t extruders_count) :
        used_mm{std::vector<float>(extruders_count, 0.0)},
        used_cm3{std::vector<float>(extruders_count, 0.0)},
        used_g{std::vector<float>(extruders_count, 0.0)},
        used_cost{std::vector<float>(extruders_count, 0.0)}
    {}

    std::vector<float> used_mm;
    std::vector<float> used_cm3;
    std::vector<float> used_g;
    std::vector<float> used_cost;
};

static FilamentStatistics get_filament_statistics(
    const std::map<uint8_t, float>& extruded_volumes,
    const std::vector<Extruder>& extruders,
    std::size_t extruders_count
)
{
    FilamentStatistics result{extruders_count};

    for (const Extruder& extruder : extruders) {
        const uint8_t extruder_id{static_cast<uint8_t>(extruder.id())};
        const auto it{extruded_volumes.find(extruder_id)};
        if (it == extruded_volumes.end()) {
            continue;
        }

        const float volume{it->second};
        const float weight{volume * static_cast<float>(extruder.filament_density()) * 0.001f};
        const double crossection{extruder.filament_crossection()};
        if (crossection > 0) {
            result.used_mm.at(extruder_id) = volume / crossection;
        }
        result.used_cm3.at(extruder_id) = volume * 0.001;
        result.used_g.at(extruder_id) = weight;
        result.used_cost.at(extruder_id) = weight * extruder.filament_cost() * 0.001;
    }

    return result;
}

static float sum(const std::vector<float>& input) {
    return std::accumulate(
        input.begin(),
        input.end(),
        0.0f,
        [](const float result, const float value) { return result + value; }
    );
}

static std::string join_with_comma(const std::vector<float>& input) {
    std::vector<std::string> values;
    std::ranges::transform(input, std::back_inserter(values), [](const float value){
        return fmt::format("{:.2f}", value);
    });
    return boost::join(values, ", ");
}

static Domain::FullPrintStatistics get_full_statistics(
    const Domain::BasicPrintStatistics& basic_statistics,
    const Domain::ExtraPrintStatistics& extra_statistics,
    const std::vector<Extruder>& extruders,
    std::size_t extruders_count
)
{
    const FilamentStatistics filament_statistics{
        get_filament_statistics(basic_statistics.volumes_per_extruder, extruders, extruders_count)
    };
    const FilamentStatistics wipe_tower_statistics{get_filament_statistics(
        basic_statistics.wipe_tower_volumes_per_extruder,
        extruders,
        extruders_count
    )};
    const FilamentStatistics flush_statistics{get_filament_statistics(
        basic_statistics.flush_volumes_per_extruder,
        extruders,
        extruders_count
    )};

    Domain::FullPrintStatistics result;

    result.used_filament_per_extruder_mm                = filament_statistics.used_mm;
    result.used_filament_per_extruder_cm3               = filament_statistics.used_cm3;
    result.used_filament_per_extruder_g                 = filament_statistics.used_g;
    result.used_filament_for_wipe_tower_per_extruder_mm = wipe_tower_statistics.used_mm;
    result.used_filament_for_wipe_tower_per_extruder_g  = wipe_tower_statistics.used_g;
    result.used_filament_for_flush_per_extruder_mm      = flush_statistics.used_mm;
    result.used_filament_for_flush_per_extruder_g       = flush_statistics.used_g;

    result.used_filaments_per_role = basic_statistics.used_filaments_per_role;
    result.used_filament_per_color_change_cm3 = basic_statistics.volumes_per_color_change;

    result.filament_cost_per_extruder     = filament_statistics.used_cost;

    result.total_used_filament_mm = sum(filament_statistics.used_mm);
    result.total_used_filament_cm3 = sum(filament_statistics.used_cm3);
    result.total_used_filament_g = sum(filament_statistics.used_g);
    result.total_filament_cost   = sum(filament_statistics.used_cost);

    result.total_wipe_tower_cost = static_cast<float>(extra_statistics.total_wipe_tower_cost);
    result.total_used_filament_for_wipe_tower_mm =
        static_cast<float>(extra_statistics.total_wipe_tower_filament);
    result.total_used_filament_for_wipe_tower_cm3 =
        static_cast<float>(extra_statistics.total_wipe_tower_filament_volume);
    result.total_used_filament_for_wipe_tower_g =
        static_cast<float>(extra_statistics.total_wipe_tower_filament_weight);
    result.total_used_filament_for_flush_mm  = sum(flush_statistics.used_mm);
    result.total_used_filament_for_flush_cm3 = sum(flush_statistics.used_cm3);
    result.total_used_filament_for_flush_g   = sum(flush_statistics.used_g);

    result.total_toolchanges = extra_statistics.total_toolchanges;
    result.normal_mode_time = basic_statistics.normal_mode_time;
    result.silent_mode_time = basic_statistics.silent_mode_time;

    result.estimated_first_layer_printing_time_normal =
        basic_statistics.normal_mode_time.first_layer_time;

    if (basic_statistics.silent_mode_time) {
        result.estimated_first_layer_printing_time_silent =
            basic_statistics.silent_mode_time->first_layer_time;
    }

    result.initial_filament_type = extra_statistics.initial_filament_type;
    result.printing_filament_types = extra_statistics.printing_filament_types;
    result.initial_extruder_id = extra_statistics.initial_extruder_id;
    result.printing_extruders = extra_statistics.printing_extruders;

    return result;
}

static std::vector<std::string> format_statistics(const Domain::FullPrintStatistics& stats)
{
    std::vector<std::string> result;
    result.push_back("; filament used [mm] = " + join_with_comma(stats.used_filament_per_extruder_mm));
    result.push_back("; filament used [cm3] = " + join_with_comma(stats.used_filament_per_extruder_cm3));
    result.push_back("; filament used [g] = " + join_with_comma(stats.used_filament_per_extruder_g));
    result.push_back("; filament cost = " + join_with_comma(stats.filament_cost_per_extruder));

    result.push_back(
        "; total filament used [g] = " + fmt::format("{:.2f}", stats.total_used_filament_g)
    );
    result.push_back("; total filament cost = " + fmt::format("{:.2f}", stats.total_filament_cost));

    result.push_back(
        "; total filament used for wipe tower [g] = "
        + fmt::format("{:.2f}", stats.total_used_filament_for_wipe_tower_g)
    );

    if (stats.total_toolchanges > 0) {
        result.push_back("; total toolchanges = " + std::to_string(stats.total_toolchanges));
    }

    result.push_back(
        "; estimated printing time (normal mode) = "
        + Utils::get_time_dhms(stats.normal_mode_time.time)
    );
    if (stats.silent_mode_time) {
        result.push_back(
            "; estimated printing time (silent mode) = "
            + Utils::get_time_dhms(stats.silent_mode_time->time)
        );
    }

    result.push_back(
        "; estimated first layer printing time (normal mode) = "
        + Utils::get_time_dhms(stats.estimated_first_layer_printing_time_normal)
    );
    if (stats.estimated_first_layer_printing_time_silent) {
        result.push_back(
            "; estimated first layer printing time (silent mode) = "
            + Utils::get_time_dhms(*stats.estimated_first_layer_printing_time_silent)
        );
    }

    for (std::string& line : result) {
        line.push_back('\n');
    }

    return result;
}

class PostProcessor
{
public:
    struct Backtrace
    {
        float time{ 60.0f };
        unsigned int steps{ 10 };
        float time_step() const { return time / float(steps); }
    };

    PostProcessor(
        const PostProcessorConfig& config,
        ProcessorResult& result,
        const std::vector<Extruder>& extruders,
        const Domain::ExtraPrintStatistics& extra_print_statistics,
        WarningCallback warning_callback
    ) :
        m_config(config),
        m_result(result),
        m_warning_callback(warning_callback)
    {
        apply_config();
        setup_filament_data();
        process(extruders, extra_print_statistics);
        finalize();
        synchronize_moves();
    }

private:
    const PostProcessorConfig& m_config;
    ProcessorResult& m_result;
    size_t m_times_cache_id{ 0 };
    // Current time
    std::array<float, TIME_MODES_COUNT> m_times{};
    // gcode times
    std::vector<std::array<float, TIME_MODES_COUNT>> m_gcode_times;
    // keeps track of last exported pair <percent, remaining time>
    std::array<std::pair<int, int>, TIME_MODES_COUNT> m_last_exported_main;
    // keeps track of last exported remaining time to next printer stop
    std::array<int, TIME_MODES_COUNT> m_last_exported_stop;
    // Iterators for the normal and silent cached time estimate entry recently processed, used by process_line_G1.
    std::array<std::vector<G1LinesCacheItem>::const_iterator, TIME_MODES_COUNT> m_g1_times_cache_it;
    std::vector<EditingItem> m_editing_items;
    FilamentData m_filament_data;

    WarningCallback m_warning_callback{ nullptr };
    // Backtrace data for Tx gcode lines
    static const Backtrace s_BACKTRACE_T;

    void apply_config() {
        for (size_t i = 0; i < TIME_MODES_COUNT; ++i) {
            m_last_exported_main[i] = { 0, time_in_minutes(m_config.time_machines[i].time) };
        }
        for (size_t i = 0; i < TIME_MODES_COUNT; ++i) {
            m_last_exported_stop[i] = time_in_minutes(m_config.time_machines[i].time);
        }
        for (size_t i = 0; i < TIME_MODES_COUNT; ++i) {
            m_g1_times_cache_it[i] = m_config.time_machines[i].g1_times_cache.begin();
        }
    }

    void setup_filament_data() {
        m_filament_data = {
            std::vector<float>(m_result.extruders_count, 0.0f),
            std::vector<float>(m_result.extruders_count, 0.0f),
            std::vector<float>(m_result.extruders_count, 0.0f),
            std::vector<float>(m_result.extruders_count, 0.0f),
            0.0f,
            0.0f
        };

        const auto* basic_print_stats{
            std::get_if<Domain::BasicPrintStatistics>(&m_result.print_statistics)
        };
        for (const auto& [id, volume] : ASSERT_VAL(basic_print_stats)->volumes_per_extruder) {
            m_filament_data.mm[id]   = volume / m_result.filament_geometry(id).area_cross_section;
            m_filament_data.cm3[id]  = volume * 0.001f;
            m_filament_data.g[id]    = m_filament_data.cm3[id] * m_result.filament_densities[id];
            m_filament_data.cost[id] = m_filament_data.g[id] * m_result.filament_costs[id] * 0.001f;
            m_filament_data.total_g    += m_filament_data.g[id];
            m_filament_data.total_cost += m_filament_data.cost[id];
        }
    }

    // collect changes to apply to gcode
    void process(
        const std::vector<Extruder>& extruders,
        const Domain::ExtraPrintStatistics& extra_print_statistics
    ) {
        m_gcode_times.resize(m_result.gcode().size(), {});

        size_t g1_lines_counter = 0;
        // In case there are multiple sources of backtracing, keeps track of the longest backtrack time needed
        // to flush the backtrace cache accordingly
        float max_backtrace_time = 120.0f;

        // collects changes to be made to the gcode
        std::string line;
        for (size_t i = 0; i < m_result.gcode().size(); ++i) {
            line = m_result.gcode()[i];
            const size_t internal_g1_lines_counter = update(line, i + 1, g1_lines_counter);

            if (is_temporary_decoration(line)) {
                m_editing_items.push_back({ EditingType::Deletion, i, {} });
                continue;
            }

            // replace placeholder lines
            std::vector<std::string> new_lines = process_placeholders(line, extruders, extra_print_statistics);
            if (!new_lines.empty()) {
                m_editing_items.push_back({ EditingType::Replacement, i, new_lines });
                continue;
            }

            // add lines M73 where needed
            if (GCodeLine::cmd_is(line, "G0") || GCodeLine::cmd_is(line, "G1")) {
                const std::vector<std::string> new_lines = process_line_G1(g1_lines_counter);
                ++g1_lines_counter;
                if (!new_lines.empty()) {
                    m_editing_items.push_back({ EditingType::Insertion, i+1, new_lines });
                    continue;
                }
            }
            else if (GCodeLine::cmd_is(line, "G2") || GCodeLine::cmd_is(line, "G3")) {
                const std::vector<std::string> new_lines = process_line_G1(g1_lines_counter + internal_g1_lines_counter);
                g1_lines_counter += (1 + internal_g1_lines_counter);
                if (!new_lines.empty()) {
                    m_editing_items.push_back({ EditingType::Insertion, i+1, new_lines });
                    continue;
                }
            }
            else if (GCodeLine::cmd_is(line, "G28"))
                ++g1_lines_counter;
            else if (m_config.do_M104_backtrace && GCodeLine::cmd_starts_with(line, "T")) {
                // add lines M104 where needed
                const auto& [insertions, replacements] =
                    process_line_T(line, i, m_warning_callback);
                bool processed = false;
                if (!insertions.empty()) {
                    for (auto it = insertions.rbegin(); it != insertions.rend(); ++it) {
                        m_editing_items.push_back({ EditingType::Insertion, it->first+1, { it->second } });
                    }
                    processed = true;
                }
                if (!replacements.empty()) {
                    for (auto it = replacements.rbegin(); it != replacements.rend(); ++it) {
                        m_editing_items.push_back({ EditingType::Replacement, it->first, { it->second } });
                    }
                    processed = true;
                }
                max_backtrace_time = std::max(max_backtrace_time, s_BACKTRACE_T.time);
                if (processed)
                    continue;
            }
        }
    }

    // apply collected changes to gcode
    void finalize() {
        // ensure the items are in ascending order
        std::sort(m_editing_items.begin(), m_editing_items.end(),
            [](const EditingItem& i1, const EditingItem& i2) { return i1.gcode_line_id < i2.gcode_line_id; });

        // merge items at the same spot
        size_t i = 0;
        while (i + 1 < m_editing_items.size()) {
            EditingItem& curr = m_editing_items[i];
            const EditingItem& next = m_editing_items[i + 1];
            bool modified = false;
            if (curr.gcode_line_id == next.gcode_line_id) {
                if (curr.type == next.type) {
                    curr.lines.insert(curr.lines.end(), next.lines.begin(),next.lines.end());
                    m_editing_items.erase(m_editing_items.begin() + i + 1);
                    modified = true;
                }
                else {
                    assert(false);
                    throw Slic3r::RuntimeError(std::string("Error while post-processing gcode (trying to apply different editing at the same gcode line)"));
                }
            }
            if (!modified)
                ++i;
        }

        m_result.gcode().apply_edits(m_editing_items);
    }

    void synchronize_moves() {
        // Move vertices referenced lines, but we have just inserted/deleted some. The indices need to be updated.
        size_t move_idx = 0;
        int32_t shift_lines = 0;
        
        for (size_t edit_idx = 0; edit_idx < m_editing_items.size(); ++edit_idx) {
            const EditingItem& edit = m_editing_items[edit_idx];
            if (edit.type == EditingType::Deletion || edit.type == EditingType::Replacement)
                --shift_lines;
            if (edit.type == EditingType::Insertion || edit.type == EditingType::Replacement)
                shift_lines += edit.lines.size();
            // Now shift all the references until the next edit (or end of the moves vector).
            while (move_idx != m_result.moves().size() && (edit_idx == m_editing_items.size() - 1 || m_result.moves()[move_idx].gcode_id < m_editing_items[edit_idx + 1].gcode_line_id)) {
                m_result.moves()[move_idx].gcode_id += shift_lines;
                ++move_idx;
            }
        }
    }

    // return: number of internal G1 lines (from G2/G3 splitting) processed
    size_t update(const std::string& gcode_line, size_t lines_counter, size_t g1_lines_counter) {
        size_t ret = 0;
        m_gcode_times[lines_counter - 1] = m_times;

        if (GCodeLine::cmd_is(gcode_line, "G0") ||
            GCodeLine::cmd_is(gcode_line, "G1") ||
            GCodeLine::cmd_is(gcode_line, "G2") ||
            GCodeLine::cmd_is(gcode_line, "G3") ||
            GCodeLine::cmd_is(gcode_line, "G28"))
            ++g1_lines_counter;
        else
            return ret;

        auto init_it = m_config.time_machines[size_t(TimeMode::Normal)].g1_times_cache.begin() + m_times_cache_id;
        auto it = init_it;
        while (it != m_config.time_machines[size_t(TimeMode::Normal)].g1_times_cache.end() && it->id < g1_lines_counter) {
            ++it;
            ++m_times_cache_id;
        }

        if (it == m_config.time_machines[size_t(TimeMode::Normal)].g1_times_cache.end() || it->id > g1_lines_counter)
            return ret;

        // search for internal G1 lines
        if (GCodeLine::cmd_is(gcode_line, "G2") || GCodeLine::cmd_is(gcode_line, "G3")) {
            while (it != m_config.time_machines[size_t(TimeMode::Normal)].g1_times_cache.end() && it->remaining_internal_g1_lines > 0) {
                ++it;
                ++m_times_cache_id;
                ++g1_lines_counter;
                ++ret;
            }
        }

        if (it != m_config.time_machines[size_t(TimeMode::Normal)].g1_times_cache.end() && it->id == g1_lines_counter) {
            m_times[size_t(TimeMode::Normal)] = it->elapsed_time;
            if (!m_config.time_machines[size_t(TimeMode::Stealth)].g1_times_cache.empty())
                m_times[size_t(TimeMode::Stealth)] = (m_config.time_machines[size_t(TimeMode::Stealth)].g1_times_cache.begin() + std::distance(m_config.time_machines[size_t(TimeMode::Normal)].g1_times_cache.begin(), it))->elapsed_time;
            m_gcode_times[lines_counter - 1] = m_times;
        }

        return ret;
    }

    std::string time_mode_to_string(TimeMode mode)
    {
        switch (mode)
        {
        case TimeMode::Normal:  { return "normal"; }
        case TimeMode::Stealth: { return "silent"; }
        default:                { assert(false); break; }
        }
        return {};
    }

    // replace placeholder lines with the proper final value
    // gcode_line is in/out parameter, to reduce expensive memory allocation
    std::vector<std::string> process_placeholders(
        const std::string_view gcode_line,
        const std::vector<Extruder>& extruders,
        const Domain::ExtraPrintStatistics& extra_print_statistics
    )
    {
        // remove trailing '\n'
        auto line = gcode_line.substr(0, gcode_line.length() - 1);

        std::vector<std::string> ret;

        if (line.length() > 1) {
            line = line.substr(1);
            if (m_config.export_remaining_time_enabled &&
                (line == reserved_tag(Tags::First_Line_M73_Placeholder) || line == reserved_tag(Tags::Last_Line_M73_Placeholder))) {
                for (size_t i = 0; i < TIME_MODES_COUNT; ++i) {
                    const TimeMachineData& machine = m_config.time_machines[i];
                    if (machine.enabled) {
                        // export pair <percent, remaining time>
                        ret.push_back(format_line_M73_main(machine.line_m73_main_mask.c_str(),
                            (line == reserved_tag(Tags::First_Line_M73_Placeholder)) ? 0 : 100,
                            (line == reserved_tag(Tags::First_Line_M73_Placeholder)) ? time_in_minutes(machine.time) : 0));

                        // export remaining time to next printer stop
                        if (line == reserved_tag(Tags::First_Line_M73_Placeholder) && !machine.stop_times.empty()) {
                            const int to_export_stop = time_in_minutes(machine.stop_times.front().elapsed_time);
                            ret.push_back(format_line_M73_stop_int(machine.line_m73_stop_mask.c_str(), to_export_stop));
                            m_last_exported_stop[i] = to_export_stop;
                        }
                    }
                }
            } else if (line == reserved_tag(Tags::Print_Statistics_Placeholder)) {
                const auto* basic_print_stats{
                    std::get_if<Domain::BasicPrintStatistics>(&m_result.print_statistics)
                };
                m_result.print_statistics = get_full_statistics(
                    *ASSERT_VAL(basic_print_stats),
                    extra_print_statistics,
                    extruders,
                    m_result.extruders_count
                );
                return format_statistics(
                    std::get<Domain::FullPrintStatistics>(m_result.print_statistics)
                );
            }
        }

        return ret;
    }

    // add lines M73 to exported gcode
    std::vector<std::string> process_line_G1(size_t g1_lines_counter) {
        std::vector<std::string> ret;

        if (m_config.export_remaining_time_enabled) {
            for (size_t i = 0; i < TIME_MODES_COUNT; ++i) {
                const TimeMachineData& machine = m_config.time_machines[i];
                if (machine.enabled) {
                    // export pair <percent, remaining time>
                    // Skip all machine.g1_times_cache below g1_lines_counter.
                    auto& it = m_g1_times_cache_it[i];
                    while (it != machine.g1_times_cache.end() && it->id < g1_lines_counter)
                        ++it;
                    if (it != machine.g1_times_cache.end() && it->id == g1_lines_counter) {
                        const std::pair<int, int> to_export_main = { int(100.0f * it->elapsed_time / machine.time),
                            time_in_minutes(machine.time - it->elapsed_time) };
                        if (m_last_exported_main[i] != to_export_main) {
                            ret.push_back(format_line_M73_main(machine.line_m73_main_mask.c_str(),
                                to_export_main.first, to_export_main.second));
                            m_last_exported_main[i] = to_export_main;
                        }
                        // export remaining time to next printer stop
                        auto it_stop = std::upper_bound(machine.stop_times.begin(), machine.stop_times.end(), it->elapsed_time,
                            [](float value, const StopTime& t) { return value < t.elapsed_time; });
                        if (it_stop != machine.stop_times.end()) {
                            const int to_export_stop = time_in_minutes(it_stop->elapsed_time - it->elapsed_time);
                            if (m_last_exported_stop[i] != to_export_stop) {
                                if (to_export_stop > 0) {
                                    if (m_last_exported_stop[i] != to_export_stop) {
                                        ret.push_back(format_line_M73_stop_int(machine.line_m73_stop_mask.c_str(), to_export_stop));
                                        m_last_exported_stop[i] = to_export_stop;
                                    }
                                }
                                else {
                                    bool is_last = false;
                                    auto next_it = it + 1;
                                    is_last |= (next_it == machine.g1_times_cache.end());

                                    if (next_it != machine.g1_times_cache.end()) {
                                        auto next_it_stop = std::upper_bound(machine.stop_times.begin(), machine.stop_times.end(), next_it->elapsed_time,
                                            [](float value, const StopTime& t) { return value < t.elapsed_time; });
                                        is_last |= (next_it_stop != it_stop);
                                      
                                        const std::string time_float_str = format_time_float(time_in_last_minute(it_stop->elapsed_time - it->elapsed_time));
                                        const std::string next_time_float_str = format_time_float(time_in_last_minute(it_stop->elapsed_time - next_it->elapsed_time));
                                        is_last |= (string_to_double_decimal_point(time_float_str) > 0. && string_to_double_decimal_point(next_time_float_str) == 0.);
                                    }
                                  
                                    if (is_last) {
                                        if (std::distance(machine.stop_times.begin(), it_stop) == ptrdiff_t(machine.stop_times.size() - 1))
                                            ret.push_back(format_line_M73_stop_int(machine.line_m73_stop_mask.c_str(), to_export_stop));
                                        else
                                            ret.push_back(format_line_M73_stop_float(machine.line_m73_stop_mask.c_str(), time_in_last_minute(it_stop->elapsed_time - it->elapsed_time)));

                                        m_last_exported_stop[i] = to_export_stop;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        return ret;
    }

    // add lines M104 to exported gcode
    std::pair<std::vector<std::pair<size_t, std::string>>, std::vector<std::pair<size_t, std::string>>>
    process_line_T(const std::string& gcode_line, size_t lines_counter,
        WarningCallback warning_callback) {

        std::pair<std::vector<std::pair<size_t, std::string>>, std::vector<std::pair<size_t, std::string>>> ret;

        const std::string cmd = GCodeLine::extract_cmd(gcode_line);
        if (cmd.size() >= 2) {
            std::stringstream ss(cmd.substr(1));
            int tool_number = -1;
            ss >> tool_number;

            const std::vector<int>& extruder_temps_config = m_config.extruder_temps_config;
            const uint32_t layer_id = m_result.layer_id_at(uint32_t(lines_counter));

            if (tool_number != -1) {
                if (tool_number < 0 || int(extruder_temps_config.size()) <= tool_number) {
                    // found an invalid value, clamp it to a valid one
                    tool_number = std::clamp<int>(0, extruder_temps_config.size() - 1, tool_number);

                    warning_callback(
                        Biz::Slicing::Warning{
                            Biz::Slicing::WarningCode::InvalidToolchange,
                            {},
                            std::nullopt,
                            Biz::Slicing::InvalidToolchangeWarningPayload{gcode_line}
                        }
                    );
                }
            }

            insert_M104_lines(lines_counter, cmd,
                // line_inserter
                [this, layer_id, tool_number, &ret](size_t line_id, const std::vector<float>& time_diffs) {
                    const int temperature = int(layer_id) != 0 ? m_config.extruder_temps_config[tool_number] : 
                        m_config.extruder_temps_first_layer_config[tool_number];
                    std::string new_line = "M104.1 T" + std::to_string(tool_number);
                    if (time_diffs.size() > 0)
                        new_line += " P" + std::to_string(int(std::round(time_diffs[0])));
                    if (time_diffs.size() > 1)
                        new_line += " Q" + std::to_string(int(std::round(time_diffs[1])));
                    new_line += " S" + std::to_string(temperature) + "\n";

                    ret.first.push_back({ line_id, new_line });
                },
                // line replacer
                [tool_number, &ret](size_t line_id, const std::string& gcode_line) {
                    if (GCodeLine::cmd_is(gcode_line, "M104")) {
                        GCodeLine gline;
                        GCodeReader reader;
                        reader.parse_line(gcode_line, [&gline](GCodeReader& reader, const GCodeLine& l) { gline = l; });
                        float val;
                        if (gline.has_value('T', val) && gline.raw().find("cooldown") != std::string::npos) {
                            if (int(val) == tool_number) {
                                // avoid duplications
                                const std::pair<size_t, std::string> new_item = { line_id, "; removed M104\n" };
                                const auto it = std::find_if(ret.second.begin(), ret.second.end(),
                                    [&new_item](const std::pair<size_t, std::string>& item) { return item == new_item; });
                                if (it == ret.second.end())
                                    ret.second.push_back(new_item);
                            }
                        }
                    }
                }
            );
        }

        return ret;
    }

    void insert_M104_lines(size_t lines_counter, const std::string& cmd,
        std::function<void(size_t, const std::vector<float>&)> line_inserter,
        std::function<void(size_t, const std::string&)> line_replacer) {
        const float time_step = s_BACKTRACE_T.time_step();
        const size_t base_rev_it_dist = m_result.gcode().size() - lines_counter; // distance from the current gcode line to the end of gcode
        auto base_gcode_rev_it = m_result.gcode().rbegin() + base_rev_it_dist; // reverse iterator to the current gcode line
        auto base_times_rev_it = m_gcode_times.rbegin() + base_rev_it_dist; // reverse iterator to the current gcode line times

        size_t rev_it_dist = 0; // distance from the current gcode line of the starting point of the backtrace
        float last_time_insertion = 0.0f; // used to avoid inserting two lines at the same time
        for (unsigned int i = 0; i < s_BACKTRACE_T.steps; ++i) {
            const float backtrace_time_i = float(i + 1) * time_step;
            const float time_threshold_i = m_times[size_t(TimeMode::Normal)] - backtrace_time_i;
            auto gcode_rev_it = base_gcode_rev_it + rev_it_dist;
            auto times_rev_it = base_times_rev_it + rev_it_dist;
            auto start_rev_it = gcode_rev_it;
            std::string curr_cmd = GCodeLine::extract_cmd(std::string{*gcode_rev_it});
            // backtrace to find the place where to insert the line
            while (gcode_rev_it != m_result.gcode().rend() && (*times_rev_it)[size_t(TimeMode::Normal)] > time_threshold_i &&
                   curr_cmd != cmd && curr_cmd != "G28" && curr_cmd != "G29") {
                const std::size_t line_id = gcode_rev_it.line_index();
                line_replacer(line_id, std::string(*gcode_rev_it));
                ++gcode_rev_it;
                ++times_rev_it;
                if (gcode_rev_it != m_result.gcode().rend())
                    curr_cmd = GCodeLine::extract_cmd(std::string{*gcode_rev_it});
            }

            // we met the previous evenience of cmd, or a G28/G29 command. stop inserting lines
            if (gcode_rev_it != m_result.gcode().rend() && (curr_cmd == cmd || curr_cmd == "G28" || curr_cmd == "G29"))
                break;

            // insert the line for the current step
            if (gcode_rev_it != m_result.gcode().rend() && gcode_rev_it != start_rev_it &&
                (*times_rev_it)[size_t(TimeMode::Normal)] != last_time_insertion) {
                last_time_insertion = (*times_rev_it)[size_t(TimeMode::Normal)];
                std::vector<float> time_diffs;
                time_diffs.push_back(m_times[size_t(TimeMode::Normal)] - last_time_insertion);
                if (m_config.time_machines[size_t(TimeMode::Stealth)].enabled)
                    time_diffs.push_back(m_times[size_t(TimeMode::Stealth)] - (*times_rev_it)[size_t(TimeMode::Stealth)]);
                const std::size_t line_id = gcode_rev_it.line_index();
                line_inserter(line_id, time_diffs);
                rev_it_dist = gcode_rev_it - base_gcode_rev_it + 1;
            }
        }
    }
};

const PostProcessor::Backtrace PostProcessor::s_BACKTRACE_T = { 120.0f, 10 };

ProcessorResult post_process(
    const PostProcessorConfig& config,
    ProcessorResult&& result,
    const std::vector<Extruder>& extruders,
    const Domain::ExtraPrintStatistics& extra_print_statistics,
    WarningCallback warning_callback
)
{
    ProcessorResult ret = std::move(result);
    PostProcessor pp(config, ret, extruders, extra_print_statistics, warning_callback);
    return ret;
}

} // namespace GCode

// Run post processing script / scripts if defined.
// Returns true if a post-processing script was executed.
// Returns false if no post-processing script was defined.
// Throws an exception on error.
// host is one of "File", "PrusaLink", "Repetier", "SL1Host", "OctoPrint", "FlashAir", "Duet", "AstroBox" ...
// For a "File" target, a temp file will be created for src_path by adding a ".pp" suffix and src_path will be updated.
// In that case the caller is responsible to delete the temp file created.
// output_name is the final name of the G-code on SD card or when uploaded to PrusaLink or OctoPrint.
// If uploading to PrusaLink or OctoPrint, then the file will be renamed to output_name first on the target host.
// The post-processing script may change the output_name.
bool run_post_process_scripts(std::string &src_path, bool make_copy, const std::string &host, std::string &output_name, const PrintConfigView &config)
{
    const auto post_process = config.get<std::vector<std::string>>("post_process");
    if (post_process.empty())
        return false;

    std::string path;
    if (make_copy) {
        // Don't run the post-processing script on the input file, it will be memory mapped by the G-code viewer.
        // Make a copy.
        path = src_path + ".pp";
        // First delete an old file if it exists.
        try {
            if (boost::filesystem::exists(path))
                boost::filesystem::remove(path);
        } catch (const std::exception &err) {
            SPDLOG_ERROR("Failed deleting an old temporary file {} before running a post-processing script: {}", path, err.what());
        }
        // Second make a copy.
        std::string error_message;
        if (copy_file(src_path, path, error_message, false) != SUCCESS)
            throw Slic3r::RuntimeError(Slic3r::format("Failed making a temporary copy of G-code file %1% before running a post-processing script: %2%", src_path, error_message));
    } else {
        // Don't make a copy of the G-code before running the post-processing script.
        path = src_path;
    }

    auto delete_copy = [&path, &src_path, make_copy]() {
        if (make_copy)
            try {
                if (boost::filesystem::exists(path))
                    boost::filesystem::remove(path);
            } catch (const std::exception &err) {
                SPDLOG_ERROR("Failed deleting a temporary copy {} of a G-code file {} : {}", path, src_path, err.what());
            }
    };

    auto gcode_file = boost::filesystem::path(path);
    if (! boost::filesystem::exists(gcode_file))
        throw Slic3r::RuntimeError(std::string("Post-processor can't find exported gcode file"));

    // Store print configuration into environment variables.

    //config.setenv_();
    PANIC("config.setenv_() not implemented");

    // Let the post-processing script know the target host ("File", "PrusaLink", "Repetier", "SL1Host", "OctoPrint", "FlashAir", "Duet", "AstroBox" ...)
    boost::nowide::setenv("SLIC3R_PP_HOST", host.c_str(), 1);
    // Let the post-processing script know the final file name. For "File" host, it is a full path of the target file name and its location, for example pointing to an SD card.
    // For "PrusaLink" or "OctoPrint", it is a file name optionally with a directory on the target host.
    boost::nowide::setenv("SLIC3R_PP_OUTPUT_NAME", output_name.c_str(), 1);

    // Path to an optional file that the post-processing script may create and populate it with a single line containing the output_name replacement.
    std::string path_output_name = path + ".output_name";
    auto remove_output_name_file = [&path_output_name, &src_path]() {
        try {
            if (boost::filesystem::exists(path_output_name))
                boost::filesystem::remove(path_output_name);
        } catch (const std::exception &err) {
            SPDLOG_ERROR("Failed deleting a file {} carrying the final name / path of a G-code file {}: {}", path_output_name, src_path, err.what());
        }
    };
    // Remove possible stalled path_output_name of the previous run.
    remove_output_name_file();

    try {
        for (const std::string &scripts : post_process) {
    		std::vector<std::string> lines;
    		boost::split(lines, scripts, boost::is_any_of("\r\n"));
            for (std::string script : lines) {
                // Ignore empty post processing script lines.
                boost::trim(script);
                if (script.empty())
                    continue;
                SPDLOG_INFO("Executing script {} on file {}", script, path);
                std::string std_err;
                const int result = run_script(script, gcode_file.string(), std_err);
                if (result != 0) {
                    const std::string msg = std_err.empty() ? (boost::format("Post-processing script %1% on file %2% failed.\nError code: %3%") % script % path % result).str()
                        : (boost::format("Post-processing script %1% on file %2% failed.\nError code: %3%\nOutput:\n%4%") % script % path % result % std_err).str();
                    SPDLOG_ERROR("{}", msg);
                    delete_copy();
                    throw Slic3r::RuntimeError(msg);
                }
                if (! boost::filesystem::exists(gcode_file)) {
                    const std::string msg = (boost::format(_u8L(
                        "Post-processing script %1% failed.\n\n"
                        "The post-processing script is expected to change the G-code file %2% in place, but the G-code file was deleted and likely saved under a new name.\n"
                        "Please adjust the post-processing script to change the G-code in place and consult the manual on how to optionally rename the post-processed G-code file.\n"))
                        % script % path).str();
                    SPDLOG_ERROR("{}", msg);
                    throw Slic3r::RuntimeError(msg);
                }
            }
        }
        if (boost::filesystem::exists(path_output_name)) {
            try {
                // Read a single line from path_output_name, which should contain the new output name of the post-processed G-code.
                boost::nowide::fstream f;
                f.open(path_output_name, std::ios::in);
                std::string new_output_name;
                std::getline(f, new_output_name);
                f.close();

                if (host == "File") {
                    namespace fs = boost::filesystem;
                    fs::path op(new_output_name);
                    if (op.is_relative() && op.has_filename() && op.parent_path().empty()) {
                        // Is this just a filename? Make it an absolute path.
                        auto outpath = fs::path(output_name).parent_path();
                        outpath /= op.string();
                        new_output_name = outpath.string();
                    }
                    else {
                        if (! op.is_absolute() || ! op.has_filename())
                            throw Slic3r::RuntimeError("Unable to parse desired new path from output name file");
                    }
                    if (! fs::exists(fs::path(new_output_name).parent_path()))
                        throw Slic3r::RuntimeError(Slic3r::format("Output directory does not exist: %1%",
                                                                  fs::path(new_output_name).parent_path().string()));
                }

                SPDLOG_TRACE("Post-processing script changed the file name from {} to {}", output_name, new_output_name);
                output_name = new_output_name;
            } catch (const std::exception &err) {
                throw Slic3r::RuntimeError(Slic3r::format("run_post_process_scripts: Failed reading a file %1% "
                                                          "carrying the final name / path of a G-code file: %2%",
                                                          path_output_name, err.what()));
            }
            remove_output_name_file();
        }
    } catch (...) {
        remove_output_name_file();
        delete_copy();
        throw;
    }

    src_path = std::move(path);
    return true;
}

} // namespace Slic3r
