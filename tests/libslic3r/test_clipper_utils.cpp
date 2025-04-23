#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <numeric>
#include <iostream>
#include <boost/filesystem.hpp>

#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/SVG.hpp"

using namespace Slic3r;
using namespace Catch;

SCENARIO("Various Clipper operations - xs/t/11_clipper.t", "[ClipperUtils]") {
	// CCW oriented contour
	Slic3r::Polygon   square{ { 200, 100 }, {200, 200}, {100, 200}, {100, 100} };
	// CW oriented contour
	Slic3r::Polygon   hole_in_square{ { 160, 140 }, { 140, 140 }, { 140, 160 }, { 160, 160 } };
	Slic3r::ExPolygon square_with_hole(square, hole_in_square);
	GIVEN("square_with_hole") {
        WHEN("offset") {
            Polygons result = Slic3r::offset(square_with_hole, 5.f);
            THEN("offset matches") {
                REQUIRE(result == Polygons { 
                    { { 205, 205 }, { 95, 205 }, { 95, 95 }, { 205, 95 }, },
                    { { 155, 145 }, { 145, 145 }, { 145, 155 }, { 155, 155 } } });
            }
        }
        WHEN("offset_ex") {
            ExPolygons result = Slic3r::offset_ex(square_with_hole, 5.f);
            THEN("offset matches") {
                REQUIRE(result == ExPolygons { { 
                    { { 205, 205 }, { 95, 205 }, { 95, 95 }, { 205, 95 }, },
                    { { 145, 145 }, { 145, 155 }, { 155, 155 }, { 155, 145 } } } } );
            }
        }
        WHEN("offset2_ex") {
            ExPolygons result = Slic3r::offset2_ex({ square_with_hole }, 5.f, -2.f);
            THEN("offset matches") {
                REQUIRE(result == ExPolygons { {
                    { { 203, 203 }, { 97, 203 }, { 97, 97 }, { 203, 97 } },
                    { { 143, 143 }, { 143, 157 }, { 157, 157 }, { 157, 143 } } } } );
            }
        }
    }
    GIVEN("square_with_hole 2") {
        Slic3r::ExPolygon square_with_hole(
            { { 20000000, 20000000 }, { 0, 20000000 }, { 0, 0 }, { 20000000, 0 } },
            { { 5000000, 15000000 }, { 15000000, 15000000 }, { 15000000, 5000000 }, { 5000000, 5000000 } });
        WHEN("offset2_ex") {
            Slic3r::ExPolygons result = Slic3r::offset2_ex(ExPolygons { square_with_hole }, -1.f, 1.f);
            THEN("offset matches") {
                REQUIRE(result.size() == 1);
                REQUIRE(square_with_hole.area() == result.front().area());
            }
        }
    }
    GIVEN("square and hole") {
        WHEN("diff_ex") {
            ExPolygons result = Slic3r::diff_ex(Polygons{ square }, Polygons{ hole_in_square });
            THEN("hole is created") {
                REQUIRE(result.size() == 1);
                REQUIRE(square_with_hole.area() == result.front().area());
            }
        }
    }
    GIVEN("polyline") {
        Polyline polyline { { 50, 150 }, { 300, 150 } };
        WHEN("intersection_pl") {
            Polylines result = Slic3r::intersection_pl(polyline, ExPolygon{ square, hole_in_square });
            THEN("correct number of result lines") {
                REQUIRE(result.size() == 2);
            }
            THEN("result lines have correct length") {
                // results are in no particular order
                REQUIRE(result[0].length() == 40);
                REQUIRE(result[1].length() == 40);
            }
        }
        WHEN("diff_pl") {
            Polylines result = Slic3r::diff_pl({ polyline }, Polygons{ square, hole_in_square });
            THEN("correct number of result lines") {
                REQUIRE(result.size() == 3);
            }
            // results are in no particular order
            THEN("the left result line has correct length") {
                REQUIRE(std::count_if(result.begin(), result.end(), [](const Polyline &pl) { return pl.length() == 50; }) == 1);
            }
            THEN("the right result line has correct length") {
                REQUIRE(std::count_if(result.begin(), result.end(), [](const Polyline &pl) { return pl.length() == 100; }) == 1);
            }
            THEN("the central result line has correct length") {
                REQUIRE(std::count_if(result.begin(), result.end(), [](const Polyline &pl) { return pl.length() == 20; }) == 1);
            }
        }
    }
	GIVEN("Clipper bug #96 / Slic3r issue #2028") {
		Slic3r::Polyline subject{
			{ 44735000, 31936670 }, { 55270000, 31936670 }, { 55270000, 25270000 }, { 74730000, 25270000 }, { 74730000, 44730000 }, { 68063296, 44730000 }, { 68063296, 55270000 }, { 74730000, 55270000 },
			{ 74730000, 74730000 }, { 55270000, 74730000 }, { 55270000, 68063296 }, { 44730000, 68063296 }, { 44730000, 74730000 }, { 25270000, 74730000 }, { 25270000, 55270000 }, { 31936670, 55270000 },
			{ 31936670, 44730000 }, { 25270000, 44730000 }, { 25270000, 25270000 }, { 44730000, 25270000 }, { 44730000, 31936670 } };
		Slic3r::Polygon clip { {75200000, 45200000}, {54800000, 45200000}, {54800000, 24800000}, {75200000, 24800000} };
        Slic3r::Polylines result = Slic3r::intersection_pl(subject, ExPolygon{ clip });
		THEN("intersection_pl - result is not empty") {
			REQUIRE(result.size() == 1);
		}
	}
	GIVEN("Clipper bug #122") {
		Slic3r::Polyline subject { { 1975, 1975 }, { 25, 1975 }, { 25, 25 }, { 1975, 25 }, { 1975, 1975 } };
		Slic3r::Polygons clip { { { 2025, 2025 }, { -25, 2025 } , { -25, -25 }, { 2025, -25 } },
								{ { 525, 525 }, { 525, 1475 }, { 1475, 1475 }, { 1475, 525 } } };
		Slic3r::Polylines result = Slic3r::intersection_pl({ subject }, clip);
		THEN("intersection_pl - result is not empty") {
			REQUIRE(result.size() == 1);
			REQUIRE(result.front().points.size() == 5);
		}
	}
	GIVEN("Clipper bug #126") {
		Slic3r::Polyline subject { { 200000, 19799999 }, { 200000, 200000 }, { 24304692, 200000 }, { 15102879, 17506106 }, { 13883200, 19799999 }, { 200000, 19799999 } };
		Slic3r::Polygon clip { { 15257205, 18493894 }, { 14350057, 20200000 }, { -200000, 20200000 }, { -200000, -200000 }, { 25196917, -200000 } };
		Slic3r::Polylines result = Slic3r::intersection_pl(subject, ExPolygon{ clip });
		THEN("intersection_pl - result is not empty") {
			REQUIRE(result.size() == 1);
		}
		THEN("intersection_pl - result has same length as subject polyline") {
			REQUIRE(result.front().length() == Approx(subject.length()));
		}
	}

#if 0
	{
		# Clipper does not preserve polyline orientation
		my $polyline = Slic3r::Polyline->new([50, 150], [300, 150]);
		my $result = Slic3r::Geometry::Clipper::intersection_pl([$polyline], [$square]);
		is scalar(@$result), 1, 'intersection_pl - correct number of result lines';
		is_deeply $result->[0]->pp, [[100, 150], [200, 150]], 'clipped line orientation is preserved';
	}
	{
		# Clipper does not preserve polyline orientation
		my $polyline = Slic3r::Polyline->new([300, 150], [50, 150]);
		my $result = Slic3r::Geometry::Clipper::intersection_pl([$polyline], [$square]);
		is scalar(@$result), 1, 'intersection_pl - correct number of result lines';
		is_deeply $result->[0]->pp, [[200, 150], [100, 150]], 'clipped line orientation is preserved';
	}
	{
		# Disabled until Clipper bug #127 is fixed
		my $subject = [
			Slic3r::Polyline->new([-90000000, -100000000], [-90000000, 100000000]), # vertical
				Slic3r::Polyline->new([-100000000, -10000000], [100000000, -10000000]), # horizontal
				Slic3r::Polyline->new([-100000000, 0], [100000000, 0]), # horizontal
				Slic3r::Polyline->new([-100000000, 10000000], [100000000, 10000000]), # horizontal
		];
		my $clip = Slic3r::Polygon->new(# a circular, convex, polygon
			[99452190, 10452846], [97814760, 20791169], [95105652, 30901699], [91354546, 40673664], [86602540, 50000000],
			[80901699, 58778525], [74314483, 66913061], [66913061, 74314483], [58778525, 80901699], [50000000, 86602540],
			[40673664, 91354546], [30901699, 95105652], [20791169, 97814760], [10452846, 99452190], [0, 100000000],
			[-10452846, 99452190], [-20791169, 97814760], [-30901699, 95105652], [-40673664, 91354546],
			[-50000000, 86602540], [-58778525, 80901699], [-66913061, 74314483], [-74314483, 66913061],
			[-80901699, 58778525], [-86602540, 50000000], [-91354546, 40673664], [-95105652, 30901699],
			[-97814760, 20791169], [-99452190, 10452846], [-100000000, 0], [-99452190, -10452846],
			[-97814760, -20791169], [-95105652, -30901699], [-91354546, -40673664], [-86602540, -50000000],
			[-80901699, -58778525], [-74314483, -66913061], [-66913061, -74314483], [-58778525, -80901699],
			[-50000000, -86602540], [-40673664, -91354546], [-30901699, -95105652], [-20791169, -97814760],
			[-10452846, -99452190], [0, -100000000], [10452846, -99452190], [20791169, -97814760],
			[30901699, -95105652], [40673664, -91354546], [50000000, -86602540], [58778525, -80901699],
			[66913061, -74314483], [74314483, -66913061], [80901699, -58778525], [86602540, -50000000],
			[91354546, -40673664], [95105652, -30901699], [97814760, -20791169], [99452190, -10452846], [100000000, 0]
			);
		my $result = Slic3r::Geometry::Clipper::intersection_pl($subject, [$clip]);
		is scalar(@$result), scalar(@$subject), 'intersection_pl - expected number of polylines';
		is sum(map scalar(@$_), @$result), scalar(@$subject) * 2, 'intersection_pl - expected number of points in polylines';
	}
#endif
}

SCENARIO("Various Clipper operations - t/clipper.t", "[ClipperUtils]") {
    GIVEN("square with hole") {
        // CCW oriented contour
        Slic3r::Polygon   square { { 10, 10 }, { 20, 10 }, { 20, 20 }, { 10, 20 } };
        Slic3r::Polygon   square2 { { 5, 12 }, { 25, 12 }, { 25, 18 }, { 5, 18 } };
        // CW oriented contour
        Slic3r::Polygon   hole_in_square { { 14, 14 }, { 14, 16 }, { 16, 16 }, { 16, 14 } };
        WHEN("intersection_ex with another square") {
            ExPolygons intersection = Slic3r::intersection_ex(Polygons{ square, hole_in_square }, Polygons{ square2 });
            THEN("intersection area matches (hole is preserved)") {
                ExPolygon match({ { 20, 18 }, { 10, 18 }, { 10, 12 }, { 20, 12 } },
                                { { 14, 16 }, { 16, 16 }, { 16, 14 }, { 14, 14 } });
                REQUIRE(intersection.size() == 1);
                REQUIRE(intersection.front().area() == Approx(match.area()));
            }
        }

        ExPolygons expolygons { ExPolygon { square, hole_in_square } };
        WHEN("Clipping line 1") {
            Polylines intersection = intersection_pl({ Polyline { { 15, 18 }, { 15, 15 } } }, expolygons);
            THEN("line is clipped to square with hole") {
                REQUIRE((Vec2f(15, 18) - Vec2f(15, 16)).norm() == Approx(intersection.front().length()));
            }
        }
        WHEN("Clipping line 2") {
            Polylines intersection = intersection_pl({ Polyline { { 15, 15 }, { 15, 12 } } }, expolygons);
            THEN("line is clipped to square with hole") {
                REQUIRE((Vec2f(15, 14) - Vec2f(15, 12)).norm() == Approx(intersection.front().length()));
            }
        }
        WHEN("Clipping line 3") {
            Polylines intersection = intersection_pl({ Polyline { { 12, 18 }, { 18, 18 } } }, expolygons);
            THEN("line is clipped to square with hole") {
                REQUIRE((Vec2f(18, 18) - Vec2f(12, 18)).norm() == Approx(intersection.front().length()));
            }
        }
        WHEN("Clipping line 4") {
            Polylines intersection = intersection_pl({ Polyline { { 5, 15 }, { 30, 15 } } }, expolygons);
            THEN("line is clipped to square with hole") {
                REQUIRE((Vec2f(14, 15) - Vec2f(10, 15)).norm() == Approx(intersection.front().length()));
                REQUIRE((Vec2f(20, 15) - Vec2f(16, 15)).norm() == Approx(intersection[1].length()));
            }
        }
        WHEN("Clipping line 5") {
            Polylines intersection = intersection_pl({ Polyline { { 30, 15 }, { 5, 15 } } }, expolygons);
            THEN("reverse line is clipped to square with hole") {
                REQUIRE((Vec2f(20, 15) - Vec2f(16, 15)).norm() == Approx(intersection.front().length()));
                REQUIRE((Vec2f(14, 15) - Vec2f(10, 15)).norm() == Approx(intersection[1].length()));
            }
        }
        WHEN("Clipping line 6") {
            Polylines intersection = intersection_pl({ Polyline { { 10, 18 }, { 20, 18 } } }, expolygons);
            THEN("tangent line is clipped to square with hole") {
                REQUIRE((Vec2f(20, 18) - Vec2f(10, 18)).norm() == Approx(intersection.front().length()));
            }
        }
    }
    GIVEN("square with hole 2") {
        // CCW oriented contour
        Slic3r::Polygon   square { { 0, 0 }, { 40, 0 }, { 40, 40 }, { 0, 40 } };
        Slic3r::Polygon   square2 { { 10, 10 }, { 30, 10 }, { 30, 30 }, { 10, 30 } };
        // CW oriented contour
        Slic3r::Polygon   hole { { 15, 15 }, { 15, 25 }, { 25, 25 }, {25, 15 } };
        WHEN("union_ex with another square") {
            ExPolygons union_ = Slic3r::union_ex({ square, square2, hole });
            THEN("union of two ccw and one cw is a contour with no holes") {
                REQUIRE(union_.size() == 1);
                REQUIRE(union_.front() == ExPolygon { { 40, 40 }, { 0, 40 }, { 0, 0 }, { 40, 0 } } );
            }
        }
        WHEN("diff_ex with another square") {
			ExPolygons diff = Slic3r::diff_ex(Polygons{ square, square2 }, Polygons{ hole });
            THEN("difference of a cw from two ccw is a contour with one hole") {
                REQUIRE(diff.size() == 1);
                REQUIRE(diff.front().area() == Approx(ExPolygon({ {40, 40}, {0, 40}, {0, 0}, {40, 0} }, { {15, 25}, {25, 25}, {25, 15}, {15, 15} }).area()));
            }
        }
    }
    GIVEN("yet another square") {
        Slic3r::Polygon  square { { 10, 10 }, { 20, 10 }, { 20, 20 }, { 10, 20 } };
        Slic3r::Polyline square_pl = square.split_at_first_point();
        WHEN("no-op diff_pl") {
            Slic3r::Polylines res = Slic3r::diff_pl({ square_pl }, Polygons{});
            THEN("returns the right number of polylines") {
                REQUIRE(res.size() == 1);
            }
            THEN("returns the unmodified input polyline") {
                REQUIRE(res.front().points.size() == square_pl.points.size());
            }
        }
    }
    GIVEN("circle") {
        Slic3r::ExPolygon circle_with_hole { Polygon::new_scale({
                { 151.8639,288.1192 }, {133.2778,284.6011}, { 115.0091,279.6997 }, { 98.2859,270.8606 }, { 82.2734,260.7933 }, 
                { 68.8974,247.4181 }, { 56.5622,233.0777 }, { 47.7228,216.3558 }, { 40.1617,199.0172 }, { 36.6431,180.4328 }, 
                { 34.932,165.2312 }, { 37.5567,165.1101 }, { 41.0547,142.9903 }, { 36.9056,141.4295 }, { 40.199,124.1277 }, 
                { 47.7776,106.7972 }, { 56.6335,90.084 }, { 68.9831,75.7557 }, { 82.3712,62.3948 }, { 98.395,52.3429 }, 
                { 115.1281,43.5199 }, { 133.4004,38.6374 }, { 151.9884,35.1378 }, { 170.8905,35.8571 }, { 189.6847,37.991 }, 
                { 207.5349,44.2488 }, { 224.8662,51.8273 }, { 240.0786,63.067 }, { 254.407,75.4169 }, { 265.6311,90.6406 }, 
                { 275.6832,106.6636 }, { 281.9225,124.52 }, { 286.8064,142.795 }, { 287.5061,161.696 }, { 286.7874,180.5972 }, 
                { 281.8856,198.8664 }, { 275.6283,216.7169 }, { 265.5604,232.7294 }, { 254.3211,247.942 }, { 239.9802,260.2776 }, 
                { 224.757,271.5022 }, { 207.4179,279.0635 }, { 189.5605,285.3035 }, { 170.7649,287.4188 }
            }) };
        circle_with_hole.holes = { Polygon::new_scale({
                { 158.227,215.9007 }, { 164.5136,215.9007 }, { 175.15,214.5007 }, { 184.5576,210.6044 }, { 190.2268,207.8743 }, 
                { 199.1462,201.0306 }, { 209.0146,188.346 }, { 213.5135,177.4829 }, { 214.6979,168.4866 }, { 216.1025,162.3325 }, 
                { 214.6463,151.2703 }, { 213.2471,145.1399 }, { 209.0146,134.9203 }, { 199.1462,122.2357 }, { 189.8944,115.1366 }, 
                { 181.2504,111.5567 }, { 175.5684,108.8205 }, { 164.5136,107.3655 }, { 158.2269,107.3655 }, { 147.5907,108.7656 }, 
                { 138.183,112.6616 }, { 132.5135,115.3919 }, { 123.5943,122.2357 }, { 113.7259,134.92 }, { 109.2269,145.7834 }, 
                { 108.0426,154.7799 }, { 106.638,160.9339 }, { 108.0941,171.9957 }, { 109.4933,178.1264 }, { 113.7259,188.3463 }, 
                { 123.5943,201.0306 }, { 132.8461,208.1296 }, { 141.4901,211.7094 }, { 147.172,214.4458 }
            }) };
        THEN("contour is counter-clockwise") {
            REQUIRE(circle_with_hole.contour.is_counter_clockwise());
        }
        THEN("hole is counter-clockwise") {
            REQUIRE(circle_with_hole.holes.size() == 1);
            REQUIRE(circle_with_hole.holes.front().is_clockwise());
        }
    
        WHEN("clipping a line") {
            auto line = Polyline::new_scale({ { 152.742,288.086671142818 }, { 152.742,34.166466971035 } });    
            Polylines intersection = intersection_pl(line, to_polygons(circle_with_hole));
            THEN("clipped to two pieces") {
                REQUIRE(intersection.front().length() == Approx((Vec2d(152742000, 215178843) - Vec2d(152742000, 288086661)).norm()));
                REQUIRE(intersection[1].length() == Approx((Vec2d(152742000, 35166477) - Vec2d(152742000, 108087507)).norm()));
            }
        }
    }
    GIVEN("line") {
        THEN("expand by 5") {
            REQUIRE(offset(Polyline({10,10}, {20,10}), 5).front().area() == Polygon({ {10,5}, {20,5}, {20,15}, {10,15} }).area());
        }
    }
}

template<e_ordering o = e_ordering::OFF, class P, class P_Alloc, class Tree>
double polytree_area(const Tree &tree, std::vector<P, P_Alloc> *out)
{
    traverse_pt<o>(tree, out);
    
    return std::accumulate(out->begin(), out->end(), 0.0,
                           [](double a, const P &p) { return a + p.area(); });
}

size_t count_polys(const ExPolygons& expolys)
{
    size_t c = 0;
    for (auto &ep : expolys) c += ep.holes.size() + 1;
    
    return c;
}

TEST_CASE("Traversing Clipper PolyTree", "[ClipperUtils]") {
    // Create a polygon representing unit box
    Polygon unitbox;
    const auto UNIT = coord_t(1. / SCALING_FACTOR);
    unitbox.points = { Vec2crd{0, 0}, Vec2crd{UNIT, 0}, Vec2crd{UNIT, UNIT}, Vec2crd{0, UNIT}};
    
    Polygon box_frame = unitbox;
    box_frame.scale(20, 10);
    
    Polygon hole_left = unitbox;
    hole_left.scale(8);
    hole_left.translate(UNIT, UNIT);
    hole_left.reverse();
    
    Polygon hole_right = hole_left;
    hole_right.translate(UNIT * 10, 0);
    
    Polygon inner_left = unitbox;
    inner_left.scale(4);
    inner_left.translate(UNIT * 3, UNIT * 3);
    
    Polygon inner_right = inner_left;
    inner_right.translate(UNIT * 10, 0);
    
    Polygons reference = union_({box_frame, hole_left, hole_right, inner_left, inner_right});
    
    ClipperLib::PolyTree tree = union_pt(reference);
    double area_sum = box_frame.area() + hole_left.area() +
                      hole_right.area() + inner_left.area() +
                      inner_right.area();
    
    REQUIRE(area_sum > 0);

    SECTION("Traverse into Polygons WITHOUT spatial ordering") {
        Polygons output;
        REQUIRE(area_sum == Approx(polytree_area(tree.GetFirst(), &output)));
        REQUIRE(output.size() == reference.size());
    }
    
    SECTION("Traverse into ExPolygons WITHOUT spatial ordering") {
        ExPolygons output;
        REQUIRE(area_sum == Approx(polytree_area(tree.GetFirst(), &output)));
        REQUIRE(count_polys(output) == reference.size());
    }
    
    SECTION("Traverse into Polygons WITH spatial ordering") {
        Polygons output;
        REQUIRE(area_sum == Approx(polytree_area<e_ordering::ON>(tree.GetFirst(), &output)));
        REQUIRE(output.size() == reference.size());
    }
    
    SECTION("Traverse into ExPolygons WITH spatial ordering") {
        ExPolygons output;
        REQUIRE(area_sum == Approx(polytree_area<e_ordering::ON>(tree.GetFirst(), &output)));
        REQUIRE(count_polys(output) == reference.size());
    }
}

TEST_CASE("test clip_clipper_polygon_with_subject_bbox ", "[ClipperUtils]") {
    
    SECTION("complicated clip that can create self-intersect")
    {
        Slic3r::Polygon big_poly({Point{875431,-14923904},Point{957624,-14918301},Point{1339389,-14888220},Point{1463327,-14877498},Point{1547974,-14869452},Point{1722573,-14666759},Point{1711235,-14485113},Point{1721031,-14016670},
                                Point{1381926,-11254880},Point{1649680,-11218838},Point{1880819,-11182229},Point{1934647,-11173048},Point{2163702,-11131283},Point{2694641,-13862720},Point{2848716,-14305217},Point{2894015,-14481305},
                                Point{3122424,-14620357},Point{3199359,-14603198},Point{3291474,-14582086},Point{3663682,-14492706},Point{3773110,-14465303},Point{3855290,-14444158},Point{3995735,-14216622},Point{3956138,-14039083},
                                Point{3892534,-13574881},Point{3125572,-10900159},Point{3382738,-10823168},Point{3605214,-10750682},Point{3658705,-10732835},Point{3878380,-10655761},Point{4830071,-13270509},Point{5051469,-13683452},
                                Point{5123775,-13850325},Point{5371241,-13951852},Point{5444033,-13923070},Point{5543310,-13883041},Point{5896867,-13736595},Point{5989320,-13697057},Point{6067615,-13663129},Point{6170480,-13416448},
                                Point{6103624,-13247358},Point{5968188,-12798820},Point{4792239,-10276988},Point{5035869,-10159909},Point{5244180,-10053769},Point{5292670,-10028334},Point{5497561,-9917859},Point{6846569,-12351538},
                                Point{7129841,-12724762},Point{7226963,-12877633},Point{7487701,-12939807},Point{7554757,-12900197},Point{7592434,-12877077},Point{7657547,-12838340},Point{7983468,-12638634},Point{8058159,-12591560},
                                Point{8130441,-12545637},Point{8193280,-12285948},Point{8100820,-12129441},Point{7896873,-11707594},Point{6340868,-9400725},Point{6566755,-9244505},Point{6755982,-9107025},Point{6796265,-9076930},
                                Point{6981372,-8935746},Point{8694484,-11128430},Point{9032655,-11452748},Point{9153074,-11589202},Point{9420083,-11608887},Point{9480128,-11559262},Point{9580405,-11474709},Point{9871464,-11226167},
                                Point{9928543,-11176104},Point{9992972,-11119245},Point{10014335,-10852955},Point{9898528,-10712836},Point{9631091,-10328080},Point{7733274,-8292918},Point{7935060,-8100442},Point{8100464,-7935037},
                                Point{8132554,-7901990},Point{8293277,-7733605},Point{10328317,-9631310},Point{10713042,-9898719},Point{10853251,-10014595},Point{11119845,-9992666},Point{11171847,-9933739},Point{11215722,-9883311},
                                Point{11514007,-9534155},Point{11554557,-9485435},Point{11609335,-9419150},Point{11588834,-9152766},Point{11452523,-9032477},Point{11128182,-8694288},Point{8935362,-6981069},Point{9107030,-6755976},
                                Point{9244546,-6566700},Point{9268484,-6532618},Point{9400977,-6341037},Point{11707817,-7897022},Point{12129661,-8100966},Point{12286210,-8193449},Point{12546038,-8130271},Point{12588304,-8063740},
                                Point{12630566,-7996357},Point{12830748,-7669614},Point{12869840,-7605093},Point{12896266,-7560930},Point{12937895,-7490455},Point{12879096,-7227910},Point{12724473,-7129680},Point{12351180,-6846370},
                                Point{9917100,-5497138},Point{9942702,-5450172},Point{10053731,-5244257},Point{10159919,-5035849},Point{10176444,-5001994},Point{10277197,-4792335},Point{12799060,-5968299},Point{13247601,-6103737},
                                Point{13416783,-6170632},Point{13663609,-6067170},Point{13694338,-5996246},Point{13741911,-5883859},Point{13888407,-5530249},Point{13921449,-5447688},Point{13951847,-5370803},Point{13850092,-5123687},
                                Point{13683259,-5051398},Point{13270282,-4829987},Point{10655329,-3878222},Point{10734498,-3652573},Point{10750707,-3605138},Point{10823171,-3382733},Point{10833754,-3348144},Point{10900370,-3125631},
                                Point{13575168,-3892615},Point{14039385,-3956223},Point{14216938,-3995825},Point{14444568,-3854955},Point{14463262,-3782280},Point{14493239,-3661373},Point{14582688,-3288897},Point{14602837,-3200518},
                                Point{14620297,-3122227},Point{14481166,-2893988},Point{14305069,-2848686},Point{13862534,-2694604},Point{11130847,-2163616},Point{11173684,-1928680},Point{11182236,-1880776},Point{11218842,-1649667},
                                Point{11224159,-1612046},Point{11255132,-1381955},Point{14016990,-1721069},Point{14485444,-1711274},Point{14666980,-1722611},Point{14869790,-1547951},Point{14876811,-1474014},Point{14887202,-1354182},
                                Point{14917261,-972467},Point{14923784,-876006},Point{14928733,-796744},Point{14755629,-593006},Point{14574604,-575809},Point{14113415,-492851},Point{11332323,-395733},Point{11338812,-116944},
                                Point{11338812,116937},Point{11338151,163677},Point{11332748,395748},Point{14113654,492860},Point{14574813,575818},Point{14755824,593013},Point{14928889,796996},Point{14924231,871563},
                                Point{14917267,972400},Point{14887215,1354045},Point{14877064,1469530},Point{14869595,1548150},Point{14666801,1722590},Point{14485256,1711252},Point{14016793,1721046},Point{11254881,1381926},
                                Point{11218839,1649680},Point{11182239,1880761},Point{11172960,1935294},Point{11131312,2163707},Point{13862788,2694654},Point{14305291,2848731},Point{14481376,2894030},Point{14620413,3122531},
                                Point{14604065,3195824},Point{14582709,3288807},Point{14493277,3661217},Point{14464280,3777572},Point{14444329,3855128},Point{14216754,3995772},Point{14039193,3956169},Point{13574975,3892561},
                                Point{10900160,3125572},Point{10823165,3382751},Point{10750697,3605193},Point{10732734,3659047},Point{10655779,3878387},Point{13270548,4830085},Point{13683493,5051484},Point{13850334,5123776},
                                Point{13951926,5371173},Point{13923752,5442427},Point{13888442,5530161},Point{13741957,5883753},Point{13696591,5990469},Point{13663285,6067330},Point{13416561,6170528},Point{13247382,6103636},
                                Point{12798839,5968197},Point{10276989,4792239},Point{10159910,5035869},Point{10053770,5244180},Point{10028335,5292670},Point{9917860,5497561},Point{12351539,6846569},Point{12724760,7129839},
                                Point{12878238,7227340},Point{12939917,7487525},Point{12899944,7555186},Point{12854354,7631362},Point{12654472,7957490},Point{12591481,8058284},Point{12545935,8129968},Point{12286108,8193368},
                                Point{12129438,8100819},Point{11707595,7896873},Point{9400726,6340868},Point{9244506,6566755},Point{9107026,6755982},Point{9076931,6796265},Point{8935747,6981372},Point{11128430,8694483},
                                Point{11452749,9032655},Point{11589095,9152975},Point{11609242,9419655},Point{11559207,9480196},Point{11494519,9557473},Point{11246151,9848267},Point{11196137,9904999},Point{11175534,9929188},
                                Point{11119358,9992838},Point{10852451,10013912},Point{10712852,9898541},Point{10328098,9631106},Point{8292987,7733337},Point{8100505,7934995},Point{7934969,8100531},Point{7901991,8132554},
                                Point{7733606,8293277},Point{9631310,10328315},Point{9898720,10713042},Point{10014568,10853213},Point{9992783,11119745},Point{9933874,11171728},Point{9860115,11236031},Point{9569162,11484510},
                                Point{9484986,11554930},Point{9419435,11609103},Point{9152841,11588920},Point{9032478,11452523},Point{8694289,11128182},Point{6981070,8935362},Point{6755977,9107030},Point{6566701,9244546},
                                Point{6532583,9268509},Point{6341038,9400977},Point{7897023,11707817},Point{8100967,12129661},Point{8193448,12286204},Point{8130277,12546034},Point{8063743,12588302},Point{7996358,12630566},
                                Point{7669652,12830725},Point{7611759,12865963},Point{7561000,12896224},Point{7487162,12939840},Point{7227208,12877991},Point{7129680,12724471},Point{6846371,12351180},Point{5497139,9917100},
                                Point{5450240,9942665},Point{5244258,10053732},Point{5035856,10159917},Point{5002013,10176429},Point{4792332,10277190},Point{5968292,12799044},Point{6103730,13247582},Point{6170626,13416785},
                                Point{6067187,13663583},Point{5995321,13694723},Point{5938708,13718701},Point{5876178,13744992},Point{5522350,13891589},Point{5450571,13920223},Point{5370608,13951836},Point{5123645,13849981},
                                Point{5051371,13683184},Point{4829963,13270214},Point{3878213,10655303},Point{3652342,10734549},Point{3605147,10750708},Point{3382667,10823192},Point{3348811,10833528},Point{3125626,10900347},
                                Point{3892600,13575112},Point{3956208,14039327},Point{3995590,14215932},Point{3855436,14444374},Point{3779804,14463834},Point{3736712,14474154},Point{3670706,14491062},Point{3299009,14580301},
                                Point{3205676,14601538},Point{3122119,14620172},Point{2893965,14481031},Point{2848653,14304888},Point{2694562,13862319},Point{2163538,11130435},Point{2110913,11140274},Point{1880745,11182243},
                                Point{1649633,11218847},Point{1612791,11224028},Point{1381953,11255102},Point{1721062,14016918},Point{1711266,14485364},Point{1722598,14666840},Point{1548338,14869670},Point{1470113,14877101},
                                Point{1377210,14885379},Point{995650,14915392},Point{882114,14923243},Point{796928,14928556},Point{593017,14755579},Point{575805,14574456},Point{492847,14113287},Point{395734,11332323},
                                Point{116945,11338812},Point{-116961,11338812},Point{-162977,11338136},Point{-395746,11332717},Point{-492856,14113578},Point{-575814,14574730},Point{-593012,14755750},Point{-796889,14928810},
                                Point{-875415,14923907},Point{-950040,14918790},Point{-1399899,14883305},Point{-1463233,14877508},Point{-1548694,14869385},Point{-1722559,14666533},Point{-1711234,14485114},Point{-1721030,14016671},
                                Point{-1381925,11254881},Point{-1649679,11218839},Point{-1880818,11182230},Point{-1934646,11173049},Point{-2163701,11131284},Point{-2694640,13862721},Point{-2848716,14305218},Point{-2894050,14481428},
                                Point{-3122889,14620252},Point{-3199206,14603232},Point{-3321081,14574900},Point{-3693152,14485602},Point{-3772680,14465413},Point{-3855750,14444039},Point{-3995701,14216480},Point{-3956138,14039087},
                                Point{-3892533,13574880},Point{-3125571,10900160},Point{-3382737,10823169},Point{-3605213,10750683},Point{-3658704,10732836},Point{-3878379,10655762},Point{-4830070,13270510},Point{-5051471,13683458},
                                Point{-5123330,13849286},Point{-5370933,13951969},Point{-5443467,13923294},Point{-5480488,13907806},Point{-5543787,13882842},Point{-5896644,13736683},Point{-5958897,13709644},Point{-5989712,13696887},
                                Point{-6067829,13663033},Point{-6170168,13415675},Point{-6103622,13247353},Point{-5968186,12798818},Point{-4792239,10276989},Point{-5035868,10159909},Point{-5244179,10053770},Point{-5292669,10028335},
                                Point{-5497560,9917860},Point{-6846568,12351539},Point{-7129838,12724760},Point{-7227339,12878238},Point{-7487524,12939917},Point{-7555223,12899921},Point{-7631319,12854379},Point{-7957617,12654393},
                                Point{-8058037,12591637},Point{-8129969,12545934},Point{-8193368,12286105},Point{-8100815,12129432},Point{-7896872,11707595},Point{-6340868,9400726},Point{-6566706,9244541},Point{-6755981,9107026},
                                Point{-6796264,9076931},Point{-6981371,8935747},Point{-8694482,11128430},Point{-9032654,11452749},Point{-9152987,11589109},Point{-9419694,11609210},Point{-9480348,11559081},Point{-9534075,11514076},
                                Point{-9877281,11221057},Point{-9928521,11176124},Point{-9992961,11119253},Point{-10014336,10852962},Point{-9898527,10712837},Point{-9631090,10328081},Point{-7733273,8292919},Point{-7935068,8100434},
                                Point{-8100463,7935038},Point{-8132553,7901991},Point{-8293276,7733606},Point{-10328316,9631311},Point{-10713041,9898720},Point{-10853278,10014625},Point{-11119975,9992521},Point{-11171673,9933936},
                                Point{-11250672,9842899},Point{-11499276,9551855},Point{-11554715,9485246},Point{-11609335,9419153},Point{-11588834,9152767},Point{-11452522,9032478},Point{-11128181,8694289},Point{-8935361,6981070},
                                Point{-9107029,6755977},Point{-9244545,6566701},Point{-9268508,6532583},Point{-9400976,6341038},Point{-11707816,7897023},Point{-12129659,8100967},Point{-12286224,8193462},Point{-12546084,8130200},
                                Point{-12588235,8063849},Point{-12642485,7977245},Point{-12842484,7650861},Point{-12896405,7560692},Point{-12939744,7487326},Point{-12878035,7227235},Point{-12724469,7129680},Point{-12351178,6846371},
                                Point{-9917099,5497139},Point{-9942664,5450240},Point{-10053730,5244258},Point{-10159918,5035849},Point{-10176465,5001949},Point{-10277196,4792336},Point{-12799059,5968300},Point{-13247600,6103738},
                                Point{-13416782,6170633},Point{-13663608,6067171},Point{-13694337,5996247},Point{-13741910,5883860},Point{-13888419,5530217},Point{-13921448,5447689},Point{-13951846,5370804},Point{-13850091,5123688},
                                Point{-13683258,5051399},Point{-13270281,4829988},Point{-10655328,3878223},Point{-10734497,3652574},Point{-10750707,3605136},Point{-10823170,3382734},Point{-10833753,3348145},Point{-10900369,3125632},
                                Point{-13575167,3892616},Point{-14039384,3956224},Point{-14216937,3995826},Point{-14444567,3854956},Point{-14463261,3782281},Point{-14493246,3661340},Point{-14582687,3288898},Point{-14602836,3200518},
                                Point{-14620295,3122231},Point{-14481162,2893990},Point{-14305022,2848679},Point{-13862438,2694585},Point{-11130461,2163543},Point{-11140317,2110679},Point{-11182241,1880753},Point{-11218841,1649668},
                                Point{-11224158,1612047},Point{-11255131,1381956},Point{-14016989,1721070},Point{-14485443,1711275},Point{-14666979,1722612},Point{-14869789,1547952},Point{-14876810,1474015},Point{-14887202,1354176},
                                Point{-14917264,972429},Point{-14923794,875838},Point{-14928732,796746},Point{-14755630,593007},Point{-14574626,575812},Point{-14113455,492853},Point{-11332490,395740},Point{-11336609,254257},
                                Point{-11338651,137250},Point{-11337605,123204},Point{-11319913,-497},Point{-11324484,-21041},Point{-11338651,-137266},Point{-11336122,-279745},Point{-11332747,-395747},Point{-14113653,-492859},
                                Point{-14574812,-575817},Point{-14755823,-593012},Point{-14928888,-796995},Point{-14924230,-871562},Point{-14917266,-972399},Point{-14887214,-1354044},Point{-14877063,-1469529},Point{-14869594,-1548149},
                                Point{-14666800,-1722589},Point{-14485255,-1711251},Point{-14016792,-1721045},Point{-11254880,-1381925},Point{-11218838,-1649679},Point{-11182238,-1880760},Point{-11172959,-1935293},Point{-11131311,-2163706},
                                Point{-13862787,-2694653},Point{-14305290,-2848730},Point{-14481375,-2894029},Point{-14620412,-3122530},Point{-14604064,-3195823},Point{-14582704,-3288822},Point{-14493276,-3661216},Point{-14464271,-3777603},
                                Point{-14444327,-3855127},Point{-14216753,-3995771},Point{-14039192,-3956168},Point{-13574974,-3892560},Point{-10900159,-3125571},Point{-10823164,-3382750},Point{-10750680,-3605241},Point{-10732733,-3659046},
                                Point{-10655778,-3878386},Point{-13270547,-4830084},Point{-13683492,-5051483},Point{-13850333,-5123775},Point{-13951925,-5371172},Point{-13923751,-5442426},Point{-13888441,-5530160},Point{-13741956,-5883752},
                                Point{-13696590,-5990468},Point{-13663284,-6067329},Point{-13416560,-6170527},Point{-13247381,-6103635},Point{-12798838,-5968196},Point{-10276988,-4792238},Point{-10159909,-5035868},Point{-10053769,-5244179},
                                Point{-10028334,-5292669},Point{-9917859,-5497560},Point{-12351538,-6846568},Point{-12724762,-7129840},Point{-12878257,-7227353},Point{-12939874,-7487593},Point{-12899897,-7555263},Point{-12862274,-7618053},
                                Point{-12662038,-7944711},Point{-12622271,-8009360},Point{-12591697,-8057942},Point{-12545686,-8130361},Point{-12285976,-8193292},Point{-12129432,-8100815},Point{-11707594,-7896872},Point{-9400725,-6340868},
                                Point{-9244540,-6566706},Point{-9107025,-6755981},Point{-9076930,-6796264},Point{-8935746,-6981371},Point{-11128430,-8694483},Point{-11452748,-9032654},Point{-11589077,-9152958},Point{-11609299,-9419585},
                                Point{-11559071,-9480357},Point{-11504370,-9545782},Point{-11255696,-9836897},Point{-11176079,-9928572},Point{-11119712,-9992437},Point{-10853136,-10014479},Point{-10712836,-9898527},Point{-10328081,-9631091},
                                Point{-8292918,-7733273},Point{-8100442,-7935059},Point{-7935037,-8100463},Point{-7901990,-8132553},Point{-7733605,-8293276},Point{-9631310,-10328316},Point{-9898719,-10713041},Point{-10014567,-10853212},
                                Point{-9992782,-11119744},Point{-9933873,-11171727},Point{-9860114,-11236030},Point{-9569161,-11484509},Point{-9484930,-11554975},Point{-9419436,-11609101},Point{-9152839,-11588916},Point{-9032443,-11452481},
                                Point{-8694232,-11128111},Point{-6980847,-8935079},Point{-6941894,-8965226},Point{-6755989,-9107020},Point{-6566700,-9244545},Point{-6532582,-9268508},Point{-6341037,-9400976},Point{-7897022,-11707816},
                                Point{-8100966,-12129659},Point{-8193475,-12286248},Point{-8130108,-12546142},Point{-8063992,-12588142},Point{-7970693,-12646495},Point{-7644385,-12846462},Point{-7560854,-12896309},Point{-7487206,-12939814},
                                Point{-7227205,-12877998},Point{-7129699,-12724508},Point{-6846416,-12351264},Point{-5497322,-9917431},Point{-5450123,-9942729},Point{-5244257,-10053730},Point{-5035832,-10159928},Point{-5002059,-10176405},
                                Point{-4792331,-10277189},Point{-5968292,-12799046},Point{-6103730,-13247587},Point{-6170627,-13416776},Point{-6067157,-13663597},Point{-5995626,-13694590},Point{-5883007,-13742252},Point{-5529496,-13888710},
                                Point{-5450342,-13920311},Point{-5370649,-13951816},Point{-5123648,-13849991},Point{-5051370,-13683183},Point{-4829962,-13270213},Point{-3878212,-10655302},Point{-3652341,-10734548},Point{-3605146,-10750707},
                                Point{-3382666,-10823191},Point{-3348810,-10833527},Point{-3125625,-10900346},Point{-3892599,-13575111},Point{-3956207,-14039326},Point{-3995855,-14217115},Point{-3854725,-14444558},Point{-3779302,-14463963},
                                Point{-3722638,-14477989},Point{-3649297,-14495986},Point{-3277123,-14585380},Point{-3205928,-14601481},Point{-3121932,-14620212},Point{-2893950,-14480971},Point{-2848651,-14304885},Point{-2694561,-13862318},
                                Point{-2163537,-11130434},Point{-2110912,-11140273},Point{-1880744,-11182242},Point{-1649632,-11218846},Point{-1612790,-11224027},Point{-1381952,-11255101},Point{-1721061,-14016918},Point{-1711265,-14485363},
                                Point{-1722597,-14666871},Point{-1548229,-14869678},Point{-1469997,-14877110},Point{-1400230,-14883277},Point{-942463,-14919245},Point{-882198,-14923237},Point{-796505,-14928583},Point{-593000,-14755442},
                                Point{-575803,-14574441},Point{-492845,-14113263},Point{-395729,-11332228},Point{-116958,-11338811},Point{116962,-11338811},Point{162978,-11338135},Point{395747,-11332716},Point{492857,-14113577},
                                Point{575815,-14574729},Point{593010,-14755729},Point{796830,-14928812}});

        BoundingBox bbox(Points{{-12023800, -9713631}, { -11278843, -8839465 }});

        Slic3r::Polygon bad_result({Point{-11250672,9842899},Point{-11128430,-8694483},Point{-11452748,-9032654},Point{-11589077,-9152958},Point{-11609299,-9419585},Point{-11559071,-9480357},Point{-11504370,-9545782},Point{-11255696,-9836897}});
        
        Slic3r::Polygon bad_simplified;
        Slic3r::ClipperUtils::clip_clipper_polyline_with_subject_bbox(big_poly.points, bbox, bad_simplified.points);
        Slic3r::Polygon good_simplified = Slic3r::ClipperUtils::clip_clipper_polygon_with_subject_bbox(big_poly, bbox);

        bool has_intersect = false;
        Lines bad_lines = bad_simplified.lines();
        Point pt_temp;
        for (size_t line_idx = 0; line_idx < bad_lines.size(); ++line_idx) {
            for (size_t line2_idx = line_idx + 2; line2_idx < bad_lines.size(); ++line2_idx) {
                has_intersect = has_intersect || bad_lines[line_idx].intersection(bad_lines[line2_idx], &pt_temp);
            }
        }

        // bad but expected
        REQUIRE(has_intersect);

        // no self-intersect with polygon algo
        Lines good_lines = good_simplified.lines();
        for (size_t line_idx = 0; line_idx < good_lines.size(); ++line_idx) {
            for (size_t line2_idx = line_idx + 2; line2_idx < good_lines.size() + (line_idx == 0 ? -1 : 0);
                 ++line2_idx) {
                if (good_lines[line_idx].intersection(good_lines[line2_idx], &pt_temp)) {
                }
                REQUIRE(!good_lines[line_idx].intersection(good_lines[line2_idx], &pt_temp));
            }
        }
    }
}

