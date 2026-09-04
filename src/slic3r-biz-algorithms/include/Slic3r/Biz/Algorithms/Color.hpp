#pragma once

#include "Slic3r/Domain/Color.hpp"

#include <string>
#include <vector>

namespace Slic3r::Biz::Algorithms::Color {

Domain::ColorRGB lerp(const Domain::ColorRGB& a, const Domain::ColorRGB& b, float t);
Domain::ColorRGBA lerp(const Domain::ColorRGBA& a, const Domain::ColorRGBA& b, float t);

Domain::ColorRGB complementary(const Domain::ColorRGB& color);
Domain::ColorRGBA complementary(const Domain::ColorRGBA& color);

Domain::ColorRGB saturate(const Domain::ColorRGB& color, float factor);
Domain::ColorRGBA saturate(const Domain::ColorRGBA& color, float factor);

Domain::ColorRGB opposite(const Domain::ColorRGB& color);
Domain::ColorRGB opposite(const Domain::ColorRGB& a, const Domain::ColorRGB& b);

bool can_decode_color(const std::string& color);

bool decode_color(const std::string& color_in, Domain::ColorRGB& color_out);
bool decode_color(const std::string& color_in, Domain::ColorRGBA& color_out);

bool decode_colors(const std::vector<std::string>& colors_in, std::vector<Domain::ColorRGB>& colors_out);
bool decode_colors(const std::vector<std::string>& colors_in, std::vector<Domain::ColorRGBA>& colors_out);

std::string encode_color(const Domain::ColorRGB& color);
std::string encode_color(const Domain::ColorRGBA& color);

Domain::ColorRGB to_rgb(const Domain::ColorRGBA& other_rgba);
Domain::ColorRGBA to_rgba(const Domain::ColorRGB& other_rgb);
Domain::ColorRGBA to_rgba(const Domain::ColorRGB& other_rgb, float alpha);

std::vector<Domain::ColorRGB> to_rgb(const std::vector<Domain::ColorRGBA>& colors);
std::vector<Domain::ColorRGBA> to_rgba(const std::vector<Domain::ColorRGB>& colors);

Domain::ColorRGBA picking_decode(unsigned int id);
unsigned int picking_encode(unsigned char r, unsigned char g, unsigned char b);

/**
 * Produce an alpha channel checksum for the red green blue components. The alpha channel may then be used to verify, whether the rgb components
 * were not interpolated by alpha blending or multi sampling.
 */
unsigned char picking_checksum_alpha_channel(unsigned char red, unsigned char green, unsigned char blue);

} // namespace Slic3r::Biz::Algorithms::Color
