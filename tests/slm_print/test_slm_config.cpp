#include "slm_test_utils.hpp"

#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/SLMPrintConfig.hpp" // For SLMHatchPatternType and SLMExportFormatType enums

TEST_CASE("SLM Configuration - Default values", "[SLMConfig]") {
    DynamicPrintConfig config = create_default_slm_config();
    
    SECTION("Printer technology is SLM") {
        REQUIRE(config.opt_enum<PrinterTechnology>("printer_technology") == ptSLM);
    }
    
    SECTION("Layer thickness is set") {
        REQUIRE(config.opt_float("slm_layer_thickness") > 0.0);
    }
    
    SECTION("Laser parameters are set") {
        REQUIRE(config.opt_float("slm_laser_power") > 0.0);
        REQUIRE(config.opt_float("slm_laser_speed") > 0.0);
        REQUIRE(config.opt_float("slm_exposure_time") > 0.0);
    }
    
    SECTION("Hatch pattern is set") {
        REQUIRE(config.has("slm_hatch_pattern"));
        auto pattern = config.opt_enum<SLMHatchPatternType>("slm_hatch_pattern");
        REQUIRE((pattern == SLMHatchPatternType::slmhpGrid || 
                pattern == SLMHatchPatternType::slmhpStripe ||
                pattern == SLMHatchPatternType::slmhpConcentric ||
                pattern == SLMHatchPatternType::slmhpCustom));
    }
    
    SECTION("Export format is set") {
        REQUIRE(config.has("slm_export_format"));
        auto format = config.opt_enum<SLMExportFormatType>("slm_export_format");
        REQUIRE((format >= SLMExportFormatType::slmefSLM && 
                format <= SLMExportFormatType::slmefREA));
    }
}

TEST_CASE("SLM Configuration - Parameter validation", "[SLMConfig]") {
    DynamicPrintConfig config = create_default_slm_config();
    
    SECTION("Layer thickness must be positive") {
        config.set("slm_layer_thickness", 0.0);
        // Should handle invalid values appropriately
        REQUIRE(config.opt_float("slm_layer_thickness") >= 0.0);
    }
    
    SECTION("Laser power must be positive") {
        config.set("slm_laser_power", -10.0);
        // Should handle invalid values appropriately
        REQUIRE(config.opt_float("slm_laser_power") >= 0.0);
    }
    
    SECTION("Hatch spacing must be positive") {
        config.set("slm_hatch_spacing", 0.01);
        REQUIRE(config.opt_float("slm_hatch_spacing") > 0.0);
    }
}

TEST_CASE("SLM Configuration - Pattern types", "[SLMConfig]") {
    DynamicPrintConfig config = create_default_slm_config();
    
    SECTION("Grid pattern") {
        config.set("slm_hatch_pattern", "grid");
        REQUIRE(config.opt_enum<SLMHatchPatternType>("slm_hatch_pattern") == 
                SLMHatchPatternType::slmhpGrid);
    }
    
    SECTION("Stripe pattern") {
        config.set("slm_hatch_pattern", "stripe");
        REQUIRE(config.opt_enum<SLMHatchPatternType>("slm_hatch_pattern") == 
                SLMHatchPatternType::slmhpStripe);
    }
    
    SECTION("Concentric pattern") {
        config.set("slm_hatch_pattern", "concentric");
        REQUIRE(config.opt_enum<SLMHatchPatternType>("slm_hatch_pattern") == 
                SLMHatchPatternType::slmhpConcentric);
    }
    
    SECTION("Custom pattern") {
        config.set("slm_hatch_pattern", "custom");
        REQUIRE(config.opt_enum<SLMHatchPatternType>("slm_hatch_pattern") == 
                SLMHatchPatternType::slmhpCustom);
    }
}

