#include "Slic3r/Biz/Algorithms/Color.hpp"
#include "Slic3r/Domain/Color.hpp"

#include <catch2/catch_test_macros.hpp>

using Slic3r::Domain::ColorRGB;

using Slic3r::Biz::Algorithms::Color::decode_color;
using Slic3r::Biz::Algorithms::Color::encode_color;

SCENARIO("Color encoding/decoding cycle", "[Color]") {
    GIVEN("Color") {
        const ColorRGB src_rgb(static_cast<unsigned char>(255), static_cast<unsigned char>(127), static_cast<unsigned char>(63));
        WHEN("apply encode/decode cycle") {
            const std::string encoded = encode_color(src_rgb);
            ColorRGB res_rgb;
            decode_color(encoded, res_rgb);
            const bool ret = res_rgb.r_uchar() == src_rgb.r_uchar() && res_rgb.g_uchar() == src_rgb.g_uchar() && res_rgb.b_uchar() == src_rgb.b_uchar();
            THEN("result matches source") {
                REQUIRE(ret);
            }
        }
    }
}


