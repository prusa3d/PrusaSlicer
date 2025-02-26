#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <memory>

#include "libslic3r/GCode/GCodeWriter.hpp"
#include "libslic3r/GCodeReader.hpp"

using namespace Slic3r;
using Catch::Approx;

SCENARIO("set_speed emits values with fixed-point output.", "[GCodeWriter]") {

    GIVEN("GCodeWriter instance") {
        GCodeWriter writer;
        WHEN("set_speed is called to set speed to 99999.123") {
            THEN("Output string is G1 F99999.123") {
                REQUIRE_THAT(writer.set_speed(99999.123), Catch::Matchers::Equals("G1 F99999.123\n"));
            }
        }
        WHEN("set_speed is called to set speed to 1") {
            THEN("Output string is G1 F1") {
                REQUIRE_THAT(writer.set_speed(1.0), Catch::Matchers::Equals("G1 F1\n"));
            }
        }
        WHEN("set_speed is called to set speed to 203.200022") {
            THEN("Output string is G1 F203.2") {
                REQUIRE_THAT(writer.set_speed(203.200022), Catch::Matchers::Equals("G1 F203.2\n"));
            }
        }
        WHEN("set_speed is called to set speed to 203.200522") {
            THEN("Output string is G1 F203.201") {
                REQUIRE_THAT(writer.set_speed(203.200522), Catch::Matchers::Equals("G1 F203.201\n"));
            }
        }
    }
}

void check_gcode_feedrate(const std::string& gcode, const GCodeConfig& config, double expected_speed) {
	GCodeReader parser;
    parser.parse_buffer(gcode, [&] (Slic3r::GCodeReader &self, const Slic3r::GCodeReader::GCodeLine &line) {

        const double travel_speed = config.opt_float("travel_speed");

        const double feedrate = line.has_f() ? line.f() : self.f();
        CHECK(feedrate == Approx(expected_speed * 60).epsilon(GCodeFormatter::XYZ_EPSILON));

        if (line.dist_Z(self) != 0) {
            // lift move or lift + change layer
            const double travel_speed_z = config.opt_float("travel_speed_z");
            if (travel_speed_z) {
                Vec3d move{line.dist_X(self), line.dist_Y(self), line.dist_Z(self)};
                double move_u_z = move.z() / move.norm();
                double travel_speed_ = std::abs(travel_speed_z / move_u_z);
                INFO("move Z feedrate Z component is less than or equal to travel_speed_z");
                CHECK(feedrate * std::abs(move_u_z) <= Approx(travel_speed_z * 60).epsilon(GCodeFormatter::XYZ_EPSILON));
                if (travel_speed_ < travel_speed) {
                    INFO("move Z at travel speed Z");
                    CHECK(feedrate == Approx(travel_speed_ * 60).epsilon(GCodeFormatter::XYZ_EPSILON));
                    INFO("move Z feedrate Z component is equal to travel_speed_z");
                    CHECK(feedrate * std::abs(move_u_z) == Approx(travel_speed_z * 60).epsilon(GCodeFormatter::XYZ_EPSILON));
                } else {
                    INFO("move Z at travel speed");
                    CHECK(feedrate == Approx(travel_speed * 60).epsilon(GCodeFormatter::XYZ_EPSILON));
                }
            } else {
                INFO("move Z at travel speed");
                CHECK(feedrate == Approx(travel_speed * 60).epsilon(GCodeFormatter::XYZ_EPSILON));
            }
        } else if (not line.extruding(self)) {
            // normal move
            INFO("move XY at travel speed");
            CHECK(feedrate == Approx(travel_speed * 60));
        }
    });
}

SCENARIO("travel_speed_z is zero should use travel_speed.", "[GCodeWriter]") {
    GIVEN("GCodeWriter instance") {
        GCodeWriter writer;
        WHEN("travel_speed_z is set to 0") {
            writer.config.travel_speed.value = 1000;
            writer.config.travel_speed_z.value = 0;
            THEN("XYZ move feed rate should be equal to travel_speed") {
                const Vec3d move{10, 10, 10};
                const double speed = writer.config.travel_speed.value;
                const Vec3d p1 = writer.get_position();
                const Vec3d p2 = p1 + move;
                const std::string result = writer.travel_to_xyz(p2);
                check_gcode_feedrate(result, writer.config, speed);
            }
        }
    }
}

SCENARIO("travel_speed_z is respected in Z speed component.", "[GCodeWriter]") {
    GIVEN("GCodeWriter instance") {
        GCodeWriter writer;
        WHEN("travel_speed_z is set to 10") {
            writer.config.travel_speed.value = 1000;
            writer.config.travel_speed_z.value = 10;
            THEN("Z move feed rate should be equal to travel_speed_z") {
                const Vec3d move{0, 0, 10};
                const double speed = writer.config.travel_speed_z.value;
                const Vec3d p1 = writer.get_position();
                const Vec3d p2 = p1 + move;
                const std::string result = writer.travel_to_xyz(p2);
                check_gcode_feedrate(result, writer.config, speed);
            }
            THEN("-Z move feed rate should be equal to travel_speed_z") {
                const Vec3d move{0, 0, -10};
                const double speed = writer.config.travel_speed_z.value;
                const Vec3d p1 = writer.get_position();
                const Vec3d p2 = p1 + move;
                const std::string result = writer.travel_to_xyz(p2);
                check_gcode_feedrate(result, writer.config, speed);
            }
            THEN("XY move feed rate should be equal to travel_speed") {
                const Vec3d move{10, 10, 0};
                const double speed = writer.config.travel_speed.value;
                const Vec3d p1 = writer.get_position();
                const Vec3d p2 = p1 + move;
                const std::string result = writer.travel_to_xyz(p2);
                check_gcode_feedrate(result, writer.config, speed);
            }
            THEN("-XY move feed rate should be equal to travel_speed") {
                const Vec3d move{-10, 10, 0};
                const double speed = writer.config.travel_speed.value;
                const Vec3d p1 = writer.get_position();
                const Vec3d p2 = p1 + move;
                const std::string result = writer.travel_to_xyz(p2);
                check_gcode_feedrate(result, writer.config, speed);
            }
            THEN("X-Y move feed rate should be equal to travel_speed") {
                const Vec3d move{10, -10, 0};
                const double speed = writer.config.travel_speed.value;
                const Vec3d p1 = writer.get_position();
                const Vec3d p2 = p1 + move;
                const std::string result = writer.travel_to_xyz(p2);
                check_gcode_feedrate(result, writer.config, speed);
            }
            THEN("-X-Y move feed rate should be equal to travel_speed") {
                const Vec3d move{-10, -10, 0};
                const double speed = writer.config.travel_speed.value;
                const Vec3d p1 = writer.get_position();
                const Vec3d p2 = p1 + move;
                const std::string result = writer.travel_to_xyz(p2);
                check_gcode_feedrate(result, writer.config, speed);
            }
            THEN("XZ move feed rate Z component should be equal to travel_speed_z") {
                const Vec3d move{10, 0, 10};
                const Vec3d move_u = move / move.norm();
                const double speed = std::abs(writer.config.travel_speed_z.value / move_u.z());
                Vec3d p1 = writer.get_position();
                Vec3d p2 = p1 + move;
                std::string result = writer.travel_to_xyz(p2);
                check_gcode_feedrate(result, writer.config, speed);
            }
            THEN("-XZ move feed rate Z component should be equal to travel_speed_z") {
                const Vec3d move{-10, 0, 10};
                const Vec3d move_u = move / move.norm();
                const double speed = std::abs(writer.config.travel_speed_z.value / move_u.z());
                const Vec3d p1 = writer.get_position();
                const Vec3d p2 = p1 + move;
                const std::string result = writer.travel_to_xyz(p2);
                check_gcode_feedrate(result, writer.config, speed);
            }
            THEN("X-Z move feed rate Z component should be equal to travel_speed_z") {
                const Vec3d move{10, 0, -10};
                const Vec3d move_u = move / move.norm();
                const double speed = std::abs(writer.config.travel_speed_z.value / move_u.z());
                const Vec3d p1 = writer.get_position();
                const Vec3d p2 = p1 + move;
                const std::string result = writer.travel_to_xyz(p2);
                check_gcode_feedrate(result, writer.config, speed);
            }
            THEN("-X-Z move feed rate Z component should be equal to travel_speed_z") {
                const Vec3d move{-10, 0, -10};
                const Vec3d move_u = move / move.norm();
                const double speed = std::abs(writer.config.travel_speed_z.value / move_u.z());
                const Vec3d p1 = writer.get_position();
                const Vec3d p2 = p1 + move;
                const std::string result = writer.travel_to_xyz(p2);
                check_gcode_feedrate(result, writer.config, speed);
            }
            THEN("YZ move feed rate Z component should be equal to travel_speed_z") {
                const Vec3d move{0, 10, 10};
                const Vec3d move_u = move / move.norm();
                const double speed = std::abs(writer.config.travel_speed_z.value / move_u.z());
                const Vec3d p1 = writer.get_position();
                const Vec3d p2 = p1 + move;
                const std::string result = writer.travel_to_xyz(p2);
                check_gcode_feedrate(result, writer.config, speed);
            }
            THEN("-YZ move feed rate Z component should be equal to travel_speed_z") {
                const Vec3d move{0, -10, 10};
                const Vec3d move_u = move / move.norm();
                const double speed = std::abs(writer.config.travel_speed_z.value / move_u.z());
                const Vec3d p1 = writer.get_position();
                const Vec3d p2 = p1 + move;
                const std::string result = writer.travel_to_xyz(p2);
                check_gcode_feedrate(result, writer.config, speed);
            }
            THEN("Y-Z move feed rate Z component should be equal to travel_speed_z") {
                const Vec3d move{0, 10, -10};
                const Vec3d move_u = move / move.norm();
                const double speed = std::abs(writer.config.travel_speed_z.value / move_u.z());
                const Vec3d p1 = writer.get_position();
                const Vec3d p2 = p1 + move;
                const std::string result = writer.travel_to_xyz(p2);
                check_gcode_feedrate(result, writer.config, speed);
            }
            THEN("-Y-Z move feed rate Z component should be equal to travel_speed_z") {
                const Vec3d move{0, -10, -10};
                const Vec3d move_u = move / move.norm();
                const double speed = std::abs(writer.config.travel_speed_z.value / move_u.z());
                const Vec3d p1 = writer.get_position();
                const Vec3d p2 = p1 + move;
                const std::string result = writer.travel_to_xyz(p2);
                check_gcode_feedrate(result, writer.config, speed);
            }
            THEN("XYZ move feed rate Z component should be equal to travel_speed_z") {
                const Vec3d move{10, 10, 10};
                const Vec3d move_u = move / move.norm();
                const double speed = std::abs(writer.config.travel_speed_z.value / move_u.z());
                const Vec3d p1 = writer.get_position();
                const Vec3d p2 = p1 + move;
                const std::string result = writer.travel_to_xyz(p2);
                check_gcode_feedrate(result, writer.config, speed);
            }
            THEN("-XYZ move feed rate Z component should be equal to travel_speed_z") {
                const Vec3d move{-10, 10, 10};
                const Vec3d move_u = move / move.norm();
                const double speed = std::abs(writer.config.travel_speed_z.value / move_u.z());
                const Vec3d p1 = writer.get_position();
                const Vec3d p2 = p1 + move;
                const std::string result = writer.travel_to_xyz(p2);
                check_gcode_feedrate(result, writer.config, speed);
            }
            THEN("X-YZ move feed rate Z component should be equal to travel_speed_z") {
                const Vec3d move{10, -10, 10};
                const Vec3d move_u = move / move.norm();
                const double speed = std::abs(writer.config.travel_speed_z.value / move_u.z());
                const Vec3d p1 = writer.get_position();
                const Vec3d p2 = p1 + move;
                const std::string result = writer.travel_to_xyz(p2);
                check_gcode_feedrate(result, writer.config, speed);
            }
            THEN("-X-YZ move feed rate Z component should be equal to travel_speed_z") {
                const Vec3d move{-10, -10, 10};
                const Vec3d move_u = move / move.norm();
                const double speed = std::abs(writer.config.travel_speed_z.value / move_u.z());
                const Vec3d p1 = writer.get_position();
                const Vec3d p2 = p1 + move;
                const std::string result = writer.travel_to_xyz(p2);
                check_gcode_feedrate(result, writer.config, speed);
            }
            THEN("XY-Z move feed rate Z component should be equal to travel_speed_z") {
                const Vec3d move{10, 10, -10};
                const Vec3d move_u = move / move.norm();
                const double speed = std::abs(writer.config.travel_speed_z.value / move_u.z());
                const Vec3d p1 = writer.get_position();
                const Vec3d p2 = p1 + move;
                const std::string result = writer.travel_to_xyz(p2);
                check_gcode_feedrate(result, writer.config, speed);
            }
            THEN("-XY-Z move feed rate Z component should be equal to travel_speed_z") {
                const Vec3d move{-10, 10, -10};
                const Vec3d move_u = move / move.norm();
                const double speed = std::abs(writer.config.travel_speed_z.value / move_u.z());
                const Vec3d p1 = writer.get_position();
                const Vec3d p2 = p1 + move;
                const std::string result = writer.travel_to_xyz(p2);
                check_gcode_feedrate(result, writer.config, speed);
            }
            THEN("X-Y-Z move feed rate Z component should be equal to travel_speed_z") {
                const Vec3d move{10, -10, -10};
                const Vec3d move_u = move / move.norm();
                const double speed = std::abs(writer.config.travel_speed_z.value / move_u.z());
                const Vec3d p1 = writer.get_position();
                const Vec3d p2 = p1 + move;
                const std::string result = writer.travel_to_xyz(p2);
                check_gcode_feedrate(result, writer.config, speed);
            }
            THEN("-X-Y-Z move feed rate Z component should be equal to travel_speed_z") {
                const Vec3d move{-10, -10, -10};
                const Vec3d move_u = move / move.norm();
                const double speed = std::abs(writer.config.travel_speed_z.value / move_u.z());
                const Vec3d p1 = writer.get_position();
                const Vec3d p2 = p1 + move;
                const std::string result = writer.travel_to_xyz(p2);
                check_gcode_feedrate(result, writer.config, speed);
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