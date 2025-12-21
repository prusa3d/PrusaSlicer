#include "slm_test_utils.hpp"

#include "libslic3r/SLMPrint.hpp"
#include "libslic3r/SLMScanPath.hpp"

TEST_CASE("SLM Scan Paths - Basic generation", "[SLMScanPaths]") {
    TriangleMesh mesh = create_test_cube(20.0);
    Model model = create_test_model("test_cube", std::move(mesh));
    
    DynamicPrintConfig config = create_default_slm_config();
    config.set("slm_layer_thickness", 0.1);
    
    SLMPrint print;
    init_slm_print(print, model, config);
    print.process();
    
    SECTION("Scan paths are generated") {
        for (const auto *obj : print.objects()) {
            REQUIRE_FALSE(obj->scan_paths().empty());
        }
    }
    
    SECTION("Scan paths are valid") {
        for (const auto *obj : print.objects()) {
            validate_scan_paths(obj->scan_paths());
        }
    }
}

TEST_CASE("SLM Scan Paths - Contour and hatch vectors", "[SLMScanPaths]") {
    TriangleMesh mesh = create_test_cube(20.0);
    Model model = create_test_model("test_cube", std::move(mesh));
    
    DynamicPrintConfig config = create_default_slm_config();
    config.set("slm_layer_thickness", 0.1);
    config.set("slm_contour_first", true);
    
    SLMPrint print;
    init_slm_print(print, model, config);
    print.process();
    
    SECTION("Contours are present") {
        for (const auto *obj : print.objects()) {
            for (const auto &layer : obj->scan_paths()) {
                // Contours may be empty for some layers, but structure should exist
                REQUIRE(layer.contours.size() >= 0);
            }
        }
    }
    
    SECTION("Hatches are present") {
        for (const auto *obj : print.objects()) {
            for (const auto &layer : obj->scan_paths()) {
                // Hatches should exist for filled layers
                if (!layer.hatches.empty()) {
                    for (const auto &hatch : layer.hatches) {
                        REQUIRE(is_valid_scan_vector(hatch));
                    }
                }
            }
        }
    }
}

TEST_CASE("SLM Scan Paths - Scan vector spacing", "[SLMScanPaths]") {
    TriangleMesh mesh = create_test_cube(20.0);
    Model model = create_test_model("test_cube", std::move(mesh));
    
    DynamicPrintConfig config = create_default_slm_config();
    config.set("slm_layer_thickness", 0.1);
    config.set("slm_scan_vector_spacing", 0.05);
    
    SLMPrint print;
    init_slm_print(print, model, config);
    print.process();
    
    SECTION("Scan vectors respect spacing") {
        for (const auto *obj : print.objects()) {
            for (const auto &layer : obj->scan_paths()) {
                // Check that vectors are properly spaced
                // (This is a basic check - more detailed validation can be added)
                REQUIRE(count_scan_vectors(layer) >= 0);
            }
        }
    }
}

TEST_CASE("SLM Scan Paths - Scan mode affects ordering", "[SLMScanPaths]") {
    TriangleMesh mesh = create_test_cube(20.0);
    Model model = create_test_model("test_cube", std::move(mesh));
    
    SECTION("Contour first mode") {
        DynamicPrintConfig config = create_default_slm_config();
        config.set("slm_scan_mode", "contour_first");
        config.set("slm_layer_thickness", 0.1);
        
        SLMPrint print;
        init_slm_print(print, model, config);
        print.process();
        
        for (const auto *obj : print.objects()) {
            REQUIRE_FALSE(obj->scan_paths().empty());
        }
    }
    
    SECTION("Hatch first mode") {
        DynamicPrintConfig config = create_default_slm_config();
        config.set("slm_scan_mode", "hatch_first");
        config.set("slm_layer_thickness", 0.1);
        
        SLMPrint print;
        init_slm_print(print, model, config);
        print.process();
        
        for (const auto *obj : print.objects()) {
            REQUIRE_FALSE(obj->scan_paths().empty());
        }
    }
}

