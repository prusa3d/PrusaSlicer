#include <catch2/catch_test_macros.hpp>

#include "libslic3r/Arachne/WallToolPaths.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/SVG.hpp"
#include "libslic3r/Utils.hpp"

using namespace Slic3r;
using namespace Slic3r::Arachne;

//#define ARACHNE_DEBUG_OUT

#ifdef ARACHNE_DEBUG_OUT
static void export_perimeters_to_svg(const std::string &path, const Polygons &contours, const std::vector<Arachne::VariableWidthLines> &perimeters, const ExPolygons &infill_area)
{
    coordf_t    stroke_width = scale_(0.03);
    BoundingBox bbox         = get_extents(contours);
    bbox.offset(scale_(1.));
    ::Slic3r::SVG svg(path.c_str(), bbox);

    svg.draw(infill_area, "cyan");

    for (const Arachne::VariableWidthLines &perimeter : perimeters)
        for (const Arachne::ExtrusionLine &extrusion_line : perimeter) {
            ThickPolyline thick_polyline = to_thick_polyline(extrusion_line);
            svg.draw({thick_polyline}, "green", "blue", stroke_width);
        }

    for (const Line &line : to_lines(contours))
        svg.draw(line, "red", stroke_width);
}
#endif

TEST_CASE("Arachne - Closed ExtrusionLine", "[ArachneClosedExtrusionLine]") {
    Polygon poly = {
        Point(-40000000, 10000000),
        Point(-62480000, 10000000),
        Point(-62480000, -7410000),
        Point(-58430000, -7330000),
        Point(-58400000, -5420000),
        Point(-58720000, -4710000),
        Point(-58940000, -3870000),
        Point(-59020000, -3000000),
    };

    Polygons polygons    = {poly};
    coord_t  spacing     = 407079;
    coord_t  inset_count = 5;

    Arachne::WallToolPaths wallToolPaths(polygons, spacing, spacing, inset_count, 0, 0.2, PrintObjectConfig::defaults(), PrintConfig::defaults());
    wallToolPaths.generate();
    std::vector<Arachne::VariableWidthLines> perimeters = wallToolPaths.getToolPaths();

#ifdef ARACHNE_DEBUG_OUT
    export_perimeters_to_svg(debug_out_path("arachne-closed-extrusion-line.svg"), polygons, perimeters, union_ex(wallToolPaths.getInnerContour()));
#endif

    for (VariableWidthLines &perimeter : perimeters)
        for (ExtrusionLine &el : perimeter)
            if (el.is_closed) {
                REQUIRE(el.junctions.front().p == el.junctions.back().p);
            }
}

// This test case was distilled from GitHub issue #8472.
// Where for wall_distribution_count == 3 sometime middle perimeter was missing.
TEST_CASE("Arachne - Missing perimeter - #8472", "[ArachneMissingPerimeter8472]") {
    Polygon poly = {
        Point(-9000000,  8054793),
        Point( 7000000,  8054793),
        Point( 7000000, 10211874),
        Point(-8700000, 10211874),
        Point(-9000000,  9824444)
    };

    Polygons polygons    = {poly};
    coord_t  spacing     = 437079;
    coord_t  inset_count = 3;

    PrintObjectConfig print_object_config = PrintObjectConfig::defaults();
    print_object_config.wall_distribution_count.setInt(3);

    Arachne::WallToolPaths wallToolPaths(polygons, spacing, spacing, inset_count, 0, 0.2, print_object_config, PrintConfig::defaults());
    wallToolPaths.generate();
    std::vector<Arachne::VariableWidthLines> perimeters = wallToolPaths.getToolPaths();

#ifdef ARACHNE_DEBUG_OUT
    export_perimeters_to_svg(debug_out_path("arachne-missing-perimeter-8472.svg"), polygons, perimeters, union_ex(wallToolPaths.getInnerContour()));
#endif

    REQUIRE(perimeters.size() == 3);
}

// This test case was distilled from GitHub issue #8593.
// Where on the symmetrical model, there were missing parts of extrusions in gear teeth based on model rotation.
TEST_CASE("Arachne - #8593 - Missing a part of the extrusion", "[ArachneMissingPartOfExtrusion8593]") {
    const Polygon poly_orig = {
        Point( 1800000, 28500000),
        Point( 1100000, 30000000),
        Point( 1000000, 30900000),
        Point(  600000, 32300000),
        Point( -600000, 32300000),
        Point(-1000000, 30900000),
        Point(-1100000, 30000000),
        Point(-1800000, 29000000),
    };

    coord_t  spacing     = 377079;
    coord_t  inset_count = 3;

    PrintObjectConfig print_object_config = PrintObjectConfig::defaults();
    print_object_config.min_bead_width         = ConfigOptionFloatOrPercent(0.315, false);
    print_object_config.wall_transition_angle  = ConfigOptionFloat(40.);
    print_object_config.wall_transition_length = ConfigOptionFloatOrPercent(1., false);


    // This behavior seems to be related to the rotation of the input polygon.
    // There are specific angles in which this behavior is always triggered.
    for (const double angle : {0., -PI / 2., -PI / 15.}) {
        Polygon poly = poly_orig;
        if (angle != 0.)
            poly.rotate(angle);

        Polygons polygons    = {poly};
        Arachne::WallToolPaths wall_tool_paths(polygons, spacing, spacing, inset_count, 0, 0.2, print_object_config, PrintConfig::defaults());
        wall_tool_paths.generate();
        std::vector<Arachne::VariableWidthLines> perimeters = wall_tool_paths.getToolPaths();

#ifdef ARACHNE_DEBUG_OUT
        {
            static int iRun = 0;
            export_perimeters_to_svg(debug_out_path("arachne-missing-part-of-extrusion-8593-%d.svg", iRun++), polygons, perimeters, union_ex(wall_tool_paths.getInnerContour()));
        }
#endif
    }
}

// This test case was distilled from GitHub issue #8573.
TEST_CASE("Arachne - #8573 - A gap in the perimeter - 1", "[ArachneGapInPerimeter8573_1]") {
    const Polygon poly = {
        Point(13960000,  500000),
        Point(13920000, 1210000),
        Point(13490000, 2270000),
        Point(12960000, 3400000),
        Point(12470000, 4320000),
        Point(12160000, 4630000),
        Point(12460000, 3780000),
        Point(12700000, 2850000),
        Point(12880000, 1910000),
        Point(12950000, 1270000),
        Point(13000000,  500000),
    };

    Polygons polygons    = {poly};
    coord_t  spacing     = 407079;
    coord_t  inset_count = 2;

    PrintObjectConfig print_object_config = PrintObjectConfig::defaults();
//    print_object_config.wall_transition_angle = ConfigOptionFloat(20.);

    Arachne::WallToolPaths wallToolPaths(polygons, spacing, spacing, inset_count, 0, 0.2, print_object_config, PrintConfig::defaults());
    wallToolPaths.generate();
    std::vector<Arachne::VariableWidthLines> perimeters = wallToolPaths.getToolPaths();

#ifdef ARACHNE_DEBUG_OUT
    export_perimeters_to_svg(debug_out_path("arachne-gap-in-perimeter-1-8573.svg"), polygons, perimeters, union_ex(wallToolPaths.getInnerContour()));
#endif
}

// This test case was distilled from GitHub issue #8444.
TEST_CASE("Arachne - #8444 - A gap in the perimeter - 2", "[ArachneGapInPerimeter8444_2]") {
    const Polygon poly = {
        Point(14413938, 3825902),
        Point(16817613,  711749),
        Point(19653030,   67154),
        Point(20075592,  925370),
        Point(20245428, 1339788),
        Point(20493219, 2121894),
        Point(20570295, 2486625),
        Point(20616559, 2835232),
        Point(20631964, 3166882),
        Point(20591800, 3858877),
        Point(19928267, 2153012),
        Point(19723020, 1829802),
        Point(19482017, 1612364),
        Point(19344810, 1542433),
        Point(19200249, 1500902),
        Point(19047680, 1487200),
        Point(18631073, 1520777),
        Point(18377524, 1567627),
        Point(18132517, 1641174),
        Point(17896307, 1741360),
        Point(17669042, 1868075),
        Point(17449999, 2021790),
    };

    Polygons polygons    = {poly};
    coord_t  spacing     = 594159;
    coord_t  inset_count = 2;

    PrintObjectConfig print_object_config = PrintObjectConfig::defaults();
    //    print_object_config.wall_transition_angle = ConfigOptionFloat(20.);

    Arachne::WallToolPaths wallToolPaths(polygons, spacing, spacing, inset_count, 0, 0.4, print_object_config, PrintConfig::defaults());
    wallToolPaths.generate();
    std::vector<Arachne::VariableWidthLines> perimeters = wallToolPaths.getToolPaths();

#ifdef ARACHNE_DEBUG_OUT
    export_perimeters_to_svg(debug_out_path("arachne-gap-in-perimeter-2-8444.svg"), polygons, perimeters, union_ex(wallToolPaths.getInnerContour()));
#endif
}

// This test case was distilled from GitHub issue #8528.
// There is a hole in the place where the number of perimeters is changing from 6 perimeters to 7 perimeters.
TEST_CASE("Arachne - #8528 - A hole when number of perimeters is changing", "[ArachneHoleOnPerimetersChange8528]") {
    const Polygon poly = {
        Point(-30000000, 27650000),
        Point(-30000000, 33500000),
        Point(-40000000, 33500000),
        Point(-40500000, 33500000),
        Point(-41100000, 33400000),
        Point(-41600000, 33200000),
        Point(-42100000, 32900000),
        Point(-42600000, 32600000),
        Point(-43000000, 32200000),
        Point(-43300000, 31700000),
        Point(-43600000, 31200000),
        Point(-43800000, 30700000),
        Point(-43900000, 30100000),
        Point(-43900000, 29600000),
        Point(-43957080, 25000000),
        Point(-39042920, 25000000),
        Point(-39042920, 27650000),
    };

    Polygons polygons    = {poly};
    coord_t  spacing     = 814159;
    coord_t  inset_count = 5;

    PrintObjectConfig print_object_config = PrintObjectConfig::defaults();
    print_object_config.min_bead_width = ConfigOptionFloatOrPercent(0.68, false);

    // Changing min_bead_width to 0.66 seems that resolve this issue, at least in this case.
    print_object_config.min_bead_width = ConfigOptionFloatOrPercent(0.66, false);

    Arachne::WallToolPaths wallToolPaths(polygons, spacing, spacing, inset_count, 0, 0.4, print_object_config, PrintConfig::defaults());
    wallToolPaths.generate();
    std::vector<Arachne::VariableWidthLines> perimeters = wallToolPaths.getToolPaths();

#ifdef ARACHNE_DEBUG_OUT
    export_perimeters_to_svg(debug_out_path("arachne-hole-on-perimeters-change-8528.svg"), polygons, perimeters, union_ex(wallToolPaths.getInnerContour()));
#endif
}

// This test case was distilled from GitHub issue #8528.
// There is an inconsistency between layers in length of the single perimeters.
TEST_CASE("Arachne - #8555 - Inconsistent single perimeter", "[ArachneInconsistentSinglePerimeter8555]") {
    const Polygon poly_0 = {
        Point(5527411, -38490007),
        Point(11118814, -36631169),
        Point(13529600, -36167120),
        Point(11300145, -36114514),
        Point(10484024, -36113916),
        Point(5037323, -37985945),
        Point(4097054, -39978866)
    };
    const Polygon poly_1 = {
        Point(5566841, -38517205),
        Point(11185208, -36649404),
        Point(13462719, -36211009),
        Point(11357290, -36161329),
        Point(10583855, -36160763),
        Point(5105952, -38043516),
        Point(4222019, -39917031)
    };
    const Polygon poly_2 = {
        Point(5606269, -38544404),
        Point(11251599, -36667638),
        Point(13391666, -36255700),
        Point(10683552, -36207653),
        Point(5174580, -38101085),
        Point(4346981, -39855197)
    };
    const Polygon poly_3 = {
        Point(5645699, -38571603),
        Point(11317993, -36685873),
        Point(13324786, -36299588),
        Point(10783383, -36254499),
        Point(5243209, -38158655),
        Point(4471947, -39793362)
    };
    const Polygon poly_4 = {
        Point(5685128, -38598801),
        Point(11384385, -36704108),
        Point(13257907, -36343476),
        Point(10883211, -36301345),
        Point(5311836, -38216224),
        Point(4596909, -39731528)
    };
    const Polygon poly_5 = {
        Point(5724558, -38626000),
        Point(11450778, -36722343),
        Point(13191026, -36387365),
        Point(10983042, -36348191),
        Point(5380466, -38273795),
        Point(4721874, -39669693)
    };

    Polygons polygons    = {poly_0, poly_1, poly_2, poly_3, poly_4, poly_5};
    coord_t  spacing     = 417809;
    coord_t  inset_count = 2;

    for (size_t poly_idx = 0; poly_idx < polygons.size(); ++poly_idx) {
        Polygons input_polygons{polygons[poly_idx]};
        Arachne::WallToolPaths wallToolPaths(input_polygons, spacing, spacing, inset_count, 0, 0.15, PrintObjectConfig::defaults(), PrintConfig::defaults());
        wallToolPaths.generate();
        std::vector<Arachne::VariableWidthLines> perimeters = wallToolPaths.getToolPaths();

#ifdef ARACHNE_DEBUG_OUT
        export_perimeters_to_svg(debug_out_path("arachne-inconsistent-single-perimeter-8555-%d.svg", poly_idx), input_polygons, perimeters, union_ex(wallToolPaths.getInnerContour()));
#endif
    }
}

// This test case was distilled from GitHub issue #8633.
// Open perimeter extrusion is shorter on endpoints in comparison to closed perimeter.
TEST_CASE("Arachne - #8633 - Shorter open perimeter", "[ArachneShorterOpenPerimeter8633]") {
    const Polygon poly_0 = {
        Point(6507498, 4189461),
        Point(6460382, 3601960),
        Point(6390896, 3181097),
        Point(6294072, 2765838),
        Point(6170293, 2357794),

        Point(7090581, 2045388),
        Point(7232821, 2514293),
        Point(7344089, 2991501),
        Point(7423910, 3474969),
        Point(7471937, 3962592),
        Point(7487443, 4436235),
        Point(6515575, 4436235),
    };

    const Polygon poly_1 = {
        Point(6507498, 4189461),
        Point(6460382, 3601960),
        Point(6390896, 3181097),
        Point(6294072, 2765838),
        Point(6170293, 2357794),

        Point(6917958, 1586830),
        Point(7090552, 2045398),

        Point(7232821, 2514293),
        Point(7344089, 2991501),
        Point(7423910, 3474969),
        Point(7471937, 3962592),
        Point(7487443, 4436235),
        Point(6515575, 4436235),
    };

    Polygons polygons    = {poly_0, poly_1};
    coord_t  spacing     = 617809;
    coord_t  inset_count = 1;

    PrintObjectConfig print_object_config = PrintObjectConfig::defaults();
    print_object_config.min_bead_width         = ConfigOptionFloatOrPercent(0.51, false);
    print_object_config.min_feature_size       = ConfigOptionFloatOrPercent(0.15, false);
    print_object_config.wall_transition_length = ConfigOptionFloatOrPercent(0.6, false);

    for (size_t poly_idx = 0; poly_idx < polygons.size(); ++poly_idx) {
        Polygons input_polygons{polygons[poly_idx]};
        Arachne::WallToolPaths wallToolPaths(input_polygons, spacing, spacing, inset_count, 0, 0.15, print_object_config, PrintConfig::defaults());
        wallToolPaths.generate();
        std::vector<Arachne::VariableWidthLines> perimeters = wallToolPaths.getToolPaths();

#ifdef ARACHNE_DEBUG_OUT
        export_perimeters_to_svg(debug_out_path("arachne-shorter-open-perimeter-8633-%d.svg", poly_idx), input_polygons, perimeters, union_ex(wallToolPaths.getInnerContour()));
#endif
    }
}

// This test case was distilled from GitHub issue #8597.
// There was just an issue with decrementing std::vector::begin() in a specific case.
TEST_CASE("Arachne - #8597 - removeSmallAreas", "[ArachneRemoveSmallAreas8597]") {
    const Polygon poly_0 = {
        Point(-38768167, -3636556),
        Point(-38763631, -3617883),
        Point(-38763925, -3617820),
        Point(-38990169, -3919539),
        Point(-38928506, -3919539),
    };

    const Polygon poly_1 = {
        Point(-39521732, -4480560),
        Point(-39383333, -4398498),
        Point(-39119825, -3925307),
        Point(-39165608, -3926212),
        Point(-39302205, -3959445),
        Point(-39578719, -4537002),
    };

    Polygons polygons    = {poly_0, poly_1};
    coord_t  spacing     = 407079;
    coord_t  inset_count = 2;

    Arachne::WallToolPaths wallToolPaths(polygons, spacing, spacing, inset_count, 0, 0.2, PrintObjectConfig::defaults(), PrintConfig::defaults());
    wallToolPaths.generate();
    std::vector<Arachne::VariableWidthLines> perimeters = wallToolPaths.getToolPaths();

#ifdef ARACHNE_DEBUG_OUT
    export_perimeters_to_svg(debug_out_path("arachne-remove-small-areas-8597.svg"), polygons, perimeters, union_ex(wallToolPaths.getInnerContour()));
#endif

    REQUIRE(perimeters.size() == 1);
}

// Test case for missing infill that is probably caused by PolylineStitcher, which produced an open polyline.
TEST_CASE("Arachne - Missing infill", "[ArachneMissingInfill]") {
    const Polygon poly_0 = {
        Point( 5525881,  3649657),
        Point(  452351, -2035297),
        Point(-1014702, -2144286),
        Point(-5142096, -9101108),
        Point( 5525882, -9101108),
    };

     const Polygon poly_1 = {
        Point(1415524, -2217520),
        Point(1854189, -2113857),
        Point(1566974, -2408538),
    };

    const Polygon poly_2 = {
        Point(-42854, -3771357),
        Point(310500, -3783332),
        Point( 77735, -4059215),
    };

    Polygons polygons    = {poly_0, poly_1, poly_2};
    coord_t  spacing     = 357079;
    coord_t  inset_count = 2;

    Arachne::WallToolPaths wallToolPaths(polygons, spacing, spacing, inset_count, 0, 0.2, PrintObjectConfig::defaults(), PrintConfig::defaults());
    wallToolPaths.generate();
    std::vector<Arachne::VariableWidthLines> perimeters = wallToolPaths.getToolPaths();

#ifdef ARACHNE_DEBUG_OUT
    export_perimeters_to_svg(debug_out_path("arachne-missing-infill.svg"), polygons, perimeters, union_ex(wallToolPaths.getInnerContour()));
#endif

//    REQUIRE(wallToolPaths.getInnerContour().size() == 1);
}

// This test case was distilled from GitHub issue #8849.
// Missing part of the model after simplifying generated tool-paths by simplifyToolPaths.
TEST_CASE("Arachne - #8849 - Missing part of model", "[ArachneMissingPart8849]") {
    const Polygon poly_0 = {
        Point(-29700000, -10600000),
        Point(-28200000, -10600000),
        Point( 20000000, -10600000),
        Point( 20000000, - 9900000),
        Point(-28200000, - 9900000),
        Point(-28200000,         0),
        Point(-29700000,         0),
    };

    Polygons polygons              = {poly_0};
    coord_t  ext_perimeter_spacing = 449999;
    coord_t  perimeter_spacing     = 757079;
    coord_t  inset_count           = 2;

    Arachne::WallToolPaths wall_tool_paths(polygons, ext_perimeter_spacing, perimeter_spacing, inset_count, 0, 0.32, PrintObjectConfig::defaults(), PrintConfig::defaults());
    wall_tool_paths.generate();
    std::vector<Arachne::VariableWidthLines> perimeters = wall_tool_paths.getToolPaths();

#ifdef ARACHNE_DEBUG_OUT
    export_perimeters_to_svg(debug_out_path("arachne-missing-part-8849.svg"), polygons, perimeters, union_ex(wall_tool_paths.getInnerContour()));
#endif

    [[maybe_unused]] int64_t total_extrusion_length = 0;
    for (Arachne::VariableWidthLines &perimeter : perimeters)
        for (Arachne::ExtrusionLine &extrusion_line : perimeter)
            total_extrusion_length += extrusion_line.getLength();

    // Total extrusion length should be around 30mm when the part is missing and around 120 when everything is ok.
    // REQUIRE(total_extrusion_length >= scaled<int64_t>(120.));
}

// This test case was distilled from GitHub issue #8446.
// Boost Voronoi generator produces non-planar Voronoi diagram with two intersecting linear Voronoi edges.
// Those intersecting edges are causing that perimeters are also generated in places where they shouldn't be.
TEST_CASE("Arachne - #8446 - Degenerated Voronoi diagram - Linear edges", "[ArachneDegeneratedDiagram8446LinearEdges]") {
    Polygon poly_0 = {
        Point( 42240656,  9020315),
        Point(  4474248, 42960681),
        Point( -4474248, 42960681),
        Point( -4474248, 23193537),
        Point( -6677407, 22661038),
        Point( -8830542, 21906307),
        Point( -9702935, 21539826),
        Point(-13110431, 19607811),
        Point(-18105334, 15167780),
        Point(-20675743, 11422461),
        Point(-39475413, 17530840),
        Point(-42240653,  9020315)
    };

    Polygons polygons              = {poly_0};
    coord_t  ext_perimeter_spacing = 407079;
    coord_t  perimeter_spacing     = 407079;
    coord_t  inset_count           = 1;

    Arachne::WallToolPaths wall_tool_paths(polygons, ext_perimeter_spacing, perimeter_spacing, inset_count, 0, 0.2, PrintObjectConfig::defaults(), PrintConfig::defaults());
    wall_tool_paths.generate();
    std::vector<Arachne::VariableWidthLines> perimeters = wall_tool_paths.getToolPaths();

#ifdef ARACHNE_DEBUG_OUT
    export_perimeters_to_svg(debug_out_path("arachne-degenerated-diagram-8446-linear-edges.svg"), polygons, perimeters, union_ex(wall_tool_paths.getInnerContour()));
#endif

    int64_t total_extrusion_length = 0;
    for (Arachne::VariableWidthLines &perimeter : perimeters)
        for (Arachne::ExtrusionLine &extrusion_line : perimeter)
            total_extrusion_length += extrusion_line.getLength();

    // Total extrusion length should be around 211.2mm when the part is ok and 212.1mm when it has perimeters in places where they shouldn't be.
    REQUIRE(total_extrusion_length <= scaled<int64_t>(211.5));
}

// This test case was distilled from GitHub issue #8846.
// Boost Voronoi generator produces degenerated Voronoi diagram with one parabolic edge intersecting linear Voronoi edge.
// Those intersecting edges are causing that perimeters are also generated in places where they shouldn't be.
TEST_CASE("Arachne - #8846 - Degenerated Voronoi diagram - One Parabola", "[ArachneDegeneratedDiagram8846OneParabola]") {
    const Polygon poly_0 = {
        Point(101978540, -41304489),  Point(101978540, 41304489),
        Point(94709788, 42514051),    Point(94709788, 48052315),
        Point(93352716, 48052315),    Point(93352716, 42514052),
        Point(75903540, 42514051),    Point(75903540, 48052315),
        Point(74546460, 48052315),    Point(74546460, 42514052),
        Point(69634788, 42514051),    Point(69634788, 48052315),
        Point(68277708, 48052315),    Point(68277708, 42514051),
        Point(63366040, 42514051),    Point(63366040, 48052315),
        Point(62008960, 48052315),    Point(62008960, 42514051),
        Point(57097292, 42514051),    Point(57097292, 48052315),
        Point(55740212, 48052315),    Point(55740212, 42514052),
        Point(50828540, 42514052),    Point(50828540, 48052315),
        Point(49471460, 48052315),    Point(49471460, 42514051),
        Point(25753540, 42514051),    Point(25753540, 48052315),
        Point(24396460, 48052315),    Point(24396460, 42514051),
        Point(19484790, 42514052),    Point(19484790, 48052315),
        Point(18127710, 48052315),    Point(18127710, 42514051),
        Point(-5590210, 42514051),    Point(-5590210, 48052315),
        Point(-6947290, 48052315),    Point(-6947290, 42514051),
        Point(-11858960, 42514051),   Point(-11858960, 48052315),
        Point(-13216040, 48052315),   Point(-13216040, 42514051),
        Point(-18127710, 42514051),   Point(-18127710, 48052315),
        Point(-19484790, 48052315),   Point(-19484790, 42514052),
        Point(-49471460, 42514051),   Point(-49471460, 48052315),
        Point(-50828540, 48052315),   Point(-50828540, 42514052),
        Point(-55740212, 42514052),   Point(-55740212, 48052315),
        Point(-57097292, 48052315),   Point(-57097292, 42514051),
        Point(-68277708, 42514051),   Point(-68277708, 48052315),
        Point(-69634788, 48052315),   Point(-69634788, 42514051),
        Point(-74546460, 42514052),   Point(-74546460, 48052315),
        Point(-75903540, 48052315),   Point(-75903540, 42514051),
        Point(-80815204, 42514051),   Point(-80815204, 48052315),
        Point(-82172292, 48052315),   Point(-82172292, 42514051),
        Point(-87083956, 42514051),   Point(-87083956, 48052315),
        Point(-88441044, 48052315),   Point(-88441044, 42514051),
        Point(-99621460, 42514051),   Point(-99621460, 48052315),
        Point(-100978540, 48052315),  Point(-100978540, 42528248),
        Point(-101978540, 41304489),  Point(-101978540, -41304489),
        Point(-100978540, -48052315), Point(-99621460, -48052315),
    };

    Polygon poly_1 = {
        Point(-100671460, -40092775),
        Point(-100671460, 40092775),
        Point(100671460, 40092775),
        Point(100671460, -40092775),
    };

    Polygons polygons              = {poly_0, poly_1};
    coord_t  ext_perimeter_spacing = 607079;
    coord_t  perimeter_spacing     = 607079;
    coord_t  inset_count           = 1;

    Arachne::WallToolPaths wall_tool_paths(polygons, ext_perimeter_spacing, perimeter_spacing, inset_count, 0, 0.2, PrintObjectConfig::defaults(), PrintConfig::defaults());
    wall_tool_paths.generate();
    std::vector<Arachne::VariableWidthLines> perimeters = wall_tool_paths.getToolPaths();

#ifdef ARACHNE_DEBUG_OUT
    export_perimeters_to_svg(debug_out_path("arachne-degenerated-diagram-8846-one-parabola.svg"), polygons, perimeters, union_ex(wall_tool_paths.getInnerContour()));
#endif

    int64_t total_extrusion_length = 0;
    for (Arachne::VariableWidthLines &perimeter : perimeters)
        for (Arachne::ExtrusionLine &extrusion_line : perimeter)
            total_extrusion_length += extrusion_line.getLength();

    // Total extrusion length should be around 1335mm when the part is ok and 1347mm when it has perimeters in places where they shouldn't be.
    REQUIRE(total_extrusion_length <= scaled<int64_t>(1335.));
}

// This test case was distilled from GitHub issue #9357.
// Boost Voronoi generator produces degenerated Voronoi diagram with two intersecting parabolic Voronoi edges.
// Those intersecting edges are causing that perimeters are also generated in places where they shouldn't be.
TEST_CASE("Arachne - #9357 - Degenerated Voronoi diagram - Two parabolas", "[ArachneDegeneratedDiagram9357TwoParabolas]") {
    const Polygon poly_0 = {
        Point(78998946, -11733905),
        Point(40069507,  -7401251),
        Point(39983905,  -6751055),
        Point(39983905,   8251054),
        Point(79750000,  10522762),
        Point(79983905,  10756667),
        Point(79983905,  12248946),
        Point(79950248,  12504617),
        Point(79709032,  12928156),
        Point(79491729,  13102031),
        Point(78998946,  13233905),
        Point(38501054,  13233905),
        Point(37258117,  12901005),
        Point(36349000,  11991885),
        Point(36100868,  11392844),
        Point(36016095,  10748947),
        Point(36016095,  -6751054),
        Point(35930493,  -7401249),
        Point(4685798,  -11733905),
    };

    Polygons polygons              = {poly_0};
    coord_t  ext_perimeter_spacing = 407079;
    coord_t  perimeter_spacing     = 407079;
    coord_t  inset_count           = 1;

    Arachne::WallToolPaths wall_tool_paths(polygons, ext_perimeter_spacing, perimeter_spacing, inset_count, 0, 0.2, PrintObjectConfig::defaults(), PrintConfig::defaults());
    wall_tool_paths.generate();
    std::vector<Arachne::VariableWidthLines> perimeters = wall_tool_paths.getToolPaths();

#ifdef ARACHNE_DEBUG_OUT
    export_perimeters_to_svg(debug_out_path("arachne-degenerated-diagram-9357-two-parabolas.svg"), polygons, perimeters, union_ex(wall_tool_paths.getInnerContour()));
#endif

    int64_t total_extrusion_length = 0;
    for (Arachne::VariableWidthLines &perimeter : perimeters)
        for (Arachne::ExtrusionLine &extrusion_line : perimeter)
            total_extrusion_length += extrusion_line.getLength();

    // Total extrusion length should be around 256mm when the part is ok and 293mm when it has perimeters in places where they shouldn't be.
    REQUIRE(total_extrusion_length <= scaled<int64_t>(256.));
}

// This test case was distilled from GitHub issue #8846.
// Boost Voronoi generator produces degenerated Voronoi diagram with some Voronoi edges intersecting input segments.
// Those Voronoi edges intersecting input segments are causing that perimeters are also generated in places where they shouldn't be.
TEST_CASE("Arachne - #8846 - Degenerated Voronoi diagram - Voronoi edges intersecting input segment", "[ArachneDegeneratedDiagram8846IntersectingInputSegment]") {
    const Polygon poly_0 = {
        Point( 60000000,  58000000),
        Point(-20000000,  53229451),
        Point( 49312250,  53229452),
        Point( 49443687,  53666225),
        Point( 55358348,  50908580),
        Point( 53666223,  49443687),
        Point( 53229452,  49312250),
        Point( 53229452, -49312250),
        Point( 53666014, -49443623),
        Point(-10000000, -58000000),
        Point( 60000000, -58000000),
    };

    Polygons polygons              = {poly_0};
    coord_t  ext_perimeter_spacing = 407079;
    coord_t  perimeter_spacing     = 407079;
    coord_t  inset_count           = 1;

    Arachne::WallToolPaths wall_tool_paths(polygons, ext_perimeter_spacing, perimeter_spacing, inset_count, 0, 0.32, PrintObjectConfig::defaults(), PrintConfig::defaults());
    wall_tool_paths.generate();
    std::vector<Arachne::VariableWidthLines> perimeters = wall_tool_paths.getToolPaths();

#ifdef ARACHNE_DEBUG_OUT
    export_perimeters_to_svg(debug_out_path("arachne-degenerated-diagram-8846-intersecting-input-segment.svg"), polygons, perimeters, union_ex(wall_tool_paths.getInnerContour()));
#endif

    int64_t total_extrusion_length = 0;
    for (Arachne::VariableWidthLines &perimeter : perimeters)
        for (Arachne::ExtrusionLine &extrusion_line : perimeter)
            total_extrusion_length += extrusion_line.getLength();

    // Total extrusion length should be around 500mm when the part is ok and 680mm when it has perimeters in places where they shouldn't be.
    REQUIRE(total_extrusion_length <= scaled<int64_t>(500.));
}

// This test case was distilled from GitHub issue #10034.
// In this test case previous rotation by PI / 6 wasn't able to fix non-planar Voronoi diagram.
TEST_CASE("Arachne - #10034 - Degenerated Voronoi diagram - That wasn't fixed by rotation by PI / 6", "[ArachneDegeneratedDiagram10034RotationNotWorks]") {
    Polygon poly_0 = {
        Point(43612632, -25179766), Point(58456010, 529710),    Point(51074898, 17305660),   Point(49390982, 21042355),
        Point(48102357, 23840161),  Point(46769686, 26629546),  Point(45835761, 28472742),   Point(45205450, 29623133),
        Point(45107431, 29878059),  Point(45069846, 30174950),  Point(45069846, 50759533),   Point(-45069846, 50759533),
        Point(-45069852, 29630557), Point(-45105780, 29339980), Point(-45179725, 29130704),  Point(-46443313, 26398986),
        Point(-52272109, 13471493), Point(-58205450, 95724),    Point(-29075091, -50359531), Point(29075086, -50359531),
    };

    Polygon poly_1 = {
        Point(-37733905, 45070445), Point(-37813254, 45116257), Point(-39353851, 47784650), Point(-39353851, 47876274),
        Point(-38632470, 49125743), Point(-38553121, 49171555), Point(-33833475, 49171555), Point(-33754126, 49125743),
        Point(-33032747, 47876277), Point(-33032747, 47784653), Point(-34007855, 46095721), Point(-34573350, 45116257),
        Point(-34652699, 45070445),
    };

    Polygon poly_2 = {
        Point(-44016799, 40706401), Point(-44116953, 40806555), Point(-44116953, 46126289), Point(-44016799, 46226443),
        Point(-42211438, 46226443), Point(-42132089, 46180631), Point(-40591492, 43512233), Point(-40591492, 43420609),
        Point(-41800123, 41327194), Point(-42132089, 40752213), Point(-42211438, 40706401),
    };

    Polygon poly_3 = {
        Point(6218189, 10966609),  Point(6138840, 11012421), Point(4598238, 13680817), Point(4598238, 13772441),  Point(6138840, 16440843),
        Point(6218189, 16486655),  Point(9299389, 16486655), Point(9378738, 16440843), Point(10919340, 13772441), Point(10919340, 13680817),
        Point(10149039, 12346618), Point(9378738, 11012421), Point(9299389, 10966609),
    };

    Polygon poly_4 = {
        Point(13576879, 6718065),  Point(13497530, 6763877),  Point(11956926, 9432278),  Point(11956926, 9523902),
        Point(13497528, 12192302), Point(13576877, 12238114), Point(16658079, 12238112), Point(16737428, 12192300),
        Point(18278031, 9523904),  Point(18278031, 9432280),  Point(17507729, 8098077),  Point(16737428, 6763877),
        Point(16658079, 6718065),
    };

    Polygons polygons = {
        poly_0, poly_1, poly_2, poly_3, poly_4,
    };

    coord_t  ext_perimeter_spacing = 407079;
    coord_t  perimeter_spacing     = 407079;
    coord_t  inset_count           = 1;

    Arachne::WallToolPaths wall_tool_paths(polygons, ext_perimeter_spacing, perimeter_spacing, inset_count, 0, 0.2, PrintObjectConfig::defaults(), PrintConfig::defaults());
    wall_tool_paths.generate();
    std::vector<Arachne::VariableWidthLines> perimeters = wall_tool_paths.getToolPaths();

#ifdef ARACHNE_DEBUG_OUT
    export_perimeters_to_svg(debug_out_path("arachne-degenerated-diagram-10034-rotation-not-works.svg"), polygons, perimeters, union_ex(wall_tool_paths.getInnerContour()));
#endif
}

TEST_CASE("Arachne - SPE-1837 - No perimeters generated", "[ArachneNoPerimetersGeneratedSPE1837]") {
    Polygon poly_0 = {
        Point( 10000000,  10000000),
        Point(-10000000,  10000000),
        Point(-10000000, -10000000),
        Point( 10000000, -10000000)
    };

    Polygons polygons              = {poly_0};
    coord_t  ext_perimeter_spacing = 300000;
    coord_t  perimeter_spacing     = 700000;
    coord_t  inset_count           = 1;

    Arachne::WallToolPaths wall_tool_paths(polygons, ext_perimeter_spacing, perimeter_spacing, inset_count, 0, 0.2, PrintObjectConfig::defaults(), PrintConfig::defaults());
    wall_tool_paths.generate();
    std::vector<Arachne::VariableWidthLines> perimeters = wall_tool_paths.getToolPaths();

    REQUIRE(!perimeters.empty());
}

TEST_CASE("Arachne - SPE-2298 - Missing twin edge", "[ArachneMissingTwinEdgeSPE2298]") {
    Polygon poly_0 = {
        Point(45275325, -26003582),
        Point(46698318, -24091837),
        Point(45534079, - 7648226),
        Point(44427730,   6913138),
        Point(42406709,  31931594),
        Point(42041617,  31895427),
        Point(42556409,  25628802),
        Point(43129149,  18571997),
        Point(44061956,   6884616),
        Point(44482729,   1466404),
        Point(45172290, - 7674740),
        Point(46329004, -23890062),
        Point(46303776, -23895512),
        Point(45146815, - 7676652),
        Point(44457276,   1464203),
        Point(44036504,   6882422),
        Point(43103702,  18569730),
        Point(42015592,  31899494),
        Point(41650258,  31866937),
        Point(44100538,   1436619)
    };

    Polygons polygons = {poly_0};
    coord_t  ext_perimeter_spacing = 407079;
    coord_t  perimeter_spacing     = 407079;
    coord_t  inset_count           = 1;

    Arachne::WallToolPaths wall_tool_paths(polygons, ext_perimeter_spacing, perimeter_spacing, inset_count, 0, 0.2, PrintObjectConfig::defaults(), PrintConfig::defaults());
    wall_tool_paths.generate();
    std::vector<Arachne::VariableWidthLines> perimeters = wall_tool_paths.getToolPaths();

    REQUIRE(!perimeters.empty());
}

TEST_CASE("Arachne - SPE-2298 - Missing twin edge - 2", "[ArachneMissingTwinEdge2SPE2298]") {
    Polygon poly_0 = {
        Point(-8908308, -51405945),
        Point(-12709229, -51250796),
        Point(-12746335, -51233657),
        Point(-12830242, -51142897),
        Point(-12826443, -51134671),
        Point(-13181213, -51120650),
        Point(-13184646, -51206854),
        Point(-19253324, -50972142),
        Point(-19253413, -50972139),
        Point(-20427346, -50924668),
        Point(-20427431, -50924664),
        Point(-25802429, -50698485),
        Point(-25802568, -50698481),
        Point(-28983179, -50556020),
        Point(-28984425, -50555950),
        Point(-29799753, -50499586),
        Point(-29801136, -50499472),
        Point(-29856539, -50494137),
        Point(-29857834, -50493996),
        Point(-30921022, -50364409),
        Point(-30922312, -50364235),
        Point(-31012584, -50350908),
        Point(-31022222, -50358055),
        Point(-31060596, -50368155),
        Point(-31429495, -50322406),
        Point(-31460950, -50531962),
        Point(-31194587, -50578945),
        Point(-30054463, -50718244),
        Point(-28903516, -50799260),
        Point(-14217296, -51420133),
        Point(-8916965, -51624212)
    };

    Polygons polygons = {poly_0};
    coord_t  ext_perimeter_spacing = 407079;
    coord_t  perimeter_spacing     = 407079;
    coord_t  inset_count           = 1;

    Arachne::WallToolPaths wall_tool_paths(polygons, ext_perimeter_spacing, perimeter_spacing, inset_count, 0, 0.2, PrintObjectConfig::defaults(), PrintConfig::defaults());
    wall_tool_paths.generate();
    std::vector<Arachne::VariableWidthLines> perimeters = wall_tool_paths.getToolPaths();

    REQUIRE(!perimeters.empty());
}

TEST_CASE("Arachne - SPE-2496 - Negative extrusion width", "[Arachne_Negative_Extrusion_Width_SPE-2496]") {
    Polygon poly_0 = {
        Point(-4982523, -5994247),
        Point(-4700644, -6050605),
        Point( 5959771,  4609799),
        Point( 5901029,  4779898),
        Point( 5871716,  4899000),
        Point( 5864557,  5026026),
        Point( 5890832,  5722622),
        Point( 5870131,  5738234),
        Point( 5304622,  5553229),
        Point( 4580330,  5240254),
        Point( 4109435,  4998946),
        Point( 3606964,  4699087),
        Point( 2676357,  4015459),
        Point( 1076547,  2101298),
        Point(- 900993,    41373),
        Point(-1481954, - 514246),
        Point(-4119704, -2738265),
        Point(-4484070, -3261707),
        Point(-4628548, -3650430),
        Point(-4712361, -3810835),
        Point(-5329484, -4699252),
        Point(-5670086, -5625540),
        Point(-5727314, -6080805),
        Point(-5726304, -6081822)
    };

    Polygons polygons = {poly_0};
    coord_t  ext_perimeter_spacing = 407079;
    coord_t  perimeter_spacing     = 407079;
    coord_t  inset_count           = 3;

    PrintObjectConfig print_object_config      = PrintObjectConfig::defaults();
    print_object_config.min_bead_width         = ConfigOptionFloatOrPercent(0.1, false);

    Arachne::WallToolPaths wall_tool_paths(polygons, ext_perimeter_spacing, perimeter_spacing, inset_count, 0, 0.2, print_object_config, PrintConfig::defaults());
    wall_tool_paths.generate();
    Arachne::Perimeters perimeters = wall_tool_paths.getToolPaths();

    bool has_negative_extrusion_width = false;
    for (const Arachne::Perimeter &perimeter : perimeters) {
        for (const Arachne::ExtrusionLine &extrusion_line : perimeter) {
            for (const Arachne::ExtrusionJunction &extrusion_junction : extrusion_line) {
                if (extrusion_junction.w <= 0.) {
                    has_negative_extrusion_width = true;
                }
            }
        }
    }

    REQUIRE(!has_negative_extrusion_width);
}