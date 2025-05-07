#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <test_utils.hpp>

#include <algorithm>

#include "libslic3r/TriangleMeshSlicer.hpp"
#include "libslic3r/SLA/ZCorrection.hpp"
#include "libslic3r/MTUtils.hpp"
#include "libslic3r/SVG.hpp"

using Catch::Approx;

void print_depthmap(std::string_view prefix,
                    const Slic3r::BoundingBox &bb,
                    const Slic3r::sla::zcorr_detail::DepthMap &dm)
{
    using namespace Slic3r;

    size_t cnt = 0;
    for (const sla::zcorr_detail::DepthMapLayer &layer : dm) {
        SVG svg(std::string(prefix) + std::to_string(cnt++) + ".svg", bb);
        for (const auto &[depth, dpolys] : layer) {
            svg.draw_outline(dpolys);
            svg.draw(dpolys, "green", 1. + depth / 10.f);
        }
    }
}

TEST_CASE("Number of layers should be equal after z correction", "[ZCorr]")
{
    using namespace Slic3r;

    const size_t Layers = random_value(size_t{1}, size_t{100});
    INFO("Layers = " << Layers);

    float zcorr_depth = GENERATE(0.f, random_value(0.01f, 10.f));

    std::vector<ExPolygons> slices(Layers);
    std::vector<float> hgrid = grid(0.f, Layers * 1.f, 1.f);

    std::vector<ExPolygons> output = sla::apply_zcorrection(slices, hgrid, zcorr_depth);

    REQUIRE(slices.size() == output.size());
}

TEST_CASE("Testing DepthMap for a cube", "[ZCorr]")
{
    using namespace Slic3r;

    TriangleMesh mesh = load_model("20mm_cube.obj");
    auto bb = bounding_box(mesh);
    bb.offset(-0.1);

    std::vector<float> hgrid = grid<float>(bb.min.z(), bb.max.z(), 1.f);

    std::vector<ExPolygons> slices = slice_mesh_ex(mesh.its, hgrid, {});

    sla::zcorr_detail::DepthMap dmap = sla::zcorr_detail::create_depthmap(slices, hgrid);

    REQUIRE(dmap.size() == slices.size());

    for (size_t i = 0; i < slices.size(); ++i) {
        const auto &dlayer = dmap[i];
        const ExPolygons &slayer = slices[i];
        REQUIRE(dlayer.size() == 1);
        REQUIRE(dlayer.begin()->first == i);
        double ad = area(dlayer.begin()->second);
        double as = area(slayer);
        REQUIRE(ad == Approx(as).margin(EPSILON));
    }
}

TEST_CASE("Testing DepthMap for arbitrary shapes", "[ZCorr]")
{
    using namespace Slic3r;

    auto modelname = GENERATE("V_standing.obj", "A_upsidedown.obj");

    TriangleMesh mesh = load_model(modelname);
    auto bb = bounding_box(mesh);
    bb.offset(-0.1);

    std::vector<float> hgrid = grid<float>(bb.min.z(), bb.max.z(), 0.5f);

    std::vector<ExPolygons> slices = slice_mesh_ex(mesh.its, hgrid, {});

    size_t zcorr_layers = GENERATE(size_t{0}, random_value(size_t{1}, size_t{10}));

    sla::zcorr_detail::DepthMap dmap =
        sla::zcorr_detail::create_depthmap(slices, hgrid, zcorr_layers);

#ifndef NDEBUG
    print_depthmap("debug_dmap", scaled(to_2d(bb)), dmap);
#endif

    REQUIRE(dmap.size() == slices.size());

    auto corrslices_fast = sla::apply_zcorrection(slices, zcorr_layers);
    sla::zcorr_detail::apply_zcorrection(dmap, zcorr_layers);

    for (size_t i = 0; i < corrslices_fast.size(); ++i) {
        ExPolygons dlayer = sla::zcorr_detail::merged_layer(dmap[i]);
        const ExPolygons &slayer = corrslices_fast[i];
        double ad = area(dlayer);
        double as = area(slayer);
        REQUIRE(ad == Approx(as).margin(EPSILON));
    }
}

TEST_CASE("Test depth to layers calculation", "[ZCorr]") {
    using namespace Slic3r;

    float layer_h = 0.5f;
    std::vector<float> hgrid = grid(0.f, 100.f, layer_h);

    float depth = GENERATE(0.f,
                           random_value(0.01f, 0.499f),
                           0.5f,
                           random_value(0.501f, 10.f));

    for (size_t i = 0; i < hgrid.size(); ++i) {
        auto expected_lyrs = std::min(i, static_cast<size_t>(std::ceil(depth/layer_h)));
        REQUIRE(sla::zcorr_detail::depth_to_layers(hgrid, i, depth) == expected_lyrs);
    }
}
