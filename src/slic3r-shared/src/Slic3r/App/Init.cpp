#include "Slic3r/App/Init.hpp"

#include "Slic3r/Biz/Platform/BlacklistedLibraryCheck.hpp"
#include "Slic3r/Biz/PresetUpdater/PresetUpdaterStaging.hpp"
#include "Slic3r/Biz/Utils/CopyFile.hpp"
#include "Slic3r/Directories.hpp"
#include "Slic3r/Platform.hpp"
#include "Slic3r/Log.hpp"

#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/nowide/cstdlib.hpp>
#include <boost/nowide/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/nowide/convert.hpp>

namespace Slic3r::App {

InitParams::InitParams(const int argc, char** argv) : argc(argc), argv(argv) {}

void init_common()
{
    {
        Slic3r::set_log_level(1);
        const char* loglevel = boost::nowide::getenv("SLIC3R_LOGLEVEL");
        if (loglevel != nullptr) {
            if (loglevel[0] >= '0' && loglevel[0] <= '9' && loglevel[1] == 0) {
                Slic3r::set_log_level(loglevel[0] - '0');
            } else {
                SPDLOG_ERROR("Invalid SLIC3R_LOGLEVEL environment variable: {}", loglevel);
            }
        }
    }

    // Detect the operating system flavor after SLIC3R_LOGLEVEL is set.
    Slic3r::detect_platform();

#ifdef WIN32
    // Notify user that a blacklisted DLL was injected into PrusaSlicer process (for example Nahimic, see GH #5573).
    // We hope that if a DLL is being injected into a PrusaSlicer process, it happens at the very start of the application,
    // thus we shall detect them now.
    if (Biz::Platform::BlacklistedLibraryCheck::get_instance().perform_check()) {
        std::wstring text = L"Following DLLs have been injected into the PrusaSlicer process:\n\n";
        text += Biz::Platform::BlacklistedLibraryCheck::get_instance().get_blacklisted_string();
        text +=
            L"\n\n"
            L"PrusaSlicer is known to not run correctly with these DLLs injected. "
            L"We suggest stopping or uninstalling these services if you experience "
            L"crashes or unexpected behaviour while using PrusaSlicer.\n"
            L"For example, ASUS Sonic Studio injects a Nahimic driver, which makes PrusaSlicer "
            L"to crash on a secondary monitor, see PrusaSlicer github issue #5573";

        SPDLOG_ERROR("{}", boost::nowide::narrow(text));
    }
#endif

#ifdef __EMSCRIPTEN__
    boost::filesystem::path path_resources = "/resources";
#else
#ifdef _WIN32
    // nowide_filesystem calls std::locale("") which may throw on Linux
    // with unsupported locale, we should call this just on Windows
    boost::nowide::nowide_filesystem();
#endif

    // See Invoking prusa-slicer from $PATH environment variable crashes #5542
    // boost::filesystem::path path_to_binary = boost::filesystem::system_complete(argv[0]);
    boost::filesystem::path path_to_binary = boost::dll::program_location();

    // Path from the Slic3r binary to its resources.
#ifdef __APPLE__
    // The application is packed in the .dmg archive as 'Slic3r.app/Contents/MacOS/Slic3r'
    // The resources are packed to 'Slic3r.app/Contents/Resources'
    boost::filesystem::path path_resources =
        boost::filesystem::canonical(path_to_binary).parent_path() / "../Resources";
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
    boost::filesystem::path path_resources =
        boost::filesystem::canonical(path_to_binary).parent_path() / "../resources";
#endif

#endif // __EMSCRIPTEN__

    // Resource dirs
    Slic3r::set_resources_dir(path_resources.string());
    Slic3r::set_var_dir((path_resources / "icons").string());
    Slic3r::set_local_dir((path_resources / "localization").string());
    Slic3r::set_sys_shapes_dir((path_resources / "shapes").string());
    Slic3r::set_custom_gcodes_dir((path_resources / "custom_gcodes").string());
}

namespace {

bool can_rename_into_data_dir(const boost::filesystem::path& candidate)
{
    boost::system::error_code ec;
    boost::filesystem::create_directories(candidate, ec);
    if (ec) {
        return false;
    }

    const boost::filesystem::path probe_name =
        boost::filesystem::unique_path(".volume_probe-%%%%-%%%%-%%%%-%%%%");
    const boost::filesystem::path source = candidate / probe_name;
    const boost::filesystem::path target = boost::filesystem::path(Slic3r::data_dir()) / probe_name;

    {
        boost::nowide::ofstream probe(source.string().c_str());
        if (!probe) {
            return false;
        }
    }

    const bool renamed = !Biz::Utils::rename_file(source.string(), target.string());
    boost::filesystem::remove(source, ec);
    boost::filesystem::remove(target, ec);
    return renamed;
}

/**
 * @brief Picks the staging root, preferring the platform cache but insisting on a rename.
 *
 * Installing finishes by renaming staged files over the installed ones, which only works within
 * one volume. A data directory on another drive would otherwise silently turn every install into
 * a copy that can run out of space after the download already succeeded.
 */
std::string resolve_updater_cache_dir(bool datadir_overridden)
{
    const std::string candidate = Slic3r::get_default_updater_cache_dir(datadir_overridden);
    if (datadir_overridden) {
        // Already inside the data directory, so it shares its volume by construction.
        return candidate;
    }
    if (can_rename_into_data_dir(candidate)) {
        return candidate;
    }
    SPDLOG_INFO(
        "{} is not on the same volume as the data directory. Staging preset downloads inside the"
        " data directory instead.",
        candidate
    );
    return Slic3r::get_default_updater_cache_dir(true);
}

} // namespace

void init_paths(const InitParams& init_params)
{
    // Data/config dir
    Slic3r::set_data_dir(
        init_params.misc.datadir.has_value() ? init_params.misc.datadir.value() :
                                               Slic3r::get_default_datadir()
    );
    Slic3r::set_cache_dir(Slic3r::get_default_cachedir());

    boost::filesystem::path data_dir_path(Slic3r::data_dir());
    if (!boost::filesystem::exists(data_dir_path)
        || !boost::filesystem::is_directory(data_dir_path))
    {
        boost::filesystem::create_directory(data_dir_path);
    }
    std::initializer_list<boost::filesystem::path> sub_datadirs = {
        // Data prepared for installation, all that config wizard needs. (also pid subs?)
        data_dir_path / "update_sync",
        // all data needed for run of slicer, shared among all instances, App config, archive repo manifest,
        data_dir_path / "shared_runtime",
        // Where local repositories are unzipped, each in own unique directory
        data_dir_path / "local_repositories",
        data_dir_path / "snapshots",
        // subs are local or userid
        data_dir_path / "presets",
        // All presets that slicer reads on startup, vendor presets gets here only from wizard, user presets by user creation
        data_dir_path / "presets" / "local",
        data_dir_path / "presets" / "user",
        data_dir_path / "shapes",
        data_dir_path / "lua",
        data_dir_path / "authorized_authors",
    };

    for (const boost::filesystem::path& sub : sub_datadirs) {
        if (!boost::filesystem::exists(sub) || !boost::filesystem::is_directory(sub)) {
            boost::filesystem::create_directory(sub);
        }
    }

    // After the data dir exists: the probe renames into it.
    Slic3r::set_updater_cache_dir(
        resolve_updater_cache_dir(init_params.misc.datadir.has_value())
    );

    const boost::filesystem::path staging = Slic3r::updater_staging_dir();
    boost::system::error_code ec;
    boost::filesystem::create_directories(staging, ec);
    if (ec) {
        SPDLOG_ERROR("Failed to create directory {}: {}", staging.string(), ec.message());
    }
    Biz::PresetUpdater::sweep_stale_staging();
}

} // namespace Slic3r::App
