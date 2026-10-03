#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "CLITestUtils.hpp"

#include "Slic3r/App/CLI/CLIApp.hpp"
#include "Slic3r/App/Init.hpp"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/FileLoadingLogic.hpp"
#include "Slic3r/Domain/Bed.hpp"
#include "Slic3r/Domain/ConfigContainer.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/Percentage.hpp"
#include "Slic3r/Domain/Preset/Bundle.hpp"

#include <cstdlib>

#include <boost/filesystem.hpp>

using Slic3r::Domain::BoundingBox3d;
using Slic3r::Domain::FloatOrPercentage;
using Slic3r::Domain::ModelObject;
using Slic3r::Domain::Project;
using Slic3r::Domain::Vec2d;
using Slic3r::Domain::Vec3d;

using namespace Slic3r;
using namespace Slic3r::App;
using namespace Slic3r::Biz;
using namespace Slic3r::App::CLI::Test;

namespace fs = boost::filesystem;

TEST_CASE("CLI slices a single-tool 3MF project", "[cli][timeout]")
{
    const ScopedTempDir temp_dir;
    const fs::path cube_stl_path     = write_cube_stl(temp_dir.path(), "cube.stl", 20.);
    const fs::path exported_3mf_path = temp_dir.path() / "cube.3mf";
    const fs::path gcode_path        = temp_dir.path() / "print.gcode";

    for (const TestProfileSet& profile_set : resolved_profile_sets()) {
        App::InitParams export_params = make_params(profile_set);
        export_params.input.input_files.push_back(cube_stl_path.string());
        export_params.action.export_3mf = true;
        export_params.misc.output       = exported_3mf_path.string();

        REQUIRE(App::CLI::run(export_params) == EXIT_SUCCESS);
        REQUIRE(fs::exists(exported_3mf_path));

        App::InitParams slice_params;
        slice_params.input.input_files.push_back(exported_3mf_path.string());
        slice_params.action.export_gcode = true;
        slice_params.misc.output         = gcode_path.string();

        REQUIRE(App::CLI::run(slice_params) == EXIT_SUCCESS);
        REQUIRE(fs::exists(gcode_path));
        REQUIRE(fs::file_size(gcode_path) > 0);
    }
}

TEST_CASE("CLI slices multiple STL inputs into separate G-codes", "[cli][timeout]")
{
    const ScopedTempDir temp_dir;
    const fs::path first_cube_stl_path  = write_cube_stl(temp_dir.path(), "cube_a.stl", 20.);
    const fs::path second_cube_stl_path = write_cube_stl(temp_dir.path(), "cube_b.stl", 20.);

    App::InitParams slice_params = make_params(resolved_profile_sets().front());
    slice_params.input.input_files.push_back(first_cube_stl_path.string());
    slice_params.input.input_files.push_back(second_cube_stl_path.string());
    slice_params.action.export_gcode = true;
    slice_params.misc.output         = temp_dir.path().string();

    REQUIRE(App::CLI::run(slice_params) == EXIT_SUCCESS);
    REQUIRE(count_gcode_files(temp_dir.path()) == 2);
}

TEST_CASE("CLI reports objects scaled outside the print volume instead of freezing", "[cli]")
{
    const ScopedTempDir temp_dir;
    const fs::path cube_stl_path = write_cube_stl(temp_dir.path(), "cube.stl", 20.);
    const fs::path gcode_path    = temp_dir.path() / "huge.gcode";

    App::InitParams slice_params = make_params(resolved_profile_sets().front());
    slice_params.input.input_files.push_back(cube_stl_path.string());
    slice_params.transform.scale     = FloatOrPercentage{100.0};
    slice_params.action.export_gcode = true;
    slice_params.misc.output         = gcode_path.string();

    REQUIRE(App::CLI::run(slice_params) == EXIT_SUCCESS);
    REQUIRE_FALSE(fs::exists(gcode_path));
    REQUIRE(count_gcode_files(temp_dir.path()) == 0);
}

TEST_CASE("CLI centers a loaded STL on the bed", "[cli][timeout]")
{
    constexpr double cube_size = 20.;

    const ScopedTempDir temp_dir;
    const fs::path cube_stl_path     = write_cube_stl(temp_dir.path(), "cube.stl", cube_size);
    const fs::path exported_3mf_path = temp_dir.path() / "cube.3mf";

    const auto export_and_load = [&](const bool dont_arrange)
    {
        App::InitParams export_params = make_params(resolved_profile_sets().front());
        export_params.input.input_files.push_back(cube_stl_path.string());
        export_params.transform.dont_arrange = dont_arrange;
        export_params.action.export_3mf      = true;
        export_params.misc.output            = exported_3mf_path.string();

        REQUIRE(CLI::run(export_params) == EXIT_SUCCESS);
        return FileLoadingLogic::load_file_as_project(
            exported_3mf_path,
            Domain::Preset::Bundle{},
            nullptr
        );
    };

    const Project centered_project       = export_and_load(false);
    const ModelObject& centered_object   = *centered_project.model().objects.front();
    const Vec3d centered_instance_center = Algorithms::BoundingBox::center(
        Algorithms::ModelObject::instance_bounding_box(centered_object, 0, false)
    );
    const Vec2d bed_center = centered_project.config_containers().front()->bed().center();

    CHECK(centered_instance_center.x() == Catch::Approx(bed_center.x()).margin(Domain::EPSILON));
    CHECK(centered_instance_center.y() == Catch::Approx(bed_center.y()).margin(Domain::EPSILON));

    // Only the instance transformation may change, the mesh keeps the coordinates of the STL.
    const BoundingBox3d mesh_bounding_box = centered_object.volumes.front()->mesh().bounding_box();
    CHECK(mesh_bounding_box.min.x() == Catch::Approx(0.).margin(Domain::EPSILON));
    CHECK(mesh_bounding_box.max.x() == Catch::Approx(cube_size).margin(Domain::EPSILON));

    // --dont-arrange keeps the original XY coordinates of the source file.
    const Project original_project       = export_and_load(true);
    const Vec3d original_instance_center = Algorithms::BoundingBox::center(
        Algorithms::ModelObject::instance_bounding_box(
            *original_project.model().objects.front(),
            0,
            false
        )
    );

    CHECK(original_instance_center.x() == Catch::Approx(cube_size / 2.).margin(1e-3));
    CHECK(original_instance_center.y() == Catch::Approx(cube_size / 2.).margin(1e-3));
}

TEST_CASE("CLI exports an SLA archive for every SLA printer", "[cli][sla][timeout]")
{
    const ScopedTempDir temp_dir;
    const fs::path cube_stl_path = write_cube_stl(temp_dir.path(), "cube.stl", 20.);

    for (const TestProfileSet& profile_set : resolved_sla_profile_sets()) {
        INFO(profile_set.printer_profile_name);
        const fs::path output_dir = temp_dir.path() / "out";
        fs::remove_all(output_dir);
        fs::create_directories(output_dir);

        App::InitParams slice_params = make_params(profile_set);
        slice_params.input.input_files.push_back(cube_stl_path.string());
        slice_params.action.export_sla = true;
        slice_params.misc.output       = output_dir.string();

        REQUIRE(App::CLI::run(slice_params) == EXIT_SUCCESS);

        const std::vector<fs::path> archives = find_sla_archives(output_dir);
        REQUIRE(archives.size() == 1);
        const std::string job_name = archives.front().stem().string();

        const std::optional<std::string> config_ini = read_zip_entry(archives.front(), "config.ini");
        REQUIRE(config_ini.has_value());
        CHECK(config_ini->find("jobDir = " + job_name + "\n") != std::string::npos);
        CHECK(config_ini->find("layerHeight = 0.05\n") != std::string::npos);
        // The first layer and some layer inside the 20 mm tall cube (plus support elevation and pad).
        CHECK(read_zip_entry(archives.front(), job_name + "00000.png").has_value());
        CHECK(read_zip_entry(archives.front(), job_name + "00400.png").has_value());
    }
}

TEST_CASE("CLI slices an SLA 3MF project", "[cli][sla][timeout]")
{
    const ScopedTempDir temp_dir;
    const fs::path cube_stl_path     = write_cube_stl(temp_dir.path(), "cube.stl", 20.);
    const fs::path exported_3mf_path = temp_dir.path() / "cube.3mf";
    const fs::path archive_path      = temp_dir.path() / "print.sl1";

    App::InitParams export_params = make_params(resolved_sla_profile_sets().front());
    export_params.input.input_files.push_back(cube_stl_path.string());
    export_params.action.export_3mf = true;
    export_params.misc.output       = exported_3mf_path.string();

    REQUIRE(App::CLI::run(export_params) == EXIT_SUCCESS);
    REQUIRE(fs::exists(exported_3mf_path));

    // No profiles given: everything has to come from the 3MF.
    App::InitParams slice_params;
    slice_params.input.input_files.push_back(exported_3mf_path.string());
    slice_params.action.export_sla = true;
    slice_params.misc.output       = archive_path.string();

    REQUIRE(App::CLI::run(slice_params) == EXIT_SUCCESS);
    REQUIRE(fs::exists(archive_path));
    const std::optional<std::string> config_ini = read_zip_entry(archive_path, "config.ini");
    REQUIRE(config_ini.has_value());
    CHECK(config_ini->find("layerHeight = 0.05\n") != std::string::npos);
}

TEST_CASE("CLI does not export G-code for an SLA printer", "[cli][sla]")
{
    const ScopedTempDir temp_dir;
    const fs::path cube_stl_path = write_cube_stl(temp_dir.path(), "cube.stl", 20.);
    const fs::path gcode_path    = temp_dir.path() / "print.gcode";

    App::InitParams slice_params = make_params(resolved_sla_profile_sets().front());
    slice_params.input.input_files.push_back(cube_stl_path.string());
    slice_params.action.export_gcode = true;
    slice_params.misc.output         = gcode_path.string();

    // Note: the CLI currently reports the error but still exits with EXIT_SUCCESS.
    App::CLI::run(slice_params);
    CHECK_FALSE(fs::exists(gcode_path));
    CHECK(count_gcode_files(temp_dir.path()) == 0);
    CHECK(find_sla_archives(temp_dir.path()).empty());
}
