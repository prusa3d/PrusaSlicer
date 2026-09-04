#include <catch2/catch_session.hpp>

#include "Slic3r/App/Init.hpp"
#include "Slic3r/Directories.hpp"
#include "Slic3r/Log.hpp"

#include <boost/filesystem.hpp>
#include <boost/nowide/filesystem.hpp>

int main(int argc, char* argv[])
{
    Slic3r::init_logging();
    Slic3r::set_log_level(0);
    boost::nowide::nowide_filesystem();

    // The CLI loads the preset bundle from resources_dir()/presets.
    Slic3r::set_resources_dir(CLI_TEST_RESOURCES_DIR);

    const boost::filesystem::path test_data_dir = boost::filesystem::temp_directory_path()
        / boost::filesystem::unique_path("slic3r-cli-tests-%%%%-%%%%");

    Slic3r::App::InitParams path_init_params;
    path_init_params.misc.datadir = test_data_dir.string();
    Slic3r::App::init_paths(path_init_params);
    Slic3r::set_cache_dir((test_data_dir / "cache").string());

    const int result = Catch::Session().run(argc, argv);

    boost::system::error_code cleanup_error;
    boost::filesystem::remove_all(test_data_dir, cleanup_error);

    return result;
}
