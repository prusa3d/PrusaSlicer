#include <catch2/catch_test_macros.hpp>

#include "libslic3r/FilamentColorBlend.hpp"

#include <cmath>

using namespace Slic3r;

// Helper to compute distance between two colors in RGB space
static double color_distance(unsigned char r1, unsigned char g1, unsigned char b1,
                             unsigned char r2, unsigned char g2, unsigned char b2)
{
    double dr = double(r1) - double(r2);
    double dg = double(g1) - double(g2);
    double db = double(b1) - double(b2);
    return std::sqrt(dr * dr + dg * dg + db * db);
}

TEST_CASE("filament_color_blend t=0 returns first color", "[FilamentColorBlend]") {
    unsigned char r, g, b;
    filament_color_blend(255, 0, 0, 0, 0, 255, 0.0f, &r, &g, &b);
    CHECK(r == 255);
    CHECK(g == 0);
    CHECK(b == 0);
}

TEST_CASE("filament_color_blend t=1 returns second color", "[FilamentColorBlend]") {
    unsigned char r, g, b;
    filament_color_blend(255, 0, 0, 0, 0, 255, 1.0f, &r, &g, &b);
    CHECK(r == 0);
    CHECK(g == 0);
    CHECK(b == 255);
}

TEST_CASE("filament_color_blend t<0 clamps to first color", "[FilamentColorBlend]") {
    unsigned char r, g, b;
    filament_color_blend(100, 150, 200, 50, 75, 100, -0.5f, &r, &g, &b);
    CHECK(r == 100);
    CHECK(g == 150);
    CHECK(b == 200);
}

TEST_CASE("filament_color_blend t>1 clamps to second color", "[FilamentColorBlend]") {
    unsigned char r, g, b;
    filament_color_blend(100, 150, 200, 50, 75, 100, 1.5f, &r, &g, &b);
    CHECK(r == 50);
    CHECK(g == 75);
    CHECK(b == 100);
}

TEST_CASE("filament_color_blend same colors returns approximately same color", "[FilamentColorBlend]") {
    unsigned char r, g, b;
    filament_color_blend(128, 128, 128, 128, 128, 128, 0.5f, &r, &g, &b);
    // Polynomial model may have small rounding differences (±5)
    CHECK(std::abs(int(r) - 128) <= 5);
    CHECK(std::abs(int(g) - 128) <= 5);
    CHECK(std::abs(int(b) - 128) <= 5);
}

TEST_CASE("filament_color_blend black and white gives gray-ish", "[FilamentColorBlend]") {
    unsigned char r, g, b;
    filament_color_blend(0, 0, 0, 255, 255, 255, 0.5f, &r, &g, &b);
    // Should be somewhere in the middle, not exactly 128 due to pigment model
    CHECK(r > 50);
    CHECK(r < 200);
    CHECK(g > 50);
    CHECK(g < 200);
    CHECK(b > 50);
    CHECK(b < 200);
}

TEST_CASE("filament_color_blend blue+yellow gives green (pigment mixing)", "[FilamentColorBlend]") {
    // This is the key test for pigment-style mixing:
    // In RGB additive mixing, blue + yellow = white/gray
    // In pigment/subtractive mixing, blue + yellow = green
    unsigned char r, g, b;
    // Deep blue + bright yellow
    filament_color_blend(0, 33, 133, 252, 211, 0, 0.5f, &r, &g, &b);

    // The result should be greenish — green channel should be dominant
    // or at least higher than a naive RGB average would give
    INFO("r=" << int(r) << " g=" << int(g) << " b=" << int(b));
    CHECK(g > r); // green should dominate over red
    CHECK(g > b); // green should dominate over blue
}

TEST_CASE("filament_color_blend red+yellow gives orange-ish", "[FilamentColorBlend]") {
    unsigned char r, g, b;
    filament_color_blend(255, 0, 0, 255, 255, 0, 0.5f, &r, &g, &b);

    INFO("r=" << int(r) << " g=" << int(g) << " b=" << int(b));
    CHECK(r > 150); // should be red-heavy
    CHECK(g > 50);  // with some green (orange)
    CHECK(b < 100); // minimal blue
}

TEST_CASE("filament_color_blend red+blue gives purple-ish", "[FilamentColorBlend]") {
    unsigned char r, g, b;
    filament_color_blend(255, 0, 0, 0, 0, 255, 0.5f, &r, &g, &b);

    INFO("r=" << int(r) << " g=" << int(g) << " b=" << int(b));
    CHECK(r > 50);  // red component present
    CHECK(b > 50);  // blue component present
    // Green should be relatively low for purple
    CHECK(g < std::max(r, b));
}

TEST_CASE("filament_color_blend output is within valid range", "[FilamentColorBlend]") {
    // Test a variety of inputs to ensure outputs are always 0-255
    unsigned char r, g, b;
    for (int i = 0; i <= 10; ++i) {
        float t = float(i) / 10.f;
        filament_color_blend(0, 0, 0, 255, 255, 255, t, &r, &g, &b);
        CHECK(r >= 0);
        CHECK(r <= 255);
        CHECK(g >= 0);
        CHECK(g <= 255);
        CHECK(b >= 0);
        CHECK(b <= 255);
    }
}

TEST_CASE("filament_color_blend is monotonic for grayscale", "[FilamentColorBlend]") {
    // Blending from black to white, brightness should increase monotonically
    int prev_brightness = -1;
    for (int i = 0; i <= 10; ++i) {
        float t = float(i) / 10.f;
        unsigned char r, g, b;
        filament_color_blend(0, 0, 0, 255, 255, 255, t, &r, &g, &b);
        int brightness = int(r) + int(g) + int(b);
        CHECK(brightness >= prev_brightness);
        prev_brightness = brightness;
    }
}

TEST_CASE("filament_color_blend is continuous", "[FilamentColorBlend]") {
    // Small changes in t should produce small changes in output
    unsigned char r1, g1, b1, r2, g2, b2;
    filament_color_blend(255, 0, 0, 0, 0, 255, 0.50f, &r1, &g1, &b1);
    filament_color_blend(255, 0, 0, 0, 0, 255, 0.51f, &r2, &g2, &b2);

    double dist = color_distance(r1, g1, b1, r2, g2, b2);
    CHECK(dist < 20.0); // small t change => small color change
}
