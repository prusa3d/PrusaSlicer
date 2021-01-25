
//#define CATCH_CONFIG_DISABLE

#include <catch2/catch.hpp>
#include "test_data.hpp"
#include <libslic3r/ClipperUtils.hpp>
#include <libslic3r/MedialAxis.hpp>
#include <libslic3r/SVG.hpp>
#include <libslic3r/GCode.hpp>

using namespace Slic3r;
using namespace Slic3r::Geometry;
using namespace Slic3r::Test;

class ExtrusionVolumeVisitor : public ExtrusionVisitorConst {
    double volume = 0;
public:
    virtual void use(const ExtrusionPath &path) override { 
        for (int i = 0; i < path.polyline.size() - 1; i++) volume += path.polyline.points[i].distance_to(path.polyline.points[i + 1]) * path.mm3_per_mm;
    };
    virtual void use(const ExtrusionPath3D &path3D) override { std::cout << "error, not supported"; };
    virtual void use(const ExtrusionMultiPath &multipath) override {
        for (const ExtrusionPath &path : multipath.paths) use(path);
    }
    virtual void use(const ExtrusionMultiPath3D &multipath) override { std::cout << "error, not supported"; };
    virtual void use(const ExtrusionLoop &loop) override {
        for (const ExtrusionEntity &path : loop.paths) path.visit(*this);
    }
    virtual void use(const ExtrusionEntityCollection &collection) override {
        for (const ExtrusionEntity *path : collection.entities) path->visit(*this);
    }
    double compute(const ExtrusionEntity &entity) && {
        entity.visit(*this);
        return volume;
    }
};


SCENARIO("extrude_thinwalls") {
    GIVEN("ThickLine") {
        ExPolygon expolygon;
        expolygon.contour = Slic3r::Polygon{ Points{
            Point::new_scale(-0.5, 0),
            Point::new_scale(0.5, 0),
            Point::new_scale(0.3, 10),
            Point::new_scale(-0.3, 10) } };
        ThickPolylines res;

        MedialAxis{ expolygon, scale_(1.1), scale_(0.5), scale_(0.2) }.build(res);
        Flow periflow{ 1.1, 0.2, 0.4 };
        ExtrusionEntityCollection gap_fill = thin_variable_width(res, erGapFill, periflow);


        //std::string gcode = gcodegen.get_visitor_gcode();
        THEN("analyse extrusion.") {
            ExtrusionVolumeVisitor vis;
            std::cout << " volume is " << ExtrusionVolumeVisitor{}.compute(gap_fill) << "\n";
            std::cout << " wanted volume is " << ((0.6*0.2 * 10) + (0.2*0.2 * 10)) << "\n";
            REQUIRE(std::abs(ExtrusionVolumeVisitor{}.compute(gap_fill) - ((0.6*0.2 * 10) + (0.2*0.2 * 10)))<0.01);
        }
    }
}

SCENARIO("thin walls: ")
{


    GIVEN("Square")
    {
        Points test_set;
        test_set.reserve(4);
        Points square {Point::new_scale(100, 100),
			Point::new_scale(200, 100),
			Point::new_scale(200, 200),
			Point::new_scale(100, 200)};
        Slic3r::Polygon hole_in_square{ Points{
            Point::new_scale(140, 140),
            Point::new_scale(140, 160),
            Point::new_scale(160, 160),
            Point::new_scale(160, 140) } };
        ExPolygon expolygon;
        expolygon.contour = Slic3r::Polygon{ square };
        expolygon.holes = Slic3r::Polygons{ hole_in_square };
        WHEN("creating the medial axis"){
            Polylines res;
            expolygon.medial_axis(scale_(40), scale_(0.5), &res);

            THEN("medial axis of a square shape is a single path"){
                REQUIRE(res.size() == 1);
            }
            THEN("polyline forms a closed loop"){
                REQUIRE(res[0].first_point().coincides_with(res[0].last_point()) == true);
            }
            THEN("medial axis loop has reasonable length"){
                REQUIRE(res[0].length() > expolygon.holes[0].length());
                REQUIRE(res[0].length() < expolygon.contour.length());
            }
        }
    }

    GIVEN("narrow rectangle") {
        ExPolygon expolygon;
        expolygon.contour = Slic3r::Polygon{ Points{
            Point::new_scale(100, 100),
            Point::new_scale(120, 100),
            Point::new_scale(120, 200),
            Point::new_scale(100, 200) } };
        Polylines res;
        expolygon.medial_axis(scale_(20), scale_(0.5), &res);

        ExPolygon expolygon2;
        expolygon2.contour = Slic3r::Polygon{ Points{
            Point::new_scale(100, 100),
            Point::new_scale(120, 100),
            Point::new_scale(120, 200),
            Point::new_scale(105, 200), // extra point in the short side
            Point::new_scale(100, 200) } };
        Polylines res2;
        expolygon.medial_axis(scale_(20), scale_(0.5), &res2);
        WHEN("creating the medial axis") {

            THEN("medial axis of a narrow rectangle is a single line") {
                REQUIRE(res.size() == 1);
                THEN("medial axis has reasonable length") {
                    REQUIRE(res[0].length() >= scale_(200 - 100 - (120 - 100)) - SCALED_EPSILON);
                }
            }
            THEN("medial axis of a narrow rectangle with an extra vertex is still a single line") {
                REQUIRE(res2.size() == 1);
                THEN("medial axis of a narrow rectangle with an extra vertex has reasonable length") {
                    REQUIRE(res2[0].length() >= scale_(200 - 100 - (120 - 100)) - SCALED_EPSILON);
                }
                THEN("extra vertices don't influence medial axis") {
                    REQUIRE(res2[0].length() - res[0].length() < SCALED_EPSILON);
                }
            }
        }
    }

    //TODO: compare with mainline slic3r
    GIVEN("semicicumference") {
        ExPolygon expolygon;
        expolygon.contour = Slic3r::Polygon{ Points{
            Point{ 1185881, 829367 }, Point{ 1421988, 1578184 }, Point{ 1722442, 2303558 }, Point{ 2084981, 2999998 }, Point{ 2506843, 3662186 }, Point{ 2984809, 4285086 }, Point{ 3515250, 4863959 }, Point{ 4094122, 5394400 }, Point{ 4717018, 5872368 }, 
            Point{ 5379210, 6294226 }, Point{ 6075653, 6656769 }, Point{ 6801033, 6957229 }, Point{ 7549842, 7193328 }, Point{ 8316383, 7363266 }, Point{ 9094809, 7465751 }, Point{ 9879211, 7500000 }, Point{ 10663611, 7465750 }, Point{ 11442038, 7363265 }, 
            Point{ 12208580, 7193327 }, Point{ 12957389, 6957228 }, Point{ 13682769, 6656768 }, Point{ 14379209, 6294227 }, Point{ 15041405, 5872366 }, Point{ 15664297, 5394401 }, Point{ 16243171, 4863960 }, Point{ 16758641, 4301424 }, Point{ 17251579, 3662185 }, 
            Point{ 17673439, 3000000 }, Point{ 18035980, 2303556 }, Point{ 18336441, 1578177 }, Point{ 18572539, 829368 }, Point{ 18750748, 0 }, Point{ 19758422, 0 }, Point{ 19727293, 236479 }, Point{ 19538467, 1088188 }, Point{ 19276136, 1920196 }, 
            Point{ 18942292, 2726179 }, Point{ 18539460, 3499999 }, Point{ 18070731, 4235755 }, Point{ 17539650, 4927877 }, Point{ 16950279, 5571067 }, Point{ 16307090, 6160437 }, Point{ 15614974, 6691519 }, Point{ 14879209, 7160248 }, Point{ 14105392, 7563079 }, 
            Point{ 13299407, 7896927 }, Point{ 12467399, 8159255 }, Point{ 11615691, 8348082 }, Point{ 10750769, 8461952 }, Point{ 9879211, 8500000 }, Point{ 9007652, 8461952 }, Point{ 8142729, 8348082 }, Point{ 7291022, 8159255 }, Point{ 6459015, 7896927 },
            Point{ 5653029, 7563079 }, Point{ 4879210, 7160247 }, Point{ 4143447, 6691519 }, Point{ 3451331, 6160437 }, Point{ 2808141, 5571066 }, Point{ 2218773, 4927878 }, Point{ 1687689, 4235755 }, Point{ 1218962, 3499999 }, Point{ 827499, 2748020 }, 
            Point{ 482284, 1920196 }, Point{ 219954, 1088186 }, Point{ 31126, 236479 }, Point{ 0, 0 }, Point{ 1005754, 0 }
        } };

        WHEN("creating the medial axis") {
            Polylines res;
            expolygon.medial_axis(scale_(1.324888), scale_(0.25), &res);

            THEN("medial axis of a semicircumference is a single line") {
                REQUIRE(res.size() == 1);
            }
            THEN("all medial axis segments of a semicircumference have the same orientation (but the 2 end points)") {
                Lines lines = res[0].lines();
                double min_angle = 1, max_angle = -1;
                //std::cout << "first angle=" << lines[0].ccw(lines[1].b) << "\n";
                for (int idx = 1; idx < lines.size() - 1; idx++) {
                    double angle = lines[idx - 1].ccw(lines[idx].b);
                    if (std::abs(angle) - EPSILON < 0) angle = 0;
                    //if (angle < 0) std::cout << unscale_(lines[idx - 1].a.x()) << ":" << unscale_(lines[idx - 1].a.y()) << " -> " << unscale_(lines[idx - 1].b.x()) << ":" << unscale_(lines[idx - 1].b.y()) << " -> " << unscale_(lines[idx].b.x()) << ":" << unscale_(lines[idx].b.y()) << "\n";
                    std::cout << "angle=" << 180 * lines[idx].a.ccw_angle(lines[idx - 1].a, lines[idx].b) / PI << "\n";
                    min_angle = std::min(min_angle, angle);
                    max_angle = std::max(max_angle, angle);
                }
                //std::cout << "last angle=" << lines[lines.size() - 2].ccw(lines[lines.size() - 1].b) << "\n";
                // check whether turns are all CCW or all CW
                bool allccw = (min_angle <= 0 && max_angle <= 0);
                bool allcw = (min_angle >= 0 && max_angle >= 0);
                bool allsame_orientation = allccw || allcw;
                REQUIRE(allsame_orientation);
            }
        }
    }
    

    GIVEN("round with large and very small distance between points"){
        ExPolygon expolygon;
        expolygon.contour = Slic3r::Polygon{ Points{
            Point::new_scale(15.181601,-2.389639), Point::new_scale(15.112616,-1.320034), Point::new_scale(14.024491,-0.644338), Point::new_scale(13.978982,-0.624495), Point::new_scale(9.993299,0.855584), Point::new_scale(9.941970,0.871195), Point::new_scale(5.796743,1.872643),
            Point::new_scale(5.743826,1.882168), Point::new_scale(1.509170,2.386464), Point::new_scale(1.455460,2.389639), Point::new_scale(-2.809359,2.389639), Point::new_scale(-2.862805,2.386464), Point::new_scale(-7.097726,1.882168), Point::new_scale(-7.150378,1.872643), Point::new_scale(-11.286344,0.873576),
            Point::new_scale(-11.335028,0.858759), Point::new_scale(-14.348632,-0.237938), Point::new_scale(-14.360538,-0.242436), Point::new_scale(-15.181601,-0.737570), Point::new_scale(-15.171309,-2.388509)
        } };
        expolygon.holes.push_back(Slic3r::Polygon{ Points{
            Point::new_scale( -11.023311,-1.034226 ), Point::new_scale( -6.920984,-0.042941 ), Point::new_scale( -2.768613,0.463207 ), Point::new_scale( 1.414714,0.463207 ), Point::new_scale( 5.567085,-0.042941 ), Point::new_scale( 9.627910,-1.047563 )
            } });

        WHEN("creating the medial axis"){
            Polylines res;
            expolygon.medial_axis(scale_(2.5), scale_(0.5), &res);

           THEN("medial axis of it is two line"){
                REQUIRE(res.size() == 2);
            }
        }
    }

    GIVEN("french cross")
    {
        ExPolygon expolygon;
        expolygon.contour = Slic3r::Polygon{ Points{
            Point::new_scale(4.3, 4), Point::new_scale(4.3, 0), Point::new_scale(4, 0), Point::new_scale(4, 4), Point::new_scale(0, 4), Point::new_scale(0, 4.5), Point::new_scale(4, 4.5), Point::new_scale(4, 10), Point::new_scale(4.3, 10), Point::new_scale(4.3, 4.5),
            Point::new_scale(6, 4.5), Point::new_scale(6, 10), Point::new_scale(6.2, 10), Point::new_scale(6.2, 4.5), Point::new_scale(10, 4.5), Point::new_scale(10, 4), Point::new_scale(6.2, 4), Point::new_scale(6.2, 0), Point::new_scale(6, 0), Point::new_scale(6, 4),
        } };
        expolygon.contour.make_counter_clockwise();

        WHEN("creating the medial axis"){
            Polylines res;
            expolygon.medial_axis(scale_(0.55), scale_(0.25), &res);

            THEN("medial axis of a (bit too narrow) french cross is two lines"){
                REQUIRE(res.size() == 2);
            }
            THEN("medial axis has reasonable length"){
                REQUIRE(res[0].length() >= scale_(9.9) - SCALED_EPSILON);
                REQUIRE(res[1].length() >= scale_(9.9) - SCALED_EPSILON);
            }

            THEN("medial axis of a (bit too narrow) french cross is two lines has only strait lines (first line)"){
                Lines lines = res[0].lines();
                double min_angle = 1, max_angle = -1;
                for (int idx = 1; idx < lines.size(); idx++){
                    double angle = lines[idx - 1].ccw(lines[idx].b);
                    min_angle = std::min(min_angle, angle);
                    max_angle = std::max(max_angle, angle);
                }
                REQUIRE(min_angle == max_angle);
                REQUIRE(min_angle == 0);
            }
            THEN("medial axis of a (bit too narrow) french cross is two lines has only strait lines (second line)"){
                Lines lines = res[1].lines();
                double min_angle = 1, max_angle = -1;
                for (int idx = 1; idx < lines.size(); idx++){
                    double angle = lines[idx - 1].ccw(lines[idx].b);
                    min_angle = std::min(min_angle, angle);
                    max_angle = std::max(max_angle, angle);
                }
                REQUIRE(min_angle == max_angle);
                REQUIRE(min_angle == 0);
            }
        }

    }


    //TODO: compare with mainline slic3r
    //GIVEN("tooth")
    //{
    //    ExPolygon expolygon;
    //    expolygon.contour = Slic3r::Polygon{ Points{
    //        Point::new_scale(0.86526705, 1.4509841), Point::new_scale(0.57696039, 1.8637021), 
    //        Point::new_scale(0.4502297, 2.5569978), Point::new_scale(0.45626199, 3.2965596), 
    //        Point::new_scale(1.1218851, 3.3049455), Point::new_scale(0.96681072, 2.8243202), 
    //        Point::new_scale(0.86328971, 2.2056997), Point::new_scale(0.85367905, 1.7790778)
    //    } };
    //    expolygon.contour.make_counter_clockwise();
    //    WHEN("creating the medial axis"){
    //        Polylines res;
    //        expolygon.medial_axis(scale_(1), scale_(0.25), &res);
    //        THEN("medial axis of a tooth is two lines"){
    //            REQUIRE(res.size() == 2);
    //            THEN("medial axis has reasonable length") {
    //                REQUIRE(res[0].length() >= scale_(1.4) - SCALED_EPSILON);
    //                REQUIRE(res[1].length() >= scale_(1.4) - SCALED_EPSILON);
    //                // TODO: check if min width is < 0.3 and max width is > 0.6 (min($res->[0]->width.front, $res->[0]->width.back) # problem: can't have access to width
    //                //TODO: now i have access! correct it!
    //            }
    //        }
    //    }
    //} 

    GIVEN("Anchor & Tapers")
    {
        ExPolygon tooth;
        tooth.contour = Slic3r::Polygon{ Points{
            Point::new_scale(0,0), Point::new_scale(10,0), Point::new_scale(10,1.2), Point::new_scale(0,1.2)
        } };
        tooth.contour.make_counter_clockwise();
        ExPolygon base_part;
        base_part.contour = Slic3r::Polygon{ Points{
            Point::new_scale(0,-3), Point::new_scale(0,3), Point::new_scale(-2,3), Point::new_scale(-2,-3)
        } };
        base_part.contour.make_counter_clockwise();
        //expolygon.contour = Slic3r::Polygon{ Points{
        //    //Point::new_scale(0, 13), Point::new_scale(-1, 13), Point::new_scale(-1, 0), Point::new_scale(0.0,0.0),
        //    Point::new_scale(0,0.2), Point::new_scale(3,0.2), Point::new_scale(3,0.4), Point::new_scale(0,0.4),
        //    Point::new_scale(0,1), Point::new_scale(3,1), Point::new_scale(3,1.3), Point::new_scale(0,1.3),
        //    Point::new_scale(0,2), Point::new_scale(3,2), Point::new_scale(3,2.4), Point::new_scale(0,2.4),
        //    Point::new_scale(0,3), Point::new_scale(3,3), Point::new_scale(3,3.5), Point::new_scale(0,3.5),
        //    Point::new_scale(0,4), Point::new_scale(3,4), Point::new_scale(3,4.6), Point::new_scale(0,4.6),
        //    Point::new_scale(0,5), Point::new_scale(3,5), Point::new_scale(3,5.7), Point::new_scale(0,5.7),
        //    Point::new_scale(0,6), Point::new_scale(3,6), Point::new_scale(3,6.8), Point::new_scale(0,6.8),
        //    Point::new_scale(0,7.5), Point::new_scale(3,7.5), Point::new_scale(3,8.4), Point::new_scale(0,8.4),
        //    Point::new_scale(0,9), Point::new_scale(3,9), Point::new_scale(3,10), Point::new_scale(0,10),
        //    Point::new_scale(0,11), Point::new_scale(3,11), Point::new_scale(3,12.2), Point::new_scale(0,12.2),
        //} };
        WHEN("1 nozzle, 0.2 layer height") {
            const coord_t nozzle_diam = scale_(1);
            ExPolygon anchor = union_ex(ExPolygons{ tooth }, intersection_ex(ExPolygons{ base_part }, offset_ex(tooth, nozzle_diam / 2)), true)[0];
            ThickPolylines res;
            //expolygon.medial_axis(scale_(1), scale_(0.5), &res);
            Slic3r::MedialAxis ma(tooth, nozzle_diam * 2, nozzle_diam/3, scale_(0.2));
            ma.use_bounds(anchor)
                .use_min_real_width(nozzle_diam)
                .use_tapers(0.25*nozzle_diam);
            ma.build(res);
            THEN("medial axis of a simple line is one line") {
                REQUIRE(res.size() == 1);
                THEN("medial axis has the length of the line + the length of the anchor") {
                    std::cout << res[0].length() << "\n";
                    REQUIRE(std::abs(res[0].length() - scale_(10.5)) < SCALED_EPSILON);
                }
                THEN("medial axis has the line width as max width") {
                    double max_width = 0;
                    for (coordf_t width : res[0].width) max_width = std::max(max_width, width);
                    REQUIRE(std::abs(max_width - scale_(1.2)) < SCALED_EPSILON);
                }
                //compute the length of the tapers
                THEN("medial axis has good tapers length") {
                    int l1 = 0;
                    for (size_t idx = 0; idx < res[0].width.size() - 1 && res[0].width[idx] - nozzle_diam < SCALED_EPSILON; ++idx)
                        l1 += res[0].lines()[idx].length();
                    int l2 = 0;
                    for (size_t idx = res[0].width.size() - 1; idx > 0 && res[0].width[idx] - nozzle_diam < SCALED_EPSILON; --idx)
                        l2 += res[0].lines()[idx - 1].length();
                    REQUIRE(std::abs(l1 - l2) < SCALED_EPSILON);
                    REQUIRE(std::abs(l1 - scale_(0.25 - 0.1)) < SCALED_EPSILON);
                }
            }
        }

        WHEN("1.2 nozzle, 0.6 layer height") {
            const coord_t nozzle_diam = scale_(1.2);
            ExPolygon anchor = union_ex(ExPolygons{ tooth }, intersection_ex(ExPolygons{ base_part }, offset_ex(tooth, nozzle_diam / 4)), true)[0];
            ThickPolylines res;
            //expolygon.medial_axis(scale_(1), scale_(0.5), &res);
            Slic3r::MedialAxis ma(tooth, nozzle_diam * 2, nozzle_diam/3, scale_(0.6));
            ma.use_bounds(anchor)
                .use_min_real_width(nozzle_diam)
                .use_tapers(1.0*nozzle_diam);
            ma.build(res);
            THEN("medial axis of a simple line is one line") {
                REQUIRE(res.size() == 1);
                THEN("medial axis has the length of the line + the length of the anchor") {
                    //0.3 because it's offseted by nozzle_diam / 4
                    REQUIRE(std::abs(res[0].length() - scale_(10.3)) < SCALED_EPSILON);
                }
                THEN("medial axis can'ty have a line width below Flow::new_from_spacing(nozzle_diam).width") {
                    double max_width = 0;
                    for (coordf_t width : res[0].width) max_width = std::max(max_width, width);
                    double min_width = Flow::new_from_spacing(float(unscale_(nozzle_diam)), float(unscale_(nozzle_diam)), 0.6f, false).scaled_width();
                    REQUIRE(std::abs(max_width - min_width) < SCALED_EPSILON);
                    REQUIRE(std::abs(max_width - nozzle_diam)  > SCALED_EPSILON);
                }
                //compute the length of the tapers
                THEN("medial axis has a 45� taper and a shorter one") {
                    int l1 = 0;
                    for (size_t idx = 0; idx < res[0].width.size() - 1 && res[0].width[idx] - scale_(1.2) < SCALED_EPSILON; ++idx)
                        l1 += res[0].lines()[idx].length();
                    int l2 = 0;
                    for (size_t idx = res[0].width.size() - 1; idx > 0 && res[0].width[idx] - scale_(1.2) < SCALED_EPSILON; --idx)
                        l2 += res[0].lines()[idx - 1].length();
                    //here the taper is limited by the 0-width spacing
                    double min_width = Flow::new_from_spacing(float(unscale_(nozzle_diam)), float(unscale_(nozzle_diam)), 0.6f, false).scaled_width();
                    REQUIRE(std::abs(l1 - l2) < SCALED_EPSILON);
                    REQUIRE(l1 < scale_(0.6));
                    REQUIRE(l1  > scale_(0.4));
                }
            }
        }

    }

    GIVEN("1� rotated tooths")
    {

    }

    GIVEN("narrow trapezoid")
    {
        ExPolygon expolygon;
        expolygon.contour = Slic3r::Polygon{ Points{
            Point::new_scale(100, 100),
            Point::new_scale(120, 100),
            Point::new_scale(112, 200),
            Point::new_scale(108, 200)
        } };
        WHEN("creating the medial axis"){
            Polylines res;
            expolygon.medial_axis(scale_(20), scale_(0.5), &res);
            THEN("medial axis of a narrow trapezoid is a single line"){
                REQUIRE(res.size() == 1);
                THEN("medial axis has reasonable length") {
                    REQUIRE(res[0].length() >= scale_(200 - 100 - (120 - 100)) - SCALED_EPSILON);
                }
            }
        }
    }

    GIVEN("L shape")
    {
        ExPolygon expolygon;
        expolygon.contour = Slic3r::Polygon{ Points{
            Point::new_scale(100, 100),
            Point::new_scale(120, 100),
            Point::new_scale(120, 180),
            Point::new_scale(200, 180),
            Point::new_scale(200, 200),
            Point::new_scale(100, 200)
        } };
        WHEN("creating the medial axis"){
            Polylines res;
            expolygon.medial_axis(scale_(20), scale_(0.5), &res);
            THEN("medial axis of a L shape is a single line"){
                REQUIRE(res.size() == 1);
                THEN("medial axis has reasonable length") {
                    // 20 is the thickness of the expolygon, which is subtracted from the ends
                    REQUIRE(res[0].length() + 20 > scale_(80 * 2) - SCALED_EPSILON);
                    REQUIRE(res[0].length() + 20 < scale_(100 * 2) + SCALED_EPSILON);
                }
            }
        }
    }

    GIVEN("shape"){
        ExPolygon expolygon;
        expolygon.contour = Slic3r::Polygon{ Points{
            Point{ -203064906, -51459966 }, Point{ -219312231, -51459966 }, Point{ -219335477, -51459962 }, Point{ -219376095, -51459962 }, Point{ -219412047, -51459966 },
            Point{ -219572094, -51459966 }, Point{ -219624814, -51459962 }, Point{ -219642183, -51459962 }, Point{ -219656665, -51459966 }, Point{ -220815482, -51459966 },
            Point{ -220815482, -37738966 }, Point{ -221117540, -37738966 }, Point{ -221117540, -51762024 }, Point{ -203064906, -51762024 },
        } };
        WHEN("creating the medial axis"){
            Polylines polylines;
            expolygon.medial_axis(819998, 102499.75, &polylines);
            double perimeter_len = expolygon.contour.split_at_first_point().length();
            THEN("medial axis has reasonable length"){
                double polyline_length = 0;
                for (Slic3r::Polyline &poly : polylines) polyline_length += poly.length();
                REQUIRE(polyline_length > perimeter_len * 3. / 8. - SCALED_EPSILON);
            }
        }
    }

    GIVEN("narrow triangle")
    {
        ExPolygon expolygon;
        expolygon.contour = Slic3r::Polygon{ Points{
            Point::new_scale(50, 100),
            Point::new_scale(1000, 102),
            Point::new_scale(50, 104)
        } };

        WHEN("creating the medial axis"){
            Polylines res;
            expolygon.medial_axis(scale_(4), scale_(0.5), &res);
            THEN("medial axis of a narrow triangle is a single line"){
                REQUIRE(res.size() == 1);
                THEN("medial axis has reasonable length") {
                    REQUIRE(res[0].length() > scale_(200 - 100 - (120 - 100)) - SCALED_EPSILON);
                }
            }
        }
    }

    GIVEN("GH #2474")
    {
        ExPolygon expolygon;
        expolygon.contour = Slic3r::Polygon{ Points{
            Point{91294454, 31032190},
            Point{11294481, 31032190},
            Point{11294481, 29967810},
            Point{44969182, 29967810},
            Point{89909960, 29967808},
            Point{91294454, 29967808}
        } };
        WHEN("creating the medial axis") {
            Polylines res;
            expolygon.medial_axis(1871238, 500000, &res);
            THEN("medial axis is a single polyline") {
                REQUIRE(res.size() == 1);
                Slic3r::Polyline polyline = res[0];
                THEN("medial axis is horizontal and is centered") {
                    double sum = 0;
                    for (Line &l : polyline.lines()) sum += std::abs(l.b.y() - l.a.y());
                    coord_t expected_y = expolygon.contour.bounding_box().center().y();
                    REQUIRE((sum / polyline.size()) - expected_y < SCALED_EPSILON);
                }

                // order polyline from left to right
                if (polyline.first_point().x() > polyline.last_point().x()) polyline.reverse();

                THEN("expected x_min & x_max") {
                    BoundingBox polyline_bb = polyline.bounding_box();
                    REQUIRE(polyline.first_point().x() == polyline_bb.min.x());
                    REQUIRE(polyline.last_point().x() == polyline_bb.max.x());
                }

                THEN("medial axis is not self-overlapping") {
                    //TODO
                    //REQUIRE(polyline.first_point().x() == polyline_bb.x_min());
                    std::vector<coord_t> all_x;
                    for (Point &p : polyline.points) all_x.push_back(p.x());
                    std::vector<coord_t> sorted_x{ all_x };
                    std::sort(sorted_x.begin(), sorted_x.end());
                    for (size_t i = 0; i < all_x.size(); i++) {
                        REQUIRE(all_x[i] == sorted_x[i]);
                    }
                }
            }
        }
    }

}
