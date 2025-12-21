#include "slm_test_utils.hpp"

#include "libslic3r/SLMPrint.hpp"
#include "libslic3r/SLMPrintSteps.hpp"

TEST_CASE("SLM Slicing - Basic cube slicing", "[SLMSlicing]") {
    TriangleMesh mesh = create_test_cube(20.0);
    Model model = create_test_model("test_cube", std::move(mesh));
    
    DynamicPrintConfig config = create_default_slm_config();
    config.set("slm_layer_thickness", 0.1); // 100 microns
    
    SLMPrint print;
    init_slm_print(print, model, config);
    
    SECTION("Print is not empty after apply") {
        REQUIRE_FALSE(print.empty());
    }
    
    SECTION("Print has objects") {
        REQUIRE(print.objects().size() > 0);
    }
}

TEST_CASE("SLM Slicing - Layer height consistency", "[SLMSlicing]") {
    TriangleMesh mesh = create_test_cube(20.0);
    Model model = create_test_model("test_cube", std::move(mesh));
    
    DynamicPrintConfig config = create_default_slm_config();
    double layer_thickness = 0.05; // 50 microns
    config.set("slm_layer_thickness", layer_thickness);
    
    SLMPrint print;
    init_slm_print(print, model, config);
    
    // Process the print to generate slices
    print.process();
    
    SECTION("Objects have layer height levels") {
        for (const auto *obj : print.objects()) {
            REQUIRE_FALSE(obj->layer_height_levels().empty());
        }
    }
    
    SECTION("Layer heights are consistent") {
        for (const auto *obj : print.objects()) {
            validate_layer_heights(obj->layer_height_levels(), layer_thickness);
        }
    }
}

TEST_CASE("SLM Slicing - Different layer thicknesses", "[SLMSlicing]") {
    TriangleMesh mesh = create_test_cube(20.0);
    Model model = create_test_model("test_cube", std::move(mesh));
    
    std::vector<double> thicknesses = {0.025, 0.05, 0.1, 0.2};
    
    for (double thickness : thicknesses) {
        DynamicPrintConfig config = create_default_slm_config();
        config.set("slm_layer_thickness", thickness);
        
        SLMPrint print;
        init_slm_print(print, model, config);
        print.process();
        
        SECTION("Layer thickness " + std::to_string(thickness)) {
            for (const auto *obj : print.objects()) {
                if (!obj->layer_height_levels().empty()) {
                    validate_layer_heights(obj->layer_height_levels(), thickness);
                }
            }
        }
    }
}

TEST_CASE("SLM Slicing - Model slices are generated", "[SLMSlicing]") {
    TriangleMesh mesh = create_test_cube(20.0);
    Model model = create_test_model("test_cube", std::move(mesh));
    
    DynamicPrintConfig config = create_default_slm_config();
    config.set("slm_layer_thickness", 0.1);
    
    SLMPrint print;
    init_slm_print(print, model, config);
    print.process();
    
    SECTION("Objects have model slices") {
        for (const auto *obj : print.objects()) {
            REQUIRE_FALSE(obj->model_slices().empty());
        }
    }
    
    SECTION("Slice count matches layer count") {
        for (const auto *obj : print.objects()) {
            REQUIRE(obj->model_slices().size() == obj->layer_height_levels().size());
        }
    }
}

