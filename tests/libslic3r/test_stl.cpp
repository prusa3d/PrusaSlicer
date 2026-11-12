#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Domain/Constants.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/Types.hpp"

#include "Slic3r/Biz/Format/STL.hpp"

using Slic3r::Domain::Vec3d;

using Slic3r::Domain::is_approx;

using namespace Slic3r;

static inline std::string stl_path(const char* path)
{
    return std::string(TEST_DATA_DIR) + "/test_stl/" + path;
}

SCENARIO("Reading an STL file", "[stl]")
{
    GIVEN("umlauts in the path of a binary STL file, Czech characters in the file name")
    {
        WHEN("STL file is read")
        {
            Slic3r::Domain::TriangleMesh mesh;
            THEN("load should succeed")
            {
                auto mesh = Biz::load_stl(stl_path("Geräte/20mmbox-čřšřěá.stl"));
                REQUIRE(mesh);
                REQUIRE(is_approx(mesh->size(), Vec3d(20, 20, 20)));
            }
        }
    }
    GIVEN("in ASCII format")
    {
        WHEN("line endings LF")
        {
            Slic3r::Domain::TriangleMesh mesh;
            THEN("load should succeed")
            {
                auto mesh = Biz::load_stl(stl_path("ASCII/20mmbox-LF.stl"));
                REQUIRE(mesh);
                REQUIRE(is_approx(mesh->size(), Vec3d(20, 20, 20)));
            }
        }
        WHEN("line endings CRLF")
        {
            Slic3r::Domain::TriangleMesh mesh;
            THEN("load should succeed")
            {
                auto mesh = Biz::load_stl(stl_path("ASCII/20mmbox-CRLF.stl"));
                REQUIRE(mesh);
                REQUIRE(is_approx(mesh->size(), Vec3d(20, 20, 20)));
            }
        }

        WHEN("nonstandard STL file (text after ending tags, invalid normals, for example infinities)")
        {
            Slic3r::Domain::TriangleMesh mesh;
            THEN("load should succeed")
            {
                auto mesh = Biz::load_stl(stl_path("ASCII/20mmbox-nonstandard.stl"));
                REQUIRE(mesh);
                REQUIRE(is_approx(mesh->size(), Vec3d(20, 20, 20)));
            }
        }
    }
}
