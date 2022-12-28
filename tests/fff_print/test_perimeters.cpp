#include <catch2/catch.hpp>

#include <numeric>
#include <sstream>

#include "libslic3r/Config.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/PerimeterGenerator.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/SurfaceCollection.hpp"
#include "libslic3r/libslic3r.h"

#include "test_data.hpp"

using namespace Slic3r;

SCENARIO("Perimeter nesting", "[Perimeters]")
{
    struct TestData {
        ExPolygons          expolygons;
        // expected number of loops
        int                 total;
        // expected number of external loops
        int                 external;
        // expected external perimeter
        std::vector<bool>   ext_order;
        // expected number of internal contour loops
        int                 cinternal;
        // expected number of ccw loops
        int                 ccw;
        // expected ccw/cw order
        std::vector<bool>   ccw_order;
        // expected nesting order
        std::vector<std::vector<int>> nesting;
    };

    FullPrintConfig config;
    // PrintObject* object_ptr = new PrintObject(...);

    auto test = [&config](const TestData &data) {
        SurfaceCollection slices;
        slices.append(data.expolygons, stInternal);
        
        ExtrusionEntityCollection loops;
        ExtrusionEntityCollection gap_fill;
        SurfaceCollection         fill_surfaces;
        double z = 0.0;
        PerimeterGenerator        perimeter_generator(
            &slices, 
            1., // layer height
            Flow(1., 1., 1.),
            static_cast<const PrintRegionConfig*>(&config),
            // static_cast<const PrintObjectConfig*>(&config),
            nullptr, // object_ptr, // PrintObject*; nullptr is ok only if no map (such as fuzzy_skin_displacement_map) since maps require bounding_box for mapping.
            static_cast<const PrintConfig*>(&config),
            false, // spiral_vase
            z,
            // output:
            &loops, &gap_fill, &fill_surfaces);
        perimeter_generator.process();

        THEN("expected number of collections") {
            REQUIRE(loops.entities.size() == data.expolygons.size());
        }        
        
        loops = loops.flatten();
        THEN("expected number of loops") {
            REQUIRE(loops.entities.size() == data.total);
        }
        THEN("expected number of external loops") {
            size_t num_external = std::count_if(loops.entities.begin(), loops.entities.end(), 
                [](const ExtrusionEntity *ee){ return ee->role() == erExternalPerimeter; });
            REQUIRE(num_external == data.external);
        }
        THEN("expected external order") {
            std::vector<bool> ext_order;
            for (auto *ee : loops.entities)
                ext_order.emplace_back(ee->role() == erExternalPerimeter);
            REQUIRE(ext_order == data.ext_order);
        }
        THEN("expected number of internal contour loops") {
            size_t cinternal = std::count_if(loops.entities.begin(), loops.entities.end(), 
                [](const ExtrusionEntity *ee){ return dynamic_cast<const ExtrusionLoop*>(ee)->loop_role() == elrContourInternalPerimeter; });
            REQUIRE(cinternal == data.cinternal);
        }
        THEN("expected number of ccw loops") {
            size_t ccw = std::count_if(loops.entities.begin(), loops.entities.end(), 
                [](const ExtrusionEntity *ee){ return dynamic_cast<const ExtrusionLoop*>(ee)->polygon().is_counter_clockwise(); });
            REQUIRE(ccw == data.ccw);
        }
        THEN("expected ccw/cw order") {
            std::vector<bool> ccw_order;
            for (auto *ee : loops.entities)
                ccw_order.emplace_back(dynamic_cast<const ExtrusionLoop*>(ee)->polygon().is_counter_clockwise());
            REQUIRE(ccw_order == data.ccw_order);
        }
        THEN("expected nesting order") {
            for (const std::vector<int> &nesting : data.nesting) {
                for (size_t i = 1; i < nesting.size(); ++ i)
                    REQUIRE(dynamic_cast<const ExtrusionLoop*>(loops.entities[nesting[i - 1]])->polygon().contains(loops.entities[nesting[i]]->first_point()));
            }
        }
    };

    WHEN("Rectangle") {
        config.perimeters.value = 3;
        TestData data;
        data.expolygons  = { 
            ExPolygon{ Polygon::new_scale({ {0,0}, {100,0}, {100,100}, {0,100} }) }
        };
        data.total       = 3;
        data.external    = 1;
        data.ext_order   = { false, false, true };
        data.cinternal   = 1;
        data.ccw         = 3;
        data.ccw_order   = { true, true, true };
        data.nesting     = { { 2, 1, 0 } };
        test(data);
    }
    WHEN("Rectangle with hole") {
        config.perimeters.value = 3;
        TestData data;
        data.expolygons  = { 
            ExPolygon{ Polygon::new_scale({ {0,0}, {100,0}, {100,100}, {0,100} }), 
                       Polygon::new_scale({ {40,40}, {40,60}, {60,60}, {60,40} }) } 
        };
        data.total       = 6;
        data.external    = 2;
        data.ext_order   = { false, false, true, false, false, true };
        data.cinternal   = 1;
        data.ccw         = 3;
        data.ccw_order   = { false, false, false, true, true, true };
        data.nesting     = { { 5, 4, 3, 0, 1, 2 } };
        test(data);
    }
    WHEN("Nested rectangles with holes") {
        config.perimeters.value = 3;
        TestData data;
        data.expolygons  = {
            ExPolygon{ Polygon::new_scale({ {0,0}, {200,0}, {200,200}, {0,200} }), 
                       Polygon::new_scale({ {20,20}, {20,180}, {180,180}, {180,20} }) },
            ExPolygon{ Polygon::new_scale({ {50,50}, {150,50}, {150,150}, {50,150} }), 
                       Polygon::new_scale({ {80,80}, {80,120}, {120,120}, {120,80} }) }
        };
        data.total       = 4*3;
        data.external    = 4;
        data.ext_order   = { false, false, true, false, false, true, false, false, true, false, false, true };
        data.cinternal   = 2;
        data.ccw         = 2*3;
        data.ccw_order   = { false, false, false, true, true, true, false, false, false, true, true, true };
        test(data);
    }
    WHEN("Rectangle with multiple holes") {
        config.perimeters.value = 2;
        TestData data;
        ExPolygon expoly{ Polygon::new_scale({ {0,0}, {50,0}, {50,50}, {0,50} }) };
        expoly.holes.emplace_back(Polygon::new_scale({ {7.5,7.5},  {7.5,12.5},  {12.5,12.5}, {12.5,7.5}  }));
        expoly.holes.emplace_back(Polygon::new_scale({ {7.5,17.5}, {7.5,22.5},  {12.5,22.5}, {12.5,17.5} }));
        expoly.holes.emplace_back(Polygon::new_scale({ {7.5,27.5}, {7.5,32.5},  {12.5,32.5}, {12.5,27.5} }));
        expoly.holes.emplace_back(Polygon::new_scale({ {7.5,37.5}, {7.5,42.5},  {12.5,42.5}, {12.5,37.5} }));
        expoly.holes.emplace_back(Polygon::new_scale({ {17.5,7.5}, {17.5,12.5}, {22.5,12.5}, {22.5,7.5}  }));
        data.expolygons  = { expoly };
        data.total       = 12;
        data.external    = 6;
        data.ext_order   = { false, true, false, true, false, true, false, true, false, true, false, true };
        data.cinternal   = 1;
        data.ccw         = 2;
        data.ccw_order   = { false, false, false, false, false, false, false, false, false, false, true, true };
        data.nesting     = { {0,1},{2,3},{4,5},{6,7},{8,9} };
        test(data);
    };
}

SCENARIO("Perimeters", "[Perimeters]")
{
    auto config = Slic3r::DynamicPrintConfig::full_print_config_with({
        { "skirts",                 0 },
        { "fill_density",           0 },
        { "perimeters",             3 },
        { "top_solid_layers",       0 },
        { "bottom_solid_layers",    0 },
        // to prevent speeds from being altered
        { "cooling",                "0" },
        // to prevent speeds from being altered
        { "first_layer_speed",      "100%" }
    });

    WHEN("Bridging perimeters disabled") {
        std::string gcode = Slic3r::Test::slice({ Slic3r::Test::TestMesh::overhang }, config);

        THEN("all perimeters extruded ccw") {
            GCodeReader parser;
            bool        has_cw_loops = false;
            Polygon     current_loop;
            parser.parse_buffer(gcode, [&has_cw_loops, &current_loop](Slic3r::GCodeReader &self, const Slic3r::GCodeReader::GCodeLine &line)
            {
                if (line.extruding(self) && line.dist_XY(self) > 0) {
                    if (current_loop.empty())
                        current_loop.points.emplace_back(self.xy_scaled());
                    current_loop.points.emplace_back(line.new_XY_scaled(self));
                } else if (! line.cmd_is("M73")) {
                    // skips remaining time lines (M73)
                    if (! current_loop.empty() && current_loop.is_clockwise())
                        has_cw_loops = true;
                    current_loop.clear();
                }
            });
            REQUIRE(! has_cw_loops);
        }
    }
    
    auto test = [&config](Test::TestMesh model) {    
        // we test two copies to make sure ExtrusionLoop objects are not modified in-place (the second object would not detect cw loops and thus would calculate wrong)
        std::string gcode = Slic3r::Test::slice({ model, model }, config);
        GCodeReader parser;
        bool        has_cw_loops = false;
        bool        has_outwards_move = false;
        bool        starts_on_convex_point = false;
        // print_z => count of external loops
        std::map<coord_t, int> external_loops;
        Polygon     current_loop;
        const double external_perimeter_speed = config.get_abs_value("external_perimeter_speed") * 60.;
        parser.parse_buffer(gcode, [&has_cw_loops, &has_outwards_move, &starts_on_convex_point, &external_loops, &current_loop, external_perimeter_speed, model]
            (Slic3r::GCodeReader &self, const Slic3r::GCodeReader::GCodeLine &line)
        {
            if (line.extruding(self) && line.dist_XY(self) > 0) {
                if (current_loop.empty())
                    current_loop.points.emplace_back(self.xy_scaled());
                current_loop.points.emplace_back(line.new_XY_scaled(self));
            } else if (! line.cmd_is("M73")) {
                // skips remaining time lines (M73)
                if (! current_loop.empty()) {
                    if (current_loop.is_clockwise())
                        has_cw_loops = true;
                    if (is_approx<double>(self.f(), external_perimeter_speed)) {
                        // reset counter for second object
                        coord_t z = scaled<coord_t>(self.z());
                        auto it = external_loops.find(z);
                        if (it == external_loops.end())
                            it = external_loops.insert(std::make_pair(z, 0)).first;
                        else if (it->second == 2)
                            it->second = 0;
                        ++ it->second;
                        bool is_contour          = it->second == 2;
                        bool is_hole             = it->second == 1;
                        // Testing whether the move point after loop ends up inside the extruded loop.
                        bool loop_contains_point = current_loop.contains(line.new_XY_scaled(self));
                        if (// contour should include destination
                            (! loop_contains_point && is_contour) ||
                            // hole should not
                            (loop_contains_point && is_hole))
                            has_outwards_move = true;
                        if (model == Test::TestMesh::cube_with_concave_hole) {
                            // check that loop starts at a concave vertex
                            double cross = cross2((current_loop.points.front() - current_loop.points[current_loop.points.size() - 2]).cast<double>(), (current_loop.points[1] - current_loop.points.front()).cast<double>());
                            bool   convex = cross > 0.;
                            if ((convex && is_contour) || (! convex && is_hole))
                                starts_on_convex_point = true;
                        }
                    }
                    current_loop.clear();
                }
            }
        });
        THEN("all perimeters extruded ccw") {
            REQUIRE(! has_cw_loops);
        }
        THEN("move inwards after completing external loop") {
            REQUIRE(! has_outwards_move);
        }
        THEN("loops start on concave point if any") {
            REQUIRE(! starts_on_convex_point);
        }

    };
    // Reusing the config above.
    config.set_deserialize_strict({
        { "external_perimeter_speed", 68 }
    });
    GIVEN("Cube with hole") { test(Test::TestMesh::cube_with_hole); }
    GIVEN("Cube with concave hole") { test(Test::TestMesh::cube_with_concave_hole); }
    
    WHEN("Bridging perimeters enabled") {
        // Reusing the config above.
        config.set_deserialize_strict({
            { "perimeters",                 1 },
            { "perimeter_speed",            77 },
            { "external_perimeter_speed",   66 },
            { "bridge_speed",               99 },
            { "cooling",                    "1" },
            { "fan_below_layer_time",       "0" },
            { "slowdown_below_layer_time",  "0" },
            { "bridge_fan_speed",           "100" },
            // arbitrary value
            { "bridge_flow_ratio",          33 },
            { "overhangs",                  true }
        });
    
        std::string gcode = Slic3r::Test::slice({ mesh(Slic3r::Test::TestMesh::overhang) }, config);

        THEN("Bridging is applied to bridging perimeters") {
            GCodeReader  parser;
            // print Z => speeds
            std::map<coord_t, std::set<double>> layer_speeds;
            int          fan_speed = 0;
            const double perimeter_speed            = config.opt_float("perimeter_speed") * 60.;
            const double external_perimeter_speed   = config.get_abs_value("external_perimeter_speed") * 60.;
            const double bridge_speed               = config.opt_float("bridge_speed") * 60.;
            const double nozzle_dmr                 = config.opt<ConfigOptionFloats>("nozzle_diameter")->get_at(0);
            const double filament_dmr               = config.opt<ConfigOptionFloats>("filament_diameter")->get_at(0);
            const double bridge_mm_per_mm           = sqr(nozzle_dmr / filament_dmr) * config.opt_float("bridge_flow_ratio");
            parser.parse_buffer(gcode, [&layer_speeds, &fan_speed, perimeter_speed, external_perimeter_speed, bridge_speed, nozzle_dmr, filament_dmr, bridge_mm_per_mm]
                (Slic3r::GCodeReader &self, const Slic3r::GCodeReader::GCodeLine &line)
            {
                if (line.cmd_is("M107"))
                    fan_speed = 0;
                else if (line.cmd_is("M106"))
                    line.has_value('S', fan_speed);
                else if (line.extruding(self) && line.dist_XY(self) > 0) {
                    double feedrate = line.new_F(self);
                    REQUIRE((is_approx(feedrate, perimeter_speed) || is_approx(feedrate, external_perimeter_speed) || is_approx(feedrate, bridge_speed)));
                    layer_speeds[self.z()].insert(feedrate);
                    bool   bridging  = is_approx(feedrate, bridge_speed);
                    double mm_per_mm = line.dist_E(self) / line.dist_XY(self);
                    // Fan enabled at full speed when bridging, disabled when not bridging.
                    REQUIRE((! bridging || fan_speed == 255));
                    REQUIRE((bridging || fan_speed == 0));
                    // When bridging, bridge flow is applied.
                    REQUIRE((! bridging || std::abs(mm_per_mm - bridge_mm_per_mm) <= 0.01));
                }
            });
            // only overhang layer has more than one speed
            size_t num_overhangs = std::count_if(layer_speeds.begin(), layer_speeds.end(), [](const std::pair<double, std::set<double>> &v){ return v.second.size() > 1; });
            REQUIRE(num_overhangs == 1);
        }
    }

    GIVEN("iPad stand") {
        WHEN("Extra perimeters enabled") {
            auto config = Slic3r::DynamicPrintConfig::full_print_config_with({
                { "skirts",                     0 },
                { "perimeters",                 3 },
                { "layer_height",               0.4 },
                { "first_layer_height",         0.35 },
                { "extra_perimeters",           1 },
                // to prevent speeds from being altered
                { "cooling",                    "0" },
                // to prevent speeds from being altered
                { "first_layer_speed",          "100%" },
                { "perimeter_speed",            99 },
                { "external_perimeter_speed",   99 },
                { "small_perimeter_speed",      99 },
                { "thin_walls",                 0 },
            });
        
            std::string gcode = Slic3r::Test::slice({ Slic3r::Test::TestMesh::ipadstand }, config);
            // z => number of loops
            std::map<coord_t, int> perimeters;
            bool                   in_loop         = false;
            const double           perimeter_speed = config.opt_float("perimeter_speed") * 60.;
            GCodeReader            parser;
            parser.parse_buffer(gcode, [&perimeters, &in_loop, perimeter_speed](Slic3r::GCodeReader &self, const Slic3r::GCodeReader::GCodeLine &line)
            {
                if (line.extruding(self) && line.dist_XY(self) > 0 && is_approx<double>(line.new_F(self), perimeter_speed)) {
                    if (! in_loop) {
                        coord_t z = scaled<coord_t>(self.z());
                        auto it = perimeters.find(z);
                        if (it == perimeters.end())
                            it = perimeters.insert(std::make_pair(z, 0)).first;
                        ++ it->second;
                    }
                    in_loop = true;
                } else if (! line.cmd_is("M73")) {
                    // skips remaining time lines (M73)
                    in_loop = false;
                }
            });
            THEN("no superfluous extra perimeters") {
                const int num_perimeters = config.opt_int("perimeters");
                size_t extra_perimeters = std::count_if(perimeters.begin(), perimeters.end(), [num_perimeters](const std::pair<const coord_t, int> &v){ return (v.second % num_perimeters) > 0; });
                REQUIRE(extra_perimeters == 0);
            }
        }
    }
}

SCENARIO("Some weird coverage test", "[Perimeters]")
{
    auto config = Slic3r::DynamicPrintConfig::full_print_config_with({
        { "nozzle_diameter",                    "0.4" },
        { "perimeters",                         2 },
        { "perimeter_extrusion_width",          0.4 },
        { "external_perimeter_extrusion_width", 0.4 },
        { "infill_extrusion_width",             0.53 },
        { "solid_infill_extrusion_width",       0.53 }
    });

    // we just need a pre-filled Print object
    Print print;
    Model model;
    Slic3r::Test::init_print({ Test::TestMesh::cube_20x20x20 }, print, model, config);
    
    // override a layer's slices
    ExPolygon expolygon;
    expolygon.contour = {
        {-71974463,-139999376},{-71731792,-139987456},{-71706544,-139985616},{-71682119,-139982639},{-71441248,-139946912},{-71417487,-139942895},{-71379384,-139933984},{-71141800,-139874480},
        {-71105247,-139862895},{-70873544,-139779984},{-70838592,-139765856},{-70614943,-139660064},{-70581783,-139643567},{-70368368,-139515680},{-70323751,-139487872},{-70122160,-139338352},
        {-70082399,-139306639},{-69894800,-139136624},{-69878679,-139121327},{-69707992,-138933008},{-69668575,-138887343},{-69518775,-138685359},{-69484336,-138631632},{-69356423,-138418207},
        {-69250040,-138193296},{-69220920,-138128976},{-69137992,-137897168},{-69126095,-137860255},{-69066568,-137622608},{-69057104,-137582511},{-69053079,-137558751},{-69017352,-137317872},
        {-69014392,-137293456},{-69012543,-137268207},{-68999369,-137000000},{-63999999,-137000000},{-63705947,-136985551},{-63654984,-136977984},{-63414731,-136942351},{-63364756,-136929840},
        {-63129151,-136870815},{-62851950,-136771631},{-62585807,-136645743},{-62377483,-136520895},{-62333291,-136494415},{-62291908,-136463728},{-62096819,-136319023},{-62058644,-136284432},
        {-61878676,-136121328},{-61680968,-135903184},{-61650275,-135861807},{-61505591,-135666719},{-61354239,-135414191},{-61332211,-135367615},{-61228359,-135148063},{-61129179,-134870847},
        {-61057639,-134585262},{-61014451,-134294047},{-61000000,-134000000},{-61000000,-107999999},{-61014451,-107705944},{-61057639,-107414736},{-61129179,-107129152},{-61228359,-106851953},
        {-61354239,-106585808},{-61505591,-106333288},{-61680967,-106096816},{-61878675,-105878680},{-62096820,-105680967},{-62138204,-105650279},{-62333292,-105505591},{-62585808,-105354239},
        {-62632384,-105332207},{-62851951,-105228360},{-62900463,-105211008},{-63129152,-105129183},{-63414731,-105057640},{-63705947,-105014448},{-63999999,-105000000},{-68999369,-105000000},
        {-69012543,-104731792},{-69014392,-104706544},{-69017352,-104682119},{-69053079,-104441248},{-69057104,-104417487},{-69066008,-104379383},{-69125528,-104141799},{-69137111,-104105248},
        {-69220007,-103873544},{-69234136,-103838591},{-69339920,-103614943},{-69356415,-103581784},{-69484328,-103368367},{-69512143,-103323752},{-69661647,-103122160},{-69693352,-103082399},
        {-69863383,-102894800},{-69878680,-102878679},{-70066999,-102707992},{-70112656,-102668576},{-70314648,-102518775},{-70368367,-102484336},{-70581783,-102356424},{-70806711,-102250040},
        {-70871040,-102220919},{-71102823,-102137992},{-71139752,-102126095},{-71377383,-102066568},{-71417487,-102057104},{-71441248,-102053079},{-71682119,-102017352},{-71706535,-102014392},
        {-71731784,-102012543},{-71974456,-102000624},{-71999999,-102000000},{-104000000,-102000000},{-104025536,-102000624},{-104268207,-102012543},{-104293455,-102014392},
        {-104317880,-102017352},{-104558751,-102053079},{-104582512,-102057104},{-104620616,-102066008},{-104858200,-102125528},{-104894751,-102137111},{-105126455,-102220007},
        {-105161408,-102234136},{-105385056,-102339920},{-105418215,-102356415},{-105631632,-102484328},{-105676247,-102512143},{-105877839,-102661647},{-105917600,-102693352},
        {-106105199,-102863383},{-106121320,-102878680},{-106292007,-103066999},{-106331424,-103112656},{-106481224,-103314648},{-106515663,-103368367},{-106643575,-103581783},
        {-106749959,-103806711},{-106779080,-103871040},{-106862007,-104102823},{-106873904,-104139752},{-106933431,-104377383},{-106942896,-104417487},{-106946920,-104441248},
        {-106982648,-104682119},{-106985607,-104706535},{-106987456,-104731784},{-107000630,-105000000},{-112000000,-105000000},{-112294056,-105014448},{-112585264,-105057640},
        {-112870848,-105129184},{-112919359,-105146535},{-113148048,-105228360},{-113194624,-105250392},{-113414191,-105354239},{-113666711,-105505591},{-113708095,-105536279},
        {-113903183,-105680967},{-114121320,-105878679},{-114319032,-106096816},{-114349720,-106138200},{-114494408,-106333288},{-114645760,-106585808},{-114667792,-106632384},
        {-114771640,-106851952},{-114788991,-106900463},{-114870815,-107129151},{-114942359,-107414735},{-114985551,-107705943},{-115000000,-107999999},{-115000000,-134000000},
        {-114985551,-134294048},{-114942359,-134585263},{-114870816,-134870847},{-114853464,-134919359},{-114771639,-135148064},{-114645759,-135414192},{-114494407,-135666720},
        {-114319031,-135903184},{-114121320,-136121327},{-114083144,-136155919},{-113903184,-136319023},{-113861799,-136349712},{-113666711,-136494416},{-113458383,-136619264},
        {-113414192,-136645743},{-113148049,-136771631},{-112870848,-136870815},{-112820872,-136883327},{-112585264,-136942351},{-112534303,-136949920},{-112294056,-136985551},
        {-112000000,-137000000},{-107000630,-137000000},{-106987456,-137268207},{-106985608,-137293440},{-106982647,-137317872},{-106946920,-137558751},{-106942896,-137582511},
        {-106933991,-137620624},{-106874471,-137858208},{-106862888,-137894751},{-106779992,-138126463},{-106765863,-138161424},{-106660080,-138385055},{-106643584,-138418223},
        {-106515671,-138631648},{-106487855,-138676256},{-106338352,-138877839},{-106306647,-138917600},{-106136616,-139105199},{-106121320,-139121328},{-105933000,-139291999},
        {-105887344,-139331407},{-105685351,-139481232},{-105631632,-139515663},{-105418216,-139643567},{-105193288,-139749951},{-105128959,-139779072},{-104897175,-139862016},
        {-104860247,-139873904},{-104622616,-139933423},{-104582511,-139942896},{-104558751,-139946912},{-104317880,-139982656},{-104293463,-139985616},{-104268216,-139987456},
        {-104025544,-139999376},{-104000000,-140000000},{-71999999,-140000000}
    };
    expolygon.holes = {
        {{-105000000,-138000000},{-105000000,-104000000},{-71000000,-104000000},{-71000000,-138000000}},
        {{-69000000,-132000000},{-69000000,-110000000},{-64991180,-110000000},{-64991180,-132000000}},
        {{-111008824,-132000000},{-111008824,-110000000},{-107000000,-110000000},{-107000000,-132000000}}
    };
    PrintObject *object = print.get_object(0);
    object->slice();
    Layer       *layer = object->get_layer(1);
    LayerRegion *layerm = layer->get_region(0);
    layerm->slices.clear();
    layerm->slices.append({ expolygon }, stInternal);
    
    // make perimeters
    layer->make_perimeters();
    
    // compute the covered area
    Flow pflow = layerm->flow(frPerimeter);
    Flow iflow = layerm->flow(frInfill);
    Polygons covered_by_perimeters;
    Polygons covered_by_infill;
    {
        Polygons acc;
        for (const ExtrusionEntity *ee : layerm->perimeters.entities)
            for (const ExtrusionEntity *ee : dynamic_cast<const ExtrusionEntityCollection*>(ee)->entities)
                append(acc, offset(dynamic_cast<const ExtrusionLoop*>(ee)->polygon().split_at_first_point(), float(pflow.scaled_width() / 2.f + SCALED_EPSILON)));
        covered_by_perimeters = union_(acc);
    }
    {
        Polygons acc;
        for (const Surface &surface : layerm->fill_surfaces.surfaces)
            append(acc, to_polygons(surface.expolygon));
        for (const ExtrusionEntity *ee : layerm->thin_fills.entities)
            append(acc, offset(dynamic_cast<const ExtrusionPath*>(ee)->polyline, float(iflow.scaled_width() / 2.f + SCALED_EPSILON)));
        covered_by_infill = union_(acc);
    }
    
    // compute the non covered area
    ExPolygons non_covered = diff_ex(to_polygons(layerm->slices.surfaces), union_(covered_by_perimeters, covered_by_infill));
    
    /*
    if (0) {
        printf "max non covered = %f\n", List::Util::max(map unscale unscale $_->area, @$non_covered);
        require "Slic3r/SVG.pm";
        Slic3r::SVG::output(
            "gaps.svg",
            expolygons          => [ map $_->expolygon, @{$layerm->slices} ],
            red_expolygons      => union_ex([ map @$_, (@$covered_by_perimeters, @$covered_by_infill) ]),
            green_expolygons    => union_ex($non_covered),
            no_arrows           => 1,
            polylines           => [
                map $_->polygon->split_at_first_point, map @$_, @{$layerm->perimeters},
            ],
        );
    }
    */
    THEN("no gap between perimeters and infill") {
        size_t num_non_convered = std::count_if(non_covered.begin(), non_covered.end(), [&iflow](const ExPolygon &ex){ return ex.area() > sqr(double(iflow.scaled_width())); });
        REQUIRE(num_non_convered == 0);
    }
}

SCENARIO("Perimeters3", "[Perimeters]")
{
    auto config = Slic3r::DynamicPrintConfig::full_print_config_with({
        { "skirts",                 0 },
        { "perimeters",             3 },
        { "layer_height",           0.4 },
        { "bridge_speed",           99 },
        // to prevent bridging over sparse infill
        { "fill_density",           0 },
        { "overhangs",              true },
        // to prevent speeds from being altered
        { "cooling",                "0" },
        // to prevent speeds from being altered
        { "first_layer_speed",      "100%" }
    });

    auto test = [&config](const Vec3d &scale) {
        std::string         gcode = Slic3r::Test::slice({ mesh(Slic3r::Test::TestMesh::V, Vec3d::Zero(), scale) }, config);
        GCodeReader         parser;
        std::set<coord_t>   z_with_bridges;
        const double        bridge_speed = config.opt_float("bridge_speed") * 60.;
        parser.parse_buffer(gcode, [&z_with_bridges, bridge_speed](Slic3r::GCodeReader &self, const Slic3r::GCodeReader::GCodeLine &line)
        {
            if (line.extruding(self) && line.dist_XY(self) > 0 && is_approx<double>(line.new_F(self), bridge_speed))
                z_with_bridges.insert(scaled<coord_t>(self.z()));
        });
        return z_with_bridges.size();
    };

    GIVEN("V shape, unscaled") {
        int n = test(Vec3d(1., 1., 1.));
        // except for the two internal solid layers above void
        THEN("no overhangs printed with bridge speed") {
            REQUIRE(n == 1);
        }
    }
    GIVEN("V shape, scaled 3x in X") {
        int n = test(Vec3d(3., 1., 1.));
        // except for the two internal solid layers above void
        THEN("overhangs printed with bridge speed") {
            REQUIRE(n > 2);
        }
    }
}

SCENARIO("Perimeters4", "[Perimeters]")
{
    auto config = Slic3r::DynamicPrintConfig::full_print_config_with({
        { "seam_position",        "random" }
    });
    std::string gcode = Slic3r::Test::slice({ Slic3r::Test::TestMesh::cube_20x20x20 }, config);
    THEN("successful generation of G-code with seam_position = random") {
        REQUIRE(! gcode.empty());
    }
}

SCENARIO("Seam alignment", "[Perimeters]")
{
    auto test = [](Test::TestMesh model) {
        auto config = Slic3r::DynamicPrintConfig::full_print_config_with({
            { "seam_position",          "aligned" },
            { "skirts",                 0 },
            { "perimeters",             1 },
            { "fill_density",           0 },
            { "top_solid_layers",       0 },
            { "bottom_solid_layers",    0 },
            { "retract_layer_change",   "0" }
        });
        std::string gcode = Slic3r::Test::slice({ model }, config);
        bool        was_extruding = false;
        Points      seam_points;
        GCodeReader parser;
        parser.parse_buffer(gcode, [&was_extruding, &seam_points](Slic3r::GCodeReader &self, const Slic3r::GCodeReader::GCodeLine &line)
        {
            if (line.extruding(self)) {
                if (! was_extruding)
                    seam_points.emplace_back(self.xy_scaled());
                was_extruding = true;
            } else if (! line.cmd_is("M73")) {
                // skips remaining time lines (M73)
                was_extruding = false;
            }
        });
        THEN("seam is aligned") {
            size_t num_not_aligned = 0;
            for (size_t i = 1; i < seam_points.size(); ++ i) {
                double d = (seam_points[i] - seam_points[i - 1]).cast<double>().norm();
                // Seams shall be aligned up to 3mm.
                if (d > scaled<double>(3.))
                    ++ num_not_aligned;
            }
            REQUIRE(num_not_aligned == 0);
        }
    };

    GIVEN("20mm cube") {
        test(Slic3r::Test::TestMesh::cube_20x20x20);
    }
    GIVEN("small_dorito") {
        test(Slic3r::Test::TestMesh::small_dorito);
    }
}
