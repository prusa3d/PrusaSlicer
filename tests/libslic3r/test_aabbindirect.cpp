#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <test_utils.hpp>

#include <libslic3r/TriangleMesh.hpp>
#include <libslic3r/AABBTreeIndirect.hpp>
#include <libslic3r/AABBTreeLines.hpp>

using namespace Slic3r;
using namespace Catch;

TEST_CASE("Building a tree over a box, ray caster and closest query", "[AABBIndirect]")
{
    TriangleMesh tmesh = make_cube(1., 1., 1.);

    auto tree = AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(tmesh.its.vertices, tmesh.its.indices);
    REQUIRE(! tree.empty());

    igl::Hit hit;
	bool intersected = AABBTreeIndirect::intersect_ray_first_hit(
		tmesh.its.vertices, tmesh.its.indices,
		tree,
		Vec3d(0.5, 0.5, -5.),
		Vec3d(0., 0., 1.),
		hit);

    REQUIRE(intersected);
    REQUIRE(hit.t == Approx(5.));

    std::vector<igl::Hit> hits;
	bool intersected2 = AABBTreeIndirect::intersect_ray_all_hits(
		tmesh.its.vertices, tmesh.its.indices,
		tree,
        Vec3d(0.3, 0.5, -5.),
		Vec3d(0., 0., 1.),
		hits);
    REQUIRE(intersected2);
    REQUIRE(hits.size() == 2);
    REQUIRE(hits.front().t == Approx(5.));
    REQUIRE(hits.back().t == Approx(6.));

    size_t hit_idx;
    Vec3d  closest_point;
    double squared_distance = AABBTreeIndirect::squared_distance_to_indexed_triangle_set(
		tmesh.its.vertices, tmesh.its.indices,
		tree,
        Vec3d(0.3, 0.5, -5.),
		hit_idx, closest_point);
    REQUIRE(squared_distance == Approx(5. * 5.));
    REQUIRE(closest_point.x() == Approx(0.3));
    REQUIRE(closest_point.y() == Approx(0.5));
    REQUIRE(closest_point.z() == Approx(0.));

    squared_distance = AABBTreeIndirect::squared_distance_to_indexed_triangle_set(
		tmesh.its.vertices, tmesh.its.indices,
		tree,
        Vec3d(0.3, 0.5, 5.),
		hit_idx, closest_point);
    REQUIRE(squared_distance == Approx(4. * 4.));
    REQUIRE(closest_point.x() == Approx(0.3));
    REQUIRE(closest_point.y() == Approx(0.5));
    REQUIRE(closest_point.z() == Approx(1.));
}

TEST_CASE("Creating a several 2d lines, testing closest point query", "[AABBIndirect]")
{
    std::vector<Linef> lines { };
    lines.push_back(Linef(Vec2d(0.0, 0.0), Vec2d(1.0, 0.0)));
    lines.push_back(Linef(Vec2d(1.0, 0.0), Vec2d(1.0, 1.0)));
    lines.push_back(Linef(Vec2d(1.0, 1.0), Vec2d(0.0, 1.0)));
    lines.push_back(Linef(Vec2d(0.0, 1.0), Vec2d(0.0, 0.0)));

    auto tree = AABBTreeLines::build_aabb_tree_over_indexed_lines(lines);

    size_t hit_idx_out;
    Vec2d hit_point_out;
    auto sqr_dist = AABBTreeLines::squared_distance_to_indexed_lines(lines, tree, Vec2d(0.0, 0.0), hit_idx_out,
            hit_point_out);
    REQUIRE(sqr_dist == Approx(0.0));
    REQUIRE((hit_idx_out == 0 || hit_idx_out == 3));
    REQUIRE(hit_point_out.x() == Approx(0.0));
    REQUIRE(hit_point_out.y() == Approx(0.0));

    sqr_dist = AABBTreeLines::squared_distance_to_indexed_lines(lines, tree, Vec2d(1.5, 0.5), hit_idx_out,
            hit_point_out);
    REQUIRE(sqr_dist == Approx(0.25));
    REQUIRE(hit_idx_out == 1);
    REQUIRE(hit_point_out.x() == Approx(1.0));
    REQUIRE(hit_point_out.y() == Approx(0.5));
}

TEST_CASE("Creating a several 2d lines, testing all lines in radius query", "[AABBIndirect]")
{
    std::vector<Linef> lines { };
    lines.push_back(Linef(Vec2d(0.0, 0.0), Vec2d(10.0, 0.0)));
    lines.push_back(Linef(Vec2d(-10.0, 10.0), Vec2d(10.0, -10.0)));
    lines.push_back(Linef(Vec2d(-2.0, -1.0), Vec2d(-2.0, 1.0)));
    lines.push_back(Linef(Vec2d(-1.0, -1.0), Vec2d(-1.0, -1.0)));
    lines.push_back(Linef(Vec2d(1.0, 1.0), Vec2d(1.0, 1.0)));

    auto tree = AABBTreeLines::build_aabb_tree_over_indexed_lines(lines);

    auto indices = AABBTreeLines::all_lines_in_radius(lines, tree, Vec2d{1.0,1.0}, 4.0);

    REQUIRE(std::find(indices.begin(),indices.end(), 0) != indices.end());
    REQUIRE(std::find(indices.begin(),indices.end(), 1) != indices.end());
    REQUIRE(std::find(indices.begin(),indices.end(), 4) != indices.end());
    REQUIRE(indices.size() == 3);
}

TEST_CASE("Find the closest point from ExPolys", "[ClosestPoint]") {
    //////////////////////////////
    //  0 - 3
    //  |Ex0|   0 - 3
    //  |   |p  |Ex1|
    //  1 - 2   |   |
    //          1 - 2
    //[0,0]
    ///////////////////
    ExPolygons ex_polys{
        /*Ex0*/ {{0, 4}, {0, 1}, {2, 1}, {2, 4}}, 
        /*Ex1*/ {{4, 3}, {4, 0}, {6, 0}, {6, 3}}
    };
    Vec2d p{2.5, 3.5};

    std::vector<Linef> lines;
    auto add_lines = [&lines](const Polygon& poly) {
        for (const auto &line : poly.lines())
            lines.emplace_back(
                line.a.cast<double>(), 
                line.b.cast<double>());
    };
    for (const ExPolygon &ex_poly : ex_polys) {
        add_lines(ex_poly.contour);
        for (const Polygon &hole : ex_poly.holes) 
            add_lines(hole);
    }
    AABBTreeIndirect::Tree<2, double> tree = 
        AABBTreeLines::build_aabb_tree_over_indexed_lines(lines);

    size_t hit_idx_out = std::numeric_limits<size_t>::max();
    Vec2d  hit_point_out; 
    [[maybe_unused]] double distance_sq =
        AABBTreeLines::squared_distance_to_indexed_lines(
        lines, tree, p, hit_idx_out, hit_point_out, 0.24/* < (0.5*0.5) */);
    CHECK(hit_idx_out == std::numeric_limits<size_t>::max());
    distance_sq = AABBTreeLines::squared_distance_to_indexed_lines(
        lines, tree, p, hit_idx_out, hit_point_out, 0.26);
    CHECK(hit_idx_out != std::numeric_limits<size_t>::max());

    //double distance = sqrt(distance_sq);
    //const Linef &line = lines[hit_idx_out];
}

#if 0
#include "libslic3r/EdgeGrid.hpp"
#include <iostream>
#include <ctime>
#include <ratio>
#include <chrono>
TEST_CASE("AABBTreeLines vs SignedDistanceGrid time Benchmark", "[AABBIndirect]")
{
    std::vector<Points> lines { Points { } };
    std::vector<Linef> linesf { };
    Vec2d prevf { };

    // NOTE: max coord value of the lines is approx 83 mm
    for (int r = 1; r < 1000; ++r) {
        lines[0].push_back(Point::new_scale(Vec2d(exp(0.005f * r) * cos(r), exp(0.005f * r) * cos(r))));
        linesf.emplace_back(prevf, Vec2d(exp(0.005f * r) * cos(r), exp(0.005f * r) * cos(r)));
        prevf = linesf.back().b;
    }

    int build_num = 10000;
    using namespace std::chrono;
    {
        std::cout << "building the tree " << build_num << " times..." << std::endl;
        high_resolution_clock::time_point t1 = high_resolution_clock::now();
        for (int i = 0; i < build_num; ++i) {
            volatile auto tree = AABBTreeLines::build_aabb_tree_over_indexed_lines(linesf);
        }
        high_resolution_clock::time_point t2 = high_resolution_clock::now();

        duration<double> time_span = duration_cast<duration<double>>(t2 - t1);
        std::cout << "It took " << time_span.count() << " seconds." << std::endl << std::endl;

    }
    {
        std::cout << "building the grid res 1mm ONLY " << build_num/100 << " !!! times..." << std::endl;
        high_resolution_clock::time_point t1 = high_resolution_clock::now();
        for (int i = 0; i < build_num/100; ++i) {
            EdgeGrid::Grid grid { };
            grid.create(lines, scaled(1.0), true);
            grid.calculate_sdf();
        }
        high_resolution_clock::time_point t2 = high_resolution_clock::now();
        duration<double> time_span = duration_cast<duration<double>>(t2 - t1);
        std::cout << "It took " << time_span.count() << " seconds." << std::endl << std::endl;
    }
    {
        std::cout << "building the grid res 10mm " << build_num << " times..." << std::endl;
        high_resolution_clock::time_point t1 = high_resolution_clock::now();
        for (int i = 0; i < build_num; ++i) {
            EdgeGrid::Grid grid { };
            grid.create(lines, scaled(10.0), true);
            grid.calculate_sdf();
        }
        high_resolution_clock::time_point t2 = high_resolution_clock::now();
        duration<double> time_span = duration_cast<duration<double>>(t2 - t1);
        std::cout << "It took " << time_span.count() << " seconds." << std::endl << std::endl;
    }

    EdgeGrid::Grid grid10 { };
    grid10.create(lines, scaled(10.0), true);
    coord_t query10_res = scaled(10.0);
    grid10.calculate_sdf();

    EdgeGrid::Grid grid1 { };
    grid1.create(lines, scaled(1.0), true);
    coord_t query1_res = scaled(1.0);
    grid1.calculate_sdf();

    auto tree = AABBTreeLines::build_aabb_tree_over_indexed_lines(linesf);

    int query_num = 10000;
    Points query_points { };
    std::vector<Vec2d> query_pointsf { };
    for (int x = 0; x < query_num; ++x) {
        Vec2d qp { rand() / (double(RAND_MAX) + 1.0f) * 200.0 - 100.0, rand() / (double(RAND_MAX) + 1.0f) * 200.0
                - 100.0 };
        query_pointsf.push_back(qp);
        query_points.push_back(Point::new_scale(qp));
    }

    {
        std::cout << "querying tree " << query_num << " times..." << std::endl;
        high_resolution_clock::time_point t1 = high_resolution_clock::now();
        for (const Vec2d &qp : query_pointsf) {
            size_t hit_idx_out;
            Vec2d hit_point_out;
            AABBTreeLines::squared_distance_to_indexed_lines(linesf, tree, qp, hit_idx_out, hit_point_out);
        }
        high_resolution_clock::time_point t2 = high_resolution_clock::now();
        duration<double> time_span = duration_cast<duration<double>>(t2 - t1);
        std::cout << "It took " << time_span.count() << " seconds." << std::endl << std::endl;
    }

    {
        std::cout << "querying grid res 1mm " << query_num << " times..." << std::endl;
        high_resolution_clock::time_point t1 = high_resolution_clock::now();
        for (const Point &qp : query_points) {
            volatile auto dist = grid1.closest_point_signed_distance(qp, query1_res);
        }
        high_resolution_clock::time_point t2 = high_resolution_clock::now();
        duration<double> time_span = duration_cast<duration<double>>(t2 - t1);
        std::cout << "It took " << time_span.count() << " seconds." << std::endl << std::endl;
    }

    {
        std::cout << "querying grid res 10mm " << query_num << " times..." << std::endl;
        high_resolution_clock::time_point t1 = high_resolution_clock::now();
        for (const Point &qp : query_points) {
            volatile auto dist = grid10.closest_point_signed_distance(qp, query10_res);
        }
        high_resolution_clock::time_point t2 = high_resolution_clock::now();
        duration<double> time_span = duration_cast<duration<double>>(t2 - t1);
        std::cout << "It took " << time_span.count() << " seconds." << std::endl << std::endl;
    }

    std::cout << "Test build and queries together - same number of contour points and query points" << std::endl << std::endl;

    std::vector<int> point_counts { 100, 300, 500, 1000, 3000 };
    for (auto count : point_counts) {

        std::vector<Points> lines { Points { } };
        std::vector<Linef> linesf { };
        Vec2d prevf { };
        Points query_points { };
        std::vector<Vec2d> query_pointsf { };

        for (int x = 0; x < count; ++x) {
            Vec2d cp { rand() / (double(RAND_MAX) + 1.0f) * 200.0 - 100.0, rand() / (double(RAND_MAX) + 1.0f) * 200.0
                    - 100.0 };
            lines[0].push_back(Point::new_scale(cp));
            linesf.emplace_back(prevf, cp);
            prevf = linesf.back().b;

            Vec2d qp { rand() / (double(RAND_MAX) + 1.0f) * 200.0 - 100.0, rand() / (double(RAND_MAX) + 1.0f) * 200.0
                    - 100.0 };
            query_pointsf.push_back(qp);
            query_points.push_back(Point::new_scale(qp));
        }

        std::cout << "Test for point count: " << count << std::endl;
        {
            high_resolution_clock::time_point t1 = high_resolution_clock::now();
            auto tree = AABBTreeLines::build_aabb_tree_over_indexed_lines(linesf);
            for (const Vec2d &qp : query_pointsf) {
                size_t hit_idx_out;
                Vec2d hit_point_out;
                AABBTreeLines::squared_distance_to_indexed_lines(linesf, tree, qp, hit_idx_out, hit_point_out);
            }
            high_resolution_clock::time_point t2 = high_resolution_clock::now();
            duration<double> time_span = duration_cast<duration<double>>(t2 - t1);
            std::cout << "    Tree     took " << time_span.count() << " seconds." << std::endl;
        }

        {
            high_resolution_clock::time_point t1 = high_resolution_clock::now();
            EdgeGrid::Grid grid1 { };
            grid1.create(lines, scaled(1.0), true);
            coord_t query1_res = scaled(1.0);
            grid1.calculate_sdf();
            for (const Point &qp : query_points) {
                volatile auto dist = grid1.closest_point_signed_distance(qp, query1_res);
            }
            high_resolution_clock::time_point t2 = high_resolution_clock::now();
            duration<double> time_span = duration_cast<duration<double>>(t2 - t1);
            std::cout << "    Grid 1mm  took " << time_span.count() << " seconds." << std::endl;
        }

        {
            high_resolution_clock::time_point t1 = high_resolution_clock::now();
            EdgeGrid::Grid grid10 { };
            grid10.create(lines, scaled(10.0), true);
            coord_t query10_res = scaled(10.0);
            grid10.calculate_sdf();
            for (const Point &qp : query_points) {
                volatile auto dist = grid10.closest_point_signed_distance(qp, query10_res);
            }
            high_resolution_clock::time_point t2 = high_resolution_clock::now();
            duration<double> time_span = duration_cast<duration<double>>(t2 - t1);
            std::cout << "    Grid 10mm took " << time_span.count() << " seconds." << std::endl;
        }

    }



    std::cout << "Test build and queries together - same number of contour points and query points" << std::endl <<
            "And with limited contour edge length to 4mm " << std::endl;
      for (auto count : point_counts) {

          std::vector<Points> lines { Points { } };
          std::vector<Linef> linesf { };
          Vec2d prevf { };
          Points query_points { };
          std::vector<Vec2d> query_pointsf { };

          for (int x = 0; x < count; ++x) {
              Vec2d cp { rand() / (double(RAND_MAX) + 1.0f) * 200.0 - 100.0, rand() / (double(RAND_MAX) + 1.0f) * 200.0
                      - 100.0 };
              Vec2d contour = prevf + cp.normalized()*4.0; // limits the cnotour edge len to 4mm
              lines[0].push_back(Point::new_scale(contour));
              linesf.emplace_back(prevf, contour);
              prevf = linesf.back().b;

              Vec2d qp { rand() / (double(RAND_MAX) + 1.0f) * 200.0 - 100.0, rand() / (double(RAND_MAX) + 1.0f) * 200.0
                      - 100.0 };
              query_pointsf.push_back(qp);
              query_points.push_back(Point::new_scale(qp));
          }

          std::cout << "Test for point count: " << count << std::endl;
          {
              high_resolution_clock::time_point t1 = high_resolution_clock::now();
              auto tree = AABBTreeLines::build_aabb_tree_over_indexed_lines(linesf);
              for (const Vec2d &qp : query_pointsf) {
                  size_t hit_idx_out;
                  Vec2d hit_point_out;
                  AABBTreeLines::squared_distance_to_indexed_lines(linesf, tree, qp, hit_idx_out, hit_point_out);
              }
              high_resolution_clock::time_point t2 = high_resolution_clock::now();
              duration<double> time_span = duration_cast<duration<double>>(t2 - t1);
              std::cout << "    Tree     took " << time_span.count() << " seconds." << std::endl;
          }

          {
              high_resolution_clock::time_point t1 = high_resolution_clock::now();
              EdgeGrid::Grid grid1 { };
              grid1.create(lines, scaled(1.0), true);
              coord_t query1_res = scaled(1.0);
              grid1.calculate_sdf();
              for (const Point &qp : query_points) {
                  volatile auto dist = grid1.closest_point_signed_distance(qp, query1_res);
              }
              high_resolution_clock::time_point t2 = high_resolution_clock::now();
              duration<double> time_span = duration_cast<duration<double>>(t2 - t1);
              std::cout << "    Grid 1mm  took " << time_span.count() << " seconds." << std::endl;
          }

          {
              high_resolution_clock::time_point t1 = high_resolution_clock::now();
              EdgeGrid::Grid grid10 { };
              grid10.create(lines, scaled(10.0), true);
              coord_t query10_res = scaled(10.0);
              grid10.calculate_sdf();
              for (const Point &qp : query_points) {
                  volatile auto dist = grid10.closest_point_signed_distance(qp, query10_res);
              }
              high_resolution_clock::time_point t2 = high_resolution_clock::now();
              duration<double> time_span = duration_cast<duration<double>>(t2 - t1);
              std::cout << "    Grid 10mm took " << time_span.count() << " seconds." << std::endl;
          }

      }

}
#endif

