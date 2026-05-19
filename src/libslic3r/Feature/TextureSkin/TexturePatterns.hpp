#ifndef libslic3r_TextureSkin_TexturePatterns_hpp_
#define libslic3r_TextureSkin_TexturePatterns_hpp_

#include <cstdint>
#include <string>

#include "TextureSampler.hpp"

namespace Slic3r::Feature::TextureSkin {

// 25 built-in patterns ported from stlTexturizer/js/presetTextures.js.
// Ordered alphabetically by display name.
enum class Pattern : uint8_t {
    Basket = 0,
    Brick,
    Bubble,
    CarbonFiber,
    Crystal,
    Dots,
    Grid,
    GripSurface,
    Hexagon,
    Hexagons,
    Isogrid,
    Knitting,
    Knurling,
    Leather2,
    Noise,
    Stripes1,
    Stripes2,
    Voronoi,
    Weave1,
    Weave2,
    Weave3,
    Wood1,
    Wood2,
    Wood3,
    Custom,
};

constexpr size_t PatternCount = static_cast<size_t>(Pattern::Custom) + 1;

// Relative filename inside resources/textures/texture_skin/ for a given pattern.
// Returns empty string for Pattern::Custom.
const char* pattern_filename(Pattern p);

// Human-readable display name.
const char* pattern_display_name(Pattern p);

// Default UV scale value recommended by stlTexturizer for this pattern.
double pattern_default_scale(Pattern p);

// Load a built-in pattern from the resource directory (lazy, cached).
// `resources_dir` is PrusaSlicer's resources root (e.g. the value returned by
// Slic3r::resources_dir()). Returns a reference to a shared cached image; if
// loading fails, returns an empty GrayImage.
const GrayImage& get_pattern_image(Pattern p, const std::string &resources_dir);

// Load a custom user image (PNG) from an absolute path. Returned image is
// cached keyed by path. 8-bit grayscale PNG only.
const GrayImage& get_custom_image(const std::string &absolute_path);

} // namespace Slic3r::Feature::TextureSkin

#endif // libslic3r_TextureSkin_TexturePatterns_hpp_
