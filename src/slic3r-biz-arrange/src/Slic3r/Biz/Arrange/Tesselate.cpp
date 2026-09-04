
#include "Slic3r/Biz/Algorithms/ClipperUtils.hpp"
#include "Slic3r/Biz/Arrange/NFP.hpp"
#include "Slic3r/Biz/Algorithms/Tesselate.hpp"
#include "Slic3r/Biz/Algorithms/Scaling.hpp"
#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Domain/Polygon.hpp"

#include <iterator>
#include <vector>
#include <cstddef>

#include "Tesselate.hpp"

namespace Slic3r::Biz::Arrange {

using Biz::Algorithms::ClipperUtils::union_ex;
using Domain::ExPolygon;
using Domain::ExPolygons;
using Domain::Polygon;
using Domain::Polygons;
using Domain::Vec2crd;
using Domain::Vec2d;

Polygons convex_decomposition_tess(const Polygon& expoly)
{
    return convex_decomposition_tess(ExPolygon{expoly});
}

Polygons convex_decomposition_tess(const ExPolygon& expoly)
{
    using Slic3r::Biz::Algorithms::Tesselate::triangulate_expolygon_2d;
    std::vector<Vec2d> tr = triangulate_expolygon_2d(expoly);

    Polygons ret;
    ret.reserve(tr.size() / 3);
    for (size_t i = 0; i < tr.size(); i += 3) {
        ret.emplace_back(
            Polygon{
                Biz::Algorithms::Scaling::scaled(tr[i]),
                Biz::Algorithms::Scaling::scaled(tr[i + 1]),
                Biz::Algorithms::Scaling::scaled(tr[i + 2])
            }
        );
    }

    return ret;
}

Polygons convex_decomposition_tess(const ExPolygons& expolys)
{
    constexpr size_t AvgTriangleCountGuess = 50;

    Polygons ret;
    ret.reserve(AvgTriangleCountGuess * expolys.size());
    for (const ExPolygon& expoly : expolys) {
        Polygons convparts = convex_decomposition_tess(expoly);
        std::move(convparts.begin(), convparts.end(), std::back_inserter(ret));
    }

    return ret;
}

ExPolygons nfp_concave_concave_tess(const ExPolygon& fixed, const ExPolygon& movable)
{
    Polygons fixed_decomp   = convex_decomposition_tess(fixed);
    Polygons movable_decomp = convex_decomposition_tess(movable);

    std::vector<Vec2crd> refs_mv;
    refs_mv.reserve(movable_decomp.size());

    for (const Polygon& p : movable_decomp)
        refs_mv.emplace_back(reference_vertex(p));

    Polygons nfps;
    nfps.reserve(fixed_decomp.size() * movable_decomp.size());

    Vec2crd ref_whole = reference_vertex(movable);
    for (const Polygon& fixed_part : fixed_decomp) {
        size_t mvi = 0;
        for (const Polygon& movable_part : movable_decomp) {
            Polygon subnfp        = nfp_convex_convex(fixed_part, movable_part);
            const Vec2crd& ref_mp = refs_mv[mvi];
            auto d                = ref_whole - ref_mp;
            subnfp.translate(d);
            nfps.emplace_back(subnfp);
            mvi++;
        }
    }

    return union_ex(nfps);
}

} // namespace Slic3r::Biz::Arrange
