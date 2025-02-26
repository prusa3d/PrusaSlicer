#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/ExPolygon.hpp"

using namespace Slic3r;
using namespace Catch;

static inline bool points_close(const Point &p1, const Point &p2)
{
    return (p1 - p2).cast<double>().norm() < SCALED_EPSILON;
}

static bool polygons_close_permuted(const Polygon &poly1, const Polygon &poly2, const std::vector<int> &permutation2)
{
    if (poly1.size() != poly2.size() || poly1.size() != permutation2.size())
        return false;
    for (size_t i = 0; i < poly1.size(); ++ i)
        if (poly1[i] != poly2[permutation2[i]])
            return false;
    return true;
}

SCENARIO("Basics", "[ExPolygon]") {
    GIVEN("ccw_square") {
        Polygon ccw_square{ { 100, 100 }, { 200, 100 }, { 200, 200 }, { 100, 200 } };
        Polygon cw_hole_in_square{ { 140, 140 }, { 140, 160 }, { 160, 160 }, { 160, 140 } };
        ExPolygon expolygon { ccw_square, cw_hole_in_square };
        THEN("expolygon is valid") {
            REQUIRE(expolygon.is_valid());
        }
        THEN("expolygon area") {
            REQUIRE(expolygon.area() == Approx(100*100-20*20));
        }
        WHEN("Expolygon scaled") {
            ExPolygon expolygon2 = expolygon;
            expolygon2.scale(2.5);
            REQUIRE(expolygon.contour.size() == expolygon2.contour.size());
            REQUIRE(expolygon.holes.size() == 1);
            REQUIRE(expolygon2.holes.size() == 1);
            for (size_t i = 0; i < expolygon.contour.size(); ++ i)
                REQUIRE(points_close(expolygon.contour[i] * 2.5, expolygon2.contour[i]));
            for (size_t i = 0; i < expolygon.holes.front().size(); ++ i)
                REQUIRE(points_close(expolygon.holes.front()[i] * 2.5, expolygon2.holes.front()[i]));
        }
        WHEN("Expolygon translated") {
            ExPolygon expolygon2 = expolygon;
            expolygon2.translate(10, -5);
            REQUIRE(expolygon.contour.size() == expolygon2.contour.size());
            REQUIRE(expolygon.holes.size() == 1);
            REQUIRE(expolygon2.holes.size() == 1);
            for (size_t i = 0; i < expolygon.contour.size(); ++ i)
                REQUIRE(points_close(expolygon.contour[i] + Point(10, -5), expolygon2.contour[i]));
            for (size_t i = 0; i < expolygon.holes.front().size(); ++ i)
                REQUIRE(points_close(expolygon.holes.front()[i] + Point(10, -5), expolygon2.holes.front()[i]));
        }
        WHEN("Expolygon rotated around point") {
            ExPolygon expolygon2 = expolygon;
            expolygon2.rotate(M_PI / 2, Point(150, 150));
            REQUIRE(expolygon.contour.size() == expolygon2.contour.size());
            REQUIRE(expolygon.holes.size() == 1);
            REQUIRE(expolygon2.holes.size() == 1);
            REQUIRE(polygons_close_permuted(expolygon2.contour, expolygon.contour, { 1, 2, 3, 0}));
            REQUIRE(polygons_close_permuted(expolygon2.holes.front(), expolygon.holes.front(), { 3, 0, 1, 2}));
        }
    }
}

#include <sstream>
#include <cereal/cereal.hpp>
#include <cereal/archives/binary.hpp>
#include "libslic3r/ExPolygonSerialize.hpp"
TEST_CASE("Serialization of expolygons", "[ExPolygon, Cereal, serialization]")
{
    ExPolygons expolys{{
        // expolygon 1 - without holes
        {{0,0}, {10,0}, {10,10}, {0,10}}, // contour
        // expolygon 2 - with rect 1px hole
        {{{0,0}, {10,0}, {10,10}, {0,10}},
        {{5, 5}, {6, 5}, {6, 6}, {5, 6}}}
    }};

    std::stringstream ss; // any stream can be used
    {
        cereal::BinaryOutputArchive oarchive(ss); // Create an output archive
        oarchive(expolys);
    } // archive goes out of scope, ensuring all contents are flushed

    std::string data = ss.str();
    CHECK(!data.empty());

    ExPolygons expolys_loaded;
    {
        cereal::BinaryInputArchive iarchive(ss); // Create an input archive
        iarchive(expolys_loaded);
    }

    CHECK(expolys == expolys_loaded);
}

#include <cereal/archives/json.hpp>
#include <regex>
// It is used to serialize expolygons into 3mf.
TEST_CASE("Serialization of expolygons to string", "[ExPolygon, Cereal, serialization]")
{
    ExPolygons expolys{{
        // expolygon 1 - without holes
        {{0,0}, {10,0}, {10,10}, {0,10}}, // contour
        // expolygon 2 - with rect 1px hole
        {{{0,0}, {10,0}, {10,10}, {0,10}},
        {{5, 5}, {6, 5}, {6, 6}, {5, 6}}} 
    }};

    std::stringstream ss_out; // any stream can be used
    {
        cereal::JSONOutputArchive oarchive(ss_out); // Create an output archive
        oarchive(expolys);
    } // archive goes out of scope, ensuring all contents are flushed

    //Simplify text representation of expolygons
    std::string data = ss_out.str();
    // Data contain this JSON string
    //{
    //    "value0": [
    //        {
    //            "value0": {
    //                "value0":
    //                    [{"value0": 0, "value1": 0}, {"value0": 10, "value1": 0}, {"value0": 10, "value1": 10}, {"value0": 0, "value1": 10}]
    //            },
    //            "value1": []
    //        },
    //        {
    //            "value0": {
    //                "value0":
    //                    [{"value0": 0, "value1": 0}, {"value0": 10, "value1": 0}, {"value0": 10, "value1": 10}, {"value0": 0, "value1": 10}]
    //            },
    //            "value1": [{
    //                "value0":
    //                    [{"value0": 5, "value1": 5}, {"value0": 6, "value1": 5}, {"value0": 6, "value1": 6}, {"value0": 5, "value1": 6}]
    //            }]
    //        }
    //    ]
    //}

    // Change JSON named object to JSON arrays(without name)
    
    // RegEx for wihitespace = "[ \t\r\n\v\f]"
    std::regex r("\"value[0-9]+\":|[ \t\r\n\v\f]");
    std::string data_short = std::regex_replace(data, r , "");    
    std::replace(data_short.begin(), data_short.end(), '{', '[');
    std::replace(data_short.begin(), data_short.end(), '}', ']');
    CHECK(!data_short.empty());
    // Cereal acceptable string
    // [[[[[[0,0],[10,0],[10,10],[0,10]]],[]],[[[[0,0],[10,0],[10,10],[0,10]]],[[[[5,5],[6,5],[6,6],[5,6]]]]]]]
    std::stringstream ss_in(data_short);
    ExPolygons expolys_loaded;
    {
        cereal::JSONInputArchive iarchive(ss_in); // Create an input archive
        iarchive(expolys_loaded);
    }

    CHECK(expolys == expolys_loaded);
}