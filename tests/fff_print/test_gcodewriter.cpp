#include <catch2/catch.hpp>

#include <memory>

#include "libslic3r/GCode/GCodeWriter.hpp"

using namespace Slic3r;

SCENARIO("set_speed emits values with fixed-point output.", "[GCodeWriter]") {

    GIVEN("GCodeWriter instance") {
        GCodeWriter writer;
        WHEN("set_speed is called to set speed to 99999.123") {
            THEN("Output string is G1 F99999.123") {
                REQUIRE_THAT(writer.set_speed(99999.123), Catch::Equals("G1 F99999.123\n"));
            }
        }
        WHEN("set_speed is called to set speed to 1") {
            THEN("Output string is G1 F1") {
                REQUIRE_THAT(writer.set_speed(1.0), Catch::Equals("G1 F1\n"));
            }
        }
        WHEN("set_speed is called to set speed to 203.200022") {
            THEN("Output string is G1 F203.2") {
                REQUIRE_THAT(writer.set_speed(203.200022), Catch::Equals("G1 F203.2\n"));
            }
        }
        WHEN("set_speed is called to set speed to 203.200522") {
            THEN("Output string is G1 F203.201") {
                REQUIRE_THAT(writer.set_speed(203.200522), Catch::Equals("G1 F203.201\n"));
            }
        }
    }
}

TEST_CASE("GCodeWriter emits G1 code correctly according to XYZF_EXPORT_DIGITS", "[GCodeWriter]") {
    GCodeWriter writer;

    SECTION("Check quantize") {
        CHECK(GCodeFormatter::quantize(1.0,0) == 1.);
        CHECK(GCodeFormatter::quantize(0.0,0) == 0.);
        CHECK(GCodeFormatter::quantize(0.1,0) == 0);

        CHECK(GCodeFormatter::quantize(1.0,1) == 1.);
        CHECK(GCodeFormatter::quantize(0.0,1) == 0.);
        CHECK(GCodeFormatter::quantize(0.1,1) == Approx(0.1));
        CHECK(GCodeFormatter::quantize(0.01,1) == 0.);

        CHECK(GCodeFormatter::quantize(1.0,2) == 1.);
        CHECK(GCodeFormatter::quantize(0.0,2) == 0.);
        CHECK(GCodeFormatter::quantize(0.1,2) == Approx(0.1));
        CHECK(GCodeFormatter::quantize(0.01,2) == Approx(0.01));
        CHECK(GCodeFormatter::quantize(0.001,2) == 0.);

        CHECK(GCodeFormatter::quantize(1.0,3) == 1.);
        CHECK(GCodeFormatter::quantize(0.0,3) == 0.);
        CHECK(GCodeFormatter::quantize(0.1,3) == Approx(0.1));
        CHECK(GCodeFormatter::quantize(0.01,3) == Approx(0.01));
        CHECK(GCodeFormatter::quantize(0.001,3) == Approx(0.001));
        CHECK(GCodeFormatter::quantize(0.0001,3) == 0.);

        CHECK(GCodeFormatter::quantize(1.0,4) == 1.);
        CHECK(GCodeFormatter::quantize(0.0,4) == 0.);
        CHECK(GCodeFormatter::quantize(0.1,4) == Approx(0.1));
        CHECK(GCodeFormatter::quantize(0.01,4) == Approx(0.01));
        CHECK(GCodeFormatter::quantize(0.001,4) == Approx(0.001));
        CHECK(GCodeFormatter::quantize(0.0001,4) == Approx(0.0001));
        CHECK(GCodeFormatter::quantize(0.00001,4) == 0.);

        CHECK(GCodeFormatter::quantize(1.0,5) == 1.);
        CHECK(GCodeFormatter::quantize(0.0,5) == 0.);
        CHECK(GCodeFormatter::quantize(0.1,5) == Approx(0.1));
        CHECK(GCodeFormatter::quantize(0.01,5) == Approx(0.01));
        CHECK(GCodeFormatter::quantize(0.001,5) == Approx(0.001));
        CHECK(GCodeFormatter::quantize(0.0001,5) == Approx(0.0001));
        CHECK(GCodeFormatter::quantize(0.00001,5) == Approx(0.00001));
        CHECK(GCodeFormatter::quantize(0.000001,5) == 0.);

        CHECK(GCodeFormatter::quantize(1.0,6) == 1.);
        CHECK(GCodeFormatter::quantize(0.0,6) == 0.);
        CHECK(GCodeFormatter::quantize(0.1,6) == Approx(0.1));
        CHECK(GCodeFormatter::quantize(0.01,6) == Approx(0.01));
        CHECK(GCodeFormatter::quantize(0.001,6) == Approx(0.001));
        CHECK(GCodeFormatter::quantize(0.0001,6) == Approx(0.0001));
        CHECK(GCodeFormatter::quantize(0.00001,6) == Approx(0.00001));
        CHECK(GCodeFormatter::quantize(0.000001,6) == Approx(0.000001));
        CHECK(GCodeFormatter::quantize(0.0000001,6) == 0.);
    }

    SECTION("Check pow_10") {
        // IEEE 754 floating point numbers can represent these numbers EXACTLY.
        CHECK(GCodeFormatter::pow_10[0] == 1.);
        CHECK(GCodeFormatter::pow_10[1] == 10.);
        CHECK(GCodeFormatter::pow_10[2] == 100.);
        CHECK(GCodeFormatter::pow_10[3] == 1000.);
        CHECK(GCodeFormatter::pow_10[4] == 10000.);
        CHECK(GCodeFormatter::pow_10[5] == 100000.);
        CHECK(GCodeFormatter::pow_10[6] == 1000000.);
        CHECK(GCodeFormatter::pow_10[7] == 10000000.);
        CHECK(GCodeFormatter::pow_10[8] == 100000000.);
        CHECK(GCodeFormatter::pow_10[9] == 1000000000.);
    }

    SECTION("Check pow_10_inv") {
        // IEEE 754 floating point numbers can NOT represent these numbers exactly.
        CHECK(GCodeFormatter::pow_10_inv[0] == 1.);
        CHECK(GCodeFormatter::pow_10_inv[1] == 0.1);
        CHECK(GCodeFormatter::pow_10_inv[2] == 0.01);
        CHECK(GCodeFormatter::pow_10_inv[3] == 0.001);
        CHECK(GCodeFormatter::pow_10_inv[4] == 0.0001);
        CHECK(GCodeFormatter::pow_10_inv[5] == 0.00001);
        CHECK(GCodeFormatter::pow_10_inv[6] == 0.000001);
        CHECK(GCodeFormatter::pow_10_inv[7] == 0.0000001);
        CHECK(GCodeFormatter::pow_10_inv[8] == 0.00000001);
        CHECK(GCodeFormatter::pow_10_inv[9] == 0.000000001);
    }

    SECTION("travel_to_z Emit G1 code for very significant movement") {
        double z1 = 10.0;
        std::string result1{ writer.travel_to_z(z1) };
        CHECK(result1 == "G1 Z10 F7800\n");

        double z2 = z1 * 2;
        std::string result2{ writer.travel_to_z(z2) };
        CHECK(result2 == "G1 Z20 F7800\n");
    }

    SECTION("travel_to_z Emit G1 code for significant movement") {
        double z1 = 10.0;
        std::string result1{ writer.travel_to_z(z1) };
        CHECK(result1 == "G1 Z10 F7800\n");

        // This should test with XYZ_EPSILON exactly,
        // but IEEE 754 floating point numbers cannot pass the test.
        double z2 = z1 + GCodeFormatter::XYZ_EPSILON * 1.001;
        std::string result2{ writer.travel_to_z(z2) };

        std::ostringstream oss;
        oss << "G1 Z"
            << GCodeFormatter::quantize_xyzf(z2)
            << " F7800\n";

        CHECK(result2 == oss.str());
    }

    SECTION("travel_to_z Do not emit G1 code for insignificant movement") {
        double z1 = 10.0;
        std::string result1{ writer.travel_to_z(z1) };
        CHECK(result1 == "G1 Z10 F7800\n");

        // Movement smaller than XYZ_EPSILON
        double z2 = z1 + (GCodeFormatter::XYZ_EPSILON * 0.999);
        std::string result2{ writer.travel_to_z(z2) };
        CHECK(result2 == "");

        double z3 = z1 + (GCodeFormatter::XYZ_EPSILON * 0.1);
        std::string result3{ writer.travel_to_z(z3) };
        CHECK(result3 == "");
    }

    SECTION("travel_to_xyz Emit G1 code for very significant movement") {
        Vec3d v1{10.0, 10.0, 10.0};
        std::string result1{ writer.travel_to_xyz(v1) };
        CHECK(result1 == "G1 X10 Y10 Z10 F7800\n");

        Vec3d v2 = v1 * 2;
        std::string result2{ writer.travel_to_xyz(v2) };
        CHECK(result2 == "G1 X20 Y20 Z20 F7800\n");
    }

    SECTION("travel_to_xyz Emit G1 code for significant XYZ movement") {
        Vec3d v1{10.0, 10.0, 10.0};
        std::string result1{ writer.travel_to_xyz(v1) };
        CHECK(result1 == "G1 X10 Y10 Z10 F7800\n");

        Vec3d v2 = v1;
        // This should test with XYZ_EPSILON exactly,
        // but IEEE 754 floating point numbers cannot pass the test.
        v2.array() += GCodeFormatter::XYZ_EPSILON * 1.001;
        std::string result2{ writer.travel_to_xyz(v2) };

        std::ostringstream oss;
        oss << "G1 X"
            << GCodeFormatter::quantize_xyzf(v2.x())
            << " Y"
            << GCodeFormatter::quantize_xyzf(v2.y())
            << " Z"
            << GCodeFormatter::quantize_xyzf(v2.z())
            << " F7800\n";

        CHECK(result2 == oss.str());
    }

    SECTION("travel_to_xyz Emit G1 code for significant X movement") {
        Vec3d v1{10.0, 10.0, 10.0};
        std::string result1{ writer.travel_to_xyz(v1) };
        CHECK(result1 == "G1 X10 Y10 Z10 F7800\n");

        Vec3d v2 = v1;
        // This should test with XYZ_EPSILON exactly,
        // but IEEE 754 floating point numbers cannot pass the test.
        v2.x() += GCodeFormatter::XYZ_EPSILON * 1.001;
        std::string result2{ writer.travel_to_xyz(v2) };

        std::ostringstream oss;
        // Only X needs to be emitted in this case,
        // but this is how the code currently works.
        oss << "G1 X"
            << GCodeFormatter::quantize_xyzf(v2.x())
            << " Y"
            << GCodeFormatter::quantize_xyzf(v2.y())
            << " F7800\n";

        CHECK(result2 == oss.str());
    }

    SECTION("travel_to_xyz Emit G1 code for significant Y movement") {
        Vec3d v1{10.0, 10.0, 10.0};
        std::string result1{ writer.travel_to_xyz(v1) };
        CHECK(result1 == "G1 X10 Y10 Z10 F7800\n");

        Vec3d v2 = v1;
        // This should test with XYZ_EPSILON exactly,
        // but IEEE 754 floating point numbers cannot pass the test.
        v2.y() += GCodeFormatter::XYZ_EPSILON * 1.001;
        std::string result2{ writer.travel_to_xyz(v2) };

        std::ostringstream oss;
        // Only Y needs to be emitted in this case,
        // but this is how the code currently works.
        oss << "G1 X"
            << GCodeFormatter::quantize_xyzf(v2.x())
            << " Y"
            << GCodeFormatter::quantize_xyzf(v2.y())
            << " F7800\n";

        CHECK(result2 == oss.str());
    }

    SECTION("travel_to_xyz Emit G1 code for significant Z movement") {
        Vec3d v1{10.0, 10.0, 10.0};
        std::string result1{ writer.travel_to_xyz(v1) };
        CHECK(result1 == "G1 X10 Y10 Z10 F7800\n");

        Vec3d v2 = v1;
        // This should test with XYZ_EPSILON exactly,
        // but IEEE 754 floating point numbers cannot pass the test.
        v2.z() += GCodeFormatter::XYZ_EPSILON * 1.001;
        std::string result2{ writer.travel_to_xyz(v2) };

        std::ostringstream oss;
        oss << "G1 Z"
            << GCodeFormatter::quantize_xyzf(v2.z())
            << " F7800\n";

        CHECK(result2 == oss.str());
    }

    SECTION("travel_to_xyz Do not emit G1 code for insignificant movement") {
        Vec3d v1{10.0, 10.0, 10.0};
        std::string result1{ writer.travel_to_xyz(v1) };
        CHECK(result1 == "G1 X10 Y10 Z10 F7800\n");

        // Movement smaller than XYZ_EPSILON
        Vec3d v2 = v1;
        v2.array() += GCodeFormatter::XYZ_EPSILON * 0.999;
        std::string result2{ writer.travel_to_xyz(v2) };
        CHECK(result2 == "");

        Vec3d v3 = v1;
        v3.array() += GCodeFormatter::XYZ_EPSILON * 0.1;
        std::string result3{ writer.travel_to_xyz(v3) };
        CHECK(result3 == "");
    }
}