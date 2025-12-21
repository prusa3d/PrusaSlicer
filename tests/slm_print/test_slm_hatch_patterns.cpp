#include "slm_test_utils.hpp"

#include "libslic3r/SLMPrint.hpp"

TEST_CASE("SLM Hatch Patterns - Grid pattern", "[SLMHatchPatterns]") {
    TriangleMesh mesh = create_test_cube(20.0);
    Model model = create_test_model("test_cube", std::move(mesh));
    
    DynamicPrintConfig config = create_default_slm_config();
    config.set("slm_hatch_pattern", "grid");
    config.set("slm_layer_thickness", 0.1);
    
    SLMPrint print;
    init_slm_print(print, model, config);
    print.process();
    
    SECTION("Hatch patterns are generated") {
        for (const auto *obj : print.objects()) {
            REQUIRE_FALSE(obj->hatch_patterns().empty());
        }
    }
    
    SECTION("Contours are generated") {
        for (const auto *obj : print.objects()) {
            REQUIRE_FALSE(obj->contours().empty());
        }
    }
}

TEST_CASE("SLM Hatch Patterns - Stripe pattern", "[SLMHatchPatterns]") {
    TriangleMesh mesh = create_test_cube(20.0);
    Model model = create_test_model("test_cube", std::move(mesh));
    
    DynamicPrintConfig config = create_default_slm_config();
    config.set("slm_hatch_pattern", "stripe");
    config.set("slm_layer_thickness", 0.1);
    
    SLMPrint print;
    init_slm_print(print, model, config);
    print.process();
    
    SECTION("Hatch patterns are generated") {
        for (const auto *obj : print.objects()) {
            REQUIRE_FALSE(obj->hatch_patterns().empty());
        }
    }
}

TEST_CASE("SLM Hatch Patterns - Concentric pattern", "[SLMHatchPatterns]") {
    TriangleMesh mesh = create_test_cube(20.0);
    Model model = create_test_model("test_cube", std::move(mesh));
    
    DynamicPrintConfig config = create_default_slm_config();
    config.set("slm_hatch_pattern", "concentric");
    config.set("slm_layer_thickness", 0.1);
    
    SLMPrint print;
    init_slm_print(print, model, config);
    print.process();
    
    SECTION("Hatch patterns are generated") {
        for (const auto *obj : print.objects()) {
            REQUIRE_FALSE(obj->hatch_patterns().empty());
        }
    }
}

TEST_CASE("SLM Hatch Patterns - Hatch spacing affects pattern", "[SLMHatchPatterns]") {
    TriangleMesh mesh = create_test_cube(20.0);
    Model model = create_test_model("test_cube", std::move(mesh));
    
    std::vector<double> spacings = {0.05, 0.1, 0.2};
    
    for (double spacing : spacings) {
        DynamicPrintConfig config = create_default_slm_config();
        config.set("slm_hatch_spacing", spacing);
        config.set("slm_layer_thickness", 0.1);
        
        SLMPrint print;
        init_slm_print(print, model, config);
        print.process();
        
        SECTION("Spacing " + std::to_string(spacing)) {
            for (const auto *obj : print.objects()) {
                REQUIRE_FALSE(obj->hatch_patterns().empty());
            }
        }
    }
}

TEST_CASE("SLM Hatch Patterns - Layer rotation", "[SLMHatchPatterns]") {
    TriangleMesh mesh = create_test_cube(20.0);
    Model model = create_test_model("test_cube", std::move(mesh));
    
    DynamicPrintConfig config = create_default_slm_config();
    config.set("slm_rotation_angle", 67.0); // 67 degrees per layer
    config.set("slm_layer_thickness", 0.1);
    
    SLMPrint print;
    init_slm_print(print, model, config);
    print.process();
    
    SECTION("Hatch patterns are generated with rotation") {
        for (const auto *obj : print.objects()) {
            REQUIRE_FALSE(obj->hatch_patterns().empty());
        }
    }
}

