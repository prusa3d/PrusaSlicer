#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <libslic3r/GCode/SeamScarf.hpp>
#include <libslic3r/GCode/SmoothPath.hpp>

using namespace Slic3r;
using Seams::Scarf::Impl::PathPoint;
using namespace Catch;

TEST_CASE("Get path point", "[Seams][Scarf]") {
    using Seams::Scarf::Impl::get_path_point;
    const Points points{
        Point::new_scale(0, 0),
        Point::new_scale(0, 1),
        Point::new_scale(0, 2),
        Point::new_scale(0, 3),
        Point::new_scale(0, 4),
    };
    const ExtrusionPaths paths{
        {{points[0], points[1]}, {}},
        {{points[1], points[2]}, {}},
        {{points[2], points[3], points[4]}, {}},
    };
    const std::size_t global_index{5}; // Index if paths are flattened.
    const Point point{Point::new_scale(0, 3.5)};
    const PathPoint path_point{get_path_point(paths, point, global_index)};

    CHECK(path_point.path_index == 2);
    CHECK(path_point.previous_point_on_path_index == 1);
    CHECK(path_point.point == point);
}

TEST_CASE("Split path", "[Seams][Scarf]") {
    using Seams::Scarf::Impl::split_path;

    const Points points{
        Point::new_scale(0, 0),
        Point::new_scale(1, 0),
        Point::new_scale(2, 0),
    };

    const auto split_point{Point::new_scale(1.5, 0)};

    const ExtrusionPath path{Polyline{points}, {}};
    const auto[path_before, path_after]{split_path(
        path, split_point, 1
    )};

    REQUIRE(path_before.size() == 3);
    CHECK(path_before.first_point() == points.front());
    CHECK(path_before.last_point() == split_point);

    REQUIRE(path_after.size() == 2);
    CHECK(path_after.first_point() == split_point);
    CHECK(path_after.last_point() == points.back());
}

TEST_CASE("Split paths", "[Seams][Scarf]") {
    using Seams::Scarf::Impl::split_paths;

    const Points points{
        Point::new_scale(0, 0),
        Point::new_scale(0, 1),
        Point::new_scale(0, 2),
    };
    ExtrusionPaths paths{
        {{points[0], points[1]}, {}},
        {{points[1], points[2]}, {}},
    };
    const auto split_point{Point::new_scale(0, 1.5)};
    PathPoint path_point{};
    path_point.point = split_point;
    path_point.previous_point_on_path_index = 0;
    path_point.path_index = 1;
    const ExtrusionPaths result{split_paths(std::move(paths), path_point)};

    REQUIRE(result.size() == 3);
    CHECK(result[1].last_point() == split_point);
    CHECK(result[2].first_point() == split_point);
}

TEST_CASE("Get length", "[Seams][Scarf]") {
    using Seams::Scarf::Impl::get_length;
    using Seams::Scarf::Impl::convert_to_smooth;

    const Points points{
        Point::new_scale(0, 0),
        Point::new_scale(0, 1),
        Point::new_scale(0, 2.2),
    };
    ExtrusionPaths paths{
        {{points[0], points[1]}, {}},
        {{points[1], points[2]}, {}},
    };

    CHECK(get_length(convert_to_smooth(paths)) == scaled(2.2));
}

TEST_CASE("Linspace", "[Seams][Scarf]") {
    using Seams::Scarf::Impl::linspace;

    const auto from{Point::new_scale(1, 0)};
    const auto to{Point::new_scale(3, 0)};

    Points points{linspace(from, to, 3)};
    REQUIRE(points.size() == 3);
    CHECK(points[1] == Point::new_scale(2, 0));

    points = linspace(from, to, 5);
    REQUIRE(points.size() == 5);
    CHECK(points[1] == Point::new_scale(1.5, 0));
    CHECK(points[2] == Point::new_scale(2.0, 0));
    CHECK(points[3] == Point::new_scale(2.5, 0));
}

TEST_CASE("Ensure max distance", "[Seams][Scarf]") {
    using Seams::Scarf::Impl::ensure_max_distance;

    const Points points{
        Point::new_scale(0, 0),
        Point::new_scale(0, 1),
    };

    Points result{ensure_max_distance(points, scaled(0.5))};
    REQUIRE(result.size() == 3);
    CHECK(result[1] == Point::new_scale(0, 0.5));

    result = ensure_max_distance(points, scaled(0.49));
    REQUIRE(result.size() == 4);
}

TEST_CASE("Lineary increase extrusion height", "[Seams][Scarf]") {
    using Seams::Scarf::Impl::lineary_increase_extrusion_height;
    using GCode::SmoothPath, GCode::SmoothPathElement;

    SmoothPath path{
        {{}, {{Point::new_scale(0, 0)}, {Point::new_scale(1, 0)}}},
        {{}, {{Point::new_scale(1, 0)}, {Point::new_scale(2, 0)}}},
    };

    SmoothPath result{lineary_increase_extrusion_height(std::move(path), 0.5)};

    CHECK(result[0].path[0].height_fraction == Approx(0.5));
    CHECK(result[0].path[0].e_fraction == Approx(0.0));
    CHECK(result[0].path[1].height_fraction == Approx(0.75));
    CHECK(result[0].path[1].e_fraction == Approx(0.5));
    CHECK(result[1].path[0].height_fraction == Approx(0.75));
    CHECK(result[1].path[0].e_fraction == Approx(0.5));
    CHECK(result[1].path[1].height_fraction == Approx(1.0));
    CHECK(result[1].path[1].e_fraction == Approx(1.0));
}

TEST_CASE("Lineary reduce extrusion amount", "[Seams][Scarf]") {
    using Seams::Scarf::Impl::lineary_readuce_extrusion_amount;
    using GCode::SmoothPath, GCode::SmoothPathElement;

    SmoothPath path{
        {{}, {{Point::new_scale(0, 0)}, {Point::new_scale(1, 0)}}},
        {{}, {{Point::new_scale(1, 0)}, {Point::new_scale(2, 0)}}},
    };

    SmoothPath result{lineary_readuce_extrusion_amount(std::move(path))};

    CHECK(result[0].path[0].e_fraction == Approx(1.0));
    CHECK(result[0].path[1].e_fraction == Approx(0.5));
    CHECK(result[1].path[0].e_fraction == Approx(0.5));
    CHECK(result[1].path[1].e_fraction == Approx(0.0));
}

TEST_CASE("Elevate scarf", "[Seams][Scarf]") {
    using Seams::Scarf::Impl::elevate_scarf;
    using Seams::Scarf::Impl::convert_to_smooth;


    const Points points{
        Point::new_scale(0, 0),
        Point::new_scale(1, 0),
        Point::new_scale(2, 0),
        Point::new_scale(3, 0),
    };
    const ExtrusionPaths paths{
        {{points[0], points[1]}, {}},
        {{points[1], points[2]}, {}},
        {{points[2], points[3]}, {}},
    };

    const GCode::SmoothPath result{elevate_scarf(
        paths,
        1,
        convert_to_smooth,
        0.5
    )};


    REQUIRE(result.size() == 3);
    REQUIRE(result[0].path.size() == 2);
    CHECK(result[0].path[0].e_fraction == Approx(0.0));
    CHECK(result[0].path[0].height_fraction == Approx(0.5));
    CHECK(result[0].path[1].e_fraction == Approx(1.0));
    CHECK(result[0].path[1].height_fraction == Approx(1.0));
    REQUIRE(result[1].path.size() == 2);
    CHECK(result[1].path[0].e_fraction == Approx(1.0));
    CHECK(result[1].path[0].height_fraction == Approx(1.0));
    CHECK(result[1].path[1].e_fraction == Approx(1.0));
    CHECK(result[1].path[1].height_fraction == Approx(1.0));
    REQUIRE(result[2].path.size() == 2);
    CHECK(result[2].path[0].e_fraction == Approx(1.0));
    CHECK(result[2].path[0].height_fraction == Approx(1.0));
    CHECK(result[2].path[1].e_fraction == Approx(0.0));
    CHECK(result[2].path[1].height_fraction == Approx(1.0));
}

TEST_CASE("Get point offset from the path end", "[Seams][Scarf]") {
    using Seams::Scarf::Impl::get_point_offset_from_end;

    const Points points{
        Point::new_scale(0, 0),
        Point::new_scale(1, 0),
        Point::new_scale(2, 0),
        Point::new_scale(3, 0),
    };
    const ExtrusionPaths paths{
        {{points[0], points[1]}, {}},
        {{points[1], points[2]}, {}},
        {{points[2], points[3]}, {}},
    };

    std::optional<PathPoint> result{get_point_offset_from_end(paths, scaled(1.6))};

    REQUIRE(result);
    CHECK(result->point == Point::new_scale(1.4, 0));
    CHECK(result->previous_point_on_path_index == 0);
    CHECK(result->path_index == 1);
}

TEST_CASE("Find point on path from the path end", "[Seams][Scarf]") {
    using Seams::Scarf::Impl::get_point_offset_from_end;

    const Points points{
        Point::new_scale(0, 0),
        Point::new_scale(1, 0),
        Point::new_scale(2, 0),
        Point::new_scale(3, 0),
        Point::new_scale(4, 0),
    };
    const ExtrusionPaths paths{
        {{points[0], points[1]}, {}},
        {{points[1], points[2]}, {}},
        {{points[2], points[3], points[4]}, {}},
    };

    const auto point{Point::new_scale(3.4, 0)};

    std::optional<PathPoint> result{Seams::Scarf::Impl::find_path_point_from_end(paths, point, scaled(1e-2))};

    REQUIRE(result);
    CHECK(result->point == point);
    CHECK(result->previous_point_on_path_index == 1);
    CHECK(result->path_index == 2);
}

TEST_CASE("Add scarf seam", "[Seams][Scarf]") {
    using Seams::Scarf::add_scarf_seam;
    using Seams::Scarf::Impl::convert_to_smooth;
    using Seams::Scarf::Impl::get_length;
    using Seams::Scarf::Scarf;

    const Points points{
        Point::new_scale(0, 0),
        Point::new_scale(1, 0),
        Point::new_scale(1, 1),
        Point::new_scale(0, 1),
        Point::new_scale(0, 0),
    };
    const ExtrusionPaths paths{
        {Polyline{points}, {}},
    };

    Scarf scarf{};
    scarf.start_point = Point::new_scale(0.5, 0);
    scarf.end_point = Point::new_scale(1, 0.5);
    scarf.start_height = 0.2;
    scarf.max_segment_length = 0.1;
    scarf.end_point_previous_index = 1;
    scarf.entire_loop = false;

    const auto [path, wipe_offset]{add_scarf_seam(ExtrusionPaths{paths}, scarf, convert_to_smooth, false)};

    REQUIRE(path.size() == 4);
    CHECK(wipe_offset == 1);

    REQUIRE(path.back().path.size() >= 1.0 / scarf.max_segment_length);
    CHECK(path.back().path.back().point == scarf.end_point);
    CHECK(path.back().path.front().point == scarf.start_point);
    CHECK(path.back().path.back().e_fraction == Approx(0));

    REQUIRE(path.front().path.size() >= 1.0 / scarf.max_segment_length);
    CHECK(path.front().path.back().point == scarf.end_point);
    CHECK(path.front().path.front().point == scarf.start_point);
    CHECK(path.front().path.front().e_fraction == Approx(0));
    CHECK(path.front().path.front().height_fraction == Approx(scarf.start_height));

    CHECK(path.front().path[5].point == points[1]);
    CHECK(path.front().path[5].e_fraction == Approx(0.5));
    CHECK(path.front().path[5].height_fraction == Approx(0.6));
    CHECK(path.back().path[5].e_fraction == Approx(0.5));
    CHECK(path.back().path[5].height_fraction == Approx(1.0));

    scarf.entire_loop = true;
    const auto [loop_path, _]{add_scarf_seam(ExtrusionPaths{paths}, scarf, convert_to_smooth, false)};

    CHECK(get_length(loop_path) == scaled(8.0));
    REQUIRE(!loop_path.empty());
    REQUIRE(!loop_path.front().path.empty());
    CHECK(loop_path.front().path.front().point == scarf.end_point);
    CHECK(loop_path.front().path.front().e_fraction == Approx(0));
    REQUIRE(!loop_path.back().path.empty());
    CHECK(loop_path.back().path.back().point == scarf.end_point);

    CHECK(loop_path.front().path.at(20).e_fraction == Approx(0.5));
    CHECK(loop_path.front().path.at(20).point == Point::new_scale(0, 0.5));
}
