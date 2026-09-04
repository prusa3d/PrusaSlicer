#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <boost/filesystem.hpp>
#include "Slic3r/App/CustomGCodeMigration.hpp"
#include "Slic3r/TestUtils/TestData.hpp"


using namespace Slic3r::App;
namespace fs = boost::filesystem;

constexpr bool debug_files{true};

std::string read_file(const fs::path& filename)
{
    std::ifstream file{filename.string()};
    ASSERT(file);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

void write_file(const fs::path& filename, const std::string& value)
{
    std::ofstream file{filename.string()};
    file << value;
}

TEST_CASE("Deducing index on the same line works", "[CustomGCodeMigration]") {

    std::string code{
        "{perimter_speed[1]}\n"
        "{external_perimeter_speed} perimter_speed[7] {perimeter_speed[some_index]}\n"
        "{perimter_speed[2]}\n"
    };
    std::string expected{
        "{perimter_speed[1]}\n"
        "{external_perimeter_speed[some_index]} perimter_speed[7] {perimeter_speed[some_index]}\n"
        "{perimter_speed[2]}\n"
    };

    CHECK(expected == migrate_custom_gcode(code));
}

TEST_CASE("Automatically migrating start_gcode_1 works", "[CustomGCodeMigration]") {
    const fs::path datadir{Tests::get_datadir()};
    const fs::path test_files{datadir / fs::path{"custom_gcode_migration"}};
    const fs::path start_gcode_file{test_files / fs::path{"start_gcode_1.txt"}};
    const fs::path start_gcode_expectation_file{test_files / fs::path{"start_gcode_expectation_1.txt"}};

    std::string start_gcode{read_file(start_gcode_file)};
    std::string start_gcode_expectation{read_file(start_gcode_expectation_file)};
    std::string migrated_gcode{migrate_custom_gcode(start_gcode)};

    if (debug_files) {
        write_file("start_gcode_1_debug.txt", migrated_gcode);
    }

    CHECK(migrated_gcode == start_gcode_expectation);
}

TEST_CASE("Automatically migrating start_gcode_0 works", "[CustomGCodeMigration]") {
    const fs::path datadir{Tests::get_datadir()};
    const fs::path test_files{datadir / fs::path{"custom_gcode_migration"}};
    const fs::path start_gcode_file{test_files / fs::path{"start_gcode_0.txt"}};
    const fs::path start_gcode_expectation_file{test_files / fs::path{"start_gcode_expectation_0.txt"}};

    std::string start_gcode{read_file(start_gcode_file)};
    std::string start_gcode_expectation{read_file(start_gcode_expectation_file)};
    std::string migrated_gcode{migrate_custom_gcode(start_gcode)};

    if (debug_files) {
        write_file("start_gcode_0_debug.txt", migrated_gcode);
    }

    CHECK(migrated_gcode == start_gcode_expectation);
}
