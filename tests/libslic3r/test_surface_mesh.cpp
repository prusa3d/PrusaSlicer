#include <catch2/catch_test_macros.hpp>
#include <test_utils.hpp>


#include <libslic3r/SurfaceMesh.hpp>

using namespace Slic3r;


// Generate a broken cube mesh. Face 8 is inverted, face 11 is missing.
indexed_triangle_set its_make_cube_broken(double xd, double yd, double zd)
{
    auto x = float(xd), y = float(yd), z = float(zd);
    return {
        { {0, 1, 2}, {0, 2, 3}, {4, 5, 6}, {4, 6, 7},
          {0, 4, 7}, {0, 7, 1}, {1, 7, 6}, {1, 6, 2},
          {2, 5, 6}, {2, 5, 3}, {4, 0, 3}  /*missing face*/ },
        { {x, y, 0}, {x, 0, 0}, {0, 0, 0}, {0, y, 0},
          {x, y, z}, {0, y, z}, {0, 0, z}, {x, 0, z} }
    };
}



TEST_CASE("SurfaceMesh on a cube", "[SurfaceMesh]") {
    indexed_triangle_set cube = its_make_cube(1., 1., 1.);
    SurfaceMesh sm(cube);
    const Halfedge_index hi_first = sm.halfedge(Face_index(0));
    Halfedge_index hi = hi_first;

    REQUIRE(! hi_first.is_invalid());
    
    SECTION("next / prev halfedge") {
        hi = sm.next(hi);
        REQUIRE(hi != hi_first);
        hi = sm.next(hi);
        hi = sm.next(hi);
        REQUIRE(hi == hi_first);
        hi = sm.prev(hi);
        REQUIRE(hi != hi_first);
        hi = sm.prev(hi);
        hi = sm.prev(hi);
        REQUIRE(hi == hi_first);
    }

    SECTION("next_around_target") {
        // Check that we get to the same halfedge after applying next_around_target
        // four times.
        const Vertex_index target_vert = sm.target(hi_first);
        for (int i=0; i<4;++i) {
            hi = sm.next_around_target(hi);
            REQUIRE((hi == hi_first) == (i == 3));
            REQUIRE(sm.is_same_vertex(sm.target(hi), target_vert));
            REQUIRE(! sm.is_border(hi));
        }
    }

    SECTION("iterate around target and source") {
        hi = sm.next_around_target(hi);
        hi = sm.prev_around_target(hi);
        hi = sm.prev_around_source(hi);
        hi = sm.next_around_source(hi);
        REQUIRE(hi == hi_first);
    }

    SECTION("opposite") {
        const Vertex_index target = sm.target(hi);
        const Vertex_index source = sm.source(hi);
        hi = sm.opposite(hi);
        REQUIRE(sm.is_same_vertex(target, sm.source(hi)));
        REQUIRE(sm.is_same_vertex(source, sm.target(hi)));
        hi = sm.opposite(hi);
        REQUIRE(hi == hi_first);
    }

    SECTION("halfedges walk") {
        for (int i=0; i<4; ++i) {
            hi = sm.next(hi);
            hi = sm.opposite(hi);
        }
        REQUIRE(hi == hi_first);
    }

    SECTION("point accessor") {
        Halfedge_index hi = sm.halfedge(Face_index(0));
        hi = sm.opposite(hi);
        hi = sm.prev(hi);
        hi = sm.opposite(hi);
        REQUIRE(hi.face() == Face_index(6));
        REQUIRE(sm.point(sm.target(hi)).isApprox(cube.vertices[7])); 
    }
}




TEST_CASE("SurfaceMesh on a broken cube", "[SurfaceMesh]") {
    indexed_triangle_set cube = its_make_cube_broken(1., 1., 1.);
    SurfaceMesh sm(cube);
    
    SECTION("Check inverted face") {
        Halfedge_index hi = sm.halfedge(Face_index(8));
        for (int i=0; i<3; ++i) {
            REQUIRE(! hi.is_invalid());
            REQUIRE(sm.is_border(hi));
        }
        REQUIRE(hi == sm.halfedge(Face_index(8)));
        hi = sm.opposite(hi);
        REQUIRE(hi.is_invalid());
    }

    SECTION("missing face") {
        Halfedge_index hi = sm.halfedge(Face_index(0));
        for (int i=0; i<3; ++i)
            hi = sm.next_around_source(hi);
        hi = sm.next(hi);
        REQUIRE(sm.is_border(hi));
        REQUIRE(! hi.is_invalid());
        hi = sm.opposite(hi);
        REQUIRE(hi.is_invalid());
    }
}
