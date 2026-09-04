#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "libslic3r/TriangleMeshSlicer.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/libslic3r.h"

#include <algorithm>
#include <future>
#include <chrono>

//#include "test_options.hpp"
#include "test_data.hpp"

using namespace Slic3r;
using namespace std;
namespace triangle_mesh = Slic3r::Biz::Algorithms::TriangleMesh;
using Domain::TriangleMesh;
using Domain::Index3;

static inline TriangleMesh make_cube() { return triangle_mesh::make_cube(20., 20, 20); }

SCENARIO( "TriangleMesh: Basic mesh statistics") {
    GIVEN( "A 20mm cube, built from constexpr std::array" ) {
        std::vector<Vec3f> vertices { {20,20,0}, {20,0,0}, {0,0,0}, {0,20,0}, {20,20,20}, {0,20,20}, {0,0,20}, {20,0,20} };
        std::vector<Index3> facets { {0,1,2}, {0,2,3}, {4,5,6}, {4,6,7}, {0,4,7}, {0,7,1}, {1,7,6}, {1,6,2}, {2,6,5}, {2,5,3}, {4,0,3}, {4,3,5} };

        using Biz::Algorithms::TriangleMesh::construct;
        TriangleMesh cube(construct(vertices, facets));

        THEN( "Volume is appropriate for 20mm square cube.") {
            REQUIRE(abs(cube.volume() - 20.0*20.0*20.0) < 1e-2);
        }

        THEN( "Vertices array matches input.") {
            for (size_t i = 0U; i < cube.its.vertices.size(); i++) {
                REQUIRE(cube.its.vertices.at(i) == vertices.at(i).cast<float>());
            }
            for (size_t i = 0U; i < vertices.size(); i++) {
                REQUIRE(vertices.at(i).cast<float>() == cube.its.vertices.at(i));
            }
        }
        THEN( "Vertex count matches vertex array size.") {
            REQUIRE(cube.facets_count() == facets.size());
        }

        THEN( "Facet array matches input.") {
            for (size_t i = 0U; i < cube.its.indices.size(); i++) {
                REQUIRE(cube.its.indices.at(i) == facets.at(i));
            }

            for (size_t i = 0U; i < facets.size(); i++) {
                REQUIRE(facets.at(i) == cube.its.indices.at(i));
            }
        }
        THEN( "Facet count matches facet array size.") {
            REQUIRE(cube.facets_count() == facets.size());
        }

#if 0
        THEN( "Number of normals is equal to the number of facets.") {
            REQUIRE(cube.normals().size() == facets.size());
        }
#endif

        THEN( "center() returns the center of the object.") {
            REQUIRE(triangle_mesh::center(cube) == Vec3d(10.0,10.0,10.0));
        }

        THEN( "Size of cube is (20,20,20)") {
            REQUIRE(cube.size() == Vec3d(20,20,20));
        }

    }
}

SCENARIO( "TriangleMesh: Transformation functions affect mesh as expected.") {
    GIVEN( "A 20mm cube with one corner on the origin") {
        auto cube = make_cube();

        WHEN( "The cube is scaled 200% uniformly") {
            cube.scale(2.0);
            THEN( "The volume is equivalent to 40x40x40 (all dimensions increased by 200%") {
                REQUIRE(abs(cube.volume() - 40.0*40.0*40.0) < 1e-2);
            }
        }
        WHEN( "The resulting cube is scaled 200% in the X direction") {
            cube.scale(Vec3f(2.0, 1, 1));
            THEN( "The volume is doubled.") {
                REQUIRE(abs(cube.volume() - 2*20.0*20.0*20.0) < 1e-2);
            }
            THEN( "The X coordinate size is 200%.") {
                REQUIRE(cube.its.vertices.at(0).x() == 40.0);
            }
        }

        WHEN( "The cube is scaled 25% in the X direction") {
            cube.scale(Vec3f(0.25, 1, 1));
            THEN( "The volume is 25% of the previous volume.") {
                REQUIRE(abs(cube.volume() - 0.25*20.0*20.0*20.0) < 1e-2);
            }
            THEN( "The X coordinate size is 25% from previous.") {
                REQUIRE(cube.its.vertices.at(0).x() == 5.0);
            }
        }

        WHEN( "The cube is rotated 45 degrees.") {
            cube.rotate(float(M_PI / 4.), Domain::Axis::Z);
            THEN( "The X component of the size is sqrt(2)*20") {
                REQUIRE(abs(cube.size().x() - sqrt(2.0)*20) < 1e-2);
            }
        }

        WHEN( "The cube is translated (5, 10, 0) units with a Vec3f ") {
            cube.translate(Vec3f(5.0, 10.0, 0.0));
            THEN( "The first vertex is located at 25, 30, 0") {
                REQUIRE(cube.its.vertices.at(0) == Vec3f(25.0, 30.0, 0.0));
            }
        }

        WHEN( "The cube is translated (5, 10, 0) units with 3 doubles") {
            cube.translate(Vec3f{5.0, 10.0, 0.0});
            THEN( "The first vertex is located at 25, 30, 0") {
                REQUIRE(cube.its.vertices.at(0) == Vec3f(25.0, 30.0, 0.0));
            }
        }
    }
}

SCENARIO( "make_xxx functions produce meshes.") {
    GIVEN("make_cube() function") {
        WHEN("make_cube() is called with arguments 20,20,20") {
			TriangleMesh cube = triangle_mesh::make_cube(20,20,20);
            THEN("The resulting mesh has one and only one vertex at 0,0,0") {
                const std::vector<Vec3f> &verts = cube.its.vertices;
                REQUIRE(std::count_if(verts.begin(), verts.end(), [](const Vec3f& t) { return t.x() == 0 && t.y() == 0 && t.z() == 0; } ) == 1);
            }
            THEN("The mesh volume is 20*20*20") {
                REQUIRE(abs(cube.volume() - 20.0*20.0*20.0) < 1e-2);
            }
            THEN("There are 12 facets.") {
                REQUIRE(cube.its.indices.size() == 12);
            }
        }
    }
    GIVEN("make_cylinder() function") {
        WHEN("make_cylinder() is called with arguments 10,10, PI / 3") {
            TriangleMesh cyl = triangle_mesh::make_cylinder(10, 10, PI / 243.0);
            double angle = (2*PI / floor(2*PI / (PI / 243.0)));
            THEN("The resulting mesh has one and only one vertex at 0,0,0") {
                const std::vector<Vec3f> &verts = cyl.its.vertices;
                REQUIRE(std::count_if(verts.begin(), verts.end(), [](const Vec3f& t) { return t.x() == 0 && t.y() == 0 && t.z() == 0; } ) == 1);
            }
            THEN("The resulting mesh has one and only one vertex at 0,0,10") {
                const std::vector<Vec3f> &verts = cyl.its.vertices;
                REQUIRE(std::count_if(verts.begin(), verts.end(), [](const Vec3f& t) { return t.x() == 0 && t.y() == 0 && t.z() == 10; } ) == 1);
            }
            THEN("Resulting mesh has 2 + (2*PI/angle * 2) vertices.") { 
                REQUIRE(cyl.its.vertices.size() == (2 + ((2*PI/angle)*2)));
            }
            THEN("Resulting mesh has 2*PI/angle * 4 facets") {
                REQUIRE(cyl.its.indices.size() == (2*PI/angle)*4);
            }
            THEN( "The mesh volume is approximately 10pi * 10^2") {
                REQUIRE(abs(cyl.volume() - (10.0 * M_PI * std::pow(10,2))) < 1);
            }
        }
    }

    GIVEN("make_sphere() function") {
        WHEN("make_sphere() is called with arguments 10, PI / 3") {
            TriangleMesh sph = triangle_mesh::make_sphere(10, PI / 243.0);
            THEN( "Edge length is smaller than limit but not smaller than half of it") {
                double len = (sph.its.vertices[sph.its.indices[0][0]] - sph.its.vertices[sph.its.indices[0][1]]).norm();
                double limit = 10*PI/243.;
                REQUIRE(len <= limit);
                REQUIRE(len >= limit/2.);
            }
            THEN( "Vertices are about the correct distance from the origin") {
                bool all_vertices_ok = std::all_of(sph.its.vertices.begin(), sph.its.vertices.end(),
                    [](const stl_vertex& pt) { return is_approx(pt.squaredNorm(), 100.f); });
                REQUIRE(all_vertices_ok);
            }
            THEN( "The mesh volume is approximately 4/3 * pi * 10^3") {
                REQUIRE(abs(sph.volume() - (4.0/3.0 * M_PI * std::pow(10,3))) < 1); // 1% tolerance?
            }
        }
    }
}

using Slic3r::Biz::Algorithms::BoundingBox::approx_equals;

SCENARIO( "TriangleMesh: split functionality.") {
    GIVEN( "A 20mm cube with one corner on the origin") {
		auto cube = make_cube();
        WHEN( "The mesh is split into its component parts.") {
            std::vector<TriangleMesh> meshes = triangle_mesh::split(cube);
            THEN(" The bounding box statistics are propagated to the split copies") {
                REQUIRE(meshes.size() == 1);
                REQUIRE(approx_equals(meshes.front().bounding_box(), cube.bounding_box()));
            }
        }
    }
    GIVEN( "Two 20mm cubes, each with one corner on the origin, merged into a single TriangleMesh") {
		auto cube = make_cube();
		TriangleMesh cube2(cube);

        cube.merge(cube2);
        WHEN( "The combined mesh is split") {
            THEN( "Number of faces is 2x the source.") {
                REQUIRE(cube.facets_count() == 2 * cube2.facets_count());
            }
            std::vector<TriangleMesh> meshes = triangle_mesh::split(cube);
            THEN( "Two meshes are in the output vector.") {
                REQUIRE(meshes.size() == 2);
            }
        }
    }
}

SCENARIO( "TriangleMesh: Mesh merge functions") {
    GIVEN( "Two 20mm cubes, each with one corner on the origin") {
		auto cube = make_cube();
		TriangleMesh cube2(cube);

        WHEN( "The two meshes are merged") {
            cube.merge(cube2);
            THEN( "There are twice as many facets in the merged mesh as the original.") {
                REQUIRE(cube.facets_count() == 2 * cube2.facets_count());
            }
        }
    }
}

SCENARIO( "TriangleMeshSlicer: Cut behavior.") {
    GIVEN( "A 20mm cube with one corner on the origin") {
		auto cube = make_cube();
        WHEN( "Object is cut at the bottom") {
            indexed_triangle_set upper {};
            indexed_triangle_set lower {};
            cut_mesh(cube.its, 0, &upper, &lower);
            THEN("Upper mesh has all facets except those belonging to the slicing plane.") {
                REQUIRE(upper.indices.size() == 12);
            }
            THEN("Lower mesh has no facets.") {
                REQUIRE(lower.indices.size() == 0);
            }
        }
        WHEN( "Object is cut at the center") {
            indexed_triangle_set upper {};
            indexed_triangle_set lower {};
            cut_mesh(cube.its, 10, &upper, &lower);
            THEN("Upper mesh has 2 external horizontal facets, 3 facets on each side, and 6 facets on the triangulated side (2 + 12 + 6).") {
                REQUIRE(upper.indices.size() == 2+12+6);
            }
            THEN("Lower mesh has 2 external horizontal facets, 3 facets on each side, and 6 facets on the triangulated side (2 + 12 + 6).") {
                REQUIRE(lower.indices.size() == 2+12+6);
            }
        }
    }

    GIVEN(
        "A large cube centered at origin with unusual vertex coordinates (regression test for float/double precision issue)"
    )
    {
        // This mesh triggered an assertion failure in cut_mesh due to float vs double
        // mismatch in unscaled() function that happens during refactoring.
        std::vector<Vec3f> vertices{
            {24.9459496f, 24.9459534f, -24.9459515f},
            {24.9459496f, -24.9459534f, -24.9459515f},
            {-24.9459496f, -24.9459534f, -24.9459515f},
            {-24.9459496f, 24.9459534f, -24.9459515f},
            {24.9459496f, 24.9459534f, 24.9459515f},
            {-24.9459496f, 24.9459534f, 24.9459515f},
            {-24.9459496f, -24.9459534f, 24.9459515f},
            {24.9459496f, -24.9459534f, 24.9459515f}
        };
        std::vector<Index3> facets{
            {0, 1, 2},
            {0, 2, 3},
            {4, 5, 6},
            {4, 6, 7},
            {0, 4, 7},
            {0, 7, 1},
            {1, 7, 6},
            {1, 6, 2},
            {2, 6, 5},
            {2, 5, 3},
            {4, 0, 3},
            {4, 3, 5}
        };

        indexed_triangle_set mesh;
        mesh.vertices = vertices;
        mesh.indices  = facets;

        WHEN("Object is cut at z=0 with triangulate_caps=true")
        {
            indexed_triangle_set upper{};
            indexed_triangle_set lower{};

            cut_mesh(mesh, 0.0f, &upper, &lower, true);

            THEN("Upper mesh is valid (has triangles)")
            {
                REQUIRE(upper.indices.size() > 0);
            }
            THEN("Lower mesh is valid (has triangles)")
            {
                REQUIRE(lower.indices.size() > 0);
            }
            THEN("Upper mesh has no open edges (is watertight)")
            {
                REQUIRE(triangle_mesh::its_num_open_edges(upper) == 0);
            }
            THEN("Lower mesh has no open edges (is watertight)")
            {
                REQUIRE(triangle_mesh::its_num_open_edges(lower) == 0);
            }
        }
    }
}
