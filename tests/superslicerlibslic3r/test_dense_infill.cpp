
//#define CATCH_CONFIG_DISABLE
//#include <catch2/catch.hpp>
#include <catch_main.hpp>
//#include <catch2/catch.hpp>

#include <string>
#include "test_data.hpp"
#include <libslic3r/libslic3r.h>
//#include <libslic3r/ModelArrange.hpp>
//#include <libslic3r/SVG.hpp>
//#include <libslic3r/SVG.hpp>
//#include <libslic3r/Print.hpp>

using namespace Slic3r::Test;
using namespace Slic3r;
using namespace std::literals;

Slic3r::Polygon get_polygon_scale(std::vector<std::vector<float>> points) {
    Slic3r::Polygon poly;
    for (std::vector<float>& point : points) {
        poly.points.push_back(Point::new_scale(point[0], point[1]));
    }
    return poly;
}

SCENARIO("test auto generation") {
    GIVEN("triangle with top to fill") {
        //DynamicPrintConfig& config = Slic3r::DynamicPrintConfig::full_print_config();
        //config.set_key_value("fill_density", new ConfigOptionPercent(0));
        //config.set_deserialize("nozzle_diameter", "0.4");
        //config.set_deserialize("layer_height", "0.3");
        //config.set_deserialize("infill_dense_algo", "50");
        //config.set_deserialize("extruder_clearance_radius", "10");
        WHEN("little surface") {
            ExPolygon polygon_to_cover;
            polygon_to_cover.contour = get_polygon_scale({ {0,0}, {10,0}, {10,10}, {0,10} });
            ExPolygon growing_area;
            growing_area.contour = get_polygon_scale({ {0,0}, {40,0}, {0,40} });
            ExPolygons allowedPoints;
            allowedPoints.emplace_back();
            //diff_ex(offset_ex(growing_area, scale_(1)), offset_ex(layerm->fill_no_overlap_expolygons, double(-layerm->flow(frInfill).scaled_width())));
            allowedPoints.back().contour = get_polygon_scale({ {0,0}, {40,0}, {0,40} });
            coord_t offset = scale_(2);
            float coverage = 1.f;

            ExPolygons solution = dense_fill_fit_to_size(polygon_to_cover, allowedPoints, growing_area, offset, coverage);
            THEN("little support") {
                double area_result = 0;
                for (ExPolygon& p : solution)
                    area_result += unscaled(unscaled(p.area()));
                double area_full = unscaled(unscaled(growing_area.area()));
                //for (ExPolygon& p : allowedPoints)
                //    area_full += unscaled(unscaled(p.area());
                for (ExPolygon& po : solution)
                    for (Point& p : po.contour.points)
                        std::cout << ", " << unscaled(p.x()) << ":" << unscaled(p.y());
                std::cout << "\n";
                std::cout << "area_result= " << area_result << "\n";
                std::cout << "area_full  = " << area_full << "\n";
                REQUIRE(area_full > 1.5 * area_result);
            }
        }
            //THEN("(too near)") {
            //    result = init_print_with_dist(config, 40)->validate();
            //    REQUIRE(result.first == PrintBase::PrintValidationError::pveWrongPosition);
            //}
            //THEN("(ok far)") {
            //    result = init_print_with_dist(config, 40.8)->validate();
            //    REQUIRE(result.second == "");
            //    REQUIRE(result.first == PrintBase::PrintValidationError::pveNone);
            //}
    }
}

