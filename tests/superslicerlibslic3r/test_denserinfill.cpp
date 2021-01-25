
//#define CATCH_CONFIG_DISABLE

#include <catch2/catch.hpp>
#include "test_data.hpp"
#include <libslic3r/libslic3r.h>

using namespace Slic3r;
using namespace Slic3r::Geometry;


SCENARIO("denser infills: ")
{


    GIVEN("center hole")
    {
        WHEN("dense infill to enlarged") {
            Model model{};
            Print print{};
            DynamicPrintConfig &config = Slic3r::DynamicPrintConfig::full_print_config();
            config.set_key_value("layer_height", new ConfigOptionFloat(0.2));
            config.set_key_value("first_layer_height", new ConfigOptionFloatOrPercent(0.2, false));
            config.set_key_value("infill_dense", new ConfigOptionBool(true));
            config.set_key_value("infill_dense_algo", new ConfigOptionEnum<DenseInfillAlgo>(dfaEnlarged));
            config.save("C:\\Users\\Admin\\Desktop\\config_def.ini");
            Slic3r::Test::init_print(print, { Slic3r::Test::TestMesh::di_5mm_center_notch }, model, &config, false);
            print.process();
            PrintObject& object = *(print.objects().at(0));

            //for (int lidx = 0; lidx < object.layers().size(); lidx++) {
            //    std::cout << "layer " << lidx << " : \n";
            //    std::cout << " - region_count= " << object.layers()[lidx]->region_count() << "\n";
            //    for (int ridx = 0; ridx < object.layers()[lidx]->regions().size(); ridx++) {
            //        std::cout << "   region " << ridx << " : \n";
            //        std::cout << "    - fills= " << object.layers()[lidx]->regions()[ridx]->fills.entities.size() << "\n";
            //        std::cout << "    - surfaces= " << object.layers()[lidx]->regions()[ridx]->fill_surfaces.surfaces.size() << "\n";
            //        for (int sidx = 0; sidx < object.layers()[lidx]->regions()[ridx]->fill_surfaces.surfaces.size(); sidx++) {
            //            std::cout << "       - type= " << object.layers()[lidx]->regions()[ridx]->fill_surfaces.surfaces[sidx].surface_type << "\n";
            //        }
            //    }
            //}
            /*THEN("67 layers exist in the model") {
            REQUIRE(object.layers().size() == 25);
            }
            THEN("at layer 16 , there are 1 region") {
            REQUIRE(object.layers()[16]->region_count() == 1);
            }
            THEN("at layer 17 , there are 2 region") {
            REQUIRE(object.layers()[17]->region_count() == 2);
            }
            THEN("at layer 18 , there are 2 region") {
            REQUIRE(object.layers()[18]->region_count() == 2);
            }*/
            THEN("correct number of fills") {
                REQUIRE(object.layers()[20]->regions()[0]->fills.entities.size() == 1); //sparse
                REQUIRE(object.layers()[21]->regions()[0]->fills.entities.size() == 2); //sparse + dense
                REQUIRE(object.layers()[21]->regions()[0]->fill_surfaces.surfaces.size() == 2);
                REQUIRE(object.layers()[21]->regions()[0]->fill_surfaces.surfaces[0].surface_type == (SurfaceType::stDensSparse | SurfaceType::stPosInternal));
                REQUIRE(object.layers()[21]->regions()[0]->fill_surfaces.surfaces[1].surface_type == (SurfaceType::stDensSparse | SurfaceType::stPosInternal));
                REQUIRE(std::min(object.layers()[21]->regions()[0]->fill_surfaces.surfaces[0].maxNbSolidLayersOnTop, object.layers()[21]->regions()[0]->fill_surfaces.surfaces[1].maxNbSolidLayersOnTop) == 1);
                Surface* srfSparse = &object.layers()[21]->regions()[0]->fill_surfaces.surfaces[0];
                Surface* srfDense = &object.layers()[21]->regions()[0]->fill_surfaces.surfaces[1];
                if (srfSparse->maxNbSolidLayersOnTop == 1) {
                    srfSparse = srfDense;
                    srfDense = &object.layers()[21]->regions()[0]->fill_surfaces.surfaces[0];
                }
                //std::cout << "sparse area = " << unscaled(unscaled(srfSparse->area())) << " , dense area = " << unscaled(unscaled(srfDense->area())) << "\n";
                REQUIRE(unscaled(unscaled(srfSparse->area())) > unscaled(unscaled(srfDense->area())));
                REQUIRE(object.layers()[22]->regions()[0]->fills.entities.size() == 2); //sparse + solid-bridge
                REQUIRE(object.layers()[22]->regions()[0]->fill_surfaces.surfaces.size() == 2);
                if (object.layers()[22]->regions()[0]->fill_surfaces.surfaces[0].surface_type == (SurfaceType::stDensSparse | SurfaceType::stPosInternal)) {
                    REQUIRE(object.layers()[22]->regions()[0]->fill_surfaces.surfaces[1].surface_type == (SurfaceType::stDensSolid | SurfaceType::stPosInternal | SurfaceType::stModBridge));
                } else {
                    REQUIRE(object.layers()[22]->regions()[0]->fill_surfaces.surfaces[0].surface_type == (SurfaceType::stDensSolid | SurfaceType::stPosInternal | SurfaceType::stModBridge));
                    REQUIRE(object.layers()[22]->regions()[0]->fill_surfaces.surfaces[1].surface_type == (SurfaceType::stDensSparse | SurfaceType::stPosInternal));
                }
                REQUIRE(object.layers()[23]->regions()[0]->fills.entities.size() == 2); //sparse + solid
                REQUIRE(object.layers()[24]->regions()[0]->fills.entities.size() == 3); //sparse + solid-top + solid-top (over perimeters)
                REQUIRE(object.layers()[25]->regions()[0]->fills.entities.size() == 1); //sparse
            }
        }
    }

    GIVEN("side hole")
    {
        WHEN("dense infill to auto") {
            Model model{};
            Print print{};
            DynamicPrintConfig &config = Slic3r::DynamicPrintConfig::full_print_config();
            config.set_key_value("layer_height", new ConfigOptionFloat(0.2));
            config.set_key_value("first_layer_height", new ConfigOptionFloatOrPercent(0.2, false));
            config.set_key_value("infill_dense", new ConfigOptionBool(true));
            config.set_key_value("infill_dense_algo", new ConfigOptionEnum<DenseInfillAlgo>(dfaAutomatic));
            config.save("C:\\Users\\Admin\\Desktop\\config_def.ini");
            Slic3r::Test::init_print(print, { Slic3r::Test::TestMesh::di_10mm_notch }, model, &config, false);
            print.process();
            PrintObject& object = *(print.objects().at(0));
            THEN("correct number of fills") {
                REQUIRE(object.layers().size() == 50);
                REQUIRE(object.layers()[20]->regions()[0]->fills.entities.size() == 1); //sparse
                REQUIRE(object.layers()[21]->regions()[0]->fills.entities.size() == 2); //sparse + dense
                REQUIRE(object.layers()[21]->regions()[0]->fill_surfaces.surfaces.size() == 2);
                REQUIRE(object.layers()[21]->regions()[0]->fill_surfaces.surfaces[0].surface_type == (SurfaceType::stDensSparse | SurfaceType::stPosInternal));
                REQUIRE(object.layers()[21]->regions()[0]->fill_surfaces.surfaces[1].surface_type == (SurfaceType::stDensSparse | SurfaceType::stPosInternal));
                REQUIRE(std::min(object.layers()[21]->regions()[0]->fill_surfaces.surfaces[0].maxNbSolidLayersOnTop, object.layers()[21]->regions()[0]->fill_surfaces.surfaces[1].maxNbSolidLayersOnTop) == 1);
                Surface* srfSparse = &object.layers()[21]->regions()[0]->fill_surfaces.surfaces[0];
                Surface* srfDense = &object.layers()[21]->regions()[0]->fill_surfaces.surfaces[1];
                if (srfSparse->maxNbSolidLayersOnTop == 1) {
                    srfSparse = srfDense;
                    srfDense = &object.layers()[21]->regions()[0]->fill_surfaces.surfaces[0];
                }
                REQUIRE(unscaled(unscaled(srfSparse->area())) > unscaled(unscaled(srfDense->area())));
                REQUIRE(object.layers()[22]->regions()[0]->fills.entities.size() == 2); //sparse + solid-bridge
                REQUIRE(object.layers()[22]->regions()[0]->fill_surfaces.surfaces.size() == 2);
                if (object.layers()[22]->regions()[0]->fill_surfaces.surfaces[0].surface_type == (SurfaceType::stDensSparse | SurfaceType::stPosInternal)) {
                    REQUIRE(object.layers()[22]->regions()[0]->fill_surfaces.surfaces[1].surface_type == (SurfaceType::stDensSolid | SurfaceType::stPosInternal | SurfaceType::stModBridge));
                } else {
                    REQUIRE(object.layers()[22]->regions()[0]->fill_surfaces.surfaces[0].surface_type == (SurfaceType::stDensSolid | SurfaceType::stPosInternal | SurfaceType::stModBridge));
                    REQUIRE(object.layers()[22]->regions()[0]->fill_surfaces.surfaces[1].surface_type == (SurfaceType::stDensSparse | SurfaceType::stPosInternal));
                }
                REQUIRE(object.layers()[23]->regions()[0]->fills.entities.size() == 2); //sparse + solid
                REQUIRE(object.layers()[24]->regions()[0]->fills.entities.size() == 3); //sparse + solid-top + solid-top (over perimeters)
                REQUIRE(object.layers()[25]->regions()[0]->fills.entities.size() == 1); //sparse
                REQUIRE(object.layers()[45]->regions()[0]->fills.entities.size() == 1); //sparse
                REQUIRE(object.layers()[45]->regions()[0]->fill_surfaces.surfaces.size() == 1);
                REQUIRE(object.layers()[45]->regions()[0]->fill_surfaces.surfaces[0].surface_type == (SurfaceType::stDensSparse | SurfaceType::stPosInternal));
                REQUIRE(object.layers()[45]->regions()[0]->fill_surfaces.surfaces[0].maxNbSolidLayersOnTop > 1);
                REQUIRE(object.layers()[46]->regions()[0]->fills.entities.size() == 1); //dense
                REQUIRE(object.layers()[46]->regions()[0]->fills.entities.size() == 1);
                REQUIRE(object.layers()[46]->regions()[0]->fill_surfaces.surfaces.size() == 1);
                REQUIRE(object.layers()[46]->regions()[0]->fill_surfaces.surfaces[0].surface_type == (SurfaceType::stDensSparse | SurfaceType::stPosInternal));
                REQUIRE(object.layers()[46]->regions()[0]->fill_surfaces.surfaces[0].maxNbSolidLayersOnTop == 1);
                REQUIRE(object.layers()[47]->regions()[0]->fills.entities.size() == 1); //solid-bridge
                REQUIRE(object.layers()[47]->regions()[0]->fill_surfaces.surfaces.size() == 1);
                REQUIRE(object.layers()[47]->regions()[0]->fill_surfaces.surfaces[0].surface_type == (SurfaceType::stDensSolid | SurfaceType::stPosInternal | SurfaceType::stModBridge));
                REQUIRE(object.layers()[47]->regions()[0]->fill_surfaces.surfaces[0].maxNbSolidLayersOnTop > 1);
                REQUIRE(object.layers()[48]->regions()[0]->fills.entities.size() == 1); //solid
                REQUIRE(object.layers()[48]->regions()[0]->fill_surfaces.surfaces.size() == 1);
                REQUIRE(object.layers()[48]->regions()[0]->fill_surfaces.surfaces[0].surface_type == (SurfaceType::stDensSolid | SurfaceType::stPosInternal));
                REQUIRE(object.layers()[48]->regions()[0]->fill_surfaces.surfaces[0].maxNbSolidLayersOnTop > 1);
                REQUIRE(object.layers()[49]->regions()[0]->fills.entities.size() == 1); //top
                REQUIRE(object.layers()[49]->regions()[0]->fill_surfaces.surfaces.size() == 1);
                REQUIRE(object.layers()[49]->regions()[0]->fill_surfaces.surfaces[0].surface_type == (SurfaceType::stDensSolid | SurfaceType::stPosTop));
                REQUIRE(object.layers()[49]->regions()[0]->fill_surfaces.surfaces[0].maxNbSolidLayersOnTop > 1);
            }
        }

        WHEN("dense infill to auto-not-full") {
            Model model{};
            Print print{};
            DynamicPrintConfig &config = Slic3r::DynamicPrintConfig::full_print_config();
            config.set_key_value("layer_height", new ConfigOptionFloat(0.2));
            config.set_key_value("first_layer_height", new ConfigOptionFloatOrPercent(0.2, false));
            config.set_key_value("infill_dense", new ConfigOptionBool(true));
            config.set_key_value("infill_dense_algo", new ConfigOptionEnum<DenseInfillAlgo>(dfaAutoNotFull));
            config.save("C:\\Users\\Admin\\Desktop\\config_def.ini");
            Slic3r::Test::init_print(print, { Slic3r::Test::TestMesh::di_10mm_notch }, model, &config, false);
            print.process();
            PrintObject& object = *(print.objects().at(0));
            THEN("correct number of fills") {
                REQUIRE(object.layers().size() == 50);
                REQUIRE(object.layers()[20]->regions()[0]->fills.entities.size() == 1); //sparse
                REQUIRE(object.layers()[21]->regions()[0]->fills.entities.size() == 2); //sparse + dense
                REQUIRE(object.layers()[21]->regions()[0]->fill_surfaces.surfaces.size() == 2);
                REQUIRE(object.layers()[21]->regions()[0]->fill_surfaces.surfaces[0].surface_type == (SurfaceType::stDensSparse | SurfaceType::stPosInternal));
                REQUIRE(object.layers()[21]->regions()[0]->fill_surfaces.surfaces[1].surface_type == (SurfaceType::stDensSparse | SurfaceType::stPosInternal));
                REQUIRE(std::min(object.layers()[21]->regions()[0]->fill_surfaces.surfaces[0].maxNbSolidLayersOnTop, object.layers()[21]->regions()[0]->fill_surfaces.surfaces[1].maxNbSolidLayersOnTop) == 1);
                Surface* srfSparse = &object.layers()[21]->regions()[0]->fill_surfaces.surfaces[0];
                Surface* srfDense = &object.layers()[21]->regions()[0]->fill_surfaces.surfaces[1];
                if (srfSparse->maxNbSolidLayersOnTop == 1) {
                    srfSparse = srfDense;
                    srfDense = &object.layers()[21]->regions()[0]->fill_surfaces.surfaces[0];
                }
                REQUIRE(unscaled(unscaled(srfSparse->area())) > unscaled(unscaled(srfDense->area())));
                REQUIRE(object.layers()[22]->regions()[0]->fills.entities.size() == 2); //sparse + solid-bridge
                REQUIRE(object.layers()[22]->regions()[0]->fill_surfaces.surfaces.size() == 2);
                if (object.layers()[22]->regions()[0]->fill_surfaces.surfaces[0].surface_type == (SurfaceType::stDensSparse | SurfaceType::stPosInternal)) {
                    REQUIRE(object.layers()[22]->regions()[0]->fill_surfaces.surfaces[1].surface_type == (SurfaceType::stDensSolid | SurfaceType::stPosInternal | SurfaceType::stModBridge));
                } else {
                    REQUIRE(object.layers()[22]->regions()[0]->fill_surfaces.surfaces[0].surface_type == (SurfaceType::stDensSolid | SurfaceType::stPosInternal | SurfaceType::stModBridge));
                    REQUIRE(object.layers()[22]->regions()[0]->fill_surfaces.surfaces[1].surface_type == (SurfaceType::stDensSparse | SurfaceType::stPosInternal));
                }
                REQUIRE(object.layers()[23]->regions()[0]->fills.entities.size() == 2); //sparse + solid
                REQUIRE(object.layers()[24]->regions()[0]->fills.entities.size() == 3); //sparse + solid-top + solid-top (over perimeters)
                REQUIRE(object.layers()[25]->regions()[0]->fills.entities.size() == 1); //sparse
                REQUIRE(object.layers()[45]->regions()[0]->fills.entities.size() == 1); //sparse
                REQUIRE(object.layers()[45]->regions()[0]->fill_surfaces.surfaces.size() == 1);
                REQUIRE(object.layers()[45]->regions()[0]->fill_surfaces.surfaces[0].surface_type == (SurfaceType::stDensSparse | SurfaceType::stPosInternal));
                REQUIRE(object.layers()[45]->regions()[0]->fill_surfaces.surfaces[0].maxNbSolidLayersOnTop > 1);
                REQUIRE(object.layers()[46]->regions()[0]->fills.entities.size() == 1); //dense
                REQUIRE(object.layers()[46]->regions()[0]->fill_surfaces.surfaces.size() == 1);
                REQUIRE(object.layers()[46]->regions()[0]->fill_surfaces.surfaces[0].surface_type == (SurfaceType::stDensSparse | SurfaceType::stPosInternal));
                REQUIRE(object.layers()[46]->regions()[0]->fill_surfaces.surfaces[0].maxNbSolidLayersOnTop > 1);
                REQUIRE(object.layers()[47]->regions()[0]->fills.entities.size() == 1); //solid-bridge
                REQUIRE(object.layers()[47]->regions()[0]->fill_surfaces.surfaces.size() == 1);
                REQUIRE(object.layers()[47]->regions()[0]->fill_surfaces.surfaces[0].surface_type == (SurfaceType::stDensSolid | SurfaceType::stPosInternal | SurfaceType::stModBridge));
                REQUIRE(object.layers()[47]->regions()[0]->fill_surfaces.surfaces[0].maxNbSolidLayersOnTop > 1);
                REQUIRE(object.layers()[48]->regions()[0]->fills.entities.size() == 1); //solid
                REQUIRE(object.layers()[48]->regions()[0]->fill_surfaces.surfaces.size() == 1);
                REQUIRE(object.layers()[48]->regions()[0]->fill_surfaces.surfaces[0].surface_type == (SurfaceType::stDensSolid | SurfaceType::stPosInternal));
                REQUIRE(object.layers()[48]->regions()[0]->fill_surfaces.surfaces[0].maxNbSolidLayersOnTop > 1);
                REQUIRE(object.layers()[49]->regions()[0]->fills.entities.size() == 1); //top
                REQUIRE(object.layers()[49]->regions()[0]->fill_surfaces.surfaces.size() == 1);
                REQUIRE(object.layers()[49]->regions()[0]->fill_surfaces.surfaces[0].surface_type == (SurfaceType::stDensSolid | SurfaceType::stPosTop));
                REQUIRE(object.layers()[49]->regions()[0]->fill_surfaces.surfaces[0].maxNbSolidLayersOnTop > 1);
            }
        }
    }

}
