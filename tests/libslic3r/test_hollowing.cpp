#include <iostream>
#include <fstream>
#include <catch2/catch_test_macros.hpp>

#include "libslic3r/SLA/Hollowing.hpp"

TEST_CASE("Hollow two overlapping spheres") {
    namespace triangle_mesh = Slic3r::Biz::Algorithms::TriangleMesh;
    using namespace Slic3r;

    Domain::TriangleMesh sphere1 = triangle_mesh::make_sphere(10., 2 * PI / 20.), sphere2 = sphere1;

    sphere1.translate(Vec3f{-5.f, 0.f, 0.f});
    sphere2.translate(Vec3f{5.f, 0.f, 0.f});

    sphere1.merge(sphere2);

    sla::hollow_mesh(sphere1, sla::HollowingConfig{}, sla::HollowingFlags::hfRemoveInsideTriangles);

    triangle_mesh::write_obj_file(sphere1, "twospheres.obj");
}

