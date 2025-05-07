#include <catch2/catch_test_macros.hpp>
#include <algorithm>

#include "libslic3r/KDTreeIndirect.hpp"
#include "libslic3r/Execution/ExecutionSeq.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/PointGrid.hpp"

using namespace Slic3r;

//template<class G>
//struct Within { // Wrapper for the `within` predicate that counts calls.

//    kdtree::Within<G> pred;

//    Within(G box): pred{box} {}

//       // Number of times the predicate was called
//    mutable size_t call_count = 0;

//    std::pair<bool, unsigned int> operator() (const Vec3f &p, size_t dim)
//    {
//        ++call_count;

//        return pred(p, dim);
//    }
//};

static double volume(const BoundingBox3Base<Vec3f> &box)
{
    auto sz = box.size();
    return sz.x() * sz.y() * sz.z();
}

TEST_CASE("Test kdtree query for a Box", "[KDTreeIndirect]")
{
    auto vol = BoundingBox3Base<Vec3f>{{0.f, 0.f, 0.f}, {10.f, 10.f, 10.f}};

    auto pgrid = point_grid(ex_seq, vol, Vec3f{0.1f, 0.1f, 0.1f});

    REQUIRE(!pgrid.empty());

    auto coordfn = [&pgrid] (size_t i, size_t D) { return pgrid.get(i)(int(D)); };
    KDTreeIndirect<3, float, decltype(coordfn)> tree{coordfn, pgrid.point_count()};

    std::vector<size_t> out;

    auto qbox = BoundingBox3Base{Vec3f{0.f, 0.f, 0.f}, Vec3f{.5f, .5f, .5f}};

    size_t call_count = 0;
    out = find_nearby_points(tree, qbox.min, qbox.max, [&call_count](size_t) {
        call_count++;
        return true;
    });

    // Output shall be non-empty
    REQUIRE(!out.empty());

    std::sort(out.begin(), out.end());

    // No duplicates allowed in the output
    auto it = std::unique(out.begin(), out.end());
    REQUIRE(it == out.end());

    // Test if inside points are in the output and outside points are not.
    bool succ = true;
    for (size_t i = 0; i < pgrid.point_count(); ++i) {
        auto foundit = std::find(out.begin(), out.end(), i);
        bool contains = qbox.contains(pgrid.get(i));
        succ = succ && contains ? foundit != out.end() : foundit == out.end();

        if (!succ) {
            std::cout << "invalid point: " << i << " " << pgrid.get(i).transpose()
                      << std::endl;
            break;
        }
    }

    REQUIRE(succ);

    // Test for the expected cost of the query.
    double gridvolume = volume(vol);
    double queryvolume = volume(qbox);
    double volratio = (queryvolume / gridvolume);
    REQUIRE(call_count < 3 * volratio * pgrid.point_count());
    REQUIRE(call_count < pgrid.point_count());
}

TEST_CASE("Test kdtree closests points", "[KDTreeIndirect]") {
    Points pts{
        Point{-9000000,  9000000},
        Point{-9000000, -9000000},
        Point{ 9000000, -9000000},
        Point{ 9000000,  9000000},
        Point{25, 25}
    };
    auto point_accessor = [&pts](size_t idx, size_t dim) -> coord_t & {
        return pts[idx][dim];
    };
    KDTreeIndirect<2, coord_t, decltype(point_accessor)> tree(point_accessor, pts.size());

    std::array<size_t, 5> closest = find_closest_points<5>(tree, Point{0, 0});
    CHECK(closest[0] == 4);
}

//TEST_CASE("Test kdtree query for a Sphere", "[KDTreeIndirect]") {
//    auto vol = BoundingBox3Base<Vec3f>{{0.f, 0.f, 0.f}, {10.f, 10.f, 10.f}};

//    auto pgrid = point_grid(ex_seq, vol, Vec3f{0.1f, 0.1f, 0.1f});

//    REQUIRE(!pgrid.empty());

//    auto coordfn = [&pgrid] (size_t i, size_t D) { return pgrid.get(i)(int(D)); };
//    kdtree::KDTreeIndirect<3, float, decltype(coordfn)> tree{coordfn, pgrid.point_count()};

//    std::vector<size_t> out;

//    auto querysphere = kdtree::Sphere{Vec3f{5.f, 5.f, 5.f}, 2.f};

//    auto pred = Within(querysphere);

//    kdtree::query(tree, pred, std::back_inserter(out));

//    // Output shall be non-empty
//    REQUIRE(!out.empty());

//    std::sort(out.begin(), out.end());

//    // No duplicates allowed in the output
//    auto it = std::unique(out.begin(), out.end());
//    REQUIRE(it == out.end());

//    // Test if inside points are in the output and outside points are not.
//    bool succ = true;
//    for (size_t i = 0; i < pgrid.point_count(); ++i) {
//        auto foundit = std::find(out.begin(), out.end(), i);
//        bool contains = (querysphere.center - pgrid.get(i)).squaredNorm() < pred.pred.r2;
//        succ = succ && contains ? foundit != out.end() : foundit == out.end();

//        if (!succ) {
//            std::cout << "invalid point: " << i << " " << pgrid.get(i).transpose()
//                      << std::endl;
//            break;
//        }
//    }

//    REQUIRE(succ);

//    // Test for the expected cost of the query.
//    double gridvolume = volume(vol);
//    double queryvolume = volume(querysphere);
//    double volratio = (queryvolume / gridvolume);
//    REQUIRE(pred.call_count < 3 * volratio * pgrid.point_count());
//    REQUIRE(pred.call_count < pgrid.point_count());
//}
