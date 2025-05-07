#include <catch2/catch_test_macros.hpp>
#include <libslic3r/SLA/SupportIslands/LineUtils.hpp>

using namespace Slic3r;
using namespace Slic3r::sla;

TEST_CASE("Intersection point", "[Utils], [LineUtils]")
{
    Point a1(0, 0);
    Point b1(3, 6);
    Line  l1(a1, b1);
    auto  intersection = LineUtils::intersection(l1, Line(Point(0, 4),
                                                         Point(5, 4)));
    CHECK(intersection.has_value());
    Point i_point = intersection->cast<coord_t>();
    CHECK(PointUtils::is_equal(i_point, Point(2, 4)));

    // same line
    auto bad_intersection = LineUtils::intersection(l1, l1);
    CHECK(!bad_intersection.has_value());

    // oposit direction
    bad_intersection = LineUtils::intersection(l1, Line(b1, a1));
    CHECK(!bad_intersection.has_value());

    // parallel line
    bad_intersection = LineUtils::intersection(l1, Line(a1 + Point(0, 1),
                                                        b1 + Point(0, 1)));
    CHECK(!bad_intersection.has_value());

    // out of line segment, but ray has intersection
    Line l2(Point(0, 8), Point(6, 8));
    intersection       = LineUtils::intersection(l1, l2);
    auto intersection2 = LineUtils::intersection(l2, l1);
    CHECK(intersection.has_value());
    CHECK(intersection2.has_value());
    i_point = intersection->cast<coord_t>();
    CHECK(PointUtils::is_equal(i_point, Point(4, 8)));
    CHECK(PointUtils::is_equal(i_point, intersection2->cast<coord_t>()));

    Line l3(Point(-2, -2), Point(1, -2));
    intersection  = LineUtils::intersection(l1, l3);
    intersection2 = LineUtils::intersection(l3, l1);
    CHECK(intersection.has_value());
    CHECK(intersection2.has_value());
    i_point = intersection->cast<coord_t>();
    CHECK(PointUtils::is_equal(i_point, Point(-1, -2)));
    CHECK(PointUtils::is_equal(i_point, intersection2->cast<coord_t>()));
}

TEST_CASE("Point belongs to line", "[Utils], [LineUtils]")
{
    Line l(Point(10, 10), Point(50, 30));
    CHECK(LineUtils::belongs(l, Point(30, 20)));
    CHECK(!LineUtils::belongs(l, Point(30, 30)));
    CHECK(LineUtils::belongs(l, Point(30, 30), 10.));
    CHECK(!LineUtils::belongs(l, Point(30, 10)));
    CHECK(!LineUtils::belongs(l, Point(70, 40)));
}
