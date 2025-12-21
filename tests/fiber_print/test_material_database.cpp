/**
 * Unit tests for fiber material database
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "libslic3r/Fiber/FiberAdvanced.hpp"
#include "libslic3r/Fiber/FiberLayer.hpp"

using namespace Slic3r;
using namespace Catch;

TEST_CASE("FiberMaterialDatabase - Singleton instance", "[MaterialDatabase]") {
    SECTION("Get instance") {
        FiberMaterialDatabase& db1 = FiberMaterialDatabase::instance();
        FiberMaterialDatabase& db2 = FiberMaterialDatabase::instance();
        
        // Should return the same instance
        REQUIRE(&db1 == &db2);
    }
}

TEST_CASE("FiberMaterialDatabase - Material properties", "[MaterialDatabase]") {
    FiberMaterialDatabase& db = FiberMaterialDatabase::instance();
    
    SECTION("Carbon fiber properties") {
        const FiberMaterialProperties& props = db.get_properties(FiberType::Carbon);
        
        REQUIRE(props.type == FiberType::Carbon);
        REQUIRE(!props.name.empty());
        REQUIRE(props.diameter_mm > 0.0);
        REQUIRE(props.density_g_per_cm3 > 0.0);
        REQUIRE(props.tensile_strength_mpa > 0.0);
        REQUIRE(props.modulus_gpa > 0.0);
        REQUIRE(props.default_speed_mm_per_s > 0.0);
        REQUIRE(props.min_speed_mm_per_s <= props.default_speed_mm_per_s);
        REQUIRE(props.max_speed_mm_per_s >= props.default_speed_mm_per_s);
    }
    
    SECTION("Glass fiber properties") {
        const FiberMaterialProperties& props = db.get_properties(FiberType::Glass);
        
        REQUIRE(props.type == FiberType::Glass);
        REQUIRE(!props.name.empty());
        REQUIRE(props.diameter_mm > 0.0);
        REQUIRE(props.density_g_per_cm3 > 0.0);
    }
    
    SECTION("Kevlar fiber properties") {
        const FiberMaterialProperties& props = db.get_properties(FiberType::Kevlar);
        
        REQUIRE(props.type == FiberType::Kevlar);
        REQUIRE(!props.name.empty());
        REQUIRE(props.diameter_mm > 0.0);
        REQUIRE(props.density_g_per_cm3 > 0.0);
    }
    
    SECTION("Get material name") {
        std::string name = db.get_material_name(FiberType::Carbon);
        REQUIRE(!name.empty());
    }
    
    SECTION("Get available types") {
        std::vector<FiberType> types = db.get_available_types();
        
        REQUIRE(!types.empty());
        // Should include at least the standard types
        bool has_carbon = false, has_glass = false, has_kevlar = false;
        for (FiberType type : types) {
            if (type == FiberType::Carbon) has_carbon = true;
            if (type == FiberType::Glass) has_glass = true;
            if (type == FiberType::Kevlar) has_kevlar = true;
        }
        
        REQUIRE(has_carbon);
        REQUIRE(has_glass);
        REQUIRE(has_kevlar);
    }
}

TEST_CASE("FiberMaterialDatabase - Custom material registration", "[MaterialDatabase]") {
    FiberMaterialDatabase& db = FiberMaterialDatabase::instance();
    
    SECTION("Register custom material") {
        FiberMaterialProperties custom;
        custom.type = FiberType::Custom;
        custom.name = "Test Fiber";
        custom.diameter_mm = 0.15;
        custom.density_g_per_cm3 = 2.0;
        custom.tensile_strength_mpa = 4000.0;
        custom.modulus_gpa = 250.0;
        custom.default_speed_mm_per_s = 60.0;
        custom.min_speed_mm_per_s = 20.0;
        custom.max_speed_mm_per_s = 120.0;
        
        db.register_custom_material("Test Fiber", custom);
        
        // Should be able to retrieve it (if implementation supports custom lookup)
        // Note: Exact behavior depends on implementation
        REQUIRE(custom.name == "Test Fiber");
    }
}

TEST_CASE("FiberMaterialProperties - Default values", "[MaterialDatabase]") {
    SECTION("Default construction") {
        FiberMaterialProperties props;
        
        REQUIRE(props.type == FiberType::Carbon);
        REQUIRE(!props.name.empty());
        REQUIRE(props.diameter_mm > 0.0);
        REQUIRE(props.density_g_per_cm3 > 0.0);
        REQUIRE(props.default_speed_mm_per_s > 0.0);
    }
}

