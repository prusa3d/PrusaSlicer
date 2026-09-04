#pragma once

#include "Slic3r/Domain/Color.hpp"

#include <imgui/imstb_truetype.h>

#include <string>
#include <vector>
#include <cstdint>
#include <optional>

namespace Slic3r::Domain {
class Image;
}

namespace Slic3r::App::Render {

class TextImageGenerator
{
public:
    TextImageGenerator(const std::string& font_filename, uint8_t font_height);

    Domain::Image to_image(const std::string& text, uint8_t padding_x, uint8_t padding_y, const std::optional<Domain::ColorRGB>& color = std::nullopt);

private:
    struct Atlas
    {
        std::vector<uint8_t> bitmap;
        std::vector<stbtt_packedchar> packed_chars;

        int width{ 512 };
        int height{ 256 };
        // currently only digits are supported
        int first_char{ 48 }; // 0
        int chars_count{ 10 }; // 0-9
    };

    Atlas m_atlas;
};

} // namespace Slic3r::App::Render
