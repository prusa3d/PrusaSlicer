#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/catch_approx.hpp>
#include "test_utils.hpp"

#include <libslic3r/Execution/ExecutionSeq.hpp>

#include <arrange/ArrangeBase.hpp>
#include <arrange/ArrangeFirstFit.hpp>
#include <arrange/NFP/PackStrategyNFP.hpp>
#include <arrange/NFP/RectangleOverfitPackingStrategy.hpp>
#include <arrange/NFP/Kernels/GravityKernel.hpp>
#include <arrange/NFP/Kernels/TMArrangeKernel.hpp>
#include <arrange/NFP/NFPConcave_Tesselate.hpp>

#include <arrange-wrapper/Items/SimpleArrangeItem.hpp>
#include <arrange-wrapper/Items/ArrangeItem.hpp>
#include <arrange-wrapper/Items/TrafoOnlyArrangeItem.hpp>

#include <libslic3r/Model.hpp>

#include <libslic3r/Optimize/BruteforceOptimizer.hpp>

#include <libslic3r/Geometry/ConvexHull.hpp>
#include <libslic3r/ClipperUtils.hpp>

#include "../data/prusaparts.hpp"

#include <libslic3r/SVG.hpp>
#include <libslic3r/BoostAdapter.hpp>

#include <boost/log/trivial.hpp>

#include <boost/geometry/geometries/polygon.hpp>
#include <boost/geometry/geometries/point_xy.hpp>
#include <boost/geometry/geometries/multi_polygon.hpp>
#include <boost/geometry/algorithms/convert.hpp>

#include <random>

using namespace Catch;

template<class ArrItem = Slic3r::arr2::ArrangeItem>
static std::vector<ArrItem> prusa_parts(double infl = 0.) {
    using namespace Slic3r;

    std::vector<ArrItem> ret;

    if(ret.empty()) {
        ret.reserve(PRUSA_PART_POLYGONS.size());
        for(auto& inp : PRUSA_PART_POLYGONS) {
            ExPolygons inp_cpy{ExPolygon(inp)};
            inp_cpy.back().contour.points.pop_back();

            std::reverse(inp_cpy.back().contour.begin(),
                         inp_cpy.back().contour.end());

            REQUIRE(inp_cpy.back().contour.is_counter_clockwise());

            if (infl > 0.)
                inp_cpy = offset_ex(inp_cpy, scaled(std::ceil(infl / 2.)));

            ArrItem item{Geometry::convex_hull(inp_cpy)};

            ret.emplace_back(std::move(item));
        }
    }

    return ret;
}

static std::vector<Slic3r::arr2::ArrangeItem> prusa_parts_ex(double infl = 0.)
{
    using namespace Slic3r;

    std::vector<arr2::ArrangeItem> ret;

    if (ret.empty()) {
        ret.reserve(PRUSA_PART_POLYGONS_EX.size());
        for (auto &inp : PRUSA_PART_POLYGONS_EX) {
            ExPolygons inp_cpy{inp};

            REQUIRE(std::all_of(inp_cpy.begin(), inp_cpy.end(),
                                [](const ExPolygon &p) {
                                    return p.contour.is_counter_clockwise();
                                }));

            if (infl > 0.)
                inp_cpy = offset_ex(inp_cpy, scaled(std::ceil(infl / 2.)));

            Point c = get_extents(inp_cpy).center();
            for (auto &p : inp_cpy)
                p.translate(-c);

            arr2::ArrangeItem item{inp_cpy};

            ret.emplace_back(std::move(item));
        }
    }

    return ret;
}

using Slic3r::arr2::ArrangeItem;
using Slic3r::arr2::DecomposedShape;

struct ItemPair {
    ArrangeItem orbiter;
    ArrangeItem stationary;
};

using Slic3r::scaled;
using Slic3r::Vec2f;

std::vector<ItemPair> nfp_testdata = {
    {
        ArrangeItem { DecomposedShape {
            scaled(Vec2f{80, 50}) ,
            scaled(Vec2f{120, 50}),
            scaled(Vec2f{100, 70}),
        }},
        ArrangeItem { DecomposedShape {
            scaled(Vec2f{40, 10}),
            scaled(Vec2f{40, 40}),
            scaled(Vec2f{10, 40}),
            scaled(Vec2f{10, 10}),
        }}
    },
    {
        ArrangeItem {
            scaled(Vec2f{120, 50}),
            scaled(Vec2f{140, 70}),
            scaled(Vec2f{120, 90}),
            scaled(Vec2f{80, 90}) ,
            scaled(Vec2f{60, 70}) ,
            scaled(Vec2f{80, 50}) ,
        },
        ArrangeItem {
            scaled(Vec2f{40, 10}),
            scaled(Vec2f{40, 40}),
            scaled(Vec2f{10, 40}),
            scaled(Vec2f{10, 10}),
        }
    },
};

struct PolyPair { Slic3r::ExPolygon orbiter; Slic3r::ExPolygon stationary; };

std::vector<PolyPair> nfp_concave_testdata = {
    { // ItemPair
     {
         {
             scaled(Vec2f{53.3726f, 14.2141f}),
             scaled(Vec2f{53.2359f, 14.3386f}),
             scaled(Vec2f{53.0141f, 14.2155f}),
             scaled(Vec2f{52.8649f, 16.0091f}),
             scaled(Vec2f{53.3659f, 15.7607f}),
             scaled(Vec2f{53.8669f, 16.0091f}),
             scaled(Vec2f{53.7178f, 14.2155f}),
             scaled(Vec2f{53.4959f, 14.3386f})
         }
     },
     {
         {
             scaled(Vec2f{11.8305f, 1.1603f}),
             scaled(Vec2f{11.8311f, 2.6616f}),
             scaled(Vec2f{11.3311f, 2.6611f}),
             scaled(Vec2f{10.9311f, 2.9604f}),
             scaled(Vec2f{10.9300f, 4.4608f}),
             scaled(Vec2f{10.9311f, 4.9631f}),
             scaled(Vec2f{11.3300f, 5.2636f}),
             scaled(Vec2f{11.8311f, 5.2636f}),
             scaled(Vec2f{11.8308f, 10.3636f}),
             scaled(Vec2f{22.3830f, 10.3636f}),
             scaled(Vec2f{23.6845f, 9.0642f}),
             scaled(Vec2f{23.6832f, 1.1630f}),
             scaled(Vec2f{23.2825f, 1.1616f}),
             scaled(Vec2f{21.0149f, 1.1616f}),
             scaled(Vec2f{21.1308f, 1.3625f}),
             scaled(Vec2f{20.9315f, 1.7080f}),
             scaled(Vec2f{20.5326f, 1.7080f}),
             scaled(Vec2f{20.3334f, 1.3629f}),
             scaled(Vec2f{20.4493f, 1.1616f})
         }
     },
     }
};

static void check_nfp(const std::string & outfile_prefix,
                      const Slic3r::Polygons &stationary,
                      const Slic3r::Polygons &orbiter,
                      const Slic3r::ExPolygons &bedpoly,
                      const Slic3r::ExPolygons &nfp)
{
    using namespace Slic3r;

    auto stationary_ex = to_expolygons(stationary);
    auto bedbb = get_extents(bedpoly);
    bedbb.offset(scaled(1.));
    auto bedrect = arr2::to_rectangle(bedbb);

    ExPolygons bed_negative = diff_ex(bedrect, bedpoly);
    ExPolygons orb_ex_r = to_expolygons(orbiter);
    ExPolygons orb_ex_r_ch = {ExPolygon(Geometry::convex_hull(orb_ex_r))};
    auto orb_ex_offs_pos_r = offset_ex(orb_ex_r,  scaled<float>(EPSILON));
    auto orb_ex_offs_neg_r = offset_ex(orb_ex_r, -scaled<float>(EPSILON));
    auto orb_ex_offs_pos_r_ch = offset_ex(orb_ex_r_ch,  scaled<float>(EPSILON));
    auto orb_ex_offs_neg_r_ch = offset_ex(orb_ex_r_ch, -scaled<float>(EPSILON));

    auto bedpoly_offs = offset_ex(bedpoly, static_cast<float>(SCALED_EPSILON));

    auto check_at_nfppos = [&](const Point &pos) {
        ExPolygons orb_ex = orb_ex_r;
        Point d = pos - reference_vertex(orbiter);
        for (ExPolygon &poly : orb_ex)
            poly.translate(d);

        bool touching = false;
        bool check_failed = false;

        bool within_bed = false;
        bool touches_fixed = false;
        bool touches_bedwall = false;

        try {
            auto beddiff = diff_ex(orb_ex, bedpoly_offs);
            if (beddiff.empty())
                within_bed = true;

            auto orb_ex_offs_pos = orb_ex_offs_pos_r;
            for (ExPolygon &poly: orb_ex_offs_pos)
                poly.translate(d);

            auto orb_ex_offs_neg = orb_ex_offs_neg_r;
            for (ExPolygon &poly: orb_ex_offs_neg)
                poly.translate(d);

            auto orb_ex_offs_pos_ch = orb_ex_offs_pos_r_ch;
            for (ExPolygon &poly: orb_ex_offs_pos_ch)
                poly.translate(d);

            auto orb_ex_offs_neg_ch = orb_ex_offs_neg_r_ch;
            for (ExPolygon &poly: orb_ex_offs_neg_ch)
                poly.translate(d);

            if (!touches_bedwall) {
                auto inters_pos = intersection_ex(bed_negative, orb_ex_offs_pos_ch);
                auto inters_neg = intersection_ex(bed_negative, orb_ex_offs_neg_ch);
                if (!inters_pos.empty() && inters_neg.empty())
                    touches_bedwall = true;
            }

            if (!touches_fixed) {
                auto inters_pos = intersection_ex(stationary_ex, orb_ex_offs_pos);
                auto inters_neg = intersection_ex(stationary_ex, orb_ex_offs_neg);
                if (!inters_pos.empty() && inters_neg.empty())
                    touches_fixed = true;
            }

            touching = within_bed && (touches_fixed || touches_bedwall);
        } catch (...) {
            check_failed = true;
            touching = false;
        }

#ifndef NDEBUG
        if (!touching || check_failed) {

            auto bb = get_extents(bedpoly);
            SVG svg(outfile_prefix + ".svg", bb, 0, true);
            svg.draw(orbiter, "orange");
            svg.draw(stationary, "yellow");
            svg.draw(bed_negative, "blue", 0.5f);
            svg.draw(nfp, "green", 0.5f);
            svg.draw(orb_ex, "red");

            svg.Close();
        }
#endif
        REQUIRE(!check_failed);
        REQUIRE(touching);
    };

    if (nfp.empty()) {
        auto bb = get_extents(bedpoly);
        SVG svg(outfile_prefix + ".svg", bb, 0, true);
        svg.draw(orbiter, "orange");
        svg.draw(stationary, "yellow");
        svg.draw(bedpoly, "blue", 0.5f);

        svg.Close();
    }

    REQUIRE(!nfp.empty());

    for (const ExPolygon &nfp_part : nfp) {
        for (const Point &nfp_pos : nfp_part.contour) {
            check_at_nfppos(nfp_pos);
        }
        for (const Polygon &h : nfp_part.holes)
            for (const Point &nfp_pos : h) {
                check_at_nfppos(nfp_pos);
            }
    }
}

template<class Bed, class PairType>
void test_itempairs(const std::vector<PairType> &testdata,
                    const Bed &bed,
                    const std::string &outfile_prefix = "")
{
    using namespace Slic3r;

    size_t testnum = 0;

    ExPolygons bedshape = arr2::to_expolygons(bed);

    for(auto td : testdata) {
        Polygons orbiter = td.orbiter.envelope().transformed_outline();
        Polygons stationary = td.stationary.shape().transformed_outline();
        Point center = bounding_box(bed).center();
        Point stat_c = get_extents(stationary).center();
        Point d =  center - stat_c;
        arr2::translate(td.stationary, d);
        stationary = td.stationary.shape().transformed_outline();

        std::array<std::reference_wrapper<const decltype(td.stationary)>, 1> fixed = {{td.stationary}};
        auto nfp = arr2::calculate_nfp(td.orbiter, arr2::default_context(fixed), bed);

        check_nfp(outfile_prefix + "nfp_test_" + std::to_string(testnum),
                  stationary,
                  orbiter,
                  bedshape,
                  nfp);

        testnum++;
    }
}

template<class It, class Fn>
static void foreach_combo(const Slic3r::Range<It> &range, const Fn &fn)
{
    std::vector<bool> pairs(range.size(), false);

    assert(range.size() >= 2);
    pairs[range.size() - 1] = true;
    pairs[range.size() - 2] = true;

    do {
        std::vector<typename std::iterator_traits<It>::value_type> items;
        for (size_t i = 0; i < pairs.size(); i++) {
            if (pairs[i]) {
                auto it = range.begin();
                std::advance(it, i);
                items.emplace_back(*it);
            }
        }
        fn (items[0], items[1]);
    } while (std::next_permutation(pairs.begin(), pairs.end()));
}

TEST_CASE("Static type tests for arrange items", "[arrange2]")
{
    using namespace Slic3r;

    REQUIRE(arr2::IsDataStore<arr2::ArrangeItem>);
    REQUIRE(arr2::IsMutableItem<arr2::ArrangeItem>);

    REQUIRE(! arr2::IsDataStore<arr2::SimpleArrangeItem>);
    REQUIRE(arr2::IsMutableItem<arr2::SimpleArrangeItem>);

    REQUIRE(arr2::IsDataStore<arr2::TrafoOnlyArrangeItem>);
    REQUIRE(arr2::IsMutableItem<arr2::TrafoOnlyArrangeItem>);
}

template<class Bed> Bed init_bed() { return {}; }
template<> inline Slic3r::arr2::InfiniteBed init_bed<Slic3r::arr2::InfiniteBed>()
{
    return Slic3r::arr2::InfiniteBed{{scaled(250.) / 2., scaled(210.) / 2.}};
}

template<> inline Slic3r::arr2::RectangleBed init_bed<Slic3r::arr2::RectangleBed>()
{
    return Slic3r::arr2::RectangleBed{scaled(500.), scaled(500.)};
}

template<> inline Slic3r::arr2::CircleBed init_bed<Slic3r::arr2::CircleBed>()
{
    return Slic3r::arr2::CircleBed{Slic3r::Point::Zero(), scaled(300.), Slic3r::Vec2crd{0, 0}};
}

template<> inline Slic3r::arr2::IrregularBed init_bed<Slic3r::arr2::IrregularBed>()
{
    using namespace Slic3r;
    BoundingBox bb_outer{Point::Zero(), {scaled(500.), scaled(500.)}};
    BoundingBox corner{Point::Zero(), {scaled(50.), scaled(50.)}};

    auto transl = [](BoundingBox bb, Point t) { bb.translate(t); return bb; };

    Polygons rect_outer = {arr2::to_rectangle(bb_outer)};
    Polygons corners = {arr2::to_rectangle(transl(corner, {scaled(10.), scaled(10.)})),
                        arr2::to_rectangle(transl(corner, {scaled(440.), scaled(10.)})),
                        arr2::to_rectangle(transl(corner, {scaled(440.), scaled(440.)})),
                        arr2::to_rectangle(transl(corner, {scaled(10.), scaled(440.)})),
                        arr2::to_rectangle(BoundingBox({scaled(80.), scaled(450.)}, {scaled(420.), scaled(510.)})),
                        arr2::to_rectangle(BoundingBox({scaled(80.), scaled(-10.)}, {scaled(420.), scaled(50.)}))};

    ExPolygons bedshape = diff_ex(rect_outer, corners);

    return arr2::IrregularBed{bedshape};
}

template<class Bed> std::string bedtype_str(const Bed &bed)
{
    return "";
}

inline std::string bedtype_str(const Slic3r::arr2::RectangleBed &bed)
{
    return "RectangleBed";
}

inline std::string bedtype_str(const Slic3r::arr2::CircleBed &bed)
{
    return "CircleBed";
}

inline std::string bedtype_str(const Slic3r::arr2::InfiniteBed &bed)
{
    return "InfiniteBed";
}

inline std::string bedtype_str(const Slic3r::arr2::IrregularBed &bed)
{
    return "IrregularBed";
}

TEST_CASE("NFP should be empty if item cannot fit into bed", "[arrange2]") {
    using namespace Slic3r;

    arr2::RectangleBed bed{scaled(10.), scaled(10.)};
    ExPolygons bedshape = arr2::to_expolygons(bed);

    for(auto& td : nfp_testdata) {
        REQUIRE(&(td.orbiter.envelope()) == &(td.orbiter.shape()));
        REQUIRE(&(td.stationary.envelope()) == &(td.stationary.shape()));
        REQUIRE(td.orbiter.envelope().reference_vertex() ==
                td.orbiter.shape().reference_vertex());
        REQUIRE(td.stationary.envelope().reference_vertex() ==
                td.stationary.shape().reference_vertex());

        ArrangeItem cpy = td.stationary;
        REQUIRE(&(cpy.envelope()) == &(cpy.shape()));
        REQUIRE(cpy.envelope().reference_vertex() ==
                cpy.shape().reference_vertex());

        std::array<std::reference_wrapper<const ArrangeItem>, 1> fixed =
            {{td.stationary}};

        auto nfp = arr2::calculate_nfp(td.orbiter, default_context(fixed), bed);

        REQUIRE(nfp.empty());
    }
}

#include <boost/filesystem/path.hpp>
#include <boost/filesystem.hpp>

TEMPLATE_TEST_CASE("NFP algorithm test", "[arrange2][Slow]",
                   Slic3r::arr2::InfiniteBed,
                   Slic3r::arr2::RectangleBed,
                   Slic3r::arr2::CircleBed,
                   Slic3r::arr2::IrregularBed)
{
    using namespace Slic3r;

    auto bed = init_bed<TestType>();
    std::string bedtypestr = bedtype_str(bed);

    SECTION("Predefined simple polygons for debugging") {
        test_itempairs(nfp_testdata, bed, bedtypestr + "_");
    }

    SECTION("All combinations of convex prusa parts without inflation") {
        auto parts = prusa_parts();

        std::vector<ItemPair> testdata;
        foreach_combo(range(parts), [&testdata](auto &i1, auto &i2){
            testdata.emplace_back(ItemPair{i1, i2});
        });

        test_itempairs(testdata, bed, bedtypestr + "_prusacombos");
    }

    SECTION("All combinations of prusa parts with random inflation") {
        std::random_device rd;
        auto seed = rd();
        std::mt19937 rng{seed};
        std::uniform_real_distribution<double> distr{0., 50.};

        INFO ("Seed = " << seed);

        auto parts = prusa_parts(distr(rng));

        std::vector<ItemPair> testdata;
        foreach_combo(range(parts), [&testdata](auto &i1, auto &i2){
            testdata.emplace_back(ItemPair{i1, i2});
        });

        test_itempairs(testdata, bed, bedtypestr + "_prusacombos_infl");
    }

    SECTION("All combinations of concave-holed prusa parts without inflation")
    {
        auto parts = prusa_parts_ex();
        for (ArrangeItem &itm : parts) {
            itm.set_envelope(arr2::DecomposedShape{itm.shape().convex_hull()});
        }

        std::vector<ItemPair> testdata;
        foreach_combo(range(parts), [&testdata](auto &i1, auto &i2){
            testdata.emplace_back(ItemPair{i1, i2});
        });

        test_itempairs(testdata, bed, bedtypestr + "_prusacombos_ex");
    }

    SECTION("All combinations of concave-holed prusa parts with inflation") {
        std::random_device rd;
        auto seed = rd();
        std::mt19937 rng{seed};
        std::uniform_real_distribution<double> distr{0., 50.};

        INFO ("Seed = " << seed);

        auto parts = prusa_parts_ex(distr(rng));
        for (ArrangeItem &itm : parts) {
            itm.set_envelope(arr2::DecomposedShape{itm.shape().convex_hull()});
        }

        std::vector<ItemPair> testdata;
        foreach_combo(range(parts), [&testdata](auto &i1, auto &i2){
            testdata.emplace_back(ItemPair{i1, i2});
        });

        test_itempairs(testdata, bed, bedtypestr + "_prusacombos_ex_infl");
    }
}

TEST_CASE("EdgeCache tests", "[arrange2]") {
    using namespace Slic3r;

    SECTION ("Empty polygon should produce empty edge-cache") {
        ExPolygon emptypoly;

        arr2::EdgeCache ep {&emptypoly};

        std::vector<arr2::ContourLocation> samples;
        ep.sample_contour(1., samples);
        REQUIRE(samples.empty());
    }

    SECTION ("Single edge polygon should be considered as 2 lines") {
        ExPolygon poly{scaled(Vec2f{0.f, 0.f}), scaled(Vec2f{10., 10.})};

        arr2::EdgeCache ep{&poly};
        std::vector<arr2::ContourLocation> samples;

        double accuracy = 1.;
        ep.sample_contour(accuracy, samples);

        REQUIRE(samples.size() == 2);
        REQUIRE(ep.coords(samples[0]) == poly.contour[1]);
        REQUIRE(ep.coords(samples[1]) == poly.contour[0]);
        REQUIRE(ep.coords({0, 0.}) == ep.coords({0, 1.}));
    }

    SECTION ("Test address range") {
        // Single edge on the int range boundary
        ExPolygon poly{scaled(Vec2f{-2000.f, 0.f}), scaled(Vec2f{2000.f, 0.f})};

        arr2::EdgeCache ep{&poly};
        REQUIRE(ep.coords({0, 0.25}) == Vec2crd{0, 0});
        REQUIRE(ep.coords({0, 0.75}) == Vec2crd{0, 0});

        // Multiple edges on the int range boundary
        ExPolygon squ{arr2::to_rectangle(scaled(BoundingBoxf{{0., 0.}, {2000., 2000.}}))};

        arr2::EdgeCache ep2{&squ};
        REQUIRE(ep2.coords({0, 0.})   == Vec2crd{0, 0});
        REQUIRE(ep2.coords({0, 0.25}) == Vec2crd{2000000000, 0});
        REQUIRE(ep2.coords({0, 0.5})  == Vec2crd{2000000000, 2000000000});
        REQUIRE(ep2.coords({0, 0.75}) == Vec2crd{0, 2000000000});
        REQUIRE(ep2.coords({0, 1.})   == Vec2crd{0, 0});
    }

    SECTION("Accuracy argument should skip corners correctly") {
        ExPolygon poly{arr2::to_rectangle(scaled(BoundingBoxf{{0., 0.}, {10., 10.}}))};

        double accuracy = 1.;
        arr2::EdgeCache ep{&poly};
        std::vector<arr2::ContourLocation> samples;
        ep.sample_contour(accuracy, samples);
        REQUIRE(samples.size() == poly.contour.size());
        for (size_t i = 0; i < samples.size(); ++i) {
            auto &cr = samples[i];
            REQUIRE(ep.coords(cr) == poly.contour.points[(i + 1) % poly.contour.size()]);
        }

        accuracy = 0.;
        arr2::EdgeCache ep0{&poly};
        samples.clear();
        ep0.sample_contour(accuracy, samples);
        REQUIRE(samples.size() == 1);
        REQUIRE(ep0.coords(samples[0]) == poly.contour.points[1]);
    }
}

// Mock packing strategy that places N items to the center of the
// bed bounding box, if the bed is larger than the item.
template<int Cap>
struct RectangleToCenterPackStrategy { static constexpr int Capacity = Cap; };

namespace Slic3r { namespace arr2 {
struct RectangleToCenterPackTag {};

template<int N> struct PackStrategyTag_<RectangleToCenterPackStrategy<N>> {
    using Tag = RectangleToCenterPackTag;
};

// Dummy arrangeitem that is a rectangle
struct RectangleItem {
    int bed_index = Unarranged;
    BoundingBox shape = {{0, 0}, scaled(Vec2d{10., 10.})};
    Vec2crd translation = {0, 0};
    double rotation = 0;

    int priority = 0;
    int packed_num = 0;

    void set_bed_index(int idx) { bed_index = idx; }
    int  get_bed_index() const noexcept { return bed_index; }
    std::optional<int> get_bed_constraint() const { return std::nullopt; }

    void set_translation(const Vec2crd &tr) { translation = tr; }
    const Vec2crd & get_translation() const noexcept { return translation; }

    void set_rotation(double r) { rotation = r; }
    double get_rotation() const noexcept { return rotation; }

    int get_priority() const noexcept { return priority; }
};

template<class Strategy, class Bed, class RemIt>
bool pack(Strategy &&strategy,
          const Bed &bed,
          RectangleItem &item,
          const PackStrategyContext<Strategy, RectangleItem> &packing_context,
          const Range<RemIt> &remaining_items,
          const RectangleToCenterPackTag &)
{
    bool ret = false;

    auto bedbb = bounding_box(bed);
    auto itmbb = item.shape;

    Vec2crd tr = bedbb.center() - itmbb.center();
    itmbb.translate(tr);

    auto fixed_items = all_items_range(packing_context);

    if (fixed_items.size() < Slic3r::StripCVRef<Strategy>::Capacity &&
        bedbb.contains(itmbb))
    {
        translate(item, tr);
        ret = true;
    }

    return ret;
}

}} // namespace Slic3r::arr2

using Slic3r::arr2::RectangleItem;

TEST_CASE("First fit selection strategy", "[arrange2]")
{
    using ArrItem = RectangleItem;
    using Cmp = Slic3r::arr2::firstfit::DefaultItemCompareFn;

    auto create_items_n = [](size_t count) {
        INFO ("Item count = " << count);

        auto items = Slic3r::reserve_vector<ArrItem>(count);
        std::generate_n(std::back_inserter(items), count, [] { return ArrItem{}; });

        return items;
    };

    auto bed = Slic3r::arr2::RectangleBed(scaled(100.), scaled(100.));

    GIVEN("A packing strategy that does not accept any items")
    {
        using PackStrategy = RectangleToCenterPackStrategy<0>;

        int on_arrange_call_count = 0;
        auto on_arranged_fn = [&on_arrange_call_count](ArrItem &itm,
                                                       auto &bed, auto &packed,
                                                       auto &rem) {
            ++on_arrange_call_count;

            Slic3r::arr2::firstfit::DefaultOnArrangedFn{}(itm, bed, packed, rem);
        };

        int cancel_call_count = 0;
        auto stop_cond = [&cancel_call_count] {
            ++cancel_call_count;
            return false;
        };

        WHEN ("attempting to pack a single item with a valid bed index")
        {
            auto items = create_items_n(1);

            Slic3r::arr2::set_bed_index(items.front(), random_value(0, 1000));

            auto sel = Slic3r::arr2::firstfit::SelectionStrategy{Cmp{}, on_arranged_fn, stop_cond};

            Slic3r::arr2::arrange(sel,
                                  PackStrategy{},
                                  Slic3r::range(items),
                                  bed);

            THEN ("the original bed index should be ignored and set to Unarranged")
            {
                REQUIRE(Slic3r::arr2::get_bed_index(items.front()) ==
                        Slic3r::arr2::Unarranged);
            }

            THEN("the arrange callback should not be called")
            {
                REQUIRE(on_arrange_call_count == 0);
            }

            THEN("the stop condition should be called at least once")
            {
                REQUIRE(cancel_call_count > 0);
            }
        }

        WHEN("attempting to pack arbitrary number > 1 of items into the bed")
        {
            auto items = create_items_n(random_value(1, 100));

            CHECK(cancel_call_count == 0);
            CHECK(on_arrange_call_count == 0);

            Slic3r::arr2::arrange(
                Slic3r::arr2::firstfit::SelectionStrategy{Cmp{}, on_arranged_fn, stop_cond},
                PackStrategy{}, Slic3r::range(items), bed);

            THEN("The item should be left unpacked")
            {
                REQUIRE(std::all_of(items.begin(), items.end(), [](const ArrItem &itm) {
                    return !Slic3r::arr2::is_arranged(itm);
                }));
            }

            THEN("the arrange callback should not be called")
            {
                REQUIRE(on_arrange_call_count == 0);
            }

            THEN("the stop condition should be called at least once for each item")
            {
                INFO("items count = " << items.size());
                REQUIRE(cancel_call_count >= static_cast<int>(items.size()));
            }
        }
    }

    GIVEN("A pack strategy that accepts only a single item")
    {
        using PackStrategy = RectangleToCenterPackStrategy<1>;

        WHEN ("attempting to pack a single item with a valid bed index")
        {
            auto items = create_items_n(1);

            Slic3r::arr2::set_bed_index(items.front(), random_value(0, 1000));

            Slic3r::arr2::arrange(Slic3r::arr2::firstfit::SelectionStrategy<>{},
                                  PackStrategy{},
                                  Slic3r::range(items),
                                  bed);

            THEN ("the original bed index should be ignored and set to zero")
            {
                REQUIRE(Slic3r::arr2::get_bed_index(items.front()) == 0);
            }
        }

        WHEN("attempting to pack arbitrary number > 1 of items into bed")
        {
            auto items = create_items_n(random_value(1, 100));

            Slic3r::arr2::arrange(Slic3r::arr2::firstfit::SelectionStrategy<>{},
                                  PackStrategy{},
                                  Slic3r::range(items),
                                  bed);

            THEN ("The number of beds created should match the number of items")
            {
                auto bed_count = Slic3r::arr2::get_bed_count(Slic3r::range(items));

                REQUIRE(bed_count == items.size());
            }

            THEN("All items should reside on their respective beds")
            {
                for (size_t i = 0; i < items.size(); ++i)
                    REQUIRE(Slic3r::arr2::get_bed_index(items[i]) == static_cast<int>(i));
            }
        }
    }

    GIVEN ("Two packed beds with an unpacked bed between them")
    {
        using PackStrategy = RectangleToCenterPackStrategy<1>;

        auto bed = Slic3r::arr2::RectangleBed(scaled(100.), scaled(100.));
        auto fixed = create_items_n(2);
        std::for_each(fixed.begin(), fixed.end(), [&bed](auto &itm){
            Slic3r::arr2::pack(PackStrategy{}, bed, itm);
        });
        for (auto [i, idx] : {std::make_pair(0, 0), std::make_pair(1, 2)})
            Slic3r::arr2::set_bed_index(fixed[i], idx);

        WHEN("attempting to pack a single item")
        {
            auto items = create_items_n(1);

            Slic3r::arr2::arrange(Slic3r::arr2::firstfit::SelectionStrategy<>{},
                                  PackStrategy{},
                                  Slic3r::range(items),
                                  Slic3r::crange(fixed),
                                  bed);

            THEN("the item should end up on the first free bed")
            {
                REQUIRE(Slic3r::arr2::get_bed_index(items.front()) == 1);
            }
        }
    }

    GIVEN ("A 100 items with increasing priorities and a packer that accepts 20 items")
    {
        static constexpr int Capacity = 20;
        static constexpr int Count = 5 * Capacity;

        using PackStrategy = RectangleToCenterPackStrategy<Capacity>;
        auto  items = create_items_n(Count);

        for (size_t i = 0; i < items.size(); ++i)
            items[i].priority = static_cast<int>(i);

        WHEN("attempting to pack all items")
        {
            auto on_arranged_fn = [](ArrItem &itm, auto &bed, auto &packed,
                                     auto &rem) {
                itm.packed_num = packed.size();
                Slic3r::arr2::firstfit::DefaultOnArrangedFn{}(itm, bed, packed, rem);
            };

            Slic3r::arr2::arrange(Slic3r::arr2::firstfit::SelectionStrategy{Cmp{}, on_arranged_fn},
                                  PackStrategy{},
                                  Slic3r::range(items),
                                  bed);

            THEN("all items should fit onto the beds from index 0 to 4")
            {
                REQUIRE(std::all_of(items.begin(), items.end(), [](const ArrItem &itm) {
                    auto bed_idx = Slic3r::arr2::get_bed_index(itm);
                    return bed_idx >= 0 && bed_idx < Count / Capacity;
                }));
            }

            // Highest priority goes first
            THEN("all the items should be packed in reverse order of their priority value")
            {
                REQUIRE(std::all_of(items.begin(), items.end(), [](const ArrItem &itm) {
                    return itm.packed_num == (99 - itm.priority);
                }));
            }
        }
    }
}

template<>
Slic3r::BoundingBox Slic3r::arr2::NFPArrangeItemTraits_<
    RectangleItem>::envelope_bounding_box(const RectangleItem &itm)
{
    return itm.shape;
}

template<>
Slic3r::Vec2crd Slic3r::arr2::NFPArrangeItemTraits_<
    RectangleItem>::reference_vertex(const RectangleItem &itm)
{
    return itm.shape.center();
}

TEST_CASE("Optimal nfp position search with GravityKernel using RectangleItem and InfiniteBed",
          "[arrange2]")
{
    auto bed = Slic3r::arr2::InfiniteBed{};
    auto strategy = Slic3r::arr2::PackStrategyNFP{Slic3r::arr2::GravityKernel{bed.center}};

    GIVEN("An nfp made of a single point coincident with the bed center")
    {
        WHEN ("searching for optimal position")
        {
            THEN ("the optimum should be at the single nfp point")
            {
                Slic3r::ExPolygons nfp;
                nfp.emplace_back(Slic3r::ExPolygon{{bed.center}});

                auto item = RectangleItem{};

                double score = pick_best_spot_on_nfp_verts_only(item, nfp, bed, strategy);

                Slic3r::Vec2crd D = bed.center - item.shape.center();
                REQUIRE(item.translation == D);
                REQUIRE(score == Approx(0.).margin(EPSILON));
            }
        }
    }
}

TEMPLATE_TEST_CASE("RectangleOverfitPackingStrategy test", "[arrange2]",
                   Slic3r::arr2::SimpleArrangeItem, Slic3r::arr2::ArrangeItem)
{
    using Slic3r::arr2::RectangleOverfitPackingStrategy;
    using Slic3r::arr2::PackStrategyNFP;
    using Slic3r::arr2::GravityKernel;
    using Slic3r::arr2::get_bed_index;

    namespace firstfit = Slic3r::arr2::firstfit;

    using ArrItem = TestType;

    auto frontleft_align_fn = [](const Slic3r::BoundingBox &bedbb,
                                 const Slic3r::BoundingBox &pilebb) {
        return bedbb.min - pilebb.min;
    };

    RectangleOverfitPackingStrategy pstrategy{PackStrategyNFP{GravityKernel{}},
                                              frontleft_align_fn};

    auto bed = Slic3r::arr2::RectangleBed{scaled(100.), scaled(100.)};
    auto item_blueprint = Slic3r::arr2::to_rectangle(
        Slic3r::BoundingBox{{0, 0}, {scaled(20.), scaled(20.)}});

    auto item_gen_fn = [&item_blueprint] { return ArrItem{item_blueprint}; };

    GIVEN("One empty logical rectangular 100x100 mm bed ") {

        WHEN("attempting to pack one rectangle") {
            constexpr auto count = size_t{1};
            auto items = Slic3r::reserve_vector<ArrItem>(count);

            std::generate_n(std::back_inserter(items), count, item_gen_fn);

            Slic3r::arr2::arrange(firstfit::SelectionStrategy<>{}, pstrategy,
                                  Slic3r::range(items), bed);

            THEN ("Overfit kernel should take over and align the single item") {
                auto pilebb = bounding_box(Slic3r::range(items));

                Slic3r::Vec2crd D = frontleft_align_fn(bounding_box(bed), pilebb);
                REQUIRE(D.squaredNorm() == 0);
            }
        }

        WHEN("attempting to pack two rectangles") {

            constexpr auto count = size_t{2};
            auto items = Slic3r::reserve_vector<ArrItem>(count);

            std::generate_n(std::back_inserter(items), count, item_gen_fn);

            Slic3r::arr2::arrange(firstfit::SelectionStrategy<>{}, pstrategy,
                                  Slic3r::range(items), bed);

            THEN("Overfit kernel should take over and align the single item")
            {
                auto pilebb = bounding_box(Slic3r::range(items));

                Slic3r::Vec2crd D = frontleft_align_fn(bounding_box(bed), pilebb);
                REQUIRE(D.squaredNorm() == 0);
            }
        }
    }

    GIVEN("Two logical rectangular beds, the second having fixed items") {

        auto fixed_item_bb = Slic3r::BoundingBox{{0, 0}, {scaled(20.), scaled(20.)}};
        std::vector<ArrItem> fixed = {
            ArrItem{Slic3r::arr2::to_rectangle(fixed_item_bb)}};

        Slic3r::arr2::set_bed_index(fixed.front(), 1);

        WHEN("attempting to pack 3 rectangles, 1 filling the first bed") {

            auto items = Slic3r::reserve_vector<ArrItem>(3);

            // Add a big rectangle this will fill the first bed so that
            // smaller rectangles will fit only into the next bed
            items.emplace_back(ArrItem{Slic3r::arr2::to_rectangle(
                Slic3r::BoundingBox{{0, 0}, {scaled(90.), scaled(90.)}})});

            std::generate_n(std::back_inserter(items), 2, item_gen_fn);

            Slic3r::arr2::arrange(firstfit::SelectionStrategy<>{}, pstrategy,
                                  Slic3r::range(items), Slic3r::crange(fixed),
                                  bed);

            THEN("Overfit kernel should handle the 0th bed and gravity kernel handles the 1st bed")
            {
                REQUIRE(get_bed_index(items.front()) == 0);

                auto pilebb = bounding_box_on_bedidx(Slic3r::range(items), 0);
                Slic3r::Vec2crd D = frontleft_align_fn(bounding_box(bed), pilebb);
                REQUIRE(D.squaredNorm() == 0);

                REQUIRE((get_bed_index(items[1]) == get_bed_index(items[2]) == 1));

                auto pilebb1 = bounding_box_on_bedidx(Slic3r::range(items), 1);
                REQUIRE(pilebb1.overlap(fixed_item_bb));

                Slic3r::Vec2crd D1 = frontleft_align_fn(bounding_box(bed), pilebb1);
                REQUIRE(D1.squaredNorm() != 0);
            }
        }
    }
}

TEMPLATE_TEST_CASE("Test if allowed item rotations are considered", "[arrange2]",
                   Slic3r::arr2::ArrangeItem)
{
    using ArrItem = TestType;

    auto item_blueprint = Slic3r::arr2::to_rectangle(
        Slic3r::BoundingBox{{0, 0}, {scaled(20.), scaled(20.)}});

    ArrItem itm{item_blueprint};

    auto bed = Slic3r::arr2::RectangleBed{scaled(100.), scaled(100.)};

    set_allowed_rotations(itm, {PI});

    Slic3r::arr2::PackStrategyNFP strategy{Slic3r::arr2::GravityKernel{}};

    bool packed = pack(strategy, bed, itm);

    REQUIRE(packed);
    REQUIRE(get_rotation(itm) == Approx(PI));
}

