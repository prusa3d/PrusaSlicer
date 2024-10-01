#include <catch2/catch.hpp>
#include <igl/qslim.h>
#include <test_utils.hpp>

#include <libslic3r/QuadricEdgeCollapse.hpp>
#include <libslic3r/TriangleMesh.hpp> // its - indexed_triangle_set
#include "libslic3r/AABBTreeIndirect.hpp" // is similar

using namespace Slic3r;

namespace Private {

struct Similarity
{
    float max_distance = 0.f;
    float average_distance = 0.f;

    Similarity() = default;
    Similarity(float max_distance, float average_distance)
        : max_distance(max_distance), average_distance(average_distance)
    {}
};

// border for our algorithm with frog_leg model and decimation to 5%
Similarity frog_leg_5(0.32f, 0.043f);

Similarity get_similarity(const indexed_triangle_set &from,
                             const indexed_triangle_set &to)
{
    // create ABBTree
    auto tree = AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(
        from.vertices, from.indices);
    float sum_distance = 0.f;
    
    float max_distance = 0.f;
    auto collect_distances = [&](const Vec3f &surface_point) {
        size_t hit_idx;
        Vec3f  hit_point;
        float  distance2 =
            AABBTreeIndirect::squared_distance_to_indexed_triangle_set(
                from.vertices, from.indices, tree, surface_point, hit_idx,
                hit_point);
        float distance = sqrt(distance2);
        if (max_distance < distance) max_distance = distance;
        sum_distance += distance;
    };

    for (const Vec3f &vertex : to.vertices) { collect_distances(vertex); }
    for (const Vec3i &t : to.indices) {
        Vec3f center(0, 0, 0);
        for (size_t i = 0; i < 3; ++i) { center += to.vertices[t[i]] / 3; }
        collect_distances(center);
    }

    size_t count = to.vertices.size() + to.indices.size();
    float average_distance = sum_distance / count;

    std::cout << "max_distance = " << max_distance << ", average_distance = " << average_distance << std::endl;
    return Similarity(max_distance, average_distance);
}

void is_better_similarity(const indexed_triangle_set &its_first,
                          const indexed_triangle_set &its_second,
                          const Similarity &          compare)
{
    Similarity s1 = get_similarity(its_first, its_second);
    Similarity s2 = get_similarity(its_second, its_first);

    CHECK(s1.average_distance < compare.average_distance);
    CHECK(s1.max_distance     < compare.max_distance);
    CHECK(s2.average_distance < compare.average_distance);
    CHECK(s2.max_distance     < compare.max_distance);
}

void is_worse_similarity(const indexed_triangle_set &its_first,
                         const indexed_triangle_set &its_second,
                         const Similarity &          compare)
{
    Similarity s1 = get_similarity(its_first, its_second);
    Similarity s2 = get_similarity(its_second, its_first);

    if (s1.max_distance < compare.max_distance &&
        s2.max_distance < compare.max_distance)
        CHECK(false);
}
    
bool exist_triangle_with_twice_vertices(const std::vector<stl_triangle_vertex_indices> &indices)
{
    for (const auto &face : indices)
        if (face[0] == face[1] || face[0] == face[2] || face[1] == face[2])
            return true;
    return false;
}

} // namespace Private

TEST_CASE("Reduce one edge by Quadric Edge Collapse", "[its]")
{
    indexed_triangle_set its;
    its.vertices = {Vec3f(-1.f, 0.f, 0.f), Vec3f(0.f, 1.f, 0.f),
                    Vec3f(1.f, 0.f, 0.f), Vec3f(0.f, 0.f, 1.f),
                    // vertex to be removed
                    Vec3f(0.9f, .1f, -.1f)};
    its.indices  = {Vec3i(1, 0, 3), Vec3i(2, 1, 3), Vec3i(0, 2, 3),
                   Vec3i(0, 1, 4), Vec3i(1, 2, 4), Vec3i(2, 0, 4)};
    // edge to remove is between vertices 2 and 4 on trinagles 4 and 5

    indexed_triangle_set its_ = its; // copy
    // its_write_obj(its, "tetrhedron_in.obj");
    uint32_t wanted_count = its.indices.size() - 1;
    its_quadric_edge_collapse(its, wanted_count);
    // its_write_obj(its, "tetrhedron_out.obj");
    CHECK(its.indices.size() == 4);
    CHECK(its.vertices.size() == 4);

    for (size_t i = 0; i < 3; i++) {
        CHECK(its.indices[i] == its_.indices[i]);
    }

    for (size_t i = 0; i < 4; i++) {
        if (i == 2) continue;
        CHECK(its.vertices[i] == its_.vertices[i]);
    }

    const Vec3f &v  = its.vertices[2];  // new vertex
    const Vec3f &v2 = its_.vertices[2]; // moved vertex
    const Vec3f &v4 = its_.vertices[4]; // removed vertex
    for (size_t i = 0; i < 3; i++) {
        bool is_between = (v[i] < v4[i] && v[i] > v2[i]) ||
                          (v[i] > v4[i] && v[i] < v2[i]);
        CHECK(is_between);
    }
    Private::Similarity max_similarity(0.75f, 0.014f);
    Private::is_better_similarity(its, its_, max_similarity);
}

static bool is_equal(const std::vector<stl_vertex> &v1,
                     const std::vector<stl_vertex> &v2,
                     float epsilon = std::numeric_limits<float>::epsilon())
{
    // is same count?
    if (v1.size() != v2.size()) return false;

    // check all v1 vertices
    for (const auto &v1_ : v1) {
        auto is_equal = [&v1_, epsilon](const auto &v2_) {
            for (size_t i = 0; i < 3; i++)
                if (fabs(v1_[i] - v2_[i]) > epsilon)
                    return false;
            return true;
        };
        // is v1 vertex in v2 vertices?
        if(std::find_if(v2.begin(), v2.end(), is_equal) == v2.end()) return false;
    }
    return true;
}

TEST_CASE("Reduce to one triangle by Quadric Edge Collapse", "[its]")
{
    // !!! Not work (no manifold - open edges{0-1, 1-2, 2-4, 4-5, 5-3, 3-0}):
    /////////////image////
    //    * 5           //
    //    |\            //
    //    | \           //
    //  3 *--* 4        //
    //    | /|\         //
    //    |/ | \        //
    //  0 *--*--* 2     //
    //       1          //
    //////////////////////
    // all triangles are on a plane therefore quadric is zero and
    // when reduce edge between vertices 3 and 4 new vertex lay on vertex 3 not 4 !!!

    indexed_triangle_set its;
    its.vertices = {Vec3f(0.f, 0.f, 0.f), Vec3f(1.f, 0.f, 0.f),
                    Vec3f(2.f, 0.f, 0.f), Vec3f(0.f, 1.f, 0.f),
                    Vec3f(1.f, 1.f, 0.f), Vec3f(0.f, 2.f, 0.f)};
    its.indices  = {Vec3i(0, 1, 4), Vec3i(1, 2, 4), Vec3i(0, 4, 3),
                   Vec3i(3, 4, 5)};
    std::vector<stl_vertex> triangle_vertices = {its.vertices[0],
                                                 its.vertices[2],
                                                 its.vertices[5]};

    uint32_t wanted_count = 1;
    its_quadric_edge_collapse(its, wanted_count);    
    // result should be one triangle made of vertices 0, 2, 5
    
    // NOT WORK
    //CHECK(its.indices.size() == wanted_count);
    //// check all triangle vertices
    //CHECK(is_equal(its.vertices, triangle_vertices));
}

TEST_CASE("Reduce to one tetrahedron by Quadric Edge Collapse", "[its]")
{
    // Extend previous test to tetrahedron to make it manifold
    indexed_triangle_set its;
    its.vertices = {
        Vec3f(0.f, 0.f, 0.f), Vec3f(1.f, 0.f, 0.f), Vec3f(2.f, 0.f, 0.f),
        Vec3f(0.f, 1.f, 0.f), Vec3f(1.f, 1.f, 0.f), 
        Vec3f(0.f, 2.f, 0.f)
        // tetrahedron extetion
        , Vec3f(0.f, 0.f, -2.f)
    };
    std::vector<stl_vertex> tetrahedron_vertices = {its.vertices[0],
                                                    its.vertices[2],
                                                    its.vertices[5],
                                                    // tetrahedron extetion
                                                    its.vertices[6]};
    its.indices  = {Vec3i(0, 1, 4), Vec3i(1, 2, 4), Vec3i(0, 4, 3), Vec3i(3, 4, 5),
        // tetrahedron extetion
        Vec3i(4, 2, 6), Vec3i(5, 4, 6), Vec3i(3, 5, 6), Vec3i(0, 3, 6), Vec3i(1, 0, 6),  Vec3i(2, 1, 6)
    };
    uint32_t wanted_count = 4;

    //its_write_obj(its, "tetrhedron_in.obj");
    its_quadric_edge_collapse(its, wanted_count);
    //its_write_obj(its, "tetrhedron_out.obj");

    // result should be tetrahedron
    CHECK(its.indices.size() == wanted_count);
    // check all tetrahedron  vertices
    CHECK(is_equal(its.vertices, tetrahedron_vertices));
}

TEST_CASE("Simplify frog_legs.obj to 5% by Quadric edge collapse", "[its][quadric_edge_collapse]")
{
    TriangleMesh mesh            = load_model("frog_legs.obj");
    double       original_volume = its_volume(mesh.its);
    uint32_t     wanted_count    = mesh.its.indices.size() * 0.05;
    REQUIRE_FALSE(mesh.empty());
    indexed_triangle_set its       = mesh.its; // copy
    float                max_error = std::numeric_limits<float>::max();
    its_quadric_edge_collapse(its, wanted_count, &max_error);
    // its_write_obj(its, "frog_legs_qec.obj");
    CHECK(its.indices.size() <= wanted_count);
    double volume = its_volume(its);
    CHECK(fabs(original_volume - volume) < 33.);

    Private::is_better_similarity(mesh.its, its, Private::frog_leg_5);
}

TEST_CASE("Simplify frog_legs.obj to 5% by IGL/qslim", "[]")
{
    std::string  obj_filename    = "frog_legs.obj";
    TriangleMesh mesh            = load_model(obj_filename);
    REQUIRE_FALSE(mesh.empty());
    indexed_triangle_set &its = mesh.its;
    //double       original_volume = its_volume(its);
    uint32_t     wanted_count    = its.indices.size() * 0.05;
    
    Eigen::MatrixXd V(its.vertices.size(), 3);
    Eigen::MatrixXi F(its.indices.size(), 3);
    for (size_t j = 0; j < its.vertices.size(); ++j) {
        Vec3d vd = its.vertices[j].cast<double>();
        for (int i = 0; i < 3; ++i) V(j, i) = vd(i);
    }

    for (size_t j = 0; j < its.indices.size(); ++j) {
        const auto &f = its.indices[j];
        for (int i = 0; i < 3; ++i) F(j, i) = f(i);
    }

    size_t max_m = wanted_count;
    Eigen::MatrixXd U;
    Eigen::MatrixXi G;
    Eigen::VectorXi J, I;
    CHECK(igl::qslim(V, F, max_m, U, G, J, I));

    // convert to its
    indexed_triangle_set its_out;
    its_out.vertices.reserve(U.size()/3);
    its_out.indices.reserve(G.size()/3);
    size_t U_size = U.size() / 3;
    for (size_t i = 0; i < U_size; i++)
        its_out.vertices.emplace_back(U(i, 0), U(i, 1), U(i, 2));
    size_t G_size = G.size() / 3;
    for (size_t i = 0; i < G_size; i++)
        its_out.indices.emplace_back(G(i, 0), G(i, 1), G(i, 2));

    // check if algorithm is still worse than our
    Private::is_worse_similarity(its_out, its, Private::frog_leg_5);
    // its_out, its --> avg_distance: 0.0351217, max_distance 0.364316
    // its, its_out --> avg_distance: 0.0412358, max_distance 0.238913
}

TEST_CASE("Simplify trouble case", "[its]")
{
    TriangleMesh tm = load_model("simplification.obj");
    REQUIRE_FALSE(tm.empty());
    float    max_error    = std::numeric_limits<float>::max();
    uint32_t wanted_count = 0;
    its_quadric_edge_collapse(tm.its, wanted_count, &max_error);
    CHECK(!Private::exist_triangle_with_twice_vertices(tm.its.indices));
}

TEST_CASE("Simplified cube should not be empty.", "[its]")
{
    auto     its          = its_make_cube(1, 2, 3);
    float    max_error    = std::numeric_limits<float>::max();
    uint32_t wanted_count = 0;
    its_quadric_edge_collapse(its, wanted_count, &max_error);
    CHECK(!its.indices.empty());
}
