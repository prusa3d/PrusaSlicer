#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <test_utils.hpp>

#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Domain/Types.hpp"

#include "Slic3r/Biz/CGAL/Algorithms/MeshBoolean.hpp"

using namespace Slic3r;
using namespace Catch;
using Domain::Index3;
using Domain::TriangleMesh;
using Domain::Vec3d;
using Domain::Vec3f;
namespace MeshBoolean = Biz::CGAL::Algorithms::MeshBoolean;

namespace triangle_mesh = Biz::Algorithms::TriangleMesh;

TEST_CASE("CGAL and TriangleMesh conversions", "[MeshBoolean]") {
    TriangleMesh sphere = triangle_mesh::make_sphere(1.);
    
    auto cgalmesh_ptr = MeshBoolean::cgal::triangle_mesh_to_cgal(sphere);
    
    REQUIRE(cgalmesh_ptr);
    REQUIRE(! MeshBoolean::cgal::does_self_intersect(*cgalmesh_ptr));
    
    TriangleMesh M = MeshBoolean::cgal::cgal_to_triangle_mesh(*cgalmesh_ptr);
    
    REQUIRE(M.its.vertices.size() == sphere.its.vertices.size());
    REQUIRE(M.its.indices.size() == sphere.its.indices.size());
    
    REQUIRE(M.volume() == Approx(sphere.volume()));
    
    REQUIRE(! MeshBoolean::cgal::does_self_intersect(M));
}

Vec3d calc_normal(const Index3 &triangle, const std::vector<Vec3f> &vertices)
{
    Vec3d v0 = vertices[triangle[0]].cast<double>();
    Vec3d v1 = vertices[triangle[1]].cast<double>();
    Vec3d v2 = vertices[triangle[2]].cast<double>();
    // n = triangle normal
    Vec3d n = (v1 - v0).cross(v2 - v0);
    n.normalize();
    return n;
}

TEST_CASE("Add TriangleMeshes", "[MeshBoolean]")
{
    TriangleMesh tm1 = triangle_mesh::make_sphere(1.6, 1.6);
    size_t init_size = tm1.its.indices.size();
    Vec3f move(5, -3, 7);
    move.normalize();
    tm1.translate(0.3 * move);
    //its_write_obj(tm1.its, "tm1.obj");
    TriangleMesh tm2 = triangle_mesh::make_cube(1., 1., 1.);
    //its_write_obj(tm2.its, "tm2.obj");
    MeshBoolean::cgal::plus(tm1, tm2);
    //its_write_obj(tm1.its, "test_add.obj");
    CHECK(tm1.its.indices.size() > init_size);
}
