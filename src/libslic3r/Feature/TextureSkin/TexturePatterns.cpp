///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "TexturePatterns.hpp"

#include <fstream>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "libslic3r/PNGReadWrite.hpp"

namespace Slic3r::Feature::TextureSkin {

namespace {

struct PatternMeta {
    const char* file;
    const char* name;
    double      default_scale;
};

// Must match the order of the Pattern enum.
static const PatternMeta PATTERNS[] = {
    {"basket.png",        "Basket",       0.5 },
    {"brick.png",         "Brick",        0.5 },
    {"bubble.png",        "Bubble",       0.5 },
    {"carbonFiber.png",   "Carbon Fiber", 0.5 },
    {"crystal.png",       "Crystal",      0.5 },
    {"dots.png",          "Dots",         0.1 },
    {"grid.png",          "Grid",         1.0 },
    {"gripSurface.png",   "Grip Surface", 0.5 },
    {"hexagon.png",       "Hexagon",      0.5 },
    {"hexagons.png",      "Hexagons",     1.0 },
    {"isogrid.png",       "Isogrid",      0.5 },
    {"knitting.png",      "Knitting",     0.25},
    {"knurling.png",      "Knurling",     0.15},
    {"leather2.png",      "Leather 2",    0.5 },
    {"noise.png",         "Noise",        0.3 },
    {"stripes.png",       "Stripes 1",    0.5 },
    {"stripes_02.png",    "Stripes 2",    1.0 },
    {"voronoi.png",       "Voronoi",      0.5 },
    {"weave.png",         "Weave 1",      0.5 },
    {"weave_02.png",      "Weave 2",      0.5 },
    {"weave_03.png",      "Weave 3",      0.5 },
    {"wood.png",          "Wood 1",       0.5 },
    {"woodgrain_02.png",  "Wood 2",       1.0 },
    {"woodgrain_03.png",  "Wood 3",       1.0 },
    {"",                  "Custom",       1.0 },
};

static_assert(sizeof(PATTERNS) / sizeof(PATTERNS[0]) == PatternCount,
              "PATTERNS[] and Pattern enum out of sync");

static GrayImage s_empty;
static std::unordered_map<std::string, GrayImage> s_cache;
static std::mutex s_cache_mutex;

static GrayImage load_png_file(const std::string &path)
{
    GrayImage out;
    std::ifstream in(path, std::ios::binary);
    if (!in) return out;

    // Read entire file into a buffer.
    in.seekg(0, std::ios::end);
    const std::streamsize sz = in.tellg();
    if (sz <= 0) return out;
    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> buf(static_cast<size_t>(sz));
    if (!in.read(reinterpret_cast<char*>(buf.data()), sz)) return out;

    Slic3r::png::ImageGreyscale img;
    const Slic3r::png::ReadBuf readbuf{buf.data(), buf.size()};
    if (!Slic3r::png::decode_png(readbuf, img)) return out;

    out.width  = img.cols;
    out.height = img.rows;
    out.pixels = std::move(img.buf);
    return out;
}

} // anonymous namespace

const char* pattern_filename(Pattern p)
{
    return PATTERNS[static_cast<size_t>(p)].file;
}

const char* pattern_display_name(Pattern p)
{
    return PATTERNS[static_cast<size_t>(p)].name;
}

double pattern_default_scale(Pattern p)
{
    return PATTERNS[static_cast<size_t>(p)].default_scale;
}

const GrayImage& get_pattern_image(Pattern p, const std::string &resources_dir)
{
    if (p == Pattern::Custom) return s_empty;

    const char* fname = pattern_filename(p);
    if (!fname || !*fname) return s_empty;

    const std::string path = resources_dir + "/textures/texture_skin/" + fname;
    std::lock_guard<std::mutex> lock(s_cache_mutex);
    auto it = s_cache.find(path);
    if (it != s_cache.end()) return it->second;
    auto [ins, _] = s_cache.emplace(path, load_png_file(path));
    return ins->second;
}

const GrayImage& get_custom_image(const std::string &absolute_path)
{
    if (absolute_path.empty()) return s_empty;
    std::lock_guard<std::mutex> lock(s_cache_mutex);
    auto it = s_cache.find(absolute_path);
    if (it != s_cache.end()) return it->second;
    auto [ins, _] = s_cache.emplace(absolute_path, load_png_file(absolute_path));
    return ins->second;
}

} // namespace Slic3r::Feature::TextureSkin
