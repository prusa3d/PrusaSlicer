#include <catch2/catch_test_macros.hpp>

#include "test_data.hpp"
#include "libslic3r/ClipperZUtils.hpp"
#include "libslic3r/clipper.hpp"

using namespace Slic3r;

// tests for ExPolygon::overlaps(const ExPolygon &other)
SCENARIO("Clipper intersection with polyline", "[Clipper]")
{
    struct TestData { 
        ClipperLib::Path subject;
        ClipperLib::Path clip;
        ClipperLib::Paths result;
    };

    auto run_test = [](const TestData &data) {
        ClipperLib::Clipper clipper;
        clipper.AddPath(data.subject, ClipperLib::ptSubject, false);
        clipper.AddPath(data.clip, ClipperLib::ptClip, true);

        ClipperLib::PolyTree polytree;
        ClipperLib::Paths    paths;
        clipper.Execute(ClipperLib::ctIntersection, polytree, ClipperLib::pftNonZero, ClipperLib::pftNonZero);
        ClipperLib::PolyTreeToPaths(polytree, paths);

        REQUIRE(paths == data.result);
    };

    WHEN("Open polyline completely inside stays inside") {
        run_test({
            { { 10, 0 }, { 20, 0 } },
            { { -1000, -1000 }, { -1000,  1000 }, { 1000,  1000 }, { 1000, -1000 } },
            { { { 20, 0 }, { 10, 0 } } }
        });
    };
    WHEN("Closed polyline completely inside stays inside") {
        run_test({
            { { 10, 0 }, { 20, 0 }, { 20, 20 }, { 10, 20 }, { 10, 0 } },
            { { -1000, -1000 }, { -1000,  1000 }, { 1000,  1000 }, { 1000, -1000 } },
            { { { 10, 0 }, { 20, 0 }, { 20, 20 }, { 10, 20 }, { 10, 0 } } }
        });
    };
    WHEN("Polyline which crosses right rectangle boundary is trimmed") {
        run_test({
            { { 10, 0 }, { 2000, 0 } },
            { { -1000, -1000 }, { -1000,  1000 }, { 1000,  1000 }, { 1000, -1000 } },
            { { { 1000, 0 }, { 10, 0 } } }
        });
    };
    WHEN("Polyline which is outside clipping region is removed") {
        run_test({
            { { 1500, 0 }, { 2000, 0 } },
            { { -1000, -1000 }, { -1000,  1000 }, { 1000,  1000 }, { 1000, -1000 } },
            { }
        });
    };

    WHEN("Polyline on left vertical boundary is kept") {
        run_test({
            { { -1000, -1000 }, { -1000, 1000 } },
            { { -1000, -1000 }, { -1000, 1000 }, { 1000, 1000 }, { 1000, -1000 } },
            { { { -1000, -1000 }, { -1000, 1000 } } }
        });
        run_test({
            { { -1000, 1000 }, { -1000, -1000 } },
            { { -1000, -1000 }, { -1000, 1000 }, { 1000, 1000 }, { 1000, -1000 } },
            { { { -1000, 1000 }, { -1000, -1000 } } }
        });
    };
    WHEN("Polyline on right vertical boundary is kept") {
        run_test({
            { { 1000, -1000 }, { 1000, 1000 } },
            { { -1000, -1000 }, { -1000, 1000 }, { 1000,  1000 }, { 1000, -1000 } },
            { { { 1000, -1000 }, { 1000, 1000 } } }
        });
        run_test({
            { { 1000, 1000 }, { 1000, -1000 } },
            { { -1000, -1000 }, { -1000, 1000 }, { 1000,  1000 }, { 1000, -1000 } },
            { { { 1000, 1000 }, { 1000, -1000 } } }
        });
    };
    WHEN("Polyline on bottom horizontal boundary is removed") {
        run_test({
            { { -1000, -1000 }, { 1000, -1000 } },
            { { -1000, -1000 }, { -1000, 1000 }, { 1000, 1000 }, { 1000, -1000 } },
            { }
        });
        run_test({
            { { 1000, -1000 }, { -1000, -1000 } },
            { { -1000, -1000 }, { -1000, 1000 }, { 1000, 1000 }, { 1000, -1000 } },
            { }
            });
    };
    WHEN("Polyline on top horizontal boundary is removed") {
        run_test({
            { { -1000, 1000 }, { 1000, 1000 } },
            { { -1000, -1000 }, { -1000, 1000 }, { 1000, 1000 }, { 1000, -1000 } },
            { }
        });
        run_test({
            { { 1000, 1000 }, { -1000, 1000 } },
            { { -1000, -1000 }, { -1000, 1000 }, { 1000, 1000 }, { 1000, -1000 } },
            { }
            });
    };
}

SCENARIO("Clipper Z", "[ClipperZ]")
{
    ClipperLib_Z::Path subject { { -2000, -1000, 10 }, { -2000,  1000, 10 }, { 2000,  1000, 10 }, { 2000, -1000, 10 } };
    ClipperLib_Z::Path clip{ { -1000, -2000, -5 }, { -1000,  2000, -5 }, { 1000,  2000, -5 }, { 1000, -2000, -5 } };

    ClipperLib_Z::Clipper clipper;
    clipper.ZFillFunction([](const ClipperLib_Z::IntPoint &e1bot, const ClipperLib_Z::IntPoint &e1top, const ClipperLib_Z::IntPoint &e2bot,
                             const ClipperLib_Z::IntPoint &e2top, ClipperLib_Z::IntPoint &pt) {
        pt.z() = 1;
    });

    clipper.AddPath(subject, ClipperLib_Z::ptSubject, false);
    clipper.AddPath(clip, ClipperLib_Z::ptClip, true);

    ClipperLib_Z::PolyTree polytree;
    ClipperLib_Z::Paths    paths;
    clipper.Execute(ClipperLib_Z::ctIntersection, polytree, ClipperLib_Z::pftNonZero, ClipperLib_Z::pftNonZero);
    ClipperLib_Z::PolyTreeToPaths(polytree, paths);

    REQUIRE(paths.size() == 1);
    REQUIRE(paths.front().size() == 2);
    for (const ClipperLib_Z::IntPoint &pt : paths.front())
        REQUIRE(pt.z() == 1);
}

SCENARIO("Intersection with multiple polylines", "[ClipperZ]")
{
    // 1000x1000 CCQ square
    ClipperLib_Z::Path clip { { 0, 0, 1 }, { 1000, 0, 1 }, { 1000, 1000, 1 }, { 0, 1000, 1 } };
    // Two lines interseting inside the square above, crossing the bottom edge of the square.
    ClipperLib_Z::Path line1 { { +100, -100, 2 }, { +900, +900, 2 } };
    ClipperLib_Z::Path line2 { { +100, +900, 3 }, { +900, -100, 3 } };

    ClipperLib_Z::Clipper clipper;
    ClipperZUtils::ClipperZIntersectionVisitor::Intersections intersections;
    ClipperZUtils::ClipperZIntersectionVisitor visitor(intersections);
    clipper.ZFillFunction(visitor.clipper_callback());
    clipper.AddPath(line1, ClipperLib_Z::ptSubject, false);
    clipper.AddPath(line2, ClipperLib_Z::ptSubject, false);
    clipper.AddPath(clip,  ClipperLib_Z::ptClip, true);

    ClipperLib_Z::PolyTree polytree;
    ClipperLib_Z::Paths    paths;
    clipper.Execute(ClipperLib_Z::ctIntersection, polytree, ClipperLib_Z::pftNonZero, ClipperLib_Z::pftNonZero);
    ClipperLib_Z::PolyTreeToPaths(polytree, paths);

    REQUIRE(paths.size() == 2);

    THEN("First output polyline is a trimmed 2nd line") {
        // Intermediate point (intersection) was removed)
        REQUIRE(paths.front().size() == 2);
        REQUIRE(paths.front().front().z() == 3);
        REQUIRE(paths.front().back().z() < 0);
        REQUIRE(intersections[- paths.front().back().z() - 1] == std::pair<coord_t, coord_t>(1, 3));
    }

    THEN("Second output polyline is a trimmed 1st line") {
        // Intermediate point (intersection) was removed)
        REQUIRE(paths[1].size() == 2);
        REQUIRE(paths[1].front().z() < 0);
        REQUIRE(paths[1].back().z() == 2);
        REQUIRE(intersections[- paths[1].front().z() - 1] == std::pair<coord_t, coord_t>(1, 2));
    }
}

SCENARIO("Interseting a closed loop as an open polyline", "[ClipperZ]")
{
    // 1000x1000 CCQ square
    ClipperLib_Z::Path clip{ { 0, 0, 1 }, { 1000, 0, 1 }, { 1000, 1000, 1 }, { 0, 1000, 1 } };
    // Two lines interseting inside the square above, crossing the bottom edge of the square.
    ClipperLib_Z::Path rect{ { 500, 500, 2}, { 500, 1500, 2 }, { 1500, 1500, 2}, { 500, 1500, 2}, { 500, 500, 2 } };

    ClipperLib_Z::Clipper clipper;
    clipper.AddPath(rect, ClipperLib_Z::ptSubject, false);
    clipper.AddPath(clip, ClipperLib_Z::ptClip, true);

    ClipperLib_Z::PolyTree polytree;
    ClipperLib_Z::Paths    paths;
    ClipperZUtils::ClipperZIntersectionVisitor::Intersections intersections;
    ClipperZUtils::ClipperZIntersectionVisitor visitor(intersections);
    clipper.ZFillFunction(visitor.clipper_callback());
    clipper.Execute(ClipperLib_Z::ctIntersection, polytree, ClipperLib_Z::pftNonZero, ClipperLib_Z::pftNonZero);
    ClipperLib_Z::PolyTreeToPaths(std::move(polytree), paths);

    THEN("Open polyline is clipped into two pieces") {
        REQUIRE(paths.size() == 2);
        REQUIRE(paths.front().size() == 2);
        REQUIRE(paths.back().size() == 2);
        REQUIRE(paths.front().front().z() == 2);
        REQUIRE(paths.back().back().z() == 2);
        REQUIRE(paths.front().front().x() == paths.back().back().x());
        REQUIRE(paths.front().front().y() == paths.back().back().y());
    }
}
