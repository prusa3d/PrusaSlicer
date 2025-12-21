#include "slm_test_utils.hpp"

#include "libslic3r/SLMPrint.hpp"

TEST_CASE("SLM Integration - Full pipeline", "[SLMIntegration]") {
    TriangleMesh mesh = create_test_cube(20.0);
    Model model = create_test_model("test_cube", std::move(mesh));
    
    DynamicPrintConfig config = create_default_slm_config();
    config.set("slm_layer_thickness", 0.1);
    
    SLMPrint print;
    init_slm_print(print, model, config);
    
    SECTION("Apply succeeds") {
        REQUIRE_FALSE(print.empty());
    }
    
    SECTION("Process completes") {
        print.process();
        REQUIRE(print.finished());
    }
    
    SECTION("All steps are complete") {
        print.process();
        for (const auto *obj : print.objects()) {
            REQUIRE(obj->is_step_done(slmposSlice));
            REQUIRE(obj->is_step_done(slmposGenerateHatches));
            REQUIRE(obj->is_step_done(slmposGenerateScanPaths));
        }
    }
}

TEST_CASE("SLM Integration - Multiple objects", "[SLMIntegration]") {
    Model model;
    
    // Add multiple objects
    for (int i = 0; i < 3; ++i) {
        TriangleMesh mesh = create_test_cube(10.0);
        ModelObject *object = model.add_object();
        object->name = "cube_" + std::to_string(i);
        ModelVolume *volume = object->add_volume(std::move(mesh));
        volume->name = "volume_" + std::to_string(i);
        object->add_instance();
    }
    
    DynamicPrintConfig config = create_default_slm_config();
    config.set("slm_layer_thickness", 0.1);
    
    SLMPrint print;
    init_slm_print(print, model, config);
    print.process();
    
    SECTION("All objects are processed") {
        REQUIRE(print.objects().size() == 3);
        REQUIRE(print.finished());
    }
    
    SECTION("Each object has scan paths") {
        for (const auto *obj : print.objects()) {
            REQUIRE_FALSE(obj->scan_paths().empty());
        }
    }
}

TEST_CASE("SLM Integration - Edge cases", "[SLMIntegration]") {
    SECTION("Thin wall model") {
        TriangleMesh mesh = make_cube(20.0, 20.0, 20.0);
        // Create a thin wall by scaling one dimension
        mesh.scale(Vec3f(1.0, 1.0, 0.1));
        
        Model model = create_test_model("thin_wall", std::move(mesh));
        DynamicPrintConfig config = create_default_slm_config();
        config.set("slm_layer_thickness", 0.05);
        
        SLMPrint print;
        init_slm_print(print, model, config);
        print.process();
        
        REQUIRE(print.finished());
    }
    
    SECTION("Small model") {
        TriangleMesh mesh = create_test_cube(1.0); // 1mm cube
        Model model = create_test_model("small_cube", std::move(mesh));
        DynamicPrintConfig config = create_default_slm_config();
        config.set("slm_layer_thickness", 0.05);
        
        SLMPrint print;
        init_slm_print(print, model, config);
        print.process();
        
        REQUIRE(print.finished());
    }
}

TEST_CASE("SLM Integration - Different export formats", "[SLMIntegration]") {
    TriangleMesh mesh = create_test_cube(20.0);
    Model model = create_test_model("test_cube", std::move(mesh));
    
    std::vector<std::string> formats = {"slm", "mtt", "sli", "cli", "rea"};
    
    for (const auto &format : formats) {
        DynamicPrintConfig config = create_default_slm_config();
        config.set("slm_export_format", format);
        config.set("slm_layer_thickness", 0.1);
        
        SLMPrint print;
        init_slm_print(print, model, config);
        print.process();
        
        SECTION("Format " + format) {
            REQUIRE(print.finished());
            // Export format is set correctly
            REQUIRE(config.opt_string("slm_export_format") == format);
        }
    }
}

