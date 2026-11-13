#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Biz/ResultExport/ExportNameParser.hpp"

using Slic3r::Biz::ExportNameParser::resolve_fdm_extension;

TEST_CASE("FDM export extension defaults to the printer's binary_gcode setting", "[ExportNameParser]")
{
    SECTION("nothing remembered") {
        CHECK(resolve_fdm_extension("", ".gcode", true) == ".bgcode");
        CHECK(resolve_fdm_extension("", ".gcode", false) == ".gcode");
    }

    SECTION("remembered G-code extension is kept") {
        CHECK(resolve_fdm_extension(".gcode", ".gcode", true) == ".gcode");
        CHECK(resolve_fdm_extension(".bgcode", ".gcode", true) == ".bgcode");
        CHECK(resolve_fdm_extension(".BGCODE", ".gcode", true) == ".BGCODE");
        CHECK(resolve_fdm_extension(".bgcode", ".gcode", false) == ".gcode");
    }

    // The last used extension is shared by every save dialog (projects, configs, meshes),
    // so it may hold an unrelated extension. That must not hide binary G-code.
    SECTION("remembered extension of an unrelated file type is ignored") {
        CHECK(resolve_fdm_extension(".3mf", ".gcode", true) == ".bgcode");
        CHECK(resolve_fdm_extension(".ini", ".gcode", true) == ".bgcode");
        CHECK(resolve_fdm_extension(".stl", ".gcode", false) == ".gcode");
        CHECK(resolve_fdm_extension(".sl1", ".gcode", true) == ".bgcode");
    }

    SECTION("output_filename_format ending with .bgcode while binary G-code is disabled") {
        CHECK(resolve_fdm_extension("", ".bgcode", false) == ".gcode");
        CHECK(resolve_fdm_extension(".3mf", ".bgcode", false) == ".gcode");
    }
}
