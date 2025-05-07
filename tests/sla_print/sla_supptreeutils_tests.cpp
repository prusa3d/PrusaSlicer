#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <test_utils.hpp>

#include <unordered_set>

#include "libslic3r/Execution/ExecutionSeq.hpp"
#include "libslic3r/SLA/SupportTreeUtils.hpp"
#include "libslic3r/SLA/SupportTreeUtilsLegacy.hpp"

using Catch::Approx;

// Test pair hash for 'nums' random number pairs.
template <class I, class II> void test_pairhash()
{
    const constexpr size_t nums = 1000;
    I A[nums] = {0}, B[nums] = {0};
    std::unordered_set<I> CH;
    std::unordered_map<II, std::pair<I, I>> ints;

    std::random_device rd;
    std::mt19937 gen(rd());

    const I Ibits = int(sizeof(I) * CHAR_BIT);
    const II IIbits = int(sizeof(II) * CHAR_BIT);

    int bits = IIbits / 2 < Ibits ? Ibits / 2 : Ibits;
    if (std::is_signed<I>::value) bits -= 1;
    const I Imin = 0;
    const I Imax = I(std::pow(2., bits) - 1);

    std::uniform_int_distribution<I> dis(Imin, Imax);

    for (size_t i = 0; i < nums;) {
        I a = dis(gen);
        if (CH.find(a) == CH.end()) { CH.insert(a); A[i] = a; ++i; }
    }

    for (size_t i = 0; i < nums;) {
        I b = dis(gen);
        if (CH.find(b) == CH.end()) { CH.insert(b); B[i] = b; ++i; }
    }

    for (size_t i = 0; i < nums; ++i) {
        I a = A[i], b = B[i];

        REQUIRE(a != b);

        II hash_ab = Slic3r::sla::pairhash<I, II>(a, b);
        II hash_ba = Slic3r::sla::pairhash<I, II>(b, a);
        REQUIRE(hash_ab == hash_ba);

        auto it = ints.find(hash_ab);

        if (it != ints.end()) {
            REQUIRE((
                (it->second.first == a && it->second.second == b) ||
                (it->second.first == b && it->second.second == a)
                ));
        } else
            ints[hash_ab] = std::make_pair(a, b);
    }
}

TEST_CASE("Pillar pairhash should be unique", "[suptreeutils]") {
    test_pairhash<int, int>();
    test_pairhash<int, long>();
    test_pairhash<unsigned, unsigned>();
    test_pairhash<unsigned, unsigned long>();
}

static void eval_ground_conn(const Slic3r::sla::GroundConnection &conn,
                             const Slic3r::sla::SupportableMesh &sm,
                             const Slic3r::sla::Junction &j,
                             double end_r,
                             const std::string &stl_fname = "output.stl")
{
    using namespace Slic3r;

//#ifndef NDEBUG

    sla::SupportTreeBuilder builder;

    if (!conn)
        builder.add_junction(j);

    sla::build_ground_connection(builder, sm, conn);

    indexed_triangle_set mesh = *sm.emesh.get_triangle_mesh();
    its_merge(mesh, builder.merged_mesh());

    its_write_stl_ascii(stl_fname.c_str(), "stl_fname", mesh);
//#endif

    REQUIRE(bool(conn));

    // The route should include the source and one avoidance junction.
    REQUIRE(conn.path.size() == 2);

    // Check if the radius increases with each node
    REQUIRE(conn.path.front().r < conn.path.back().r);
    REQUIRE(conn.path.back().r < conn.pillar_base->r_top);

    // The end radius and the pillar base's upper radius should match
    REQUIRE(conn.pillar_base->r_top == Approx(end_r));
}

TEST_CASE("Pillar search dumb case", "[suptreeutils]") {
    using namespace Slic3r;

    constexpr double FromR = 0.5;
    auto j = sla::Junction{Vec3d::Zero(), FromR};

    SECTION("with empty mesh") {
        sla::SupportableMesh sm{indexed_triangle_set{},
                                sla::SupportPoints{},
                                sla::SupportTreeConfig{}};

        constexpr double EndR = 1.;
        sla::GroundConnection conn =
                sla::deepsearch_ground_connection(ex_seq, sm, j, EndR, sla::DOWN);

        REQUIRE(conn);
//        REQUIRE(conn.path.size() == 1);
        REQUIRE(conn.pillar_base->pos.z() == Approx(ground_level(sm)));
    }

    SECTION("with zero R source and destination") {
        sla::SupportableMesh sm{indexed_triangle_set{},
                                sla::SupportPoints{},
                                sla::SupportTreeConfig{}};

        j.r = 0.;
        constexpr double EndR = 0.;
        sla::GroundConnection conn =
                sla::deepsearch_ground_connection(ex_seq, sm, j, EndR, sla::DOWN);

        REQUIRE(conn);
//        REQUIRE(conn.path.size() == 1);
        REQUIRE(conn.pillar_base->pos.z() == Approx(ground_level(sm)));
        REQUIRE(conn.pillar_base->r_top == Approx(0.));
    }

    SECTION("with zero init direction") {
        sla::SupportableMesh sm{indexed_triangle_set{},
                                sla::SupportPoints{},
                                sla::SupportTreeConfig{}};

        constexpr double EndR = 1.;
        Vec3d init_dir = Vec3d::Zero();
        sla::GroundConnection conn =
                sla::deepsearch_ground_connection(ex_seq, sm, j, EndR, init_dir);

        REQUIRE(conn);
//        REQUIRE(conn.path.size() == 1);
        REQUIRE(conn.pillar_base->pos.z() == Approx(ground_level(sm)));
    }
}

TEST_CASE("Avoid disk below junction", "[suptreeutils]")
{
    // In this test there will be a disk mesh with some radius, centered at
    // (0, 0, 0) and above the disk, a junction from which the support pillar
    // should be routed. The algorithm needs to find an avoidance route.

    using namespace Slic3r;

    constexpr double FromRadius = .5;
    constexpr double EndRadius  = 1.;
    constexpr double CylRadius  = 4.;
    constexpr double CylHeight  = 1.;

    sla::SupportTreeConfig cfg;

    indexed_triangle_set disk = its_make_cylinder(CylRadius, CylHeight);

    // 2.5 * CyRadius height should be enough to be able to insert a bridge
    // with 45 degree tilt above the disk.
    sla::Junction j{Vec3d{0., 0., 2.5 * CylRadius}, FromRadius};

    sla::SupportableMesh sm{disk, sla::SupportPoints{}, cfg};

    SECTION("with elevation") {

        sla::GroundConnection conn =
                sla::deepsearch_ground_connection(ex_tbb, sm, j, EndRadius, sla::DOWN);

        eval_ground_conn(conn, sm, j, EndRadius, "disk.stl");

        // Check if the avoidance junction is indeed outside of the disk barrier's
        // edge.
        auto p = conn.path.back().pos;
        double pR = std::sqrt(p.x() * p.x()) + std::sqrt(p.y() * p.y());
        REQUIRE(pR + FromRadius > CylRadius);
    }

    SECTION("without elevation") {
        sm.cfg.object_elevation_mm = 0.;

        sla::GroundConnection conn =
                sla::deepsearch_ground_connection(ex_tbb, sm, j, EndRadius, sla::DOWN);

        eval_ground_conn(conn, sm, j, EndRadius, "disk_ze.stl");

        // Check if the avoidance junction is indeed outside of the disk barrier's
        // edge.
        auto p = conn.path.back().pos;
        double pR = std::sqrt(p.x() * p.x()) + std::sqrt(p.y() * p.y());
        REQUIRE(pR + FromRadius > CylRadius);
    }
}

TEST_CASE("Avoid disk below junction with barrier on the side", "[suptreeutils]")
{
    // In this test there will be a disk mesh with some radius, centered at
    // (0, 0, 0) and above the disk, a junction from which the support pillar
    // should be routed. The algorithm needs to find an avoidance route.

    using namespace Slic3r;

    constexpr double FromRadius = .5;
    constexpr double EndRadius  = 1.;
    constexpr double CylRadius  = 4.;
    constexpr double CylHeight  = 1.;
    constexpr double JElevX     = 2.5;

    sla::SupportTreeConfig cfg;

    indexed_triangle_set disk = its_make_cylinder(CylRadius, CylHeight);
    indexed_triangle_set wall = its_make_cube(1., 2 * CylRadius, JElevX * CylRadius);
    its_translate(wall, Vec3f{float(FromRadius), -float(CylRadius), 0.f});
    its_merge(disk, wall);

    // 2.5 * CyRadius height should be enough to be able to insert a bridge
    // with 45 degree tilt above the disk.
    sla::Junction j{Vec3d{0., 0., JElevX * CylRadius}, FromRadius};

    sla::SupportableMesh sm{disk, sla::SupportPoints{}, cfg};

    SECTION("with elevation") {
        sla::GroundConnection conn =
                sla::deepsearch_ground_connection(ex_seq, sm, j, EndRadius, sla::DOWN);

        eval_ground_conn(conn, sm, j, EndRadius, "disk_with_barrier.stl");

        // Check if the avoidance junction is indeed outside of the disk barrier's
        // edge.
        auto p = conn.path.back().pos;
        double pR = std::sqrt(p.x() * p.x()) + std::sqrt(p.y() * p.y());
        REQUIRE(pR + FromRadius > CylRadius);
    }

    SECTION("without elevation") {
        sm.cfg.object_elevation_mm = 0.;

        sla::GroundConnection conn =
                sla::deepsearch_ground_connection(ex_seq, sm, j, EndRadius, sla::DOWN);

        eval_ground_conn(conn, sm, j, EndRadius, "disk_with_barrier_ze.stl");

        // Check if the avoidance junction is indeed outside of the disk barrier's
        // edge.
        auto p = conn.path.back().pos;
        double pR = std::sqrt(p.x() * p.x()) + std::sqrt(p.y() * p.y());
        REQUIRE(pR + FromRadius > CylRadius);
    }
}

TEST_CASE("Find ground route just above ground", "[suptreeutils]") {
    using namespace Slic3r;

    sla::SupportTreeConfig cfg;
    cfg.object_elevation_mm = 0.;

    sla::Junction j{Vec3d{0., 0., 2. * cfg.head_back_radius_mm}, cfg.head_back_radius_mm};

    sla::SupportableMesh sm{{}, sla::SupportPoints{}, cfg};

    sla::GroundConnection conn =
        sla::deepsearch_ground_connection(ex_seq, sm, j, Geometry::spheric_to_dir(3 * PI/ 4, PI));

    REQUIRE(conn);

    REQUIRE(conn.pillar_base->pos.z() >= Approx(ground_level(sm)));
}

TEST_CASE("BranchingSupports::MergePointFinder", "[suptreeutils]") {
    using namespace Slic3r;

    SECTION("Identical points have the same merge point") {
        Vec3f a{0.f, 0.f, 0.f}, b = a;
        auto slope = float(PI / 4.);

        auto mergept = sla::find_merge_pt(a, b, slope);

        REQUIRE(bool(mergept));
        REQUIRE((*mergept - b).norm() < EPSILON);
        REQUIRE((*mergept - a).norm() < EPSILON);
    }

    // ^ Z
    // | a *
    // |
    // | b * <= mergept
    SECTION("Points at different heights have the lower point as mergepoint") {
        Vec3f a{0.f, 0.f, 0.f}, b = {0.f, 0.f, -1.f};
        auto slope = float(PI / 4.);

        auto mergept = sla::find_merge_pt(a, b, slope);

        REQUIRE(bool(mergept));
        REQUIRE((*mergept - b).squaredNorm() < 2 * EPSILON);
    }

    // -|---------> X
    //  a       b
    //  *       *
    //      * <= mergept
    SECTION("Points at different X have mergept in the middle at lower Z") {
        Vec3f a{0.f, 0.f, 0.f}, b = {1.f, 0.f, 0.f};
        auto slope = float(PI / 4.);

        auto mergept = sla::find_merge_pt(a, b, slope);

        REQUIRE(bool(mergept));

        // Distance of mergept should be equal from both input points
        float D = std::abs((*mergept - b).squaredNorm() - (*mergept - a).squaredNorm());

        REQUIRE(D < EPSILON);
        REQUIRE(!sla::is_outside_support_cone(a, *mergept, slope));
        REQUIRE(!sla::is_outside_support_cone(b, *mergept, slope));
    }

    // -|---------> Y
    //  a       b
    //  *       *
    //      * <= mergept
    SECTION("Points at different Y have mergept in the middle at lower Z") {
        Vec3f a{0.f, 0.f, 0.f}, b = {0.f, 1.f, 0.f};
        auto slope = float(PI / 4.);

        auto mergept = sla::find_merge_pt(a, b, slope);

        REQUIRE(bool(mergept));

        // Distance of mergept should be equal from both input points
        float D = std::abs((*mergept - b).squaredNorm() - (*mergept - a).squaredNorm());

        REQUIRE(D < EPSILON);
        REQUIRE(!sla::is_outside_support_cone(a, *mergept, slope));
        REQUIRE(!sla::is_outside_support_cone(b, *mergept, slope));
    }

    SECTION("Points separated by less than critical angle have the lower point as mergept") {
        Vec3f a{-1.f, -1.f, -1.f}, b = {-1.5f, -1.5f, -2.f};
        auto slope = float(PI / 4.);

        auto mergept = sla::find_merge_pt(a, b, slope);

        REQUIRE(bool(mergept));
        REQUIRE((*mergept - b).norm() < 2 * EPSILON);
    }

    // -|----------------------------> Y
    //  a                          b
    //  *            * <= mergept  *
    //
    SECTION("Points at same height have mergepoint in the middle if critical angle is zero ") {
        Vec3f a{-1.f, -1.f, -1.f}, b = {-1.5f, -1.5f, -1.f};
        auto slope = EPSILON;

        auto mergept = sla::find_merge_pt(a, b, slope);

        REQUIRE(bool(mergept));
        Vec3f middle = (b + a) / 2.;
        REQUIRE((*mergept - middle).norm() < 4 * EPSILON);
    }
}

