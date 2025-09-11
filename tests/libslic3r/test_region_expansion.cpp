#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <libslic3r/libslic3r.h>
#include <libslic3r/Algorithm/RegionExpansion.hpp>
#include <libslic3r/ClipperUtils.hpp>
#include <libslic3r/ExPolygon.hpp>
#include <libslic3r/Polygon.hpp>
#include <libslic3r/SVG.cpp>

using namespace Slic3r;
using namespace Catch;

//#define DEBUG_TEMP_DIR "d:\\temp\\"

SCENARIO("Region expansion basics", "[RegionExpansion]") {
    static constexpr const coord_t ten = scaled<coord_t>(10.);
    GIVEN("two touching squares") {
        Polygon square1{ { 1 * ten, 1 * ten }, { 2 * ten, 1 * ten }, { 2 * ten, 2 * ten }, { 1 * ten, 2 * ten } };
        Polygon square2{ { 2 * ten, 1 * ten }, { 3 * ten, 1 * ten }, { 3 * ten, 2 * ten }, { 2 * ten, 2 * ten } };
        Polygon square3{ { 1 * ten, 2 * ten }, { 2 * ten, 2 * ten }, { 2 * ten, 3 * ten }, { 1 * ten, 3 * ten } };
        static constexpr const float expansion = scaled<float>(1.);
        auto test_expansion = [](const Polygon &src, const Polygon &boundary) {
            std::vector<Polygons> expanded = Algorithm::expand_expolygons({ ExPolygon{src} }, { ExPolygon{boundary} },
                expansion,
                scaled<float>(0.3), // expansion step
                5);                 // max num steps
            THEN("Single anchor is produced") {
                REQUIRE(expanded.size() == 1);
            }
            THEN("The area of the anchor is 10mm2") {
                REQUIRE(area(expanded.front()) == Approx(expansion * ten));
            }
        };

        WHEN("second square expanded into the first square (to left)") {
            test_expansion(square2, square1);
        }
        WHEN("first square expanded into the second square (to right)") {
            test_expansion(square1, square2);
        }
        WHEN("third square expanded into the first square (down)") {
            test_expansion(square3, square1);
        }
        WHEN("first square expanded into the third square (up)") {
            test_expansion(square1, square3);
        }
    }

    GIVEN("simple bridge") {
        Polygon square1{ { 1 * ten, 1 * ten }, { 2 * ten, 1 * ten }, { 2 * ten, 2 * ten }, { 1 * ten, 2 * ten } };
        Polygon square2{ { 2 * ten, 1 * ten }, { 3 * ten, 1 * ten }, { 3 * ten, 2 * ten }, { 2 * ten, 2 * ten } };
        Polygon square3{ { 3 * ten, 1 * ten }, { 4 * ten, 1 * ten }, { 4 * ten, 2 * ten }, { 3 * ten, 2 * ten } };

        WHEN("expanded") {
            static constexpr const float expansion = scaled<float>(1.);
            std::vector<Polygons> expanded = Algorithm::expand_expolygons({ ExPolygon{square2} }, { ExPolygon{square1}, ExPolygon{square3} },
                expansion,
                scaled<float>(0.3), // expansion step
                5);                 // max num steps
            THEN("Two anchors are produced") {
                REQUIRE(expanded.size() == 1);
                REQUIRE(expanded.front().size() == 2);
            }
            THEN("The area of each anchor is 10mm2") {
                REQUIRE(area(expanded.front().front()) == Approx(expansion * ten));
                REQUIRE(area(expanded.front().back()) == Approx(expansion * ten));
            }
        }

        WHEN("fully expanded") {
            static constexpr const float expansion = scaled<float>(10.1);
            std::vector<Polygons> expanded = Algorithm::expand_expolygons({ ExPolygon{square2} }, { ExPolygon{square1}, ExPolygon{square3} },
                expansion,
                scaled<float>(2.3), // expansion step
                5);                 // max num steps
            THEN("Two anchors are produced") {
                REQUIRE(expanded.size() == 1);
                REQUIRE(expanded.front().size() == 2);
            }
            THEN("The area of each anchor is 100mm2") {
                REQUIRE(area(expanded.front().front()) == Approx(sqr<double>(ten)));
                REQUIRE(area(expanded.front().back()) == Approx(sqr<double>(ten)));
            }
        }
    }

    GIVEN("two bridges") {
        Polygon left_support  { { 1 * ten, 1 * ten }, { 2 * ten, 1 * ten }, { 2 * ten, 4 * ten }, { 1 * ten, 4 * ten } };
        Polygon right_support { { 3 * ten, 1 * ten }, { 4 * ten, 1 * ten }, { 4 * ten, 4 * ten }, { 3 * ten, 4 * ten } };
        Polygon bottom_bridge { { 2 * ten, 1 * ten }, { 3 * ten, 1 * ten }, { 3 * ten, 2 * ten }, { 2 * ten, 2 * ten } };
        Polygon top_bridge    { { 2 * ten, 3 * ten }, { 3 * ten, 3 * ten }, { 3 * ten, 4 * ten }, { 2 * ten, 4 * ten } };

        WHEN("expanded") {
            static constexpr const float expansion = scaled<float>(1.);
            std::vector<Polygons> expanded = Algorithm::expand_expolygons({ ExPolygon{bottom_bridge}, ExPolygon{top_bridge} }, { ExPolygon{left_support}, ExPolygon{right_support} },
                expansion,
                scaled<float>(0.3), // expansion step
                5);                 // max num steps
#if 0
            SVG::export_expolygons(DEBUG_TEMP_DIR "two_bridges-out.svg",
                { { { { ExPolygon{left_support}, ExPolygon{right_support} } }, { "supports", "orange", 0.5f } },
                  { { { ExPolygon{bottom_bridge}, ExPolygon{top_bridge} } },   { "bridges",  "blue",   0.5f } },
                  { { union_ex(union_(expanded.front(), expanded.back())) },   { "expanded", "red",    "black", "", scaled<coord_t>(0.1f), 0.5f } } });
#endif
            THEN("Two anchors are produced for each bridge") {
                REQUIRE(expanded.size() == 2);
                REQUIRE(expanded.front().size() == 2);
                REQUIRE(expanded.back().size() == 2);
            }
            THEN("The area of each anchor is 10mm2") {
                double a = expansion * ten + M_PI * sqr(expansion) / 4;
                double eps = sqr(scaled<double>(0.1));
                REQUIRE(is_approx(area(expanded.front().front()), a, eps));
                REQUIRE(is_approx(area(expanded.front().back()), a, eps));
                REQUIRE(is_approx(area(expanded.back().front()), a, eps));
                REQUIRE(is_approx(area(expanded.back().back()), a, eps));
            }
        }
    }

    GIVEN("rectangle with rhombic cut-out") {
        double  diag = 1 * ten * sqrt(2.) / 4.;
        Polygon square_with_rhombic_cutout{ { 0, 0 }, { 1 * ten, 0 }, { ten / 2, ten / 2 }, { 1 * ten, 1 * ten }, { 0, 1 * ten } };
        Polygon rhombic { { ten / 2, ten / 2 }, { 3 * ten / 4, ten / 4 }, { 1 * ten, ten / 2 }, { 3 * ten / 4, 3 * ten / 4 } };

        WHEN("expanded") {
            static constexpr const float expansion = scaled<float>(1.);
            std::vector<Polygons> expanded = Algorithm::expand_expolygons({ ExPolygon{rhombic} }, { ExPolygon{square_with_rhombic_cutout} },
                expansion,
                scaled<float>(0.1), // expansion step
                11);                // max num steps
#if 0
            SVG::export_expolygons(DEBUG_TEMP_DIR "rectangle_with_rhombic_cut-out.svg",
                { { { { ExPolygon{square_with_rhombic_cutout} } }, { "square_with_rhombic_cutout", "orange", 0.5f } },
                  { { { ExPolygon{rhombic} } },                    { "rhombic",                    "blue",   0.5f } },
                  { { union_ex(expanded.front()) },                { "bridges",                    "red",    "black", "", scaled<coord_t>(0.1f), 0.5f } } });
#endif
            THEN("Single anchor is produced") {
                REQUIRE(expanded.size() == 1);
            }
            THEN("The area of anchor is correct") {
                double area_calculated = area(expanded.front());
                double area_expected = 2. * diag * expansion + M_PI * sqr(expansion) * 0.75;
                REQUIRE(is_approx(area_expected, area_calculated, sqr(scaled<double>(0.2))));
            }
        }

        WHEN("extra expanded") {
            static constexpr const float expansion = scaled<float>(2.5);
            std::vector<Polygons> expanded = Algorithm::expand_expolygons({ ExPolygon{rhombic} }, { ExPolygon{square_with_rhombic_cutout} },
                expansion,
                scaled<float>(0.25), // expansion step
                11);                 // max num steps
#if 0
            SVG::export_expolygons(DEBUG_TEMP_DIR "rectangle_with_rhombic_cut-out2.svg",
                { { { { ExPolygon{square_with_rhombic_cutout} } }, { "square_with_rhombic_cutout", "orange", 0.5f } },
                  { { { ExPolygon{rhombic} } },                    { "rhombic",                    "blue",   0.5f } },
                  { { union_ex(expanded.front()) },                { "bridges",                    "red",    "black", "", scaled<coord_t>(0.1f), 0.5f } } });
#endif
            THEN("Single anchor is produced") {
                REQUIRE(expanded.size() == 1);
            }
            THEN("The area of anchor is correct") {
                double area_calculated = area(expanded.front());
                double area_expected = 2. * diag * expansion + M_PI * sqr(expansion) * 0.75;
                REQUIRE(is_approx(area_expected, area_calculated, sqr(scaled<double>(0.3))));
            }
        }
    }

    GIVEN("square with two holes") {
        Polygon outer{ { 0, 0 }, { 3 * ten, 0 }, { 3 * ten, 5 * ten }, { 0, 5 * ten } };
        Polygon hole1{ { 1 * ten, 1 * ten }, { 1 * ten, 2 * ten }, { 2 * ten, 2 * ten }, { 2 * ten, 1 * ten } };
        Polygon hole2{ { 1 * ten, 3 * ten }, { 1 * ten, 4 * ten }, { 2 * ten, 4 * ten }, { 2 * ten, 3 * ten } };
        ExPolygon boundary(outer);
        boundary.holes = { hole1, hole2 };

        Polygon anchor{ { -1 * ten, coord_t(1.5 * ten) }, { 0 * ten, coord_t(1.5 * ten) }, { 0, coord_t(3.5 * ten) }, { -1 * ten, coord_t(3.5 * ten) } };

        WHEN("expanded") {
            static constexpr const float expansion = scaled<float>(5.);
            std::vector<Polygons> expanded = Algorithm::expand_expolygons({ ExPolygon{anchor} }, { boundary },
                expansion,
                scaled<float>(0.4), // expansion step
                15);                // max num steps
#if 0
            SVG::export_expolygons(DEBUG_TEMP_DIR "square_with_two_holes-out.svg",
                { { { { ExPolygon{anchor} } }, { "anchor", "orange", 0.5f } },
                  { { { boundary } },          { "boundary",  "blue", 0.5f } },
                  { { union_ex(expanded.front()) }, { "expanded", "red", "black", "", scaled<coord_t>(0.1f), 0.5f } } });
#endif
            THEN("The anchor expands into a single region") {
                REQUIRE(expanded.size() == 1);
                REQUIRE(expanded.front().size() == 1);
            }
            THEN("The area of anchor is correct") {
                double area_calculated = area(expanded.front());
                double area_expected = double(expansion) * 2. * double(ten) + M_PI * sqr(expansion) * 0.5;
                REQUIRE(is_approx(area_expected, area_calculated, sqr(scaled<double>(0.45))));
            }
        }
        WHEN("expanded even more") {
            static constexpr const float expansion = scaled<float>(25.);
            std::vector<Polygons> expanded = Algorithm::expand_expolygons({ ExPolygon{anchor} }, { boundary },
                expansion,
                scaled<float>(2.), // expansion step
                15);               // max num steps
#if 0
            SVG::export_expolygons(DEBUG_TEMP_DIR "square_with_two_holes-expanded2-out.svg",
                { { { { ExPolygon{anchor} } }, { "anchor", "orange", 0.5f } },
                  { { { boundary } },          { "boundary",  "blue", 0.5f } },
                  { { union_ex(expanded.front()) }, { "expanded", "red", "black", "", scaled<coord_t>(0.1f), 0.5f } } });
#endif
            THEN("The anchor expands into a single region") {
                REQUIRE(expanded.size() == 1);
                REQUIRE(expanded.front().size() == 1);
            }
        }
        WHEN("expanded yet even more") {
            static constexpr const float expansion = scaled<float>(28.);
            std::vector<Polygons> expanded = Algorithm::expand_expolygons({ ExPolygon{anchor} }, { boundary },
                expansion,
                scaled<float>(2.), // expansion step
                20);               // max num steps
#if 0
            SVG::export_expolygons(DEBUG_TEMP_DIR "square_with_two_holes-expanded3-out.svg",
                { { { { ExPolygon{anchor} } }, { "anchor", "orange", 0.5f } },
                  { { { boundary } },          { "boundary",  "blue", 0.5f } },
                  { { union_ex(expanded.front()) }, { "expanded", "red", "black", "", scaled<coord_t>(0.1f), 0.5f } } });
#endif
            THEN("The anchor expands into a single region with two holes") {
                REQUIRE(expanded.size() == 1);
                REQUIRE(expanded.front().size() == 3);
            }
        }
        WHEN("expanded fully") {
            static constexpr const float expansion = scaled<float>(35.);
            std::vector<Polygons> expanded = Algorithm::expand_expolygons({ ExPolygon{anchor} }, { boundary },
                expansion,
                scaled<float>(2.), // expansion step
                25);               // max num steps
#if 0
            SVG::export_expolygons(DEBUG_TEMP_DIR "square_with_two_holes-expanded_fully-out.svg",
                { { { { ExPolygon{anchor} } }, { "anchor", "orange", 0.5f } },
                  { { { boundary } },          { "boundary",  "blue", 0.5f } },
                  { { union_ex(expanded.front()) }, { "expanded", "red", "black", "", scaled<coord_t>(0.1f), 0.5f } } });
#endif
            THEN("The anchor expands into a single region with two holes, fully covering the boundary") {
                REQUIRE(expanded.size() == 1);
                REQUIRE(expanded.front().size() == 3);
                REQUIRE(area(expanded.front()) == Approx(area(boundary)));
            }
        }
    }
    GIVEN("square with hole, hole edge anchored") {
        Polygon outer{ { -1 * ten, -1 * ten }, { 2 * ten, -1 * ten }, { 2 * ten, 2 * ten }, { -1 * ten, 2 * ten } };
        Polygon hole { { 0, ten }, { ten, ten }, { ten, 0 }, { 0, 0 } };
        Polygon anchor{ { 0, 0 }, { ten, 0 }, { ten, ten }, { 0, ten } };
        ExPolygon boundary(outer);
        boundary.holes = { hole };

        WHEN("expanded") {
            static constexpr const float expansion = scaled<float>(5.);
            std::vector<Polygons> expanded = Algorithm::expand_expolygons({ ExPolygon{anchor} }, { boundary },
                expansion,
                scaled<float>(0.4), // expansion step
                15);                // max num steps
#if 0
            SVG::export_expolygons(DEBUG_TEMP_DIR "square_with_hole_anchored-out.svg",
                { { { { ExPolygon{anchor} } }, { "anchor", "orange", 0.5f } },
                  { { { boundary } },          { "boundary", "blue", 0.5f } },
                  { { union_ex(expanded.front()) }, { "expanded", "red", "black", "", scaled<coord_t>(0.1f), 0.5f } } });
#endif
            THEN("The anchor expands into a single region with a hole") {
                REQUIRE(expanded.size() == 1);
                REQUIRE(expanded.front().size() == 2);
            }
            THEN("The area of anchor is correct") {
                double area_calculated = area(expanded.front());
                double area_expected = double(expansion) * 4. * double(ten) + M_PI * sqr(expansion);
                REQUIRE(is_approx(area_expected, area_calculated, sqr(scaled<double>(0.6))));
            }
        }
    }
}

TEST_CASE("WaveSeed - ZFillFunction - SPE-2698", "[WaveSeedZFillFunctionSPE2698]")
{
    const ExPolygons boundary = {{
        Point(-5900000, -20000000),
        Point(-6518889, -22009467),
        Point(-5779768, -22315621),
        Point(-5662934, -22033558),
        Point(-5615689, -21919501),
        Point(-5779767, -22315622),
        Point(-5040682, -22621761),
        Point(-4000000, -20000000),
    }};

    const ExPolygons src = {{
        Point(-5615689, -21919501),
        Point(-5662934, -22033558),
        Point(-5779768, -22315621),
        Point(-5779767, -22315622),
    }};

    std::vector<Slic3r::Algorithm::WaveSeed> wave_seeds = Slic3r::Algorithm::wave_seeds(src, boundary, 83561.8046, true);
    for (const Slic3r::Algorithm::WaveSeed &wave_seed : wave_seeds) {
        REQUIRE(wave_seed.src < src.size());
        REQUIRE(wave_seed.boundary < boundary.size());
    }
}

